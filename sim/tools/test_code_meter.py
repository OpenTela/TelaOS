#!/usr/bin/env python3
"""Tests for code_meter.py:  python3 -m unittest discover -s sim/tools -p 'test_*.py'"""

import os
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from code_meter import count, measure, summary  # noqa: E402


class CodeMeter(unittest.TestCase):
    def test_each_kind(self):
        src = """
            if (strcmp(a, b) == 0 && strncmp(a, "x", 1)) {}
            const char* p = strstr(s, "<"); p = strchr(s, '>');
            snprintf(buf, sizeof(buf), "%d", 1);
            strcpy(d, s); strncat(d, s, 3);
            char tag[TAG_BUF_LEN]; char closing[64];
            void* m = malloc(10); free(m);
        """
        self.assertEqual(count(src), {"strcmp": 2, "strfind": 2, "printf": 1, "strcpy": 2,
                                      "charbuf": 2, "malloc": 2})

    def test_comments_and_strings_do_not_count(self):
        src = """
            // strcmp(a, b) in a comment
            /* char old[16]; malloc(1) */
            const char* s = "strcmp(a, b) and free(x)";
            auto html = R"HTML(<p>strstr(</p> "quoted" )HTML";
            char c = '"'; char q = '\\'';
            int n = 1'000'000;
            if (strcmp(x, y)) {}
        """
        self.assertEqual(count(src)["strcmp"], 1)
        self.assertEqual(sum(count(src).values()), 1)

    def test_prefixed_names_are_not_libc(self):
        src = "heap_caps_free(p); lv_free(p); ps_malloc(4); my_strcmp(a, b); Str::strcmp_ci(a);"
        self.assertEqual(sum(count(src).values()), 0)

    def test_char_pointer_is_not_a_buffer(self):
        self.assertEqual(count("char* p; const char *q; char buf[8];")["charbuf"], 1)

    def test_measure_a_checkout(self):
        root = Path(tempfile.mkdtemp())
        (root / "src" / "ui").mkdir(parents=True)
        (root / "src" / "ui" / "a.cpp").write_text("int f(){ return strcmp(a,b); }\n")
        (root / "src" / "b.h").write_text("char x[4];\nchar y[4];\n")
        (root / "docs").mkdir()
        (root / "docs" / "c.cpp").write_text("strcmp(a,b);")  # outside src/: not counted
        m = measure(root)
        self.assertEqual(m["files"], 2)
        self.assertEqual(m["metrics"]["strcmp"], 1)
        self.assertEqual(m["metrics"]["charbuf"], 2)
        self.assertEqual(m["top"], [(2, "src/b.h"), (1, "src/ui/a.cpp")])

    def test_summary(self):
        base = {"metrics": {"strcmp": 179, "strfind": 44, "printf": 68, "strcpy": 9, "charbuf": 103, "malloc": 14}}
        same = {"metrics": dict(base["metrics"])}
        less = {"metrics": dict(base["metrics"], strcmp=175, charbuf=101)}
        more = {"metrics": dict(base["metrics"], malloc=15)}
        self.assertEqual(summary(same, base), "**Сишные места в src/:** 417, столько же")
        self.assertEqual(summary(less, base),
                         "**Сишные места в src/:** 417 → 411 (−6): `strcmp` −4, `char x[N]` −2")
        self.assertEqual(summary(more, base), "**Сишные места в src/:** 417 → 418 (+1): `malloc/free` +1")
        self.assertEqual(summary(same), "**Сишные места в src/:** 417")


if __name__ == "__main__":
    unittest.main()
