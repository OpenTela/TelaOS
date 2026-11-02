#!/usr/bin/env python3
"""Tests for mem_report.py:  python3 -m unittest discover -s sim/tools -p 'test_*.py'"""

import json
import os
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from mem_report import lines, load  # noqa: E402

KB = 1024


def mem(dram_free, dram_min, psram_free=8000 * KB, failed=0, estimate=True):
    return {"dram": {"total": 320 * KB, "free": dram_free, "min_free": dram_min, "largest": dram_free,
                     "blocks": 10, "failed": failed},
            "psram": {"total": 8192 * KB, "free": psram_free, "min_free": psram_free, "largest": psram_free,
                      "blocks": 5, "failed": 0},
            "estimate": estimate}


class MemReport(unittest.TestCase):
    def test_lowest_screen_and_no_change(self):
        base = {"excel__main": mem(180 * KB, 40 * KB), "dice__main": mem(120 * KB, 110 * KB)}
        head = {"excel__main": mem(180 * KB, 40 * KB), "dice__main": mem(120 * KB, 110 * KB)}
        self.assertEqual(lines(base, head), [
            "**Память** (оценка: DRAM 320 КБ, PSRAM 8,0 МБ, ещё не сверено с часами)", "",
            "- Меньше всего DRAM оставалось на **excel** · main: 40,0 КБ (до: 40,0 КБ)",
            "- Ни на одном из 2 экранов расход не изменился на 1 КБ и больше",
            "",
        ])

    def test_changes_of_a_kilobyte_or_more(self):
        base = {"dice__main": mem(120 * KB, 110 * KB), "word__main": mem(100 * KB, 90 * KB)}
        head = {"dice__main": mem(113 * KB, 103 * KB, psram_free=7996 * KB), "word__main": mem(100 * KB - 512, 90 * KB)}
        out = lines(base, head)
        self.assertIn("- Изменилось на 1 КБ и больше:", out)
        self.assertIn("    - **dice** · main: DRAM занято 200 → 207 КБ (+7,0), PSRAM занято 192 → 196 КБ (+4,0)",
                      out)
        self.assertFalse(any("word" in line for line in out if line.startswith("    -")))  # 0.5 KB: noise

    def test_failed_allocations_are_flagged(self):
        head = {"excel__main": mem(10 * KB, 2 * KB, failed=3)}
        self.assertIn("- ⚠️ Не хватило памяти: **excel** · main (3)", lines({}, head))

    def test_calibrated_sizes_are_not_called_an_estimate(self):
        out = lines({}, {"dice__main": mem(120 * KB, 110 * KB, estimate=False)})
        self.assertEqual(out[0], "**Память** (DRAM 320 КБ, PSRAM 8,0 МБ)")

    def test_nothing_without_the_model(self):
        self.assertEqual(lines({}, {}), [])

    def test_load_takes_trees_with_memory_only(self):
        d = Path(tempfile.mkdtemp())
        (d / "a__main.json").write_text(json.dumps({"mem": mem(1, 1), "screen": {}}))
        (d / "b__main.json").write_text(json.dumps({"screen": {}}))  # an older simulator
        (d / "c__main.json").write_text("{broken")
        self.assertEqual(sorted(load(d)), ["a__main"])


if __name__ == "__main__":
    unittest.main()
