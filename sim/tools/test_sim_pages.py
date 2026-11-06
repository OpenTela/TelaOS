#!/usr/bin/env python3
"""Tests for sim_pages.py (no simulator: drawing is faked):
python3 -m unittest discover -s sim/tools -p 'test_*.py'"""

import json
import os
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import sim_pages  # noqa: E402
import sim_screens  # noqa: E402

# as the robot's `tree` writes them
BUTTON = {"type": "btn", "name": "ok", "x": 10, "y": 10, "w": 100, "h": 40,
          "children": [{"type": "label", "x": 30, "y": 20, "w": 40, "h": 20, "value": "OK"}]}
LABEL = {"type": "label", "x": 0, "y": 60, "w": 200, "h": 20, "value": "Hi"}


def screen(*children):
    return {"time_ms": 1100, "mem": {"dram": {"total": 100, "free": 50}},
            "screen": {"type": "obj", "x": 0, "y": 0, "w": 200, "h": 200, "children": list(children)}}


def picture(path, dot=None):
    rgb = bytearray(200 * 200 * 3)
    if dot:
        x, y = dot
        rgb[(y * 200 + x) * 3:(y * 200 + x) * 3 + 3] = b"\xff\x00\x00"
    sim_screens.write_png(path, 200, 200, bytes(rgb))


# page -> what each run draws: (tree, red dot or None), or None when the run did not draw it
SCENARIO = {
    # same: the elements a script sees ("dom", only the head's robot writes them) are not what is drawn
    "gen_0000": {"base": (screen(BUTTON), None), "head": ({**screen(BUTTON), "dom": [{"tag": "page"}]}, None)},
    "gen_0001": {"base": (screen(BUTTON), None), "head": (screen(BUTTON, LABEL), None)},          # a new label
    "gen_0002": {"base": (screen(BUTTON), None), "head": (screen(BUTTON), (20, 20))},             # pixels in #ok
    "gen_0003": {"base": (screen(BUTTON), None), "head": (screen(BUTTON), (30, 30)), "head-2": (screen(BUTTON), (31, 31))},
    "gen_0004": {"base": (screen(BUTTON), None), "head": None},                                   # stopped
}


def fake_draw(binary, batches, shots, jobs):
    run = shots.name
    shots.mkdir(parents=True, exist_ok=True)
    for page, runs in SCENARIO.items():
        drawn = runs.get(run, runs["base" if run == "base" else "head"])
        if drawn:
            tree, dot = drawn
            (shots / f"{page}.json").write_text(json.dumps(tree if run != "head-2" else {**tree, "time_ms": 9}))
            picture(shots / f"{page}.png", dot)
    return []


class SimPages(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        real, sim_pages.draw = sim_pages.draw, fake_draw
        try:
            with tempfile.TemporaryDirectory() as d:
                cls.result = sim_pages.check("base-sim", "head-sim", Path(d) / "pages", count=len(SCENARIO))
        finally:
            sim_pages.draw = real

    def test_what_differs_and_how(self):
        r = self.result
        self.assertEqual(r["same"], 1)  # gen_0000; the capture time and the elements do not count
        self.assertEqual([c["page"] for c in r["changed"]], ["gen_0001", "gen_0002"])
        self.assertTrue(any("«Hi»" in w for w in r["changed"][0]["changes"]), r["changed"][0])
        self.assertEqual(r["changed"][1]["changes"], ["Виджеты те же — изменились только пиксели, в `btn #ok`"])
        self.assertEqual(r["unstable"], [{"page": "gen_0003", "where": "label в btn #ok"}])
        self.assertEqual(r["missing"], ["gen_0004"])

    def test_report_lines(self):
        out = sim_pages.lines(self.result)
        self.assertEqual(out[0], "**Случайные страницы:** изменилось 2 из 5 (`gen_pages.py --seed 1`)")
        self.assertIn("- **gen_0002**: виджеты те же — изменились только пиксели, в `btn #ok`", out)
        self.assertTrue(any(l.startswith("⚠️ Нестабильные: 1") and "**gen_0003** (в `label в btn #ok`)" in l for l in out))
        self.assertTrue(any(l.startswith("❌ Не нарисовались: 1") for l in out))

    def test_one_line_when_nothing_changed(self):
        quiet = {"count": 300, "seed": 1, "same": 300, "changed": [], "unstable": [], "missing": [], "failed": []}
        self.assertEqual(sim_pages.lines(quiet), ["**Случайные страницы:** 300 из 300 без изменений (`gen_pages.py --seed 1`)", ""])
        self.assertEqual(sim_pages.lines(None), [])


if __name__ == "__main__":
    unittest.main()
