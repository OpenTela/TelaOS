#!/usr/bin/env python3
"""Tests for doc_check.py: python3 -m unittest discover -s .github/scripts -p 'test_*.py'"""

import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import doc_check as dc  # noqa: E402

DOC = """# Simulator v0.14

Text.

## Changelog

- **v0.14**: timers.
- **v0.13**: idle.
- **v0.12**: ASan.

---

## Robot

- **v0.99**: not a Changelog entry: it is past the Changelog.
"""


class Versions(unittest.TestCase):
    def test_title_and_changelog_agree(self):
        self.assertEqual(dc.versions(DOC), [])

    def test_an_unversioned_document(self):
        self.assertEqual(dc.versions("# README\n\nNo version here.\n"), [])

    def test_a_merge_that_left_them_apart(self):
        got = dc.versions(DOC.replace("# Simulator v0.14", "# Simulator v0.15"))
        self.assertEqual(got, ["в заголовке v0.15, а первая запись Changelog — v0.14"])

    def test_two_prs_with_one_number(self):
        doc = DOC.replace("- **v0.13**: idle.", "- **v0.14**: idle.")
        self.assertEqual(dc.versions(doc), ["Changelog не по убыванию: v0.14, а за ней v0.14"])

    def test_numbers_not_strings(self):
        doc = "# X v0.10\n\n## Changelog\n\n- **v0.10**: ten.\n- **v0.9**: nine.\n"
        self.assertEqual(dc.versions(doc), [])

    def test_a_version_without_a_changelog(self):
        self.assertEqual(dc.versions("# Console Protocol v2.7 — spec\n\nText.\n"),
                         ["в заголовке v2.7, а Changelog с версиями нет"])


class Coverage(unittest.TestCase):
    TABLE = """## Работает: 2 из 4

| | Что | Как |
|---|---|---|
| ✅ | `a` | yes |
| ✅ | `b` | yes |
| ½ | `c` | half |
| — | `d` | no point |

## Значения

| ✅ | `e` | another table, not counted |
"""

    def test_the_count_and_the_tables_agree(self):
        self.assertEqual(dc.coverage(self.TABLE), [])

    def test_a_row_added_without_the_count(self):
        doc = self.TABLE.replace("| — | `d` | no point |", "| — | `d` | no point |\n| ✅ | `f` | new |")
        self.assertEqual(dc.coverage(doc), ["«Работает: 2 из 4», а в таблицах ✅ 3 из 5"])


class Code(unittest.TestCase):
    HEADER = "enum Cat : uint8_t {\n    UI = 0,\n    LUA,\n    SD,\n    COUNT\n};\nenum Level : uint8_t { Off = 0, Info };\n"

    def test_categories_and_levels_agree(self):
        spec = "**Категории:** `ui`, `lua`, `sd`\n**Уровни:** `off`, `info`\n"
        self.assertEqual(dc.log_names(self.HEADER, spec), [])

    def test_missing_and_extra(self):
        spec = "**Категории:** `ui`, `lua`, `gps`\n**Уровни:** `off`, `info`\n"
        self.assertEqual(dc.log_names(self.HEADER, spec),
                         ["«Категории»: в коде есть, в протоколе нет — sd",
                          "«Категории»: в протоколе есть, в коде нет — gps"])

    def test_the_protocol_version_is_the_code_s(self):
        spec = "# Console Protocol v2.7 — spec\n"
        cpp = 'static constexpr const char* PROTOCOL_VERSION = "2.7";'
        self.assertEqual(dc.protocol(spec, {"console.cpp": cpp, "ble_assistant.py": 'PROTOCOL_VERSION = "2.7"'}), [])
        self.assertEqual(dc.protocol(spec, {"ble_assistant.py": 'PROTOCOL_VERSION = "2.8"'}),
                         ["протокол v2.7, а в ble_assistant.py — 2.8"])
        self.assertEqual(dc.protocol(spec, {"x.py": "nothing"}), ["в x.py нет PROTOCOL_VERSION"])


if __name__ == "__main__":
    unittest.main()
