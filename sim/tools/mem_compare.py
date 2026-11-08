#!/usr/bin/env python3
"""mem_compare.py — the watch's memory against the simulator's estimate.

    python3 sim/tools/mem_compare.py --watch bench.json --sim build/screens/shots-head

bench.json: what tools/ble_assistant.py `bench` measured on a watch (sys mem): for each app, the
launcher right before it and the app's first screen. --sim: the simulator's screens of the same
apps, each a tree with "mem" (sim_screens.py, the robot's `tree`): launcher.json, <app>__<page>.json
or <app>.json. Prints, in Markdown:

- what the firmware had at boot on the watch (sys mem at_boot) and what the simulator gives it:
  the keys to give the simulator that size (--dram-kb, --psram-kb);
- per app: what its first screen takes, launcher to app, on the watch and in the simulator.

Both are differences: what the system holds besides the app (BLE, which the simulator does not
have) is in the launcher too and drops out. PSRAM on the watch: a font loaded once stays, so an
app pays for a size only if no app before it in the bench loaded it; in the simulator each app
starts from a fresh boot. Python standard library only.
"""

import argparse
import json
import sys
from pathlib import Path

import sim_screens

HEAPS = ("dram", "psram")


def kb(n):
    return f"{n / 1024:.1f}".replace(".", ",") + " КБ"


def screen_mem(sim, app, first):
    """mem of the app's first screen in the simulator's trees, None if there is none"""
    for name in ([f"{app}__{first}"] if first else []) + [app]:
        path = Path(sim) / f"{name}.json"
        if path.is_file():
            try:
                return json.loads(path.read_text("utf-8")).get("mem")
            except ValueError:
                return None
    return None


def cost(before, after, heap):
    """Bytes the app took of `heap`, launcher to its first screen; None without both"""
    if not before or not after or heap not in before or heap not in after:
        return None
    return before[heap]["free"] - after[heap]["free"]


def ratio(watch, sim):
    if watch is None or sim is None or watch <= 0:
        return "—"
    return f"{sim / watch:.2f}".replace(".", ",") + "×"


def compare(bench, sim, roots):
    """The lines of the report"""
    launcher = None
    path = Path(sim) / "launcher.json"
    if path.is_file():
        launcher = json.loads(path.read_text("utf-8")).get("mem")
    lines = ["## Память: часы и симулятор", ""]
    lines.append(f"Часы: прошивка {bench.get('fw') or '?'}, {bench.get('chip') or '?'}.")
    apps = bench.get("apps", {})
    first_watch = next((a["launcher"] for a in apps.values() if a.get("launcher")), None)
    if first_watch and launcher:
        lines += ["", "| | DRAM | PSRAM |", "|---|---|---|"]
        boot = [first_watch[h].get("at_boot", 0) for h in HEAPS]
        lines.append(f"| у прошивки при старте, часы (`at_boot`) | {kb(boot[0])} | {kb(boot[1])} |")
        lines.append(f"| у прошивки в симуляторе{' (оценка)' if launcher.get('estimate') else ''} "
                     f"| {kb(launcher['dram']['total'])} | {kb(launcher['psram']['total'])} |")
        lines.append(f"| свободно в лаунчере, часы | {kb(first_watch['dram']['free'])} | {kb(first_watch['psram']['free'])} |")
        lines.append(f"| свободно в лаунчере, симулятор | {kb(launcher['dram']['free'])} | {kb(launcher['psram']['free'])} |")
        if all(boot):
            lines += ["", f"Симулятору — размеры часов: `--dram-kb {boot[0] // 1024} --psram-kb {boot[1] // 1024}`."]
    lines += ["", "Сколько берёт первый экран приложения, от лаунчера (часы → симулятор):", "",
              "| Приложение | DRAM, часы | DRAM, симулятор | симулятор / часы | PSRAM, часы | PSRAM, симулятор | отказы |",
              "|---|---|---|---|---|---|---|"]
    for app in sorted(apps):
        m = apps[app]
        first, _ = sim_screens.bax_pages(app, roots)
        sim_app = screen_mem(sim, app, first)
        w = [cost(m.get("launcher"), m.get("app"), h) for h in HEAPS]
        s = [cost(launcher, sim_app, h) for h in HEAPS]
        failed = sum(m["app"][h]["failed"] - m["launcher"][h]["failed"] for h in HEAPS) if m.get("app") and m.get("launcher") else None
        cells = [app,
                 kb(w[0]) if w[0] is not None else "—", kb(s[0]) if s[0] is not None else "нет экрана",
                 ratio(w[0], s[0]),
                 kb(w[1]) if w[1] is not None else "—", kb(s[1]) if s[1] is not None else "нет экрана",
                 str(failed) if failed is not None else "—"]
        lines.append("| " + " | ".join(cells) + " |")
    lines += ["", "Отказы — выделения, которым на часах не хватило памяти, пока открывалось приложение."]
    return lines


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--watch", required=True, help="bench.json of tools/ble_assistant.py bench")
    ap.add_argument("--sim", required=True, help="the simulator's screens: launcher.json, <app>__<page>.json")
    ap.add_argument("--data", action="append", help="where the apps are (default: data/ and the fixtures)")
    args = ap.parse_args()
    here = Path(__file__).resolve().parents[2]
    roots = [Path(d) for d in args.data] if args.data else [here / "data", here / "sim" / "tests" / "fixtures"]
    bench = json.loads(Path(args.watch).read_text("utf-8"))
    print("\n".join(compare(bench, args.sim, roots)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
