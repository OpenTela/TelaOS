#!/usr/bin/env python3
"""idle_report.py — what the screens redraw while nobody touches them, before and after a change.

    python3 sim/tools/idle_report.py <shots-base> <shots-head>

After its picture and tree, every screen rests: the robot's `idle` waits without a touch and
writes what the panel redrew meanwhile, <screen>.idle.json (pixels, refreshes, the areas). On
the watch every redrawn pixel is work for the CPU and the display bus at a moment the watch
could sleep: a screen that keeps redrawing while it shows nothing new drains the battery. This
says, for the PR report: the screens that redraw at rest, how many pixels a second, what (the
widget of the area redrawn, from the screen's tree), and what changed against the base.
Python standard library only.
"""

import json
import sys
from pathlib import Path

from tree_diff import Node, agree, plural, subject

SCREENS_OF = ("экрана", "экранов", "экранов")  # after «из»: из 51 экрана, из 25 экранов
SCREENS = ("экран", "экрана", "экранов")
PIXELS = ("пиксель", "пикселя", "пикселей")
FRAMES = ("кадр", "кадра", "кадров")
CHANGE = 0.1    # a rate this much higher or lower is a change; less is the same screen
MATCH = 0.5     # the widget whose box covers an area this well (intersection / union) is it
NAMED = 2       # widgets named per screen; the rest are counted


def boxes(tree):
    """Every visible widget of a tree with a box, screen and top layer (tree_diff.Node)"""
    out = []

    def walk(n):
        if not n.visible:
            return
        if n.w > 0 and n.h > 0:
            out.append(n)
        for c in n.children:
            walk(c)

    for layer in ("screen", "top"):
        if layer in tree:
            walk(Node(tree[layer]))
    return out


def overlap(n, a):
    """Intersection over union of a widget's box and an area"""
    w = min(n.x + n.w, a["x"] + a["w"]) - max(n.x, a["x"])
    h = min(n.y + n.h, a["y"] + a["h"]) - max(n.y, a["y"])
    if w <= 0 or h <= 0:
        return 0.0
    both = w * h
    return both / (n.w * n.h + a["w"] * a["h"] - both)


def name(area, nodes, screen_px):
    """The widget redrawn in an area, in words"""
    if screen_px and area["w"] * area["h"] >= 0.9 * screen_px:
        return "весь экран"
    best, best_ov = None, MATCH
    for n in nodes:
        ov = overlap(n, area)
        if ov > best_ov or (best is not None and ov == best_ov and n.w * n.h < best.w * best.h):
            best, best_ov = n, ov
    if best is not None:
        return subject(best)
    return f"область {area['w']}×{area['h']}"


def what(rec, tree):
    """Names of the widgets redrawn, the most pixels first"""
    nodes = boxes(tree) if tree else []
    screen = tree.get("screen", {}) if tree else {}
    screen_px = screen.get("w", 0) * screen.get("h", 0)
    names = []
    for a in rec.get("areas", []):
        n = name(a, nodes, screen_px)
        if n not in names:
            names.append(n)
    return names


def load(shots_dir):
    """{screen: {"ms", "px", "frames", "what"}} for every <screen>.idle.json of a shots directory"""
    out = {}
    d = Path(shots_dir)
    if not d.is_dir():
        return out
    for p in sorted(d.glob("*.idle.json")):
        screen = p.name[: -len(".idle.json")]
        try:
            rec = json.loads(p.read_text("utf-8"))
        except (OSError, ValueError):
            continue
        if not rec.get("ms"):
            continue
        try:
            tree = json.loads((d / f"{screen}.json").read_text("utf-8"))
        except (OSError, ValueError):
            tree = None
        out[screen] = {"ms": rec["ms"], "px": rec.get("px", 0), "frames": rec.get("frames", 0),
                       "what": what(rec, tree)}
    return out


def rate(rec):
    """Pixels a second"""
    return rec["px"] * 1000 / rec["ms"]


def fps(rec):
    """Refreshes a second"""
    return rec["frames"] * 1000 / rec["ms"]


def whole(v):
    return v >= 100 or v == int(v)


def num(v):
    """9600 -> «9 600», 0.5 -> «0,5»"""
    return f"{round(v):,}".replace(",", " ") if whole(v) else f"{v:.1f}".replace(".", ",")


def count(v, forms):
    """«28 098 пикселей», «1 кадр», «0,5 кадра»: a fraction takes the form of «двух»"""
    return f"{num(v)} {plural(round(v), forms) if whole(v) else forms[1]}"


def title(screen):
    app, _, page = screen.partition("__")
    return f"**{app}** · {page}" if page else f"**{app}**"


def widgets(rec):
    names = rec["what"]
    if not names:
        return "—"
    shown = ", ".join(names[:NAMED])
    return shown + (f" и ещё {len(names) - NAMED}" if len(names) > NAMED else "")


def speed(rec):
    """«28 098 пикселей и 1 кадр в секунду»"""
    return f"{count(rate(rec), PIXELS)} и {count(fps(rec), FRAMES)} в секунду"


def lines(base, head):
    """Markdown lines for the PR report; [] when there is nothing to say"""
    if not head:
        return []
    ms = next(iter(head.values()))["ms"]
    out = [f"**Перерисовка в покое** ({num(ms / 1000)} с без касаний после снимка; на часах это работа "
           "процессора и дисплея вместо сна)", ""]
    busy = sorted((s for s in head if head[s]["px"]), key=lambda s: (-rate(head[s]), s))
    total = sum(rate(head[s]) for s in busy)
    was = ""
    if base:
        before = sum(rate(r) for r in base.values() if r["px"])
        was = (" (до: столько же)" if round(before) == round(total) else
               " (до: ни одного)" if not before else f" (до: {count(before, PIXELS)})")
    n = len(busy)
    if not n:
        out.append(f"- ✅ Ни один из {len(head)} {plural(len(head), SCREENS_OF)} не перерисовывается сам")
    elif n == 1:
        r = head[busy[0]]
        out.append(f"- Перерисовывается 1 экран из {len(head)}: {title(busy[0])} — {speed(r)}, {widgets(r)}{was}")
    else:
        out.append(f"- {agree(n, 'Перерисовывается', 'Перерисовываются')} {n} {plural(n, SCREENS)} "
                   f"из {len(head)}, всего {count(total, PIXELS)} в секунду{was}")

    started, stopped, changed = [], [], []
    for s in sorted(set(head) | set(base)):
        a, b = base.get(s), head.get(s)
        if b and b["px"] and (not a or not a["px"]):
            started.append(f"{title(s)} — {speed(b)}, {widgets(b)}" + ("" if a else " (новый экран)"))
        elif a and a["px"] and b and not b["px"]:
            stopped.append(f"{title(s)} (было {count(rate(a), PIXELS)} в секунду)")
        elif a and b and a["px"] and abs(rate(b) - rate(a)) >= CHANGE * rate(a):
            changed.append(f"{title(s)}: {num(rate(a))} → {count(rate(b), PIXELS)} в секунду")
    if base and started:
        out.append(f"- ⚠️ {agree(len(started), 'Стал', 'Стали')} перерисовываться: " + "; ".join(started))
    if stopped:
        out.append(f"- {agree(len(stopped), 'Перестал', 'Перестали')} перерисовываться: " + "; ".join(stopped))
    if changed:
        out.append(f"- Изменилось на {round(CHANGE * 100)}% и больше: " + "; ".join(changed))
    if n > 1:
        out += ["", "<details><summary>Что перерисовывают</summary>", "",
                "| Экран | пикселей в секунду | кадров в секунду | что |", "|---|---|---|---|"]
        out += [f"| {title(s)} | {num(rate(head[s]))} | {num(fps(head[s]))} | {widgets(head[s])} |" for s in busy]
        out += ["", "</details>"]
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
