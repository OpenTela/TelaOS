#!/usr/bin/env python3
"""mem_report.py — the watch's memory on every screen, before and after a change.

    python3 sim/tools/mem_report.py <shots-base> <shots-head>

Each screen's widget tree (`tree`) carries the simulator's model of the watch's heaps at
that moment ("mem": dram and psram — total, free, min_free, largest, blocks, failed). This
says, for the PR report: the lowest DRAM any screen came down to (the risk), allocations
that did not fit, and the screens whose memory use changed by a kilobyte or more.
Python standard library only.
"""

import json
import sys
from pathlib import Path

KB = 1024
THRESHOLD = KB  # smaller changes are noise for a reviewer


def load(shots_dir):
    """{screen: mem} for every tree in a shots directory that has the memory model."""
    out = {}
    d = Path(shots_dir)
    if not d.is_dir():
        return out
    for p in sorted(d.glob("*.json")):
        try:
            mem = json.loads(p.read_text("utf-8")).get("mem")
        except (OSError, ValueError):
            continue
        if mem and "dram" in mem and "psram" in mem:
            out[p.stem] = mem
    return out


def kb(n):
    v = n / KB
    return (f"{v:.1f}" if v < 100 else f"{v:.0f}").replace(".", ",") + " КБ"


def mb(n):
    return f"{n / KB / KB:.1f}".replace(".", ",") + " МБ"


def used(mem, heap):
    return mem[heap]["total"] - mem[heap]["free"]


def title(name):
    app, _, page = name.partition("__")
    return f"**{app}** · {page}" if page else f"**{app}**"


def signed_kb(n):
    return ("+" if n > 0 else "−") + kb(abs(n))[:-3]


def lines(base, head):
    """Markdown lines for the PR report; [] when there is nothing to say."""
    if not head:
        return []
    any_mem = next(iter(head.values()))
    sizes = f"DRAM {kb(any_mem['dram']['total'])}, PSRAM {mb(any_mem['psram']['total'])}"
    note = f"оценка: {sizes}, ещё не сверено с часами" if any_mem.get("estimate") else sizes
    out = [f"**Память** ({note})", ""]

    low_name = min(head, key=lambda s: (head[s]["dram"]["min_free"], s))
    low = head[low_name]["dram"]["min_free"]
    was = f" (до: {kb(base[low_name]['dram']['min_free'])})" if low_name in base else ""
    out.append(f"- Меньше всего DRAM оставалось на {title(low_name)}: {kb(low)}{was}")

    failed = {s: m["dram"]["failed"] + m["psram"]["failed"] for s, m in head.items()}
    bad = [s for s in sorted(failed) if failed[s]]
    if bad:
        out.append("- ⚠️ Не хватило памяти: " + ", ".join(f"{title(s)} ({failed[s]})" for s in bad))

    changed = []
    for s in sorted(head):
        if s not in base:
            continue
        parts = []
        for heap, label in (("dram", "DRAM"), ("psram", "PSRAM")):
            a, b = used(base[s], heap), used(head[s], heap)
            if abs(b - a) >= THRESHOLD:
                parts.append(f"{label} занято {kb(a)[:-3]} → {kb(b)} ({signed_kb(b - a)})")
        if parts:
            changed.append(f"    - {title(s)}: " + ", ".join(parts))
    if changed:
        out.append("- Изменилось на 1 КБ и больше:")
        out += changed
    elif base:
        out.append(f"- Ни на одном из {len(head)} экранов расход не изменился на 1 КБ и больше")
    out.append("")
    return out


def main(argv):
    if len(argv) != 3:
        print(__doc__.strip().splitlines()[2].strip())
        return 2
    for line in lines(load(argv[1]), load(argv[2])):
        print(line.replace("**", ""))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
