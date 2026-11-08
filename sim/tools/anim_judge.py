#!/usr/bin/env python3
"""anim_judge.py — Chromium as the judge of motion: do the transitions and @keyframes of an app
move as they do in a browser?

    python3 sim/tools/anim_judge.py --sim build/telaos_sim --frames out/anim
    python3 sim/tools/anim_judge.py --frames build/screens/anim-head --base build/screens/anim-base

The robot opens each page of the app (anim_tags) in the simulator and writes its elements
(`tree`, "dom") just before go() and at FRAMES ms after it. go() is the app's own: each .trigger
gets the class "go", each .disable the attribute disabled. Chromium without a window (Playwright)
does the same to the same page, pauses every animation (getAnimations()) and seeks it to the
moments the robot saw: the time of each tree after the one taken before go().

Judged: each element with an id, and each of its values that moves in Chromium (one of the frames
is not as before go()):
- its box (x, y, width, height), as the shift from where it stood before go(): where it stands is
  judge.py's question, here only how far it goes. Not for an element turned or scaled: Chromium
  gives the box around the turned element, LVGL the one before the turn;
- opacity, background-color, color;
- for an element turned or scaled: translate, scale, rotate of its transform.
A value agrees within TOLERANCE; a frame agrees when all its values do.

Without Playwright there is no judge: check() gives None, unmeasured() says why. The rest is the
Python standard library.
"""

import argparse
import json
import math
import re
import sys
import tempfile
from pathlib import Path

import judge

APP = "anim_tags"
FRAMES = (100, 250, 500, 750, 1000, 1500, 2000, 2500)  # ms after go()
BEFORE = "go"      # the tree just before go(): <app>__<page>~go
BOOT_MS = 500      # as sim_screens.py: the launcher is up
LAUNCH_MS = 1000   # after "app launch": its default page is drawn
NAV_MS = 400       # after "ui nav"
COMMAND_MS = 20    # a "> line" of the robot: the firmware runs it at once, then 20 ms pass
BOX = ("x", "y", "w", "h")
TURN = ("translate", "scale", "rotate")
PROPS = BOX + ("opacity", "background", "color") + TURN
TOLERANCE = {"x": 2, "y": 2, "w": 2, "h": 2, "opacity": 0.02, "background": 4, "color": 4,
             "translate": 2, "scale": 0.02, "rotate": 1}
ALPHA = 0.02  # a color's alpha
WORDS = {"x": "x", "y": "y", "w": "ширина", "h": "высота", "opacity": "opacity",
         "background": "background-color", "color": "color", "translate": "translate", "scale": "scale",
         "rotate": "rotate"}


# ============ The robot: the frames on the watch ============

def default_page(text):
    m = re.search(r"<ui\b[^>]*\bdefault=\"/?([^\"]+)\"", re.sub(r"<!--.*?-->", "", text, flags=re.S))
    return m.group(1) if m else None


def script(app, page, first):
    """The robot's script for one page: its elements before go() and at each of FRAMES after it"""
    lines = [f"wait {BOOT_MS}", f"> app launch {app}", f"wait {LAUNCH_MS}"]
    if page != first:
        lines += [f"> ui nav {page}", f"wait {NAV_MS}"]
    lines += [f"tree {app}__{page}~{BEFORE}", "> ui call go"]
    at = COMMAND_MS
    for ms in FRAMES:
        lines += [f"wait {ms - at}", f"tree {app}__{page}~{ms}"]
        at = ms
    return "\n".join(lines) + "\n"


def shoot(app_dir, out_dir, run):
    """The frames of every page into out_dir, each page in a simulator of its own (run(script file,
    out_dir) → (exit code, log)); the pages that failed"""
    app_dir, out_dir = Path(app_dir), Path(out_dir)
    app = app_dir.name
    text = (app_dir / f"{app}.bax").read_text("utf-8", "replace")
    pages, _ = judge.app_pages(text)
    out_dir.mkdir(parents=True, exist_ok=True)
    failed = []
    for page, _ in pages:
        f = out_dir / f"{page}.sim"
        f.write_text(script(app, page, default_page(text) or pages[0][0]))
        rc, log = run(f, out_dir)
        (out_dir / f"{page}.log").write_text(log)
        if rc != 0:
            failed.append(page)
    return failed


def rgba(hex_color, alpha):
    h = hex_color.lstrip("#")
    return (int(h[0:2], 16), int(h[2:4], 16), int(h[4:6], 16), alpha)


def watch_values(e):
    """The values of the watch's element, in Chromium's units; {} without an object of its own"""
    if "x" not in e:
        return {}
    v = {k: e[k] for k in BOX}
    s = e.get("style")
    if s:
        v.update(opacity=s["opa"] / 255, background=rgba(s["bg"], s["bg_opa"] / 255),
                 color=rgba(s["color"], s["color_opa"] / 255), translate=tuple(s["translate"]),
                 scale=(s["scale"][0] / 256, s["scale"][1] / 256), rotate=s["rotation"] / 10)
    return v


def by_id(elements, out=None):
    """{id: values} of the elements with an id, all through the tree"""
    out = {} if out is None else out
    for e in elements:
        if e.get("id"):
            out.setdefault(e["id"], watch_values(e))
        by_id(e.get("children", []), out)
    return out


def watch(app, page, frames_dir):
    """[(ms after go(), {id: values})...] of one page, the first one before go() (0 ms); None when
    a tree is missing or has no elements"""
    out = []
    for name in (BEFORE, *map(str, FRAMES)):
        try:
            tree = json.loads((Path(frames_dir) / f"{app}__{page}~{name}.json").read_text("utf-8"))
        except (OSError, ValueError):
            return None
        mine = judge.page_of(tree, page)
        if mine is None:
            return None
        out.append((tree["time_ms"], by_id(mine.get("children", []))))
    return [(ms - out[0][0], els) for ms, els in out]


# ============ Chromium ============

SEEK = """async (times) => {
  const faces = await Promise.all([...document.fonts].map(f => f.load().then(() => "", () => f.style + " " + f.weight)));
  await document.fonts.ready;
  const seen = () => {
    const out = {};
    for (const e of document.body.querySelectorAll("[id]")) {
      const cs = getComputedStyle(e), r = e.getBoundingClientRect();
      out[e.id] = {x: r.left, y: r.top, w: r.width, h: r.height, opacity: cs.opacity,
                   background: cs.backgroundColor, color: cs.color, transform: cs.transform};
    }
    return out;
  };
  const frames = [seen()];  // the style before go(): what a transition starts from
  for (const e of document.querySelectorAll(".trigger")) e.classList.add("go");
  for (const e of document.querySelectorAll(".disable")) e.setAttribute("disabled", "");
  const running = document.getAnimations();
  for (const a of running) a.pause();
  for (const t of times) {
    for (const a of running) a.currentTime = t;
    frames.push(seen());
  }
  return {failed: faces.filter(f => f), frames};
}"""

MISSING = ""  # why the last seek() gave nothing


def seek(documents, times, width, height):
    """{page: {ms: {id: Chromium's element}}} with 0 for before go(), and Chromium's version;
    (None, None) without one (MISSING)"""
    global MISSING
    try:
        from playwright.sync_api import sync_playwright
    except ImportError:
        MISSING = "нет Playwright"
        return None, None
    out = {}
    try:
        with tempfile.TemporaryDirectory() as tmp, sync_playwright() as p:
            browser = p.chromium.launch(args=["--font-render-hinting=none"])  # as judge.py
            page = browser.new_page(viewport={"width": width, "height": height}, device_scale_factor=1)
            for name, text in documents.items():
                f = Path(tmp) / f"{name}.html"
                f.write_text(text, "utf-8")
                page.goto(f.as_uri())
                got = page.evaluate(SEEK, list(times[name]))
                if got["failed"]:
                    MISSING = "Chromium не загрузил шрифты часов: " + ", ".join(got["failed"])
                    browser.close()
                    return None, None
                out[name] = dict(zip((0, *times[name]), got["frames"]))
            version = browser.version
            browser.close()
    except Exception as e:  # noqa: BLE001 — no browser installed, or it fell: the report says so
        MISSING = "Chromium не запустился: " + (str(e).strip().splitlines() or ["?"])[0][:160]
        return None, None
    MISSING = ""
    return out, version


def css_color(s):
    """rgb(1, 2, 3) / rgba(1, 2, 3, 0.5) → (1, 2, 3, 0.5); None for any other form"""
    m = re.fullmatch(r"rgba?\(\s*([\d.]+),\s*([\d.]+),\s*([\d.]+)(?:,\s*([\d.]+))?\s*\)", s.strip())
    if not m:
        return None
    return (float(m.group(1)), float(m.group(2)), float(m.group(3)), float(m.group(4) or 1))


def decompose(transform):
    """translate, scale and rotate (degrees) of a computed transform: none, matrix() or matrix3d()"""
    nums = [float(n) for n in re.findall(r"-?[\d.]+(?:e-?\d+)?", transform.split("(", 1)[-1])]
    if transform.startswith("matrix3d(") and len(nums) == 16:
        a, b, c, d, e, f = nums[0], nums[1], nums[4], nums[5], nums[12], nums[13]
    elif transform.startswith("matrix(") and len(nums) == 6:
        a, b, c, d, e, f = nums
    else:
        a, b, c, d, e, f = 1, 0, 0, 1, 0, 0
    sx = math.hypot(a, b)
    sy = (a * d - b * c) / sx if sx else 0.0
    return {"translate": (e, f), "scale": (sx, sy), "rotate": math.degrees(math.atan2(b, a))}


def chromium_values(e):
    v = {k: e[k] for k in BOX}
    v.update(opacity=float(e["opacity"]), background=css_color(e["background"]), color=css_color(e["color"]))
    v.update(decompose(e["transform"]))
    return v


# ============ Values and verdicts ============

def angle(a, b):
    """The difference of two angles, in degrees: 360° is 0°"""
    return abs((a - b + 180) % 360 - 180)


def differ(prop, a, b, tolerance=None):
    """a and b of prop further apart than the tolerance (0: any difference); None counts as apart"""
    tol = TOLERANCE[prop] if tolerance is None else tolerance
    if a is None or b is None:
        return a is not b
    if prop in ("background", "color"):
        alpha = ALPHA if tolerance is None else tolerance
        if a[3] <= alpha and b[3] <= alpha:
            return False  # both transparent: no color to see
        return abs(a[3] - b[3]) > alpha or any(abs(x - y) > tol for x, y in zip(a[:3], b[:3]))
    if prop == "rotate":
        return angle(a, b) > tol
    if prop in ("translate", "scale"):
        return any(abs(x - y) > tol for x, y in zip(a, b))
    return abs(a - b) > tol


STILL = 1e-3  # what Chromium gives for a value that does not move: the same, to its rounding


def turned(frames, id):
    """The element is rotated or scaled in one of Chromium's frames"""
    return any(id in f and (differ("rotate", f[id]["rotate"], 0, STILL) or differ("scale", f[id]["scale"], (1, 1), STILL))
               for f in frames)


def moving(frames):
    """{id: [props]}: the values that move in Chromium's frames (the first one before go())"""
    before, out = frames[0], {}
    for id, b in before.items():
        rotated = turned(frames, id)
        props = [p for p in PROPS if (p not in BOX or not rotated) and (p not in TURN or rotated) and
                 any(id in f and differ(p, b[p], f[id][p], STILL) for f in frames[1:])]
        if props:
            out[id] = props
    return out


def value(prop, now, before):
    """What is compared: a box as its shift from before go(), the rest as it is"""
    if now is None or prop not in now:
        return None
    if prop in BOX:
        return now[prop] - before[prop] if before and prop in before else None
    return now[prop]


def score(app, pages, watched, seen, version):
    """watched: {page: [(ms, {id: watch values})...]}; seen: {page: {ms: {id: Chromium's values}}}"""
    result = {"app": app, "chromium": version, "pages": {}, "props": {}, "frames": [], "verdicts": []}
    for page in pages:
        frames = watched[page]
        theirs = seen[page]
        moves = moving([theirs[0]] + [theirs[ms] for ms, _ in frames[1:]])
        mine_before = frames[0][1]
        for ms, mine in frames[1:]:
            vs = []
            for id, props in moves.items():
                for prop in props:
                    w = value(prop, mine.get(id), mine_before.get(id))
                    c = value(prop, theirs[ms].get(id), theirs[0][id])
                    vs.append({"page": page, "element": f"#{id}", "prop": prop, "ms": ms,
                               "ok": w is not None and not differ(prop, w, c), "watch": w, "chromium": c})
            if vs:
                result["frames"].append({"page": page, "ms": ms, "ok": all(v["ok"] for v in vs), "count": len(vs)})
            result["verdicts"] += vs
    for page in pages:
        fs = [f for f in result["frames"] if f["page"] == page]
        vs = [v for v in result["verdicts"] if v["page"] == page]
        result["pages"][page] = {"agree": sum(f["ok"] for f in fs), "count": len(fs),
                                 "values": len(vs), "agree_values": sum(v["ok"] for v in vs)}
    for prop in PROPS:
        vs = [v for v in result["verdicts"] if v["prop"] == prop]
        if vs:
            result["props"][prop] = {"agree": sum(v["ok"] for v in vs), "count": len(vs)}
    fs, vs = result["frames"], result["verdicts"]
    result.update(agree=sum(f["ok"] for f in fs), count=len(fs), agree_values=sum(v["ok"] for v in vs),
                  values=len(vs))
    return result


def check(app_dir, *frames_dirs, fonts=None):
    """The judge on one app: its pages in Chromium against the frames in each folder (the robot's
    trees, shoot()), Chromium seeked once to every moment any of them needs. One result per
    folder; None where the frames are missing, and for every folder without Playwright."""
    app_dir = Path(app_dir).resolve()
    app = app_dir.name
    fonts = Path(fonts).resolve() if fonts else Path(__file__).resolve().parents[2] / "fonts" / "ubuntu"
    bax = app_dir / f"{app}.bax"
    if not bax.is_file():
        return [None] * len(frames_dirs)
    text = bax.read_text("utf-8", "replace")
    pages, state = judge.app_pages(text)
    names = [page for page, _ in pages]
    watched = []
    for d in frames_dirs:
        got = {page: watch(app, page, d) for page in names}
        watched.append(got if names and all(got.values()) else None)
    if not any(watched):
        return [None] * len(frames_dirs)
    size = screen_of(app, names[0], next(d for d, w in zip(frames_dirs, watched) if w))
    times = {page: sorted({ms for w in watched if w for ms, _ in w[page][1:]}) for page in names}
    styles = "".join(re.findall(r"<style\b[^>]*>.*?</style>", outside_pages(text), flags=re.S))
    resources = app_dir / "resources"
    docs = {page: judge.document(styles + judge.standard(judge.fill(markup, state)), fonts,
                                 resources if resources.is_dir() else None) for page, markup in pages}
    seen, version = seek(docs, times, *size)
    if seen is None:
        return [None] * len(frames_dirs)
    seen = {page: {ms: {id: chromium_values(e) for id, e in frame.items()} for ms, frame in frames.items()}
            for page, frames in seen.items()}
    return [score(app, names, w, seen, version) if w else None for w in watched]


def outside_pages(text):
    """The .bax without its comments and pages: what the app gives all of them (<style>)"""
    text = re.sub(r"<!--.*?-->", "", text, flags=re.S)
    return re.sub(r"<page\b.*?</page>", "", text, flags=re.S)


def screen_of(app, page, frames_dir):
    tree = json.loads((Path(frames_dir) / f"{app}__{page}~{BEFORE}.json").read_text("utf-8"))
    return tree["screen"]["w"], tree["screen"]["h"]


# ============ In words ============

def unmeasured():
    """The report's line when Chromium gave nothing"""
    if not MISSING:
        return []
    return [f"**Анимации против Chromium:** не измерены — {MISSING} (docs/SIMULATOR.md)", ""]


def shown(prop, v):
    if v is None:
        return "нет"
    if prop in BOX:
        return f"{v:+.0f}"
    if prop in ("background", "color"):
        hexed = "#%02X%02X%02X" % tuple(round(x) for x in v[:3])
        return hexed if v[3] >= 1 - ALPHA else f"{hexed} {v[3] * 100:.0f}%"
    if prop == "translate":
        return f"{v[0]:.0f},{v[1]:.0f}"
    if prop == "scale":
        return f"{v[0]:.2f}" if abs(v[0] - v[1]) < STILL else f"{v[0]:.2f}×{v[1]:.2f}"
    if prop == "rotate":
        return f"{v % 360:.0f}°"
    return f"{v:.2f}"


def mismatch(v):
    """`transition` · `#fade` opacity, 250 мс: 1.00 → 0.80 (the watch → Chromium)"""
    if v["watch"] is None:
        return f"`{v['page']}` · `{v['element']}` {WORDS[v['prop']]}, {v['ms']} мс: нет на часах"
    return (f"`{v['page']}` · `{v['element']}` {WORDS[v['prop']]}, {v['ms']} мс: "
            f"{shown(v['prop'], v['watch'])} → {shown(v['prop'], v['chromium'])}")


def changed(head, base, ok, top=5):
    """The values that came to agree (or stopped) from base to head"""
    def key(v):
        return v["page"], v["element"], v["prop"], v["ms"]
    before = {key(v): v["ok"] for v in base["verdicts"]}
    now = [v for v in head["verdicts"] if v["ok"] == ok and before.get(key(v)) == (not ok)]
    if not now:
        return []
    names = ", ".join(f"`{v['page']}` · `{v['element']}` {WORDS[v['prop']]} {v['ms']} мс" for v in now[:top])
    more = f" и ещё {len(now) - top}" if len(now) > top else ""
    return [f"- {'Теперь совпадает' if ok else 'Перестало совпадать'}: {len(now)} — {names}{more}"]


def lines(head, base=None, details=20):
    """The report's part: frames and values that agree, how they moved, what differs"""
    if not head:
        return []
    a, n = head["agree"], head["count"]
    moved = ""
    if base and (base["agree"], base["count"], base["agree_values"]) != (a, n, head["agree_values"]):
        moved = (f" (было {base['agree']} из {base['count']}, значений "
                 f"{base['agree_values']} из {base['values']})")
    elif base:
        moved = ", столько же"
    out = [f"**Анимации против Chromium:** {head['app']}, кадры {FRAMES[0]}…{FRAMES[-1]} мс после go() — "
           f"**совпало кадров {a} из {n}**, значений {head['agree_values']} из {head['values']}{moved}", ""]
    if base:
        out += changed(head, base, True) + changed(head, base, False)
    out.append("- По страницам: " + " · ".join(f"`{p}` {s['agree']} из {s['count']}"
                                                 for p, s in head["pages"].items()))
    out.append("- По свойствам: " + " · ".join(f"{WORDS[p]} {s['agree']} из {s['count']}"
                                                 for p, s in head["props"].items()))
    out.append("")
    bad = [v for v in head["verdicts"] if not v["ok"]]
    if bad:
        out += [f"<details><summary>Не совпало значений: {len(bad)} (часы → Chromium; рамка — сдвиг от места "
                "до go(), px)</summary>", ""]
        out += [f"- {mismatch(v)}" for v in bad[:details]]
        if len(bad) > details:
            out.append(f"- И ещё {len(bad) - details}")
        out += ["", "</details>", ""]
    return out


def summary(result):
    """For status.json: the numbers without the verdicts"""
    if not result:
        return None
    return {k: v for k, v in result.items() if k != "verdicts"}


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--app", default=str(Path(__file__).resolve().parents[1] / "tests" / "fixtures" / "apps" / APP),
                    help="the app's folder (its .bax)")
    ap.add_argument("--frames", required=True, help="the robot's trees: <app>__<page>~<ms>.json")
    ap.add_argument("--sim", help="a simulator: take the frames with it first (into --frames)")
    ap.add_argument("--base", help="the same frames before a change: how the numbers moved")
    ap.add_argument("--json", help="write the verdicts here")
    ap.add_argument("--all", action="store_true", help="every value that does not agree")
    args = ap.parse_args()
    if args.sim:
        from sim_screens import run_sim  # here, not above: sim_screens imports this module
        extra = Path(args.app).resolve().parents[1]
        failed = shoot(args.app, args.frames, lambda f, out: run_sim(args.sim, out, script=f, extra_data=extra))
        if failed:
            print(f"anim_judge: the simulator stopped on {', '.join(failed)} (see {args.frames}/*.log)",
                  file=sys.stderr)
            return 1
    dirs = (args.frames, args.base) if args.base else (args.frames,)
    results = check(args.app, *dirs)
    if results[0] is None:
        print(f"anim_judge: {MISSING or 'no frames (a robot without style in the trees?)'}", file=sys.stderr)
        return 1
    if args.json:
        Path(args.json).write_text(json.dumps(results[0], indent=1, ensure_ascii=False), "utf-8")
    print("\n".join(lines(results[0], results[1] if args.base else None, details=10 ** 6 if args.all else 20)))
    print(f"Chromium {results[0]['chromium']}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
