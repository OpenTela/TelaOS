#!/usr/bin/env python3
"""Tests for sim_screens.py, compare() and report():
python3 -m unittest discover -s sim/tools -p 'test_*.py'"""

import json
import os
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import sim_screens  # noqa: E402


def tree(*labels):
    """A screen of stacked labels: (text, id or None) each"""
    kids = []
    for i, (text, name) in enumerate(labels):
        d = {"type": "label", "x": 10, "y": 10 + 30 * i, "w": 100, "h": 20, "value": text}
        if name:
            d["name"] = name
        kids.append(d)
    return {"time_ms": 1000, "screen": {"type": "obj", "x": 0, "y": 0, "w": 480, "h": 480, "children": kids},
            "top": {"type": "obj", "x": 0, "y": 0, "w": 480, "h": 480}}


class Screens(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        root = Path(self.tmp.name)
        self.base, self.head, self.publish = root / "base", root / "head", root / "publish"
        self.base.mkdir()
        self.head.mkdir()

    def tearDown(self):
        self.tmp.cleanup()

    def shot(self, d, name, color, t):
        sim_screens.write_png(d / f"{name}.png", 2, 2, bytes(color) * 4)
        (d / f"{name}.json").write_text(json.dumps(t), "utf-8")

    def entries(self):
        return {e["name"]: e for e in sim_screens.compare(self.base, self.head, self.publish)}

    def test_same_pixels_and_tree(self):
        self.shot(self.base, "app__main", (0, 0, 0), tree(("Hi", "greet")))
        self.shot(self.head, "app__main", (0, 0, 0), tree(("Hi", "greet")))
        self.assertEqual(self.entries()["app__main"]["status"], "same")

    def test_same_pixels_other_tree_is_changed_unseen(self):
        # what a script addresses changed and no picture shows it: ui.setText("greet") finds nothing
        self.shot(self.base, "app__main", (0, 0, 0), tree(("Hi", "greet")))
        self.shot(self.head, "app__main", (0, 0, 0), tree(("Hi", "hello")))
        e = self.entries()["app__main"]
        self.assertEqual(e["status"], "unseen")
        self.assertEqual(e["changes"], ["Пропала надпись `#greet` «Hi»", "Появилась надпись `#hello` «Hi»"])
        self.assertFalse((self.publish / "after" / "app__main.png").exists())  # nothing to look at

    def test_other_pixels_is_changed(self):
        self.shot(self.base, "app__main", (0, 0, 0), tree(("Hi", "greet")))
        self.shot(self.head, "app__main", (255, 255, 255), tree(("Hi", "greet")))
        e = self.entries()["app__main"]
        self.assertEqual(e["status"], "changed")
        self.assertEqual(e["changes"], [sim_screens.SAME_TREE])

    def test_no_tree_is_same(self):
        sim_screens.write_png(self.base / "app__main.png", 2, 2, bytes(12))
        sim_screens.write_png(self.head / "app__main.png", 2, 2, bytes(12))
        self.assertEqual(self.entries()["app__main"]["status"], "same")

    def report(self):
        entries = sim_screens.compare(self.base, self.head, self.publish)
        return sim_screens.report(entries, [], {"base": "a", "head": "b"}, "")

    def test_report_of_an_unseen_change(self):
        self.shot(self.base, "app__main", (0, 0, 0), tree(("Hi", "greet")))
        self.shot(self.head, "app__main", (0, 0, 0), tree(("Hi", "hello")))
        self.shot(self.base, "app__other", (0, 0, 0), tree(("Bye", None)))
        self.shot(self.head, "app__other", (0, 0, 0), tree(("Bye", None)))
        text = self.report()
        self.assertIn("**изменилось: 0**, незаметно: 1, новых: 0, пропало: 0, без изменений: 1.", text)
        self.assertIn("**Изменилось незаметно** — пиксели те же, а дерево виджетов другое", text)
        self.assertIn("- **app** · main\n    - Пропала надпись `#greet` «Hi»", text)
        self.assertIn("✅ На вид ни один экран не изменился.", text)
        self.assertIn("<summary>Без изменений: 1</summary>", text)

    def test_report_of_nothing_changed_stays_as_it_was(self):
        self.shot(self.base, "app__main", (0, 0, 0), tree(("Hi", "greet")))
        self.shot(self.head, "app__main", (0, 0, 0), tree(("Hi", "greet")))
        text = self.report()
        self.assertIn("**изменилось: 0**, новых: 0, пропало: 0, без изменений: 1.", text)
        self.assertIn("✅ Ни один экран не изменился.", text)
        self.assertNotIn("незаметно", text)


if __name__ == "__main__":
    unittest.main()
