#!/usr/bin/env python3
"""judge_pages.py — the page generator against Chromium: plain HTML and CSS that nobody wrote,
laid out by the watch and by Chromium, element by element.

    python3 sim/tools/judge_pages.py --sim build/sim/telaos_sim --out build/judge-pages \
        [--count 300] [--seed 1] [--base-sim <telaos_sim of the base>] [--all]

The pages are gen_pages.py --html: blocks, headings, lists, tables, buttons, text with inline
elements and a <style> of classes (margins, padding, borders, sizes, text-align, font-size,
line-height, flex). The simulator draws each one (its tree with "dom", sim_pages.py), Chromium
lays it out once, and judge.py pairs the elements and gives the verdicts, as for html_tags: an
element agrees when its box is Chromium's within ±2 px. The html_tags pages were written to
show each tag; these show what nobody thought of. A page that does not agree is named with its
seed: `gen_pages.py <dir> --html --count N --seed S` makes it again.

sim_screens.py runs this in every PR (--pages) for the PR and its base: the report says how
the number moved. Needs Playwright, as judge.py; without it check() gives None.
"""

import argparse
import json
import os
import shutil
import sys
from pathlib import Path

import gen_pages
import judge
import sim_pages

HOST = "http://judge.invalid"  # never fetched: the route below answers for it


def layout(documents, width, height, fonts):
    """{page: [element trees]} of Chromium and its version, as judge.layout, with the fonts served
    once from memory rather than carried in every page; (None, None) without Chromium"""
    try:
        from playwright.sync_api import sync_playwright
    except ImportError:
        judge.MISSING = "нет Playwright"
        return None, None
    faces = {f"{f}.ttf": (Path(fonts) / f"{f}.ttf").read_bytes() for f, _, _ in judge.FACES}
    out, now = {}, {}

    def serve(route):
        name = route.request.url.rsplit("/", 1)[1]
        if name in faces:
            route.fulfill(status=200, body=faces[name], content_type="font/ttf")
        else:
            route.fulfill(status=200, body=now["doc"], content_type="text/html; charset=utf-8")
    try:
        with sync_playwright() as p:
            browser = p.chromium.launch(args=["--font-render-hinting=none"])  # as judge.layout
            page = browser.new_page(viewport={"width": width, "height": height}, device_scale_factor=1)
            page.route(f"{HOST}/**", serve)
            for name, text in documents.items():
                now["doc"] = text
                page.goto(f"{HOST}/{name}.html")
                got = page.evaluate(judge.MEASURE)
                if got["failed"]:
                    judge.MISSING = "Chromium не загрузил шрифты часов: " + ", ".join(got["failed"])
                    browser.close()
                    return None, None
                out[name] = got["elements"]
            version = browser.version
            browser.close()
    except Exception as e:  # noqa: BLE001 — no browser, or it fell: the report says so
        judge.MISSING = "Chromium не запустился: " + (str(e).strip().splitlines() or ["?"])[0][:160]
        return None, None
    judge.MISSING = ""
    return out, version


def documents(src, count, fonts):
    """{page name: the document Chromium is given} of the pages gen_pages.write put in src"""
    docs = {}
    for i in range(count):
        name = gen_pages.page_name(i)
        bax = next(Path(src).glob(f"batch_*/apps/{name}/{name}.bax"))
        pages, state = judge.app_pages(bax.read_text("utf-8"))
        docs[name] = judge.document(judge.standard(judge.fill(pages[0][1], state)), fonts, None,
                                    font_base=f"{HOST}/fonts")
    return docs


def trees(shots, names):
    """{page name: the watch's element of the page "main"}, the screen's size"""
    found, size = {}, None
    for name in names:
        try:
            tree = json.loads((Path(shots) / f"{name}.json").read_text("utf-8"))
        except (OSError, ValueError):
            continue  # not drawn: sim_pages says so
        mine = judge.page_of(tree, "main")
        if mine is not None:
            found[name] = mine
            size = size or (tree["screen"]["w"], tree["screen"]["h"])
    return found, size


def check(head_bin, out, count=300, seed=1, base_bin=None, fonts=None, jobs=None):
    """[head, base]: the judge's result on `count` plain HTML pages of `seed` (judge.score, plus
    "seed" and "drawn"), base None without base_bin; [None, None] without Chromium"""
    out = Path(out)
    shutil.rmtree(out, ignore_errors=True)
    fonts = Path(fonts) if fonts else Path(__file__).resolve().parents[2] / "fonts" / "ubuntu"
    batches = gen_pages.write(out / "src", count, seed, html=True)
    runs = {"head": head_bin, "base": base_bin}
    drawn = {}
    for run, binary in runs.items():
        if binary:
            sim_pages.draw(binary, batches, out / run, jobs or os.cpu_count() or 2)
            drawn[run] = trees(out / run, [gen_pages.page_name(i) for i in range(count)])
    size = next((sz for _, sz in drawn.values() if sz), None)
    if size is None:
        return [None, None]
    laid, version = layout(documents(out / "src", count, fonts), *size, fonts)
    if laid is None:
        return [None, None]
    results = []
    for run in runs:
        if run not in drawn:
            results.append(None)
            continue
        mine = drawn[run][0]
        r = judge.score("случайные", sorted(mine), mine, laid, version, size, {})
        r.update(seed=seed, drawn=len(mine), of=count)
        results.append(r)
    return results


def title(result):
    return (f"**Случайные страницы против Chromium:** {result['drawn']} страниц HTML и CSS "
            f"(`gen_pages.py --html --seed {result['seed']}`), рамки элементов ±{judge.TOLERANCE} px")


def lines(head, base=None, details=20):
    """The report's part, as judge.lines, under its own title"""
    return judge.lines(head, base, details, title=title(head) if head else None)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--sim", required=True, help="telaos_sim to judge")
    ap.add_argument("--base-sim", help="telaos_sim before the change: how the number moved")
    ap.add_argument("--out", required=True)
    ap.add_argument("--count", type=int, default=300)
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--json", help="write the verdicts here")
    ap.add_argument("--all", action="store_true", help="every element that does not agree")
    args = ap.parse_args()
    head, base = check(args.sim, args.out, args.count, args.seed, args.base_sim)
    if head is None:
        print(f"judge_pages: no number — {judge.MISSING or 'the pages have no elements'}", file=sys.stderr)
        return 1
    if args.json:
        Path(args.json).write_text(json.dumps(head, indent=1, ensure_ascii=False), "utf-8")
    print("\n".join(lines(head, base, details=10 ** 6 if args.all else 20)))
    print(f"Chromium {head['chromium']}, screen {head['screen'][0]}×{head['screen'][1]}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
