#!/usr/bin/env python3
"""Tests for mem_compare.py (the files are written here):
python3 -m unittest discover -s sim/tools -p 'test_*.py'"""

import json
import os
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import mem_compare  # noqa: E402


def mem(dram_free, psram_free, failed=0, at_boot=(0, 0), total=(327680, 8388608)):
    return {"dram": {"total": total[0], "free": dram_free, "min_free": dram_free, "largest": dram_free,
                     "blocks": 10, "failed": failed, "at_boot": at_boot[0]},
            "psram": {"total": total[1], "free": psram_free, "min_free": psram_free, "largest": psram_free,
                      "blocks": 10, "failed": 0, "at_boot": at_boot[1]}}


class Compare(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        root = Path(self.tmp.name)
        self.sim = root / "shots"
        self.sim.mkdir()
        apps = root / "data" / "apps" / "weather"
        apps.mkdir(parents=True)
        (apps / "weather.bax").write_text('<app><ui default="/main"><page id="main"></page>'
                                          '<page id="cities"></page></ui></app>')
        self.roots = [root / "data"]
        sim_launcher = mem(200 * 1024, 7000 * 1024, total=(320 * 1024, 8 * 1024 * 1024))
        sim_launcher["estimate"] = True
        (self.sim / "launcher.json").write_text(json.dumps({"mem": sim_launcher}))
        (self.sim / "weather__main.json").write_text(json.dumps({"mem": mem(160 * 1024, 6900 * 1024)}))
        self.bench = {"fw": "fc686e0", "chip": "ESP32-S3", "apps": {
            "weather": {"launcher": mem(180 * 1024, 7500 * 1024, at_boot=(290 * 1024, 8100 * 1024)),
                        "app": mem(150 * 1024, 7450 * 1024, failed=1)},
            "crypto": {"launcher": mem(180 * 1024, 7500 * 1024), "app": mem(170 * 1024, 7500 * 1024)},
        }}

    def tearDown(self):
        self.tmp.cleanup()

    def report(self):
        return "\n".join(mem_compare.compare(self.bench, self.sim, self.roots))

    def test_sizes_to_give_the_simulator(self):
        text = self.report()
        self.assertIn("`--dram-kb 290 --psram-kb 8100`", text)
        self.assertIn("| у прошивки в симуляторе (оценка) | 320,0 КБ | 8192,0 КБ |", text)

    def test_app_cost_launcher_to_first_screen(self):
        # watch: 180 → 150 KB, 30 KB; simulator: 200 → 160 KB, 40 KB; PSRAM 50 and 100 KB; 1 failed
        self.assertIn("| weather | 30,0 КБ | 40,0 КБ | 1,33× | 50,0 КБ | 100,0 КБ | 1 |", self.report())

    def test_no_screen_in_the_simulator(self):
        self.assertIn("| crypto | 10,0 КБ | нет экрана | — | 0,0 КБ | нет экрана | 0 |", self.report())


if __name__ == "__main__":
    unittest.main()
