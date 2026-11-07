#!/usr/bin/env python3
"""Tests for idle_report.py:  python3 -m unittest discover -s sim/tools -p 'test_*.py'"""

import json
import os
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from idle_report import lines, load  # noqa: E402

HEADER = ("**Перерисовка в покое** (2 с без касаний после снимка; на часах это работа процессора "
          "и дисплея вместо сна)")


def rec(px=0, frames=0, what=(), ms=2000):
    return {"ms": ms, "px": px, "frames": frames, "what": list(what)}


CLOCK = rec(56196, 2, ["надпись «10:09»"])


class Lines(unittest.TestCase):
    def test_nothing_redraws(self):
        self.assertEqual(lines({"a": rec(), "b": rec()}, {"a": rec(), "b": rec()}), [
            HEADER, "", "- ✅ Ни один из 2 экранов не перерисовывается сам", ""])

    def test_one_screen_as_before(self):
        head = {"launcher": CLOCK, "dice__main": rec()}
        self.assertEqual(lines(head, head), [
            HEADER, "",
            "- Перерисовывается 1 экран из 2: **launcher** — 28 098 пикселей и 1 кадр в секунду, "
            "надпись «10:09» (до: столько же)",
            ""])

    def test_a_screen_that_started_is_flagged(self):
        base = {"launcher": rec(), "dice__main": rec()}
        head = {"launcher": rec(), "dice__main": rec(9600, 5, ["полоса 200×8", "надпись «3»", "кнопка «Go»"])}
        out = lines(base, head)
        self.assertIn("- ⚠️ Стал перерисовываться: **dice** · main — 4 800 пикселей и 2,5 кадра в секунду, "
                      "полоса 200×8, надпись «3» и ещё 1", out)
        self.assertIn("- Перерисовывается 1 экран из 2: **dice** · main — 4 800 пикселей и 2,5 кадра в секунду, "
                      "полоса 200×8, надпись «3» и ещё 1 (до: ни одного)", out)

    def test_stopped_and_changed(self):
        base = {"launcher": CLOCK, "word__main": rec(4000, 2, ["надпись `#t`"]), "dice__main": rec(4000, 2)}
        head = {"launcher": rec(), "word__main": rec(8000, 4, ["надпись `#t`"]), "dice__main": rec(4200, 2)}
        out = lines(base, head)
        self.assertIn("- Перестал перерисовываться: **launcher** (было 28 098 пикселей в секунду)", out)
        self.assertIn("- Изменилось на 10% и больше: **word** · main: 2 000 → 4 000 пикселей в секунду", out)
        self.assertFalse(any("dice" in line for line in out if line.startswith("- Изменилось")))  # +5%: the same

    def test_several_screens_get_a_table(self):
        head = {"launcher": CLOCK, "timers__stopwatch": rec(96000, 20, ["надпись `#sw`"]), "dice__main": rec()}
        out = lines({}, head)
        self.assertIn("- Перерисовываются 2 экрана из 3, всего 76 098 пикселей в секунду", out)
        rows = [line for line in out if line.startswith("| **")]
        self.assertEqual(rows, ["| **timers** · stopwatch | 48 000 | 10 | надпись `#sw` |",
                                "| **launcher** | 28 098 | 1 | надпись «10:09» |"])
        self.assertFalse(any("Стал" in line for line in out))  # no base: nothing to compare with

    def test_a_new_screen_says_so(self):
        out = lines({"launcher": rec()}, {"launcher": rec(), "clock__main": rec(2400, 2, ["надпись «12:30»"])})
        self.assertIn("- ⚠️ Стал перерисовываться: **clock** · main — 1 200 пикселей и 1 кадр в секунду, "
                      "надпись «12:30» (новый экран)", out)

    def test_nothing_without_measurements(self):
        self.assertEqual(lines({}, {}), [])


def tree(*widgets, w=480, h=480):
    return {"screen": {"type": "obj", "x": 0, "y": 0, "w": w, "h": h, "children": list(widgets)}, "top": {
        "type": "obj", "x": 0, "y": 0, "w": w, "h": h}}


class Load(unittest.TestCase):
    def setUp(self):
        self.dir = Path(tempfile.mkdtemp())

    def write(self, name, data):
        (self.dir / name).write_text(json.dumps(data), "utf-8")

    def test_the_widget_of_an_area_from_the_tree(self):
        self.write("launcher.json", tree(
            {"type": "label", "x": 130, "y": 37, "w": 220, "h": 120, "value": "10:09"},
            {"type": "label", "name": "date", "x": 130, "y": 160, "w": 220, "h": 30, "value": "1 января"},
            {"type": "label", "x": 0, "y": 300, "w": 50, "h": 50, "value": "hidden", "visible": False}))
        self.write("launcher.idle.json", {"ms": 2000, "px": 1, "frames": 1, "areas": [
            {"x": 129, "y": 35, "w": 223, "h": 128, "times": 2},      # the clock, with its outline
            {"x": 129, "y": 159, "w": 222, "h": 32, "times": 1},      # the date: its id names it
            {"x": 0, "y": 0, "w": 480, "h": 480, "times": 1},
            {"x": 0, "y": 300, "w": 50, "h": 50, "times": 1}]})        # hidden: not what was drawn
        self.assertEqual(load(self.dir)["launcher"]["what"],
                         ["надпись «10:09»", "надпись `#date`", "весь экран", "область 50×50"])

    def test_without_its_tree_an_area_is_its_size(self):
        self.write("dice__main.idle.json", {"ms": 2000, "px": 1600, "frames": 2, "areas": [
            {"x": 10, "y": 10, "w": 40, "h": 20, "times": 2}]})
        self.assertEqual(load(self.dir), {"dice__main": {"ms": 2000, "px": 1600, "frames": 2,
                                                         "what": ["область 40×20"]}})

    def test_load_skips_what_is_not_a_measurement(self):
        self.write("a__main.json", tree())                    # a tree: not a measurement
        self.write("b__main.idle.json", {"px": 5})            # no time
        (self.dir / "c__main.idle.json").write_text("{broken")
        self.assertEqual(load(self.dir), {})
        self.assertEqual(load(self.dir / "missing"), {})


if __name__ == "__main__":
    unittest.main()
