#!/usr/bin/env python3
"""Tests for judge.py (no Chromium: its layout is faked):
python3 -m unittest discover -s sim/tools -p 'test_*.py'"""

import json
import os
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import judge  # noqa: E402

BAX = """<app os="1.0" title="T">
  <!-- <page id="old">a comment is not a page</page> -->
  <ui default="/main">
    <page id="main"><p id="hello">Hi {name}</p></page>
    <group id="g" default="a"><page id="a"><div w="60" h="30%">box</div></page></group>
  </ui>
  <state>
    <string name="name" default="Tom &amp; Jerry"/>
    <int name="count" default="0"/>
  </state>
</app>
"""


# Chromium's elements and the watch's, as layout() and the robot's "dom" give them
def ours(tag, box=None, children=(), id="", hidden=False):
    e = {"tag": tag}
    if id:
        e["id"] = id
    if box:
        e.update(x=box[0], y=box[1], w=box[2], h=box[3])
        if hidden:
            e["hidden"] = True
    if children:
        e["children"] = list(children)
    return e


def theirs(tag, box=(0, 0, 0, 0), children=(), id="", shown=True, boxed=True):
    return {"tag": tag, "id": id, "x": box[0], "y": box[1], "w": box[2], "h": box[3],
            "shown": shown, "boxed": boxed, "children": list(children)}


class Pages(unittest.TestCase):
    def test_pages_and_state(self):
        pages, state = judge.app_pages(BAX)
        self.assertEqual([p for p, _ in pages], ["main", "a"])  # a page in a group too, not the comment
        self.assertEqual(state, {"name": "Tom & Jerry", "count": "0"})

    def test_fill_takes_the_default_escaped(self):
        self.assertEqual(judge.fill("<p>Hi {name}, {count} {nope}</p>", {"name": "<b>", "count": "0"}),
                         "<p>Hi &lt;b&gt;, 0 {nope}</p>")

    def test_watch_attributes_become_css(self):
        self.assertEqual(judge.standard('<div w="60" h="30%">x</div>'),
                         '<div style="width: 60px; height: 30%">x</div>')
        self.assertEqual(judge.standard('<canvas id="c" w=60 style="color: red">'),
                         '<canvas id="c" style="width: 60px; color: red">')
        self.assertEqual(judge.standard('<dialog visible="false"><p>x</p></dialog>'),
                         '<dialog hidden><p>x</p></dialog>')
        self.assertEqual(judge.standard('<dialog visible="true">'), '<dialog >')

    def test_markup_without_them_is_left_alone(self):
        for text in ('<p class="a" id=b>x</p>', '<img src="a.png" width="10">', '<br/>', "</div>"):
            self.assertEqual(judge.standard(text), text)

    def test_document_has_the_watch_fonts_and_pictures(self):
        with tempfile.TemporaryDirectory() as tmp:
            fonts = Path(tmp) / "fonts"
            fonts.mkdir()
            for face, _, _ in judge.FACES[:3]:
                (fonts / f"{face}.ttf").write_bytes(b"FONT")
            doc = judge.document("<p>x</p>", fonts, Path(tmp) / "res")
        self.assertEqual(doc.count("@font-face"), 4)
        self.assertEqual(doc.count("url('data:font/ttf;base64,Rk9OVA==')"), 3)  # in the page: no file:// to load
        self.assertIn("Ubuntu-BoldItalic.ttf'", doc)  # missing: as a file, Chromium says it failed
        self.assertIn('<base href="file://', doc)
        self.assertIn(":where(*) { font-family: Ubuntu; }", doc)
        self.assertIn("<body><p>x</p></body>", doc)

    def test_the_files_of_the_watch_are_the_checkouts(self):
        html = '<link rel="stylesheet" href="/system/tela.css"><img src=\'/system/resources/a.png\'><a href="/home">'
        out = judge.watch_files(html, "/repo/data/system")
        self.assertIn('href="file:///repo/data/system/tela.css"', out)
        self.assertIn("src='file:///repo/data/system/resources/a.png'", out)
        self.assertIn('<a href="/home">', out)  # a page of the app, not a file
        self.assertEqual(judge.watch_files(html, None), html)
        self.assertIn((judge.STYLED, "icons"), judge.NOT_JUDGED)  # what is not judged without the style

    def test_the_css_of_the_app_is_every_pages(self):
        bax = """<app>
  <!-- <link rel="stylesheet" href="old.css"> -->
  <link rel="stylesheet" href="a.css">
  <style>p { color: red; }</style>
  <ui default="/main"><page id="main"><link rel="stylesheet" href="page.css"><p>x</p></page></ui>
  <link rel="stylesheet" href="/system/b.css"/>
  <script language="lua">s = '<link rel="stylesheet" href="lua.css">'</script>
</app>"""
        head = judge.app_head(bax)
        self.assertEqual(head, '<link rel="stylesheet" href="a.css"><style>p { color: red; }</style>'
                               '<link rel="stylesheet" href="/system/b.css"/>')  # the page's link is the page's
        doc = judge.document("<p>x</p>", "/nowhere", None, head=head)
        self.assertIn(head + "</head><body><p>x</p></body>", doc)  # after the watch's fonts: the app's CSS wins


class Pairs(unittest.TestCase):
    def test_align_pairs_in_order(self):
        tags = lambda *ts: [{"tag": t} for t in ts]  # noqa: E731
        self.assertEqual(judge.align(tags("h2", "p", "ul"), tags("h2", "br", "p", "ul", "p")), [(0, 0), (1, 2), (2, 3)])
        self.assertEqual(judge.align(tags("p", "p"), tags("br", "p", "p", "br")), [(0, 1), (1, 2)])
        self.assertEqual(judge.align(tags("li", "li"), tags("li")), [(0, 0)])

    def test_agree_within_two_pixels(self):
        vs = judge.verdicts([ours("p", (8, 8, 464, 20)), ours("p", (8, 40, 464, 20))],
                            [theirs("p", (8, 10, 462, 18)), theirs("p", (8, 43, 464, 20))])
        self.assertEqual([v["ok"] for v in vs], [True, False])
        self.assertEqual(vs[1]["element"], "p:2")
        self.assertEqual(judge.mismatch(dict(vs[1], page="main")), "`main` · `p:2`: y 40 → 43")

    def test_inline_text_and_what_neither_shows_are_not_counted(self):
        vs = judge.verdicts([ours("p", (0, 0, 10, 10), [ours("b")]), ours("dialog", (0, 0, 480, 480), hidden=True)],
                            [theirs("p", (0, 0, 10, 10), [theirs("b", (1, 1, 5, 5), boxed=False)]),
                             theirs("dialog", shown=False)])
        self.assertEqual([v["element"] for v in vs], ["p"])

    def test_what_only_one_of_them_shows(self):
        vs = judge.verdicts([ours("audio", (8, 8, 464, 39), id="aud"), ours("p", (8, 50, 464, 21), hidden=True),
                             ours("ruby")],
                            [theirs("audio", shown=False, id="aud"), theirs("p", (8, 50, 464, 21)),
                             theirs("ruby", (8, 80, 28, 18)), theirs("video", (8, 100, 300, 150))])
        self.assertEqual([(v["element"], v["ok"], v["why"]) for v in vs],
                         [("audio#aud", False, "Chromium не показывает"), ("p", False, "на часах скрыт"),
                          ("ruby", False, "на часах без своей рамки"), ("video", False, "нет на часах")])

    def test_a_wrapper_the_watch_has_not_stands_for_its_children(self):
        # <table><thead><tr>…</tr></thead><tbody><tr>…</tr></tbody></table>; the watch: table > tr, tr
        watch = [ours("table", (8, 8, 100, 40), [ours("caption", (8, 8, 100, 20)),
                                                 ours("tr", (8, 20, 100, 10)), ours("tr", (8, 30, 100, 10))])]
        chromium = [theirs("table", (8, 8, 100, 40), [
            theirs("caption", (8, 8, 100, 20)),
            theirs("thead", (8, 20, 100, 10), [theirs("tr", (8, 20, 100, 10))]),
            theirs("tbody", (8, 30, 100, 10), [theirs("tr", (8, 30, 100, 10))])])]
        vs = judge.verdicts(watch, chromium)
        self.assertEqual([(v["element"], v["ok"]) for v in vs],
                         [("table", True), ("table > caption", True), ("table > thead", False),
                          ("table > thead > tr", True), ("table > tbody", False), ("table > tbody > tr", True)])
        # <picture> is inline: no verdict of its own, its <img> is the watch's
        vs = judge.verdicts([ours("img", (8, 8, 48, 48), id="p")],
                            [theirs("picture", (8, 8, 48, 48), [theirs("img", (8, 8, 48, 48), id="p")], boxed=False)])
        self.assertEqual([(v["element"], v["ok"]) for v in vs], [("img#p", True)])


class Check(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        root = Path(self.tmp.name)
        self.app = root / "apps" / "t"
        self.app.mkdir(parents=True)
        (self.app / "t.bax").write_text(BAX.replace("<page id=\"a\">", "<page id=\"b\">"))
        self.shots = root / "shots"
        self.shots.mkdir()
        self.old = root / "old"  # a robot before "dom"
        self.old.mkdir()
        for page, kids in (("main", [ours("p", (8, 8, 464, 21), id="hello")]), ("b", [ours("div", (8, 8, 60, 144))])):
            tree = {"screen": {"type": "obj", "x": 0, "y": 0, "w": 480, "h": 480},
                    "dom": [ours("page", (0, 0, 480, 480), kids, id=page)]}
            (self.shots / f"t__{page}.json").write_text(json.dumps(tree))
            (self.old / f"t__{page}.json").write_text(json.dumps({"screen": tree["screen"]}))

    def tearDown(self):
        self.tmp.cleanup()

    def fake(self, documents, width, height):
        self.seen = (sorted(documents), width, height, documents["main"])
        return {"main": [theirs("p", (8, 8, 464, 18), id="hello")], "b": [theirs("div", (8, 8, 60, 144))]}, "153.0"

    def test_number_from_the_pages(self):
        with mock.patch.object(judge, "layout", self.fake):
            head, old = judge.check(self.app, self.shots, self.old)
        self.assertIsNone(old)  # no "dom": nothing to judge
        self.assertEqual(self.seen[:3], (["b", "main"], 480, 480))
        self.assertIn("Hi Tom &amp; Jerry", self.seen[3])
        self.assertEqual((head["agree"], head["count"], head["boxes"]), (1, 2, 2))
        self.assertEqual(head["pages"], {"main": {"agree": 0, "count": 1}, "b": {"agree": 1, "count": 1}})
        self.assertEqual(head["dims"], {"x": 2, "y": 2, "w": 2, "h": 1})
        self.assertNotIn("verdicts", judge.summary(head))

    def test_pages_not_judged_are_named(self):
        with mock.patch.dict(judge.NOT_JUDGED, {("t", "b"): "why not"}), mock.patch.object(judge, "layout", self.fake):
            head, = judge.check(self.app, self.shots)
        self.assertEqual(list(head["pages"]), ["main"])
        self.assertIn("- Не судится `b`: why not", judge.lines(head))

    def test_no_chromium_no_judge(self):
        with mock.patch.object(judge, "layout", lambda *a: (None, None)):
            self.assertEqual(judge.check(self.app, self.shots), [None])
        self.assertEqual(judge.lines(None), [])
        with mock.patch.dict("sys.modules", {"playwright": None, "playwright.sync_api": None}):
            self.assertEqual(judge.layout({"main": "<p>x</p>"}, 480, 480), (None, None))
        self.assertEqual(judge.unmeasured(),
                         ["**Совместимость с Chromium:** не измерена — нет Playwright (docs/SIMULATOR.md)", ""])


class Words(unittest.TestCase):
    def result(self, ok):
        vs = [{"page": "main", "element": "p#a", "ok": ok, "why": "", "watch": (8, 8, 464, 21),
               "chromium": (8, 8, 464, 21 if ok else 18)},
              {"page": "main", "element": "p:2", "ok": False, "why": "", "watch": (8, 40, 464, 21),
               "chromium": (8, 30, 464, 18)},
              {"page": "main", "element": "video", "ok": False, "why": "нет на часах", "watch": None,
               "chromium": (8, 60, 300, 150)}]
        agree = sum(v["ok"] for v in vs)
        return {"app": "t", "agree": agree, "count": 3, "boxes": 2, "verdicts": vs, "skipped": {},
                "dims": {"x": 2, "y": 1 + ok, "w": 2, "h": int(ok)}}

    def test_the_number_and_what_differs_most(self):
        text = "\n".join(judge.lines(self.result(False)))
        self.assertIn("**Совместимость с Chromium:** t, рамки элементов ±2 px — **0 из 3, 0%**", text)
        self.assertIn("- По отдельности, из 2 рамок: x 100% · y 50% · ширина 100% · высота 0%", text)
        self.assertIn("- Высота на часах больше на 3 px — у 2", text)
        self.assertIn("- `main` · `p:2`: y 40 → 30, высота 21 → 18", text)
        self.assertIn("- `main` · `video`: нет на часах (в Chromium 8,60 300×150)", text)

    def test_how_it_moved(self):
        text = "\n".join(judge.lines(self.result(True), self.result(False)))
        self.assertIn("**1 из 3, 33%** (было 0 из 3, 0%)", text)
        self.assertIn("- По отдельности, из 2 рамок: x 100% · y 100% (было 50%) · ширина 100% · высота 50% (было 0%)", text)
        self.assertIn("- Теперь совпадает: 1 — `main` · `p#a`", text)
        self.assertNotIn("Перестало", text)
        text = "\n".join(judge.lines(self.result(False), self.result(True)))
        self.assertIn("- Перестало совпадать: 1 — `main` · `p#a`", text)
        self.assertIn("столько же", "\n".join(judge.lines(self.result(True), self.result(True))))


if __name__ == "__main__":
    unittest.main()
