#!/usr/bin/env python3
"""Tests for sim_asan.py (no simulator: the logs are written here):
python3 -m unittest discover -s sim/tools -p 'test_*.py'"""

import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import sim_asan  # noqa: E402

# As ASan writes it (the use-after-free of #67, shortened)
ASAN = """[sim] robot: 113 passed, 0 failed
=================================================================
==4242==ERROR: AddressSanitizer: heap-use-after-free on address 0x6020000a1b30 at pc 0x55d0 bp 0x7ffd sp 0x7ffd
READ of size 8 at 0x6020000a1b30 thread T0
    #0 0x55d0c1 in forget_widget_state /w/src/ui/ui_widget_builder.cpp:132
    #1 0x55d0c2 in lv_obj_destruct /w/lib/lvgl/src/core/lv_obj.c:415
SUMMARY: AddressSanitizer: heap-use-after-free /w/src/ui/ui_widget_builder.cpp:132 in forget_widget_state
"""

# As UBSan writes it: it goes on after a report
UBSAN = """/w/src/ui/css_box.cpp:77:31: runtime error: signed integer overflow: 2147483647 + 1 cannot be represented in type 'int'
    #0 0x55 in boxValue /w/src/ui/css_box.cpp:77
/w/src/ui/css_box.cpp:77:31: runtime error: signed integer overflow: 2147483600 + 90 cannot be represented in type 'int'
[sim] robot: 7 passed, 0 failed
"""


class Reports(unittest.TestCase):
    def test_clean_log(self):
        found, first = sim_asan.reports("[sim] robot: 7 passed, 0 failed\n")
        self.assertEqual(found, [])
        self.assertIsNone(first)

    def test_asan(self):
        found, first = sim_asan.reports(ASAN)
        self.assertEqual(found, [("AddressSanitizer: heap-use-after-free",
                                  "forget_widget_state /w/src/ui/ui_widget_builder.cpp:132")])
        self.assertTrue(first.startswith("==4242==ERROR: AddressSanitizer: heap-use-after-free"))
        self.assertIn("READ of size 8", first)

    def test_ubsan_each_report_one_kind(self):
        found, first = sim_asan.reports(UBSAN)
        # the same check at the same place, with other numbers: one kind
        self.assertEqual(len(found), 2)
        self.assertEqual(found[0], found[1])
        self.assertEqual(found[0][1], "/w/src/ui/css_box.cpp:77:31")
        self.assertTrue(found[0][0].startswith("UBSan: signed integer overflow"))
        self.assertIn("css_box.cpp:77:31: runtime error", first)

    def test_both_in_one_log(self):
        found, _ = sim_asan.reports(UBSAN + ASAN)
        self.assertEqual([k for k, _ in found][-1], "AddressSanitizer: heap-use-after-free")
        self.assertEqual(len(found), 3)

    def test_suppressions_file_names_why(self):
        # every rule says why it does not count: a comment right above it
        supp = os.path.join(os.path.dirname(os.path.abspath(__file__)), "ubsan.supp")
        with open(supp, encoding="utf-8") as f:
            lines = [l.rstrip("\n") for l in f]
        rules = [i for i, l in enumerate(lines) if l and not l.startswith("#")]
        self.assertTrue(rules)
        for i in rules:
            self.assertTrue(lines[i - 1].startswith("#"), f"no why above {lines[i]!r}")


class Report(unittest.TestCase):
    """asan.md: which pages were drawn, and how to draw again the one that failed"""
    TESTS = [{"name": "tela_hello", "passed": 7, "failed": 0, "ok": True}]

    def test_clean_with_two_seeds(self):
        lines = sim_asan.report(9, self.TESTS, 300, [1, 93], {}, [], [], None)
        self.assertIn("случайные страницы: 300 × seed 1, 300 × seed 93.", lines[2])
        self.assertEqual(lines[3], "Ни одного сообщения санитайзеров, все роботы прошли.")
        self.assertFalse(any("повторить" in l for l in lines))

    def test_a_page_that_stopped_says_its_seed(self):
        lines = sim_asan.report(9, self.TESTS, 3000, [57], {}, [], ["pages/seed-57/batch_12"], None)
        self.assertIn("Прогон остановился: `pages/seed-57/batch_12`", lines)
        self.assertIn("`python3 sim/tools/gen_pages.py <папка> --count 3000 --seed 57`", lines[-1])

    def test_a_report_on_a_page_and_one_on_a_screen(self):
        found = {("AddressSanitizer: heap-use-after-free", "f x.cpp:1"): ["pages/seed-93/batch_0.log"],
                 ("UBSan: shift", "y.cpp:2"): ["screens/html_tags.log"]}
        lines = sim_asan.report(9, self.TESTS, 300, [1, 93], found, [], [], "==1==ERROR: AddressSanitizer")
        self.assertIn("--seed 93`", lines[-1])
        self.assertNotIn("--seed 1`", lines[-1])

    def test_no_pages(self):
        self.assertIn("случайные страницы: 0.", sim_asan.report(9, self.TESTS, 0, [1], {}, [], [], None)[2])


if __name__ == "__main__":
    unittest.main()
