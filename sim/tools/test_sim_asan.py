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


if __name__ == "__main__":
    unittest.main()
