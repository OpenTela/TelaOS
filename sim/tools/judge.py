#!/usr/bin/env python3
"""judge.py — Chromium as the judge: do the pages of an app lay out as they do in a browser?

    python3 sim/tools/judge.py --app sim/tests/fixtures/apps/html_tags --shots out/shots-head

Chromium without a window (Playwright) lays out each page of the app: the same markup, the
watch's fonts (Ubuntu in its four faces) and its screen for the window. Each element Chromium
gives a box of its own — a block, a list item, a table cell, a picture, a form control — is paired
with the same element of the app in the simulator: the same place in the tree of elements ("dom"
in the robot's `tree`). It agrees when its box — x, y, width, height — is the box of its object
on the watch within ±2 px. The share of the elements that agree is the compatibility.

Not counted:
- inline text (<b>, <a>, <span> in a paragraph): the watch draws it inside the paragraph's text,
  it has no box of its own to compare;
- an element neither of them shows (display: none, a closed <dialog>);
- pages in NOT_JUDGED, with the reason.

What the markup means on the watch beyond HTML is given to Chromium as CSS: w="60" and h="30" are
width and height, visible="false" is hidden. {var} is the state's default, as on the first screen.

Without Playwright (pip install playwright; python -m playwright install chromium) there is no
judge: check() gives None, unmeasured() says why. The rest is the Python standard library.
"""

import argparse
import base64
import html as htmllib
import json
import re
import sys
import tempfile
from pathlib import Path

TOLERANCE = 2  # px, each of x, y, width, height
FACES = (("Ubuntu-Regular", 400, "normal"), ("Ubuntu-Bold", 700, "normal"),
         ("Ubuntu-Italic", 400, "italic"), ("Ubuntu-BoldItalic", 700, "italic"))
# (app, page): why the judge does not judge it
NOT_JUDGED = {
    ("html_tags", "icons"): "иконки и знаки ✓ ★ ☀: у Chromium нет шрифта Material Symbols, "
                            "он нарисовал бы их чужим шрифтом",
}
DIMS = (("x", "x"), ("y", "y"), ("w", "ширина"), ("h", "высота"))


# ============ The pages, as Chromium is given them ============

def app_pages(text):
    """([(page id, its markup)...], {state variable: default}) of a .bax"""
    text = re.sub(r"<!--.*?-->", "", text, flags=re.S)
    pages = [(m.group(2), m.group(3)) for m in
             re.finditer(r"<page\b([^>]*?)\bid=\"([^\"]+)\"[^>]*>(.*?)</page>", text, flags=re.S)]
    state = {}
    m = re.search(r"<state\b[^>]*>(.*?)</state>", text, flags=re.S)
    for var in re.finditer(r"<\w+\b([^>]*)/?>", m.group(1) if m else ""):
        name = re.search(r"\bname=\"([^\"]*)\"", var.group(1))
        default = re.search(r"\bdefault=\"([^\"]*)\"", var.group(1))
        if name:
            state[name.group(1)] = htmllib.unescape(default.group(1)) if default else ""
    return pages, state


def fill(markup, state):
    """{var} → the state's default, as the first screen shows it; an unknown name stays"""
    return re.sub(r"\{(\w+)\}", lambda m: htmllib.escape(state[m.group(1)], quote=False)
                  if m.group(1) in state else m.group(0), markup)


ATTR = re.compile(r"""([^\s"'>/=]+)(?:\s*=\s*("[^"]*"|'[^']*'|[^\s"'>]+))?""")


def css_length(v):
    v = v.strip()
    return v + "px" if re.fullmatch(r"-?\d+(\.\d+)?", v) else v


def standard(markup):
    """The attributes of the watch that mean CSS (w, h, visible) given to Chromium as CSS"""
    def start_tag(m):
        name, attrs = m.group(1), m.group(2)
        found = {a.group(1).lower(): a for a in ATTR.finditer(attrs)}
        if not ({"w", "h"} & found.keys() or "visible" in found):
            return m.group(0)
        style, kept = [], []
        for a in ATTR.finditer(attrs):
            key = a.group(1).lower()
            value = (a.group(2) or "").strip("\"'")
            if key in ("w", "h"):
                style.append(f"{'width' if key == 'w' else 'height'}: {css_length(value)}")
            elif key == "visible":
                if value.lower() == "false":
                    kept.append("hidden")
            elif key == "style":
                continue
            else:
                kept.append(a.group(0))
        if "style" in found:
            style.append((found["style"].group(2) or "").strip("\"'"))
        if style:
            kept.append('style="' + "; ".join(s for s in style if s).replace('"', "&quot;") + '"')
        end = "/>" if m.group(0).endswith("/>") else ">"
        return f"<{name} " + " ".join(kept) + end
    return re.sub(r"<([A-Za-z][\w-]*)((?:\s+[^>]*?)?)\s*/?>", start_tag, markup)


def font_url(path):
    """The font inside the page: nothing for Chromium to fetch, wherever the page and the
    fonts lie"""
    try:
        return "data:font/ttf;base64," + base64.b64encode(Path(path).read_bytes()).decode("ascii")
    except OSError:
        return Path(path).as_uri()  # not there: Chromium says so (MEASURE checks the faces)


def document(body, fonts, resources, font_base=None):
    """The page as a document for Chromium: the watch's fonts and the app's pictures. With
    font_base the fonts are fetched from there (judge_pages.py serves them), not carried inside"""
    def src(f):
        return f"{font_base}/{f}.ttf" if font_base else font_url(Path(fonts) / f"{f}.ttf")
    faces = "".join(f"@font-face {{ font-family: Ubuntu; src: url('{src(f)}'); "
                    f"font-weight: {w}; font-style: {s}; }}\n" for f, w, s in FACES)
    base = f'<base href="{Path(resources).as_uri()}/">' if resources else ""
    # :where(): no weight, the page's own CSS wins; the UA's monospace is not a font of the watch
    return ("<!doctype html>\n<html><head><meta charset=\"utf-8\">" + base +
            f"<style>\n{faces}:where(*) {{ font-family: Ubuntu; }}\nhtml {{ scrollbar-width: none; }}\n</style>"
            f"</head><body>{body}</body></html>\n")


# ============ Chromium ============

MEASURE = """async () => {
  const faces = await Promise.all([...document.fonts].map(f => f.load().then(() => "", () => f.style + " " + f.weight)));
  await document.fonts.ready;
  const REPLACED = new Set(["img", "input", "button", "select", "textarea", "video", "audio",
    "canvas", "iframe", "object", "embed", "svg", "progress", "meter"]);
  function walk(e) {
    const cs = getComputedStyle(e), r = e.getBoundingClientRect();
    return {tag: e.localName, id: e.id || "",
      x: Math.round(r.left), y: Math.round(r.top), w: Math.round(r.width), h: Math.round(r.height),
      // not under display: none, a closed <details> (content-visibility) or visibility: hidden
      shown: e.checkVisibility({visibilityProperty: true}) && e.getClientRects().length > 0,
      boxed: (cs.display !== "inline" && cs.display !== "contents") || REPLACED.has(e.localName),
      children: [...e.children].map(walk)};
  }
  return {failed: faces.filter(f => f), elements: [...document.body.children].map(walk)};
}"""


MISSING = ""  # why the last layout() gave nothing


def layout(documents, width, height):
    """{page: [element trees]} of Chromium and its version; (None, None) without one (MISSING)"""
    global MISSING
    try:
        from playwright.sync_api import sync_playwright
    except ImportError:
        MISSING = "нет Playwright"
        return None, None
    out = {}
    try:
        with tempfile.TemporaryDirectory() as tmp, sync_playwright() as p:
            # Without hinting: the height of a line is then the font's, not what the machine's
            # fontconfig asks for (hinted, 16px Ubuntu is 20 px high on GitHub, 18 here and on a Mac)
            browser = p.chromium.launch(args=["--font-render-hinting=none"])
            page = browser.new_page(viewport={"width": width, "height": height}, device_scale_factor=1)
            for name, text in documents.items():
                f = Path(tmp) / f"{name}.html"
                f.write_text(text, "utf-8")
                page.goto(f.as_uri())
                got = page.evaluate(MEASURE)
                if got["failed"]:  # another font would stand in for the watch's: no number
                    MISSING = "Chromium не загрузил шрифты часов: " + ", ".join(got["failed"])
                    browser.close()
                    return None, None
                out[name] = got["elements"]
            version = browser.version
            browser.close()
    except Exception as e:  # noqa: BLE001 — no browser installed, or it fell: the report says so
        MISSING = "Chromium не запустился: " + (str(e).strip().splitlines() or ["?"])[0][:160]
        return None, None
    MISSING = ""
    return out, version


def unmeasured():
    """The report's line when Chromium gave nothing"""
    if not MISSING:
        return []
    return [f"**Совместимость с Chromium:** не измерена — {MISSING} (docs/SIMULATOR.md)", ""]


# ============ Pairs and verdicts ============

def align(ours, theirs):
    """Pairs (i, j) of children with the same tag, the most there can be in order (LCS)"""
    n, m = len(ours), len(theirs)
    best = [[0] * (m + 1) for _ in range(n + 1)]
    for i in range(n - 1, -1, -1):
        for j in range(m - 1, -1, -1):
            best[i][j] = (best[i + 1][j + 1] + 1 if ours[i]["tag"] == theirs[j]["tag"]
                          else max(best[i + 1][j], best[i][j + 1]))
    pairs, i, j = [], 0, 0
    while i < n and j < m:
        if ours[i]["tag"] == theirs[j]["tag"] and best[i][j] == best[i + 1][j + 1] + 1:
            pairs.append((i, j))
            i, j = i + 1, j + 1
        elif best[i][j + 1] >= best[i + 1][j]:
            j += 1  # Chromium's own first: it has the elements the watch does not record (<br>)
        else:
            i += 1
    return pairs


def box(e):
    return (e["x"], e["y"], e["w"], e["h"]) if "x" in e else None


def label(e, path):
    return f"{e['tag']}#{e['id']}" if e.get("id") else path


def verdict(t, c):
    """The verdict on Chromium's element c and the watch's t (None: the watch has none);
    None: nothing to judge (inline text, shown by neither)"""
    if t is None:
        return {"ok": False, "why": "нет на часах", "watch": None} if c["boxed"] and c["shown"] else None
    if not c["boxed"]:
        return None
    if not c["shown"]:
        if box(t) and not t.get("hidden"):
            return {"ok": False, "why": "Chromium не показывает", "watch": box(t)}
        return None
    if not box(t):
        return {"ok": False, "why": "на часах без своей рамки", "watch": None}
    if t.get("hidden"):
        return {"ok": False, "why": "на часах скрыт", "watch": box(t)}
    d = [a - b for a, b in zip(box(t), (c["x"], c["y"], c["w"], c["h"]))]
    return {"ok": all(abs(x) <= TOLERANCE for x in d), "why": "", "watch": box(t)}


def judged(ours, theirs, path):
    """(verdicts, the indexes of ours paired here). An element the watch does not have — <tbody>
    between <table> and <tr>, <picture> around <img> — may have its content one level up: the
    elements of ours left between the pairs around it stand for its children."""
    pairs = align(ours, theirs)
    of = dict((j, i) for i, j in pairs)
    used = {i for i, _ in pairs}
    out, seen = [], {}
    for j, c in enumerate(theirs):
        seen[c["tag"]] = seen.get(c["tag"], 0) + 1
        here = f"{path} > {c['tag']}" if path else c["tag"]
        if sum(1 for d in theirs if d["tag"] == c["tag"]) > 1:
            here += f":{seen[c['tag']]}"
        t = ours[of[j]] if j in of else None
        v = verdict(t, c)
        if v is not None:
            v.update(element=label(c, here), chromium=(c["x"], c["y"], c["w"], c["h"]) if c["shown"] else None)
            out.append(v)
        if t is not None:
            out += judged(t.get("children", []), c["children"], here)[0]
            continue
        lo = max((of[k] for k in of if k < j), default=-1)
        hi = min((of[k] for k in of if k > j), default=len(ours))
        free = [i for i in range(lo + 1, hi) if i not in used]
        sub, took = judged([ours[i] for i in free], c["children"], here)
        out += sub
        used |= {free[k] for k in took}
    return out, used


def verdicts(ours, theirs, path=""):
    """One verdict per element Chromium gives a box (or the watch shows and Chromium does not):
    {"element", "ok", "why", "watch", "chromium"}; ours: the watch's children, theirs: Chromium's"""
    return judged(ours, theirs, path)[0]


def page_of(tree, page):
    for p in tree.get("dom", []):
        if p.get("id") == page:
            return p
    return None


def screens_of(app, pages, shots):
    """{page: the watch's element of the page}, the screen's size; None: a screen is missing or
    has no elements (a robot older than "dom")"""
    trees, size = {}, None
    for page in pages:
        try:
            tree = json.loads((Path(shots) / f"{app}__{page}.json").read_text("utf-8"))
        except (OSError, ValueError):
            return None, None
        mine = page_of(tree, page)
        if mine is None:
            return None, None
        trees[page] = mine
        size = size or (tree["screen"]["w"], tree["screen"]["h"])
    return trees, size


def check(app_dir, *shots, fonts=None):
    """The judge on one app: its pages in Chromium against its screens in each shots/ folder (the
    robot's trees with "dom"), Chromium laid out once. One result per folder; None where its
    screens have no elements, and for every folder without Playwright."""
    app_dir = Path(app_dir).resolve()
    app = app_dir.name
    fonts = Path(fonts).resolve() if fonts else Path(__file__).resolve().parents[2] / "fonts" / "ubuntu"
    bax = app_dir / f"{app}.bax"
    if not bax.is_file():
        return [None] * len(shots)
    pages, state = app_pages(bax.read_text("utf-8", "replace"))
    skipped = {page: NOT_JUDGED[(app, page)] for page, _ in pages if (app, page) in NOT_JUDGED}
    judged_pages = [page for page, _ in pages if page not in skipped]
    screens = [screens_of(app, judged_pages, s) for s in shots]
    size = next((sz for trees, sz in screens if trees), None)
    if not judged_pages or size is None:
        return [None] * len(shots)
    resources = app_dir / "resources"
    docs = {page: document(standard(fill(markup, state)), fonts, resources if resources.is_dir() else None)
            for page, markup in pages if page in judged_pages}
    laid, version = layout(docs, *size)
    if laid is None:
        return [None] * len(shots)
    return [score(app, judged_pages, trees, laid, version, size, skipped) if trees else None
            for trees, _ in screens]


def score(app, pages, trees, laid, version, size, skipped):
    result = {"app": app, "chromium": version, "screen": list(size), "pages": {}, "skipped": skipped,
              "verdicts": []}
    for page in pages:
        vs = verdicts(trees[page].get("children", []), laid[page])
        for v in vs:
            v["page"] = page
        result["pages"][page] = {"agree": sum(v["ok"] for v in vs), "count": len(vs)}
        result["verdicts"] += vs
    vs = result["verdicts"]
    result["agree"], result["count"] = sum(v["ok"] for v in vs), len(vs)
    both = [v for v in vs if v["watch"] and v["chromium"]]
    result["boxes"] = len(both)
    result["dims"] = {key: sum(abs(v["watch"][k] - v["chromium"][k]) <= TOLERANCE for v in both)
                      for k, (key, _) in enumerate(DIMS)}
    return result


# ============ In words ============

def percent(a, n):
    return f"{100.0 * a / n:.0f}%" if n else "—"


def frame(b):
    return f"{b[0]},{b[1]} {b[2]}×{b[3]}"


def mismatch(v):
    """`text` · h2#h2: y 8 → 20, высота 36 → 28 (the watch → Chromium)"""
    if v["why"]:
        what = v["why"]
        if v["chromium"]:
            what += f" (в Chromium {frame(v['chromium'])})"
        return f"`{v['page']}` · `{v['element']}`: {what}"
    diffs = [f"{word} {a} → {b}" for (_, word), a, b in zip(DIMS, v["watch"], v["chromium"])
             if abs(a - b) > TOLERANCE]
    return f"`{v['page']}` · `{v['element']}`: " + ", ".join(diffs)


def patterns(vs, top=5):
    """The differences most elements share: (word, watch - Chromium, how many)"""
    seen = {}
    for v in vs:
        if v["ok"] or not (v["watch"] and v["chromium"]):
            continue
        for k, (_, word) in enumerate(DIMS):
            d = v["watch"][k] - v["chromium"][k]
            if abs(d) > TOLERANCE and k in (2, 3):  # sizes: x and y follow what is above
                seen[(word, d)] = seen.get((word, d), 0) + 1
    return sorted(((w, d, n) for (w, d), n in seen.items()), key=lambda t: (-t[2], t[0], t[1]))[:top]


def turned(head, base, ok, top=5):
    """The elements whose verdict became ok (or stopped being ok) from base to head"""
    before = {(v["page"], v["element"]): v["ok"] for v in base["verdicts"]}
    now = [v for v in head["verdicts"] if v["ok"] == ok and before.get((v["page"], v["element"])) == (not ok)]
    if not now:
        return []
    names = ", ".join(f"`{v['page']}` · `{v['element']}`" for v in now[:top])
    more = f" и ещё {len(now) - top}" if len(now) > top else ""
    return [f"- {'Теперь совпадает' if ok else 'Перестало совпадать'}: {len(now)} — {names}{more}"]


def lines(head, base=None, details=20, title=None):
    """The report's part: the number, how it moved, and what differs most"""
    if not head:
        return []
    a, n = head["agree"], head["count"]
    moved = ""
    if base and (base["agree"], base["count"]) != (a, n):
        moved = f" (было {base['agree']} из {base['count']}, {percent(base['agree'], base['count'])})"
    elif base:
        moved = ", столько же"
    title = title or f"**Совместимость с Chromium:** {head['app']}, рамки элементов ±{TOLERANCE} px"
    out = [f"{title} — **{a} из {n}, {percent(a, n)}**{moved}", ""]
    if base:
        out += turned(head, base, True) + turned(head, base, False)
    b = head["boxes"]

    def dim(key, word):
        now = percent(head["dims"][key], b)
        was = percent(base["dims"][key], base["boxes"]) if base else now
        return f"{word} {now}" + (f" (было {was})" if was != now else "")
    out.append(f"- По отдельности, из {b} рамок: " + " · ".join(dim(key, word) for key, word in DIMS))
    for word, d, k in patterns(head["verdicts"]):
        out.append(f"- {word.capitalize()} на часах {'больше' if d > 0 else 'меньше'} на {abs(d)} px — у {k}")
    for page, why in sorted(head["skipped"].items()):
        out.append(f"- Не судится `{page}`: {why}")
    out.append("")
    bad = [v for v in head["verdicts"] if not v["ok"]]
    if bad:
        out += [f"<details><summary>Не совпало: {len(bad)} (часы → Chromium)</summary>", ""]
        out += [f"- {mismatch(v)}" for v in bad[:details]]
        if len(bad) > details:
            out.append(f"- И ещё {len(bad) - details}")
        out += ["", "</details>", ""]
    return out


def summary(result):
    """For status.json: the number without the verdicts"""
    if not result:
        return None
    return {k: v for k, v in result.items() if k != "verdicts"}


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--app", required=True, help="the app's folder (its .bax and resources/)")
    ap.add_argument("--shots", required=True, help="the screens of the app: <app>__<page>.json")
    ap.add_argument("--base", help="the same screens before a change: how the number moved")
    ap.add_argument("--json", help="write the verdicts here")
    ap.add_argument("--all", action="store_true", help="every element that does not agree")
    args = ap.parse_args()
    result, base = check(args.app, args.shots, args.base) if args.base else (check(args.app, args.shots)[0], None)
    if result is None:
        print("judge: no Playwright, or the screens have no \"dom\" (python3 -m pip install playwright; "
              "python3 -m playwright install chromium)", file=sys.stderr)
        return 1
    if args.json:
        Path(args.json).write_text(json.dumps(result, indent=1, ensure_ascii=False), "utf-8")
    print("\n".join(lines(result, base, details=10 ** 6 if args.all else 20)))
    print(f"Chromium {result['chromium']}, screen {result['screen'][0]}×{result['screen'][1]}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
