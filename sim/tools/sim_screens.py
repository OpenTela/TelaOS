#!/usr/bin/env python3
"""sim_screens.py — every screen of every app, before and after a change.

    python3 sim/tools/sim_screens.py --head . --base ../telaos-base --out build/screens

Builds the simulator (sim/ of --head) against both checkouts, photographs every page of
every app the firmware lists (data/apps, native apps, sim/tests/fixtures/apps), runs the
robot tests (sim/tests/*.sim) on --head, compares the screens pixel by pixel and the widget
trees of every screen (the same pixels over another tree: changed unseen), and writes:

    out/report.md        the PR comment: what changed, in words and in pictures
                         (images from --image-url, if given), the watch's memory on every
                         screen, what the screens redraw at rest (idle_report.py), how much C
                         is left in src/, how the pages of JUDGED lay out
                         against Chromium (judge.py), how the animations of ANIMATED move
                         against it (anim_judge.py), and with --pages N: N random pages
                         through both builds (sim_pages.py) and N random pages of plain
                         HTML and CSS laid out by both and by Chromium (judge_pages.py)
    out/judge.json       the judge's verdict on every element of those pages
    out/anim_judge.json  and on every value of those animations, frame by frame
    out/publish/         before/ after/ diff/ — only the screens that changed
    out/status.json      machine-readable summary
    out/shots-base/ out/shots-head/ out/tests/   everything, for artifacts (a .png and the
                         widget tree .json of every screen, and .idle.json: what it redrew
                         at rest)
    out/anim-base/ out/anim-head/   the frames of the animations (the robot's trees)

Exit code 1 if --head does not build or a robot test fails. A changed screen is not a
failure: it is what a reviewer looks at. Python standard library only; the judge takes
Chromium from Playwright, and without it the report has no compatibility.
"""

import argparse
import concurrent.futures
import json
import os
import re
import shutil
import struct
import subprocess
import sys
import zlib
from pathlib import Path

from code_meter import measure as measure_code, summary as code_summary
import anim_judge
import idle_report
import judge
import judge_pages
import mem_report
import sim_pages
from tree_diff import describe

MARKER = "<!-- telaos-sim-screens -->"
SAME_TREE = "Виджеты те же — изменились только цвета, шрифты или картинки"
BOOT_MS = 500     # virtual ms before the first command: the launcher is up
LAUNCH_MS = 1000  # after "app launch": the app's default page is drawn
NAV_MS = 400      # after "ui nav": the page is drawn
IDLE_MS = 2000    # after each screen's picture: at rest, what it redraws by itself (idle_report.py)
MAX_ROWS = 30     # screens with pictures in the comment; the rest by name
JUDGED = "html_tags"  # the fixture app whose pages Chromium judges (judge.py)
ANIMATED = "anim_tags"  # and whose animations it judges, frame by frame (anim_judge.py)


# ============ Firmware: build, list apps, run ============

def build(sim_dir, telaos_dir, build_dir, log, options=()):
    """Configure and build telaos_sim for telaos_dir; returns the binary or None.
    options: more cmake -D..., -DTELA_SIM_ASAN=ON say (sim_asan.py)"""
    cfg = ["cmake", "-S", str(sim_dir), "-B", str(build_dir), f"-DTELAOS_DIR={telaos_dir}",
           "-DCMAKE_BUILD_TYPE=Release", *options]
    if shutil.which("ccache"):
        cfg += ["-DCMAKE_C_COMPILER_LAUNCHER=ccache", "-DCMAKE_CXX_COMPILER_LAUNCHER=ccache"]
    if shutil.which("ninja"):
        cfg += ["-G", "Ninja"]
    with open(log, "w") as f:
        for cmd in (cfg, ["cmake", "--build", str(build_dir), "-j", str(os.cpu_count() or 2)]):
            f.write("$ " + " ".join(cmd) + "\n")
            f.flush()
            if subprocess.run(cmd, stdout=f, stderr=subprocess.STDOUT).returncode != 0:
                return None
    binary = Path(build_dir) / "telaos_sim"
    return binary if binary.exists() else None


def run_sim(binary, out_dir, script=None, run=None, extra_data=None, timeout=300):
    cmd = [str(binary), "--headless", "--out", str(out_dir)]
    if extra_data and Path(extra_data).is_dir():
        cmd += ["--extra-data", str(extra_data)]
    if script:
        cmd += ["--script", str(script)]
    if run:
        cmd += ["--run", run]
    try:
        p = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=timeout)
        return p.returncode, p.stdout.decode("utf-8", "replace")
    except subprocess.TimeoutExpired as e:
        out = (e.stdout or b"").decode("utf-8", "replace")
        return -1, out + f"\n[sim_screens] timeout after {timeout}s\n"


def firmware_apps(binary, extra_data, tmp, roots):
    """App names in launcher order, as the firmware prints them at boot. If that table is
    gone (the boot log changed), the app folders: fewer apps (no native ones), never none."""
    _, out = run_sim(binary, tmp, run="wait 100", extra_data=extra_data, timeout=120)
    apps, inside = [], False
    for line in out.splitlines():
        if re.match(r"=+ APPS \(\d+\) =+", line.strip()):
            inside = True
            continue
        if inside:
            m = re.match(r"\s+(\S+)\s+\"", line)
            if not m:
                break
            apps.append(m.group(1))
    if not apps:
        print("[screens] warning: no app table in the boot log, taking app folders instead", flush=True)
        apps = sorted({p.parent.name for root in roots for p in Path(root).glob("apps/*/*.bax")
                       if p.stem == p.parent.name})
    return apps


def bax_pages(app, roots):
    """(name of the first screen, [(ui nav path, screen name)...]) from the app's .bax.
    Pages of a <group> are reached as "group/page". Native apps have none: (None, [])."""
    for root in roots:
        bax = Path(root) / "apps" / app / f"{app}.bax"
        if bax.is_file():
            text = re.sub(r"<!--.*?-->", "", bax.read_text("utf-8", "replace"), flags=re.S)
            paths, groups = {}, {}  # page id -> nav path; group id -> its default page
            for g in re.finditer(r"<group\b([^>]*)>(.*?)</group>", text, flags=re.S):
                gid = re.search(r"\bid=\"([^\"]+)\"", g.group(1))
                pages = re.findall(r"<page\b[^>]*\bid=\"([^\"]+)\"", g.group(2))
                if not gid or not pages:
                    continue
                gdef = re.search(r"\bdefault=\"([^\"]+)\"", g.group(1))
                groups[gid.group(1)] = gdef.group(1) if gdef else pages[0]
                for page in pages:
                    paths[page] = f"{gid.group(1)}/{page}"
            for page in re.findall(r"<page\b[^>]*\bid=\"([^\"]+)\"", re.sub(r"<group\b.*?</group>", "", text, flags=re.S)):
                paths.setdefault(page, page)
            m = re.search(r"<ui\b[^>]*\bdefault=\"/?([^\"]+)\"", text)
            target = m.group(1) if m else next(iter(paths), "main")
            first = groups.get(target, target.rsplit("/", 1)[-1])
            return first, [(path, page) for page, path in paths.items() if page != first]
    return None, []


def snap(name):
    """The picture of a screen and its widget tree, taken at the same moment; then IDLE_MS
    without a touch, and what the screen redrew by itself meanwhile."""
    return [f"shot {name}", f"tree {name}", f"idle {name} {IDLE_MS}"]


def screens_script(app, first, pages):
    lines = [f"wait {BOOT_MS}", f"> app launch {app}", f"wait {LAUNCH_MS}",
             *snap(f"{app}__{first}" if first else app)]
    for path, name in pages:
        lines += [f"> ui nav {path}", f"wait {NAV_MS}", *snap(f"{app}__{name}")]
    return "\n".join(lines) + "\n"


def shoot(binary, roots, extra_data, out_dir, work):
    """Every screen into out_dir: the launcher, then each app in its own simulator process."""
    out_dir.mkdir(parents=True, exist_ok=True)
    work.mkdir(parents=True, exist_ok=True)
    jobs = [("launcher", "\n".join([f"wait {LAUNCH_MS}", *snap("launcher")]) + "\n")]
    for app in firmware_apps(binary, extra_data, work / "boot", roots):
        first, pages = bax_pages(app, roots)
        jobs.append((app, screens_script(app, first, pages)))

    def one(job):
        name, text = job
        script = work / f"{name}.sim"
        script.write_text(text)
        rc, log = run_sim(binary, out_dir, script=script, extra_data=extra_data)
        (work / f"{name}.log").write_text(log)
        return name, rc

    with concurrent.futures.ThreadPoolExecutor(max_workers=os.cpu_count() or 2) as ex:
        results = list(ex.map(one, jobs))
    return [name for name, rc in results if rc != 0]


def anim_frames(binary, fixtures, out_dir):
    """The frames of ANIMATED's animations into out_dir (anim_judge.py); the pages the simulator
    stopped on"""
    return anim_judge.shoot(Path(fixtures) / "apps" / ANIMATED, out_dir,
                            lambda script, out: run_sim(binary, out, script=script, extra_data=fixtures))


def run_tests(binary, tests_dir, extra_data, out_dir):
    results = []
    for script in sorted(Path(tests_dir).glob("*.sim")):
        shots = out_dir / script.stem
        rc, log = run_sim(binary, shots, script=script, extra_data=extra_data)
        (out_dir / f"{script.stem}.log").write_text(log)
        m = re.search(r"robot: (\d+) passed, (\d+) failed", log)
        passed, failed = (int(m.group(1)), int(m.group(2))) if m else (0, 1)
        fails = [l.split("✗", 1)[1].strip() for l in log.splitlines() if "✗" in l]
        results.append({"name": script.stem, "passed": passed, "failed": failed,
                        "ok": rc == 0 and failed == 0, "errors": fails[:5]})
    return results


# ============ PNG (8-bit RGB/RGBA, as the simulator writes) ============

def read_png(path):
    data = Path(path).read_bytes()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError(f"{path}: not a PNG")
    pos, idat, w = 8, [], 0
    while pos < len(data):
        n, kind = struct.unpack(">I4s", data[pos:pos + 8])
        body = data[pos + 8:pos + 8 + n]
        pos += 12 + n
        if kind == b"IHDR":
            w, h, depth, ctype, _, _, interlace = struct.unpack(">IIBBBBB", body)
            if depth != 8 or ctype not in (2, 6) or interlace:
                raise ValueError(f"{path}: only 8-bit RGB/RGBA, non-interlaced")
            bpp = 3 if ctype == 2 else 4
        elif kind == b"IDAT":
            idat.append(body)
        elif kind == b"IEND":
            break
    raw = zlib.decompress(b"".join(idat))
    stride = w * bpp
    out = bytearray(h * w * 3)
    prev = bytearray(stride)
    for y in range(h):
        f = raw[y * (stride + 1)]
        row = bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        if f == 1:
            for i in range(bpp, stride):
                row[i] = (row[i] + row[i - bpp]) & 255
        elif f == 2:
            row = bytearray((a + b) & 255 for a, b in zip(row, prev))
        elif f == 3:
            for i in range(stride):
                left = row[i - bpp] if i >= bpp else 0
                row[i] = (row[i] + ((left + prev[i]) >> 1)) & 255
        elif f == 4:
            for i in range(stride):
                a = row[i - bpp] if i >= bpp else 0
                b = prev[i]
                c = prev[i - bpp] if i >= bpp else 0
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                row[i] = (row[i] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        prev = row
        if bpp == 3:
            out[y * w * 3:(y + 1) * w * 3] = row
        else:
            out[y * w * 3:(y + 1) * w * 3] = bytes(v for i, v in enumerate(row) if i % 4 != 3)
    return w, h, bytes(out)


def write_png(path, w, h, rgb):
    def chunk(kind, body):
        return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body))
    raw = b"".join(b"\0" + rgb[y * w * 3:(y + 1) * w * 3] for y in range(h))
    Path(path).write_bytes(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)) +
                           chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


def diff_image(before, after):
    """(changed pixel count, diff picture): the new screen dimmed to grey, changed pixels in
    magenta, a yellow frame around them so a one-pixel change is still easy to find."""
    w, h, a = before
    w2, h2, b = after
    if (w, h) != (w2, h2):
        return w2 * h2, None
    out = bytearray(len(b))
    changed, box = 0, [w, h, -1, -1]
    for i in range(0, len(b), 3):
        if a[i:i + 3] != b[i:i + 3]:
            changed += 1
            out[i:i + 3] = b"\xff\x00\xff"
            x, y = (i // 3) % w, (i // 3) // w
            box = [min(box[0], x), min(box[1], y), max(box[2], x), max(box[3], y)]
        else:
            g = (b[i] * 30 + b[i + 1] * 59 + b[i + 2] * 11) // 300
            out[i:i + 3] = bytes((g, g, g))
    if changed:
        x1, y1 = max(box[0] - 3, 0), max(box[1] - 3, 0)
        x2, y2 = min(box[2] + 3, w - 1), min(box[3] + 3, h - 1)
        for t in range(2):  # 2 px frame
            for x in range(x1, x2 + 1):
                for y in (min(y1 + t, h - 1), max(y2 - t, 0)):
                    out[(y * w + x) * 3:(y * w + x) * 3 + 3] = b"\xff\xd5\x4f"
            for y in range(y1, y2 + 1):
                for x in (min(x1 + t, w - 1), max(x2 - t, 0)):
                    out[(y * w + x) * 3:(y * w + x) * 3 + 3] = b"\xff\xd5\x4f"
    return changed, (w, h, bytes(out))


def changes(before, after):
    """What changed on a screen, in words, from its two widget trees; None if a tree is missing."""
    try:
        trees = [json.loads(Path(p).read_text("utf-8")) for p in (before, after)]
    except (OSError, ValueError):
        return None
    return describe(*trees) or [SAME_TREE]


def compare(base_dir, head_dir, publish):
    names = sorted({p.stem for d in (base_dir, head_dir) if d.is_dir() for p in d.glob("*.png")})
    entries = []
    for name in names:
        a, b = base_dir / f"{name}.png", head_dir / f"{name}.png"
        e = {"name": name}
        if not a.exists():
            e["status"] = "added"
        elif not b.exists():
            e["status"] = "removed"
        elif a.read_bytes() == b.read_bytes():
            e["status"] = "same"
        else:
            before, after = read_png(a), read_png(b)
            changed, img = diff_image(before, after)
            if changed == 0:
                e["status"] = "same"  # same pixels, different encoding
            else:
                e["status"] = "changed"
                e["pixels"] = changed
                e["percent"] = round(100.0 * changed / (after[0] * after[1]), 2)
                if img:
                    (publish / "diff").mkdir(parents=True, exist_ok=True)
                    write_png(publish / "diff" / f"{name}.png", *img)
                words = changes(base_dir / f"{name}.json", head_dir / f"{name}.json")
                if words is not None:
                    e["changes"] = words
        if e["status"] == "same":
            # The same pixels over a different widget tree: something no picture shows (an id,
            # an element out of sight, a hidden one's text) changed
            words = changes(base_dir / f"{name}.json", head_dir / f"{name}.json")
            if words and words != [SAME_TREE]:
                e["status"] = "unseen"
                e["changes"] = words
        for kind, src in (("before", a), ("after", b)):
            if e["status"] not in ("same", "unseen") and src.exists():
                (publish / kind).mkdir(parents=True, exist_ok=True)
                shutil.copyfile(src, publish / kind / f"{name}.png")
        entries.append(e)
    return entries


# ============ Report: the PR comment ============

def screen_title(name):
    app, _, page = name.partition("__")
    return f"**{app}** · {page}" if page else f"**{app}**"


def img(url, kind, name, width=200):
    return f'<img src="{url}/{kind}/{name}.png" width="{width}">' if url else "—"


def described(entries):
    """A list item per screen: its changes in words, one line or nested"""
    out = []
    for e in entries[:MAX_ROWS]:
        words = e["changes"]
        if len(words) == 1:
            w = words[0]
            w = w if w[1:2].isupper() else w[:1].lower() + w[1:]  # "QR-код" stays
            out.append(f"- {screen_title(e['name'])}: {w}")
        else:
            out.append(f"- {screen_title(e['name'])}")
            out += [f"    - {w}" for w in words]  # 4 spaces: nested on GitHub and everywhere
    if len(entries) > MAX_ROWS:
        out.append("- И ещё: " + ", ".join(screen_title(e["name"]) for e in entries[MAX_ROWS:]))
    return out


def report(entries, tests, meta, url):
    changed = [e for e in entries if e["status"] == "changed"]
    unseen = [e for e in entries if e["status"] == "unseen"]
    added = [e for e in entries if e["status"] == "added"]
    removed = [e for e in entries if e["status"] == "removed"]
    same = [e for e in entries if e["status"] == "same"]
    apps = {e["name"].partition("__")[0] for e in entries} - {"launcher"}

    out = [MARKER, "## Экраны в симуляторе", ""]
    rev = f"`{meta['base']}` → `{meta['head']}` · " if meta.get("base") and meta.get("head") else ""
    quietly = f"незаметно: {len(unseen)}, " if unseen else ""
    out.append(f"{rev}приложений: {len(apps)}, экранов: {len(entries)} — **изменилось: {len(changed)}**, "
               f"{quietly}новых: {len(added)}, пропало: {len(removed)}, без изменений: {len(same)}.")
    out.append("")
    if not meta.get("base_built", True):
        out += ["> ⚠️ База не собралась в симуляторе — ниже только экраны этого PR.", ""]

    if tests:
        cells = [f"{'✅' if t['ok'] else '❌'} `{t['name']}` {t['passed']}/{t['passed'] + t['failed']}"
                 for t in tests]
        out += ["**Тесты робота:** " + " · ".join(cells), ""]
        for t in tests:
            for err in t["errors"]:
                out.append(f"- `{t['name']}`: {err}")
        if any(t["errors"] for t in tests):
            out.append("")
    if meta.get("code"):
        out += [code_summary(*meta["code"]), ""]
    out += meta.get("judge", [])
    out += meta.get("judge_pages", [])
    out += meta.get("motion", [])
    out += meta.get("memory", [])
    out += meta.get("idle", [])
    out += meta.get("pages", [])

    in_words = [e for e in changed if e.get("changes")]
    if in_words:
        out += ["**Что изменилось**", ""] + described(in_words) + [""]
    if changed:
        out += ["| Экран | До | После | Разница |", "|---|---|---|---|"]
        for e in changed[:MAX_ROWS]:
            out.append(f"| {screen_title(e['name'])}<br>{e['percent']}% пикселей | {img(url, 'before', e['name'])} | "
                       f"{img(url, 'after', e['name'])} | {img(url, 'diff', e['name'])} |")
        out.append("")
        if len(changed) > MAX_ROWS:
            out += ["И ещё: " + ", ".join(screen_title(e["name"]) for e in changed[MAX_ROWS:]), ""]
    if unseen:
        out += ["**Изменилось незаметно** — пиксели те же, а дерево виджетов другое", ""]
        out += described(unseen) + [""]
    if added:
        out += ["**Новые экраны**", "", "| Экран | После |", "|---|---|"]
        out += [f"| {screen_title(e['name'])} | {img(url, 'after', e['name'])} |" for e in added[:MAX_ROWS]]
        out.append("")
        if len(added) > MAX_ROWS:
            out += ["И ещё: " + ", ".join(screen_title(e["name"]) for e in added[MAX_ROWS:]), ""]
    if removed:
        out += ["**Пропали:** " + ", ".join(screen_title(e["name"]) for e in removed), ""]
    if not (changed or added or removed):
        out += ["✅ На вид ни один экран не изменился." if unseen else "✅ Ни один экран не изменился.", ""]
    if same:
        by_app = {}
        for e in same:
            app, _, page = e["name"].partition("__")
            by_app.setdefault(app, []).append(page or "—")
        out += [f"<details><summary>Без изменений: {len(same)}</summary>", ""]
        out += [f"- **{app}**: {', '.join(pages)}" for app, pages in sorted(by_app.items())]
        out += ["", "</details>", ""]
    if not url and (changed or added):
        out += ["Картинки — в артефакте `sim-screens` этого прогона.", ""]
    out.append("<sub>Настоящая прошивка TelaOS на ПК: LVGL 9.2, шрифты, Lua, data/ — "
               "sim/tools/sim_screens.py, docs/SIMULATOR.md</sub>")
    return "\n".join(out) + "\n"


# ============ Main ============

def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--head", required=True, help="TelaOS checkout with the change")
    ap.add_argument("--base", help="TelaOS checkout to compare with (e.g. the PR base)")
    ap.add_argument("--out", required=True)
    ap.add_argument("--sim", help="simulator sources (default: <head>/sim)")
    ap.add_argument("--image-url", default="", help="where out/publish/ will be served from")
    ap.add_argument("--base-label", default="")
    ap.add_argument("--head-label", default="")
    ap.add_argument("--pages", type=int, default=0, help="random pages through both builds (sim_pages.py)")
    ap.add_argument("--pages-seed", type=int, default=1)
    args = ap.parse_args()

    head = Path(args.head).resolve()
    base = Path(args.base).resolve() if args.base else None
    out = Path(args.out).resolve()
    for old in ("publish", "anim-head", "anim-base"):  # nothing of an earlier run is judged again
        shutil.rmtree(out / old, ignore_errors=True)
    out.mkdir(parents=True, exist_ok=True)
    sim_dir = Path(args.sim).resolve() if args.sim else head / "sim"
    fixtures = sim_dir / "tests" / "fixtures"

    print("[screens] building the simulator for head ...", flush=True)
    head_bin = build(sim_dir, head, out / "build-head", out / "build-head.log")
    if not head_bin:
        sys.stdout.write((out / "build-head.log").read_text()[-4000:])
        print("[screens] head does not build", flush=True)
        (out / "status.json").write_text(json.dumps({"ok": False, "head_built": False}))
        (out / "report.md").write_text(f"{MARKER}\n## Экраны в симуляторе\n\n❌ PR не собирается в симуляторе: "
                                      "лог — в артефакте `sim-screens`.\n")
        return 1
    base_bin = None
    if base:
        print("[screens] building the simulator for base ...", flush=True)
        base_bin = build(sim_dir, base, out / "build-base", out / "build-base.log")
        if not base_bin:
            print("[screens] base does not build: showing head screens only", flush=True)

    print("[screens] shooting every screen ...", flush=True)
    head_errors = shoot(head_bin, [head / "data", fixtures], fixtures, out / "shots-head", out / "work-head")
    if base_bin:
        shoot(base_bin, [base / "data", fixtures], fixtures, out / "shots-base", out / "work-base")
    print("[screens] frames of the animations ...", flush=True)
    anim_errors = anim_frames(head_bin, fixtures, out / "anim-head")
    if base_bin:
        anim_frames(base_bin, fixtures, out / "anim-base")

    print("[screens] robot tests ...", flush=True)
    tests = run_tests(head_bin, sim_dir / "tests", fixtures, out / "tests")
    for name in head_errors:
        tests.append({"name": f"screens:{name}", "passed": 0, "failed": 1, "ok": False,
                      "errors": ["the simulator stopped (see work-head/%s.log)" % name]})
    for page in anim_errors:
        tests.append({"name": f"anim:{page}", "passed": 0, "failed": 1, "ok": False,
                      "errors": ["the simulator stopped (see anim-head/%s.log)" % page]})

    print("[screens] comparing ...", flush=True)
    entries = compare(out / "shots-base", out / "shots-head", out / "publish")
    pages = None
    if args.pages and base_bin:
        print(f"[screens] {args.pages} random pages through both builds ...", flush=True)
        pages = sim_pages.check(base_bin, head_bin, out / "pages", args.pages, args.pages_seed)
    code = (measure_code(head), measure_code(base) if base else None)  # how much C is left in src/
    print("[screens] Chromium judges the pages of %s ..." % JUDGED, flush=True)
    verdict, verdict_base = judge.check(fixtures / "apps" / JUDGED, out / "shots-head", out / "shots-base")
    if verdict:
        (out / "judge.json").write_text(json.dumps(verdict, indent=1, ensure_ascii=False))
    random_judged = (None, None)
    if args.pages:
        print(f"[screens] Chromium judges {args.pages} random pages of plain HTML ...", flush=True)
        random_judged = judge_pages.check(head_bin, out / "judge-pages", args.pages, args.pages_seed, base_bin)
    print("[screens] Chromium judges the animations of %s ..." % ANIMATED, flush=True)
    motion, motion_base = anim_judge.check(fixtures / "apps" / ANIMATED, out / "anim-head", out / "anim-base")
    if motion:
        (out / "anim_judge.json").write_text(json.dumps(motion, indent=1, ensure_ascii=False))
    mem_base, mem_head = mem_report.load(out / "shots-base"), mem_report.load(out / "shots-head")
    idle_base, idle_head = idle_report.load(out / "shots-base"), idle_report.load(out / "shots-head")
    meta = {"base": args.base_label, "head": args.head_label, "base_built": base_bin is not None or not base,
            "code": code, "memory": mem_report.lines(mem_base, mem_head),
            "idle": idle_report.lines(idle_base, idle_head),
            "judge": judge.lines(verdict, verdict_base) or judge.unmeasured(),
            "judge_pages": judge_pages.lines(*random_judged),
            "motion": anim_judge.lines(motion, motion_base) or anim_judge.unmeasured(),
            "pages": sim_pages.lines(pages)}
    (out / "report.md").write_text(report(entries, tests, meta, args.image_url.rstrip("/")))
    ok = all(t["ok"] for t in tests)
    counts = {s: sum(1 for e in entries if e["status"] == s)
              for s in ("changed", "unseen", "added", "removed", "same")}
    (out / "status.json").write_text(json.dumps({"ok": ok, "head_built": True, "base_built": meta["base_built"],
                                                 "screens": counts, "tests": tests, "entries": entries,
                                                 "code": {"head": code[0], "base": code[1]},
                                                 "memory": {"head": mem_head, "base": mem_base},
                                                 "idle": {"head": idle_head, "base": idle_base},
                                                 "judge": {"head": judge.summary(verdict),
                                                           "base": judge.summary(verdict_base)},
                                                 "judge_pages": {"head": judge.summary(random_judged[0]),
                                                                 "base": judge.summary(random_judged[1])},
                                                 "motion": {"head": anim_judge.summary(motion),
                                                            "base": anim_judge.summary(motion_base)},
                                                 "pages": sim_pages.summary(pages)},
                                                indent=1, ensure_ascii=False))
    print(f"[screens] {counts} · tests " + ", ".join(f"{t['name']} {t['passed']}/{t['failed']}" for t in tests))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
