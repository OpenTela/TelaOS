#!/usr/bin/env python3
"""Tests for anim_judge.py (no Chromium and no simulator: both are faked):
python3 -m unittest discover -s sim/tools -p 'test_*.py'"""

import json
import os
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import anim_judge as aj  # noqa: E402

BAX = """<app os="1.0" title="T">
  <!-- <style>.x { color: red; }</style> a comment gives no style -->
  <style>#a { transition: opacity 1s linear; } #a.go { opacity: 0; }</style>
  <ui default="/p">
    <page id="p"><div id="a" class="trigger"></div><div id="still"></div></page>
    <page id="q"><style>#b { color: blue; }</style><div id="b" class="trigger"></div></page>
  </ui>
</app>
"""

STYLE = {"opa": 255, "bg": "#1E88E5", "bg_opa": 255, "color": "#FFFFFF", "color_opa": 255,
         "translate": [0, 0], "scale": [256, 256], "rotation": 0}


def node(id, box=(8, 8, 120, 14), **style):
    return {"tag": "div", "id": id, "x": box[0], "y": box[1], "w": box[2], "h": box[3], "style": dict(STYLE, **style)}


def element(box=(8, 8, 120, 14), opacity="1", background="rgb(30, 136, 229)", color="rgb(255, 255, 255)",
            transform="none"):
    """One element as Chromium's SEEK measures it"""
    return {"x": box[0], "y": box[1], "w": box[2], "h": box[3], "opacity": opacity, "background": background,
            "color": color, "transform": transform}


class Robot(unittest.TestCase):
    def test_the_frames_come_at_their_moments(self):
        lines = aj.script("t", "q", "p").splitlines()
        self.assertEqual(lines[:7], ["wait 500", "> app launch t", "wait 1000", "> ui nav q", "wait 400",
                                     "tree t__q~go", "> ui call go"])
        waits = [int(l.split()[1]) for l in lines[7:] if l.startswith("wait ")]
        trees = [l for l in lines[7:] if l.startswith("tree ")]
        self.assertEqual(trees, [f"tree t__q~{ms}" for ms in aj.FRAMES])
        at = aj.COMMAND_MS
        for wait, ms in zip(waits, aj.FRAMES):  # the "> ui call go" line itself takes COMMAND_MS
            at += wait
            self.assertEqual(at, ms)

    def test_the_first_page_needs_no_nav(self):
        self.assertNotIn("ui nav", aj.script("t", "p", "p"))
        self.assertEqual(aj.default_page(BAX), "p")

    def test_each_page_in_a_simulator_of_its_own(self):
        with tempfile.TemporaryDirectory() as tmp:
            app = Path(tmp) / "t"
            app.mkdir()
            (app / "t.bax").write_text(BAX)
            runs = []

            def run(script, out):
                runs.append(Path(script).read_text())
                return (0 if len(runs) == 1 else 1), f"log {len(runs)}"
            failed = aj.shoot(app, Path(tmp) / "out", run)
            self.assertEqual(failed, ["q"])
            self.assertNotIn("ui nav", runs[0])
            self.assertIn("> ui nav q", runs[1])
            self.assertEqual((Path(tmp) / "out" / "q.log").read_text(), "log 2")


class Values(unittest.TestCase):
    def test_the_watch_in_chromium_units(self):
        v = aj.watch_values(node("a", opa=51, bg="#E53935", bg_opa=128, translate=[160, -4], scale=[384, 256],
                                 rotation=900))
        self.assertEqual((v["x"], v["w"]), (8, 120))
        self.assertAlmostEqual(v["opacity"], 0.2)
        self.assertEqual(v["background"][:3], (229, 57, 53))
        self.assertAlmostEqual(v["background"][3], 128 / 255)
        self.assertEqual((v["translate"], v["scale"], v["rotate"]), ((160, -4), (1.5, 1.0), 90.0))
        self.assertEqual(aj.watch_values({"tag": "b", "id": "x"}), {})  # no object of its own
        self.assertEqual(set(aj.watch_values({"tag": "div", "x": 0, "y": 0, "w": 1, "h": 1})), set(aj.BOX))

    def test_colors_as_chromium_writes_them(self):
        self.assertEqual(aj.css_color("rgb(30, 136, 229)"), (30, 136, 229, 1))
        self.assertEqual(aj.css_color("rgba(0, 0, 0, 0)"), (0, 0, 0, 0))
        self.assertEqual(aj.css_color("rgba(229, 57, 53, 0.5)"), (229, 57, 53, 0.5))
        self.assertIsNone(aj.css_color("color(srgb 1 0 0)"))

    def test_a_transform_taken_apart(self):
        none = aj.decompose("none")
        self.assertEqual(none, {"translate": (0, 0), "scale": (1, 1), "rotate": 0})
        t = aj.decompose("matrix(6.12323e-17, 1, -1, 6.12323e-17, 10, 20)")  # rotate(90deg), translate(10px, 20px)
        self.assertAlmostEqual(t["rotate"], 90)
        self.assertEqual(t["translate"], (10, 20))
        self.assertAlmostEqual(t["scale"][0], 1)
        self.assertAlmostEqual(t["scale"][1], 1)
        self.assertEqual(aj.decompose("matrix(1.5, 0, 0, 2, 0, 0)")["scale"], (1.5, 2))
        m3 = aj.decompose("matrix3d(1.2, 0, 0, 0, 0, 1.2, 0, 0, 0, 0, 1, 0, 5, 6, 0, 1)")
        self.assertEqual((m3["scale"], m3["translate"]), ((1.2, 1.2), (5, 6)))

    def test_within_the_tolerance(self):
        self.assertFalse(aj.differ("opacity", 0.2, 0.215))
        self.assertTrue(aj.differ("opacity", 0.2, 0.23))
        self.assertFalse(aj.differ("x", 160, 158))
        self.assertTrue(aj.differ("x", 160, 157))
        self.assertFalse(aj.differ("color", (229, 57, 53, 1), (225, 61, 53, 1)))
        self.assertTrue(aj.differ("color", (229, 57, 53, 1), (224, 57, 53, 1)))
        self.assertTrue(aj.differ("background", (229, 57, 53, 1), (229, 57, 53, 0.9)))
        self.assertFalse(aj.differ("background", (255, 255, 255, 0), (0, 0, 0, 0.01)))  # transparent: no color
        self.assertFalse(aj.differ("rotate", 359.5, 0))  # 360° is 0°
        self.assertFalse(aj.differ("rotate", 0, 360))
        self.assertTrue(aj.differ("rotate", 90, 92))
        self.assertTrue(aj.differ("scale", (1.5, 1), (1.5, 1.03)))
        self.assertTrue(aj.differ("opacity", None, 0.2))  # nothing on the watch
        self.assertFalse(aj.differ("opacity", 0.2, 0.2004, aj.STILL))
        self.assertTrue(aj.differ("opacity", 0.2, 0.202, aj.STILL))


class Moving(unittest.TestCase):
    def test_only_what_moves_in_chromium(self):
        frames = [
            {"still": element(), "fade": element(), "slide": element(), "spin": element((8, 50, 40, 40))},
            {"still": element(), "fade": element(opacity="0.6"),
             "slide": element((88, 8, 120, 14), transform="matrix(1, 0, 0, 1, 80, 0)"),
             "spin": element((0, 42, 56, 56), transform="matrix(0.707107, 0.707107, -0.707107, 0.707107, 0, 0)")},
        ]
        frames = [{id: aj.chromium_values(e) for id, e in f.items()} for f in frames]
        self.assertEqual(aj.moving(frames), {"fade": ["opacity"], "slide": ["x"], "spin": ["rotate"]})

    def test_a_box_moves_by_its_shift(self):
        self.assertEqual(aj.value("x", {"x": 168}, {"x": 8}), 160)
        self.assertIsNone(aj.value("x", {}, {"x": 8}))  # no object on the watch
        self.assertEqual(aj.value("opacity", {"opacity": 0.5}, {"opacity": 1}), 0.5)


class Check(unittest.TestCase):
    """One app, two pages; on the watch #a jumps where Chromium fades it, #b agrees"""

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        root = Path(self.tmp.name)
        self.app = root / "apps" / "t"
        self.app.mkdir(parents=True)
        (self.app / "t.bax").write_text(BAX)
        self.frames = root / "frames"
        self.frames.mkdir()
        self.late = 5  # the robot's tree of 250 ms came at 255
        for page, kid in (("p", "a"), ("q", "b")):
            for name in (aj.BEFORE, *map(str, aj.FRAMES)):
                ms = 2020 + (0 if name == aj.BEFORE else int(name) + (self.late if name == "250" else 0))
                after = name != aj.BEFORE
                kids = ([node(kid, opa=0 if after else 255), node("still")] if page == "p" else
                        [node(kid, color="#0000FF" if after else "#FFFFFF")])
                tree = {"time_ms": ms, "screen": {"type": "obj", "x": 0, "y": 0, "w": 480, "h": 480},
                        "dom": [{"tag": "page", "id": page, "x": 0, "y": 0, "w": 480, "h": 480, "children": kids}]}
                (self.frames / f"t__{page}~{name}.json").write_text(json.dumps(tree))

    def tearDown(self):
        self.tmp.cleanup()

    def fake(self, documents, times, width, height):
        self.asked = (documents, times, width, height)
        out = {}
        for page in documents:
            frames = {0: {"a": element(), "still": element()} if page == "p" else {"b": element()}}
            for ms in times[page]:
                frames[ms] = ({"a": element(opacity=str(1 - ms / 1000 if ms < 1000 else 0)), "still": element()}
                              if page == "p" else {"b": element(color="rgb(0, 0, 255)")})
            out[page] = frames
        return out, "153.0"

    def test_frames_and_values(self):
        with mock.patch.object(aj, "seek", self.fake):
            head, = aj.check(self.app, self.frames)
        documents, times, width, height = self.asked
        self.assertEqual((width, height), (480, 480))
        self.assertEqual(times["p"], [100, 255, 500, 750, 1000, 1500, 2000, 2500])  # the moments the robot saw
        self.assertIn("#a.go { opacity: 0; }", documents["p"])  # the app's style goes to every page
        self.assertNotIn(".x { color: red; }", documents["p"])
        self.assertIn("#b { color: blue; }", documents["q"])
        # #a: opacity at 1000 and after agrees (0), before it not; "still" is not judged; #b: color, all agree
        self.assertEqual(head["pages"]["p"], {"agree": 4, "count": 8, "values": 8, "agree_values": 4})
        self.assertEqual(head["pages"]["q"], {"agree": 8, "count": 8, "values": 8, "agree_values": 8})
        self.assertEqual((head["agree"], head["count"], head["agree_values"], head["values"]), (12, 16, 12, 16))
        self.assertEqual(head["props"], {"opacity": {"agree": 4, "count": 8}, "color": {"agree": 8, "count": 8}})
        bad = [v for v in head["verdicts"] if not v["ok"]]
        self.assertEqual([(v["element"], v["ms"]) for v in bad], [("#a", ms) for ms in (100, 255, 500, 750)])
        self.assertEqual(aj.mismatch(bad[1]), "`p` · `#a` opacity, 255 мс: 0.00 → 0.74")
        self.assertNotIn("verdicts", aj.summary(head))

    def test_no_frames_no_judge(self):
        empty = Path(self.tmp.name) / "empty"
        empty.mkdir()
        with mock.patch.object(aj, "seek", self.fake):
            self.assertEqual(aj.check(self.app, self.frames, empty)[1], None)
            self.assertEqual(aj.check(self.app, empty), [None])

    def test_no_chromium_no_judge(self):
        with mock.patch.object(aj, "seek", lambda *a: (None, None)):
            self.assertEqual(aj.check(self.app, self.frames), [None])
        with mock.patch.dict("sys.modules", {"playwright": None, "playwright.sync_api": None}):
            self.assertEqual(aj.seek({"p": "<p>x</p>"}, {"p": [100]}, 480, 480), (None, None))
        self.assertEqual(aj.unmeasured(),
                         ["**Анимации против Chromium:** не измерены — нет Playwright (docs/SIMULATOR.md)", ""])
        self.assertEqual(aj.lines(None), [])


class Words(unittest.TestCase):
    def result(self, ok):
        vs = [{"page": "p", "element": "#a", "prop": "opacity", "ms": 100, "ok": ok, "watch": 0.0, "chromium": 0.9},
              {"page": "p", "element": "#a", "prop": "x", "ms": 250, "ok": False, "watch": 160, "chromium": 63.4},
              {"page": "q", "element": "#b", "prop": "background", "ms": 100, "ok": False,
               "watch": (229, 57, 53, 1), "chromium": (49, 129, 212, 0.5)},
              {"page": "q", "element": "#c", "prop": "rotate", "ms": 100, "ok": False, "watch": None, "chromium": 378}]
        return {"app": "t", "agree": int(ok), "count": 3, "agree_values": int(ok), "values": 4, "verdicts": vs,
                "pages": {"p": {"agree": int(ok), "count": 2}, "q": {"agree": 0, "count": 1}},
                "props": {"x": {"agree": 0, "count": 1}, "opacity": {"agree": int(ok), "count": 1}}}

    def test_the_numbers_and_what_differs(self):
        text = "\n".join(aj.lines(self.result(False)))
        self.assertIn("**Анимации против Chromium:** t, кадры 100…2500 мс после go() — **совпало кадров 0 из 3**, "
                      "значений 0 из 4", text)
        self.assertIn("- По страницам: `p` 0 из 2 · `q` 0 из 1", text)
        self.assertIn("- По свойствам: x 0 из 1 · opacity 0 из 1", text)
        self.assertIn("- `p` · `#a` opacity, 100 мс: 0.00 → 0.90", text)
        self.assertIn("- `p` · `#a` x, 250 мс: +160 → +63", text)
        self.assertIn("- `q` · `#b` background-color, 100 мс: #E53935 → #3181D4 50%", text)
        self.assertIn("- `q` · `#c` rotate, 100 мс: нет на часах", text)

    def test_how_they_moved(self):
        text = "\n".join(aj.lines(self.result(True), self.result(False)))
        self.assertIn("**совпало кадров 1 из 3**, значений 1 из 4 (было 0 из 3, значений 0 из 4)", text)
        self.assertIn("- Теперь совпадает: 1 — `p` · `#a` opacity 100 мс", text)
        self.assertNotIn("Перестало", text)
        self.assertIn("- Перестало совпадать: 1 — `p` · `#a` opacity 100 мс",
                      "\n".join(aj.lines(self.result(False), self.result(True))))
        self.assertIn("столько же", "\n".join(aj.lines(self.result(True), self.result(True))))


if __name__ == "__main__":
    unittest.main()
