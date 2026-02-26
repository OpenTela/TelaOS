#!/usr/bin/env python3
"""
BLE Assistant for TelaOS v1.0

Двусторонний Bluetooth мост между часами и ПК/интернетом.

Функции:
  - Синхронизация времени при подключении (sys sync)
  - HTTP proxy: часы делают fetch() → Python выполняет запрос → ответ обратно
  - CLI: отправка команд на часы
  - Бинарный трансфер: скриншоты, push файлов

Протокол v2:
  Запрос:  [id, "subsystem", "command", [args...]]
  Ответ:   [id, "ok"|"error", {data}]

Требования:
  pip install bleak requests

Запуск:
  python ble_assistant.py              # Поиск FutureClock
  python ble_assistant.py --scan       # Список BLE устройств
  python ble_assistant.py --addr XX:XX:XX:XX:XX:XX
  python ble_assistant.py --daemon     # Без CLI, только proxy + sync
"""

import asyncio
import json
import struct
import sys
import os
import time
import argparse
import signal
from datetime import datetime, timezone, timedelta

try:
    from bleak import BleakClient, BleakScanner
except ImportError:
    print("ERROR: bleak required. Install: pip install bleak")
    sys.exit(1)

try:
    import requests
except ImportError:
    print("WARNING: requests not installed. HTTP proxy disabled.")
    requests = None

# ── BLE UUIDs ──────────────────────────────────────────────────
SERVICE_UUID = "12345678-1234-5678-1234-56789abcdef0"
TX_CHAR_UUID = "12345678-1234-5678-1234-56789abcdef1"  # watch → PC (notify)
RX_CHAR_UUID = "12345678-1234-5678-1234-56789abcdef2"  # PC → watch (write)
BIN_CHAR_UUID = "12345678-1234-5678-1234-56789abcdef3"  # binary chunks

PROTOCOL_VERSION = "2.7"
DEVICE_NAME = "FutureClock"

# ── Global state ───────────────────────────────────────────────
request_id = 0
pending_responses = {}  # id → asyncio.Future
bin_buffer = bytearray()
bin_expected_size = 0
bin_expected_chunks = {}


def next_id():
    global request_id
    request_id += 1
    return request_id


def log(tag, msg):
    ts = datetime.now().strftime("%H:%M:%S")
    print(f"[{ts}] {tag} {msg}")


def log_rx(msg):
    log("←", msg)


def log_tx(msg):
    log("→", msg)


# ── Timezone detection ─────────────────────────────────────────
def get_local_timezone():
    """Returns timezone offset as string like '+03:00' or '-05:30'"""
    now = datetime.now(timezone.utc)
    local = datetime.now().astimezone()
    offset = local.utcoffset()
    total_seconds = int(offset.total_seconds())
    sign = "+" if total_seconds >= 0 else "-"
    total_seconds = abs(total_seconds)
    hours = total_seconds // 3600
    minutes = (total_seconds % 3600) // 60
    return f"{sign}{hours:02d}:{minutes:02d}"


def get_utc_iso():
    """Returns current UTC time as ISO 8601 string"""
    return datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")


# ── BLE Communication ─────────────────────────────────────────
class BLEAssistant:
    def __init__(self, address=None, daemon=False):
        self.address = address
        self.daemon = daemon
        self.client = None
        self.connected = False
        self.synced = False

    # ── Discovery ──────────────────────────────────────────
    @staticmethod
    async def scan(timeout=5.0):
        """Scan for BLE devices"""
        print(f"Scanning for {timeout}s...")
        devices = await BleakScanner.discover(timeout=timeout)
        
        found = []
        for d in sorted(devices, key=lambda x: x.rssi, reverse=True):
            name = d.name or "?"
            marker = " ◀ " if DEVICE_NAME in (name or "") else "   "
            print(f"  {d.address}  {d.rssi:4d} dBm  {marker}{name}")
            if DEVICE_NAME in (name or ""):
                found.append(d.address)
        
        if not found:
            print(f"\n  {DEVICE_NAME} не найден")
        return found

    async def find_device(self, timeout=10.0):
        """Find FutureClock by name"""
        log("•", f"Searching for {DEVICE_NAME}...")
        device = await BleakScanner.find_device_by_name(
            DEVICE_NAME, timeout=timeout
        )
        if device:
            log("•", f"Found: {device.address} ({device.rssi} dBm)")
            return device.address
        return None

    # ── Connection ─────────────────────────────────────────
    async def connect(self):
        """Connect and setup notifications"""
        addr = self.address
        if not addr:
            addr = await self.find_device()
            if not addr:
                log("✗", f"{DEVICE_NAME} not found")
                return False

        log("•", f"Connecting to {addr}...")
        self.client = BleakClient(
            addr,
            disconnected_callback=self._on_disconnect
        )
        
        try:
            await self.client.connect()
        except Exception as e:
            log("✗", f"Connection failed: {e}")
            return False

        self.connected = True
        log("✓", f"Connected to {addr}")

        # Subscribe to TX (JSON responses) and BIN (binary chunks)
        await self.client.start_notify(TX_CHAR_UUID, self._on_tx_notify)
        await self.client.start_notify(BIN_CHAR_UUID, self._on_bin_notify)

        # Auto-sync time
        await self.sync()
        return True

    def _on_disconnect(self, client):
        self.connected = False
        self.synced = False
        log("✗", "Disconnected")

    # ── TX notify (JSON responses from watch) ──────────────
    def _on_tx_notify(self, sender, data: bytearray):
        """Handle JSON messages from watch"""
        text = data.decode("utf-8", errors="replace")
        
        try:
            msg = json.loads(text)
        except json.JSONDecodeError:
            log_rx(f"(raw) {text[:120]}")
            return

        # Fetch request from watch: {"id": N, "method": "GET", "url": "..."}
        if isinstance(msg, dict) and "method" in msg and "url" in msg:
            asyncio.get_event_loop().create_task(self._handle_fetch(msg))
            return

        # Command response: [id, "ok"|"error", {data}]
        if isinstance(msg, list) and len(msg) >= 2:
            rid = msg[0]
            status = msg[1]
            payload = msg[2] if len(msg) > 2 else {}
            
            log_rx(f"[{rid}] {status} {json.dumps(payload, ensure_ascii=False)[:200]}")
            
            if rid in pending_responses:
                pending_responses[rid].set_result((status, payload))
                del pending_responses[rid]
            return

        # Other (text messages etc.)
        log_rx(json.dumps(msg, ensure_ascii=False)[:200])

    # ── BIN notify (binary chunks from watch) ──────────────
    def _on_bin_notify(self, sender, data: bytearray):
        """Handle binary chunks: [2B chunk_id LE][data]"""
        global bin_buffer
        if len(data) < 2:
            return
        
        chunk_id = struct.unpack("<H", data[:2])[0]
        chunk_data = data[2:]
        
        if chunk_id == 0:
            bin_buffer = bytearray()
        
        bin_buffer.extend(chunk_data)
        
        # Check if complete
        if bin_expected_size > 0 and len(bin_buffer) >= bin_expected_size:
            log("•", f"Binary received: {len(bin_buffer)} bytes ({chunk_id + 1} chunks)")

    # ── Send command ───────────────────────────────────────
    async def send(self, subsystem, cmd, args=None, wait=True, timeout=5.0):
        """Send command, optionally wait for response"""
        rid = next_id()
        msg = [rid, subsystem, cmd]
        if args:
            msg.append(args)

        text = json.dumps(msg, ensure_ascii=False)
        log_tx(f"[{rid}] {subsystem} {cmd} {json.dumps(args or [], ensure_ascii=False)[:120]}")

        if wait:
            future = asyncio.get_event_loop().create_future()
            pending_responses[rid] = future

        await self.client.write_gatt_char(RX_CHAR_UUID, text.encode("utf-8"))

        if wait:
            try:
                status, payload = await asyncio.wait_for(future, timeout)
                return status, payload
            except asyncio.TimeoutError:
                pending_responses.pop(rid, None)
                log("✗", f"[{rid}] timeout")
                return "error", {"code": "timeout"}
        
        return "ok", {}

    # ── Sync ───────────────────────────────────────────────
    async def sync(self):
        """Send time sync to watch"""
        tz = get_local_timezone()
        utc = get_utc_iso()
        
        log("⏱", f"Sync: {utc} {tz}")
        status, data = await self.send("sys", "sync", [PROTOCOL_VERSION, utc, tz])
        
        if status == "ok":
            proto = data.get("protocol", "?")
            osv = data.get("os", "?")
            log("⏱", f"Synced! OS {osv}, protocol {proto}")
            self.synced = True
        else:
            log("✗", f"Sync failed: {data}")
        
        return status == "ok"

    # ── HTTP Proxy ─────────────────────────────────────────
    async def _handle_fetch(self, msg):
        """Handle HTTP fetch request from watch"""
        rid = msg.get("id", 0)
        method = msg.get("method", "GET")
        url = msg.get("url", "")
        body = msg.get("body")
        fmt = msg.get("format")
        fields = msg.get("fields", [])

        log("⇄", f"fetch[{rid}] {method} {url[:80]}")

        if not requests:
            await self._send_fetch_response(rid, 0, '{"error":"requests not installed"}')
            return

        try:
            headers = {"User-Agent": "TelaOS-BLEProxy/1.0"}
            
            resp = requests.request(
                method=method,
                url=url,
                data=body.encode("utf-8") if body else None,
                headers=headers,
                timeout=15
            )

            resp_body = resp.text

            # Filter fields if requested
            if fields and fmt == "json":
                try:
                    data = resp.json()
                    filtered = {k: data[k] for k in fields if k in data}
                    resp_body = json.dumps(filtered, ensure_ascii=False)
                except (json.JSONDecodeError, KeyError):
                    pass  # Return raw body

            log("⇄", f"fetch[{rid}] → {resp.status_code} ({len(resp_body)} bytes)")
            await self._send_fetch_response(rid, resp.status_code, resp_body)

        except requests.Timeout:
            log("✗", f"fetch[{rid}] timeout")
            await self._send_fetch_response(rid, 408, '{"error":"timeout"}')
        except Exception as e:
            log("✗", f"fetch[{rid}] error: {e}")
            await self._send_fetch_response(rid, 0, json.dumps({"error": str(e)}))

    async def _send_fetch_response(self, rid, status, body):
        """Send fetch response back to watch"""
        msg = json.dumps({"id": rid, "status": status, "body": body}, ensure_ascii=False)
        
        # BLE MTU is typically 512, but chunk if needed
        encoded = msg.encode("utf-8")
        if len(encoded) > 500:
            log("⇄", f"fetch[{rid}] response large: {len(encoded)} bytes")
        
        await self.client.write_gatt_char(RX_CHAR_UUID, encoded)

    # ── Binary send (file push) ────────────────────────────
    async def send_binary(self, data: bytes, chunk_size=250):
        """Send binary data via BIN_CHAR in chunks"""
        total = len(data)
        offset = 0
        chunk_id = 0

        while offset < total:
            end = min(offset + chunk_size, total)
            header = struct.pack("<H", chunk_id)
            packet = header + data[offset:end]
            await self.client.write_gatt_char(BIN_CHAR_UUID, packet)
            offset = end
            chunk_id += 1
            await asyncio.sleep(0.015)  # Match ESP32's 15ms delay

        log("•", f"Binary sent: {total} bytes ({chunk_id} chunks)")

    # ── CLI ────────────────────────────────────────────────
    async def cli_loop(self):
        """Interactive command line"""
        print("\n  Команды: sys info | sys screen | app list | app launch <name>")
        print("           app push <dir> | sync | quit")
        print("           <subsystem> <cmd> [args...]\n")

        while self.connected:
            try:
                line = await asyncio.get_event_loop().run_in_executor(
                    None, lambda: input("assistant> ")
                )
            except (EOFError, KeyboardInterrupt):
                break

            line = line.strip()
            if not line:
                continue
            if line in ("quit", "exit", "q"):
                break
            if line == "sync":
                await self.sync()
                continue

            await self._exec_cli(line)

    async def _exec_cli(self, line):
        """Parse and execute CLI command"""
        parts = line.split(None, 2)
        if len(parts) < 2:
            print("  Usage: <subsystem> <cmd> [args...]")
            return

        subsystem = parts[0]
        cmd = parts[1]
        args = []

        # Parse args from third part
        if len(parts) > 2:
            raw = parts[2]
            # Try JSON array first
            if raw.startswith("["):
                try:
                    args = json.loads(raw)
                except json.JSONDecodeError:
                    args = [raw]
            else:
                # Split by spaces, respect quotes
                args = self._split_args(raw)

        # Special: app push <directory>
        if subsystem == "app" and cmd == "push":
            if args:
                await self._push_app(args[0])
            else:
                print("  Usage: app push <app_directory>")
            return

        # Special: sys screen → save to file
        global bin_expected_size, bin_buffer
        if subsystem == "sys" and cmd == "screen":
            if not args:
                args = ["rgb16"]
            bin_expected_size = 0
            bin_buffer = bytearray()

        status, data = await self.send(subsystem, cmd, args if args else None)

        # Handle binary payload (screenshot etc)
        if status == "ok" and "bytes" in data:
            bin_expected_size = data["bytes"]
            log("•", f"Expecting {bin_expected_size} bytes binary...")
            # Wait for binary transfer
            for _ in range(100):  # 10s max
                await asyncio.sleep(0.1)
                if len(bin_buffer) >= bin_expected_size:
                    break
            
            if len(bin_buffer) >= bin_expected_size:
                fname = f"screen_{int(time.time())}.bin"
                w = data.get("w", 0)
                h = data.get("h", 0)
                color = data.get("color", "rgb16")
                fmt = data.get("format", "raw")
                fname = f"screen_{w}x{h}_{color}.{fmt}"
                
                with open(fname, "wb") as f:
                    f.write(bin_buffer[:bin_expected_size])
                log("•", f"Saved: {fname} ({bin_expected_size} bytes)")
            else:
                log("✗", f"Binary incomplete: {len(bin_buffer)}/{bin_expected_size}")

    async def _push_app(self, app_dir):
        """Push app directory to watch"""
        import os
        
        if not os.path.isdir(app_dir):
            # Maybe it's just a .bax file
            if os.path.isfile(app_dir):
                app_name = os.path.splitext(os.path.basename(app_dir))[0]
                data = open(app_dir, "rb").read()
                files = [(os.path.basename(app_dir), data)]
            else:
                print(f"  Not found: {app_dir}")
                return
        else:
            app_name = os.path.basename(os.path.normpath(app_dir))
            files = []
            for fname in sorted(os.listdir(app_dir)):
                fpath = os.path.join(app_dir, fname)
                if os.path.isfile(fpath):
                    files.append((fname, open(fpath, "rb").read()))

        if not files:
            print("  No files to push")
            return

        log("↑", f"Push {app_name}: {len(files)} files")

        if len(files) == 1:
            # Single file: [name, filename, "size_as_string"]
            fname, fdata = files[0]
            args = [app_name, fname, str(len(fdata))]
            status, data = await self.send("app", "push", args)
        else:
            # Multi-file: [name, "*", "{file:size,...}"]
            files_obj = {name: len(data) for name, data in files}
            files_json = json.dumps(files_obj)
            args = [app_name, "*", files_json]
            status, data = await self.send("app", "push", args)

        if status != "ok":
            log("✗", f"Push rejected: {data}")
            return

        # Send binary blob (all files concatenated)
        blob = bytearray()
        for _, fdata in files:
            blob.extend(fdata)

        await self.send_binary(bytes(blob))
        log("✓", f"Push {app_name} complete ({len(blob)} bytes)")

    @staticmethod
    def _split_args(raw):
        """Split arguments respecting quotes"""
        args = []
        current = ""
        in_quote = False
        quote_char = None

        for ch in raw:
            if in_quote:
                if ch == quote_char:
                    in_quote = False
                else:
                    current += ch
            elif ch in ('"', "'"):
                in_quote = True
                quote_char = ch
            elif ch == " ":
                if current:
                    args.append(_auto_type(current))
                    current = ""
            else:
                current += ch

        if current:
            args.append(_auto_type(current))

        return args

    # ── Main loop ──────────────────────────────────────────
    async def run(self):
        """Main loop with auto-reconnect"""
        while True:
            if not await self.connect():
                log("•", "Retry in 5s...")
                await asyncio.sleep(5)
                continue

            if self.daemon:
                log("•", "Daemon mode (proxy + sync)")
                # Keep alive until disconnect
                while self.connected:
                    await asyncio.sleep(1)
            else:
                await self.cli_loop()
                break

            if not self.connected:
                log("•", "Reconnecting...")
                await asyncio.sleep(2)


def _auto_type(s):
    """Convert string to int/float if possible"""
    try:
        return int(s)
    except ValueError:
        pass
    try:
        return float(s)
    except ValueError:
        pass
    if s.lower() in ("true", "false"):
        return s.lower() == "true"
    return s


# ── Entry point ────────────────────────────────────────────────
async def main():
    parser = argparse.ArgumentParser(description="BLE Assistant for TelaOS")
    parser.add_argument("--scan", action="store_true", help="Scan BLE devices")
    parser.add_argument("--addr", "--address", type=str, help="Device MAC address")
    parser.add_argument("--daemon", action="store_true", help="No CLI, proxy + sync only")
    
    args = parser.parse_args()

    if args.scan:
        await BLEAssistant.scan()
        return

    assistant = BLEAssistant(address=args.addr, daemon=args.daemon)
    
    # Handle Ctrl+C
    loop = asyncio.get_event_loop()
    for sig in (signal.SIGINT, signal.SIGTERM):
        try:
            loop.add_signal_handler(sig, lambda: asyncio.ensure_future(shutdown(assistant)))
        except NotImplementedError:
            pass  # Windows

    await assistant.run()
    
    if assistant.client and assistant.connected:
        await assistant.client.disconnect()
    
    log("•", "Done")


async def shutdown(assistant):
    if assistant.client and assistant.connected:
        await assistant.client.disconnect()
    sys.exit(0)


if __name__ == "__main__":
    asyncio.run(main())
