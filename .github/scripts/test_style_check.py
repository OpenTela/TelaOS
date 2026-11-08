#!/usr/bin/env python3
"""Tests for style_check.py: python3 -m unittest discover -s .github/scripts -p 'test_*.py'"""

import io
import os
import subprocess
import sys
import tempfile
import unittest
from contextlib import redirect_stdout
from pathlib import Path
from unittest import mock

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import style_check as sc  # noqa: E402


def what(text, long=sc.LONG):
    """[(line, first word of the problem)]"""
    return [(at, w.split()[0] if not w.startswith("тело") else " ".join(w.split()[:2]))
            for _, _, at, w in sc.problems(text, long)]


class Blank(unittest.TestCase):
    def test_comments_strings_and_directives_are_not_code(self):
        text = ('a = "if (x) y;"; // if (x) y;\n'
                "/* if (x)\n y; */ b = '(';\n"
                "#define CHECK(x) \\\n    if (!(x)) return\n"
                "#if FOO\n")
        code = sc.blank(text)
        self.assertEqual(code.count("\n"), text.count("\n"))
        self.assertEqual(len(code), len(text))
        self.assertNotIn("if", code)
        self.assertNotIn("(", code.replace("CHECK", ""))
        self.assertEqual(code.split("\n")[0], 'a = "' + "_" * 9 + '";' + " " * 13)

    def test_raw_strings(self):
        code = sc.blank('s = R"css(a { if (x) y; })css"; t = u8R"(()";\nif (ok)\n')
        self.assertEqual(code.count("if"), 1)
        self.assertTrue(code.startswith('s = R"css(____'))

    def test_escapes_and_digit_separators(self):
        code = sc.blank("c = '\\''; n = 1'000'000; s = \"\\\"if (x) y;\";\n")
        self.assertIn("1'000'000", code)
        self.assertNotIn("if", code)


class Body(unittest.TestCase):
    def test_the_body_on_the_line_of_the_condition(self):
        self.assertEqual(what("if (ok) return;\n"), [(1, "тело if")])
        self.assertEqual(what("if (ok) { return; }\n"), [(1, "тело if")])
        self.assertEqual(what("if constexpr (ok) return;\n"), [(1, "тело if")])
        self.assertEqual(what("if (ok);\n"), [(1, "тело if")])

    def test_the_body_on_its_own_line(self):
        self.assertEqual(what("if (ok)\n    return;\n"), [])
        self.assertEqual(what("if (ok) {\n    return;\n}\n"), [])
        self.assertEqual(what("if (ok) {  // why\n    return;\n}\n"), [])
        self.assertEqual(what("if (ok) /* why */\n    return;\n"), [])

    def test_else(self):
        self.assertEqual(what("if (ok)\n    a();\nelse b();\n"), [(3, "тело else")])
        self.assertEqual(what("if (ok) {\n    a();\n} else return;\n"), [(3, "тело else")])
        self.assertEqual(what("if (ok)\n    a();\nelse\n    b();\n"), [])
        self.assertEqual(what("if (ok) {\n    a();\n} else {\n    b();\n}\n"), [])

    def test_else_if_is_an_if(self):
        self.assertEqual(what("if (a)\n    x();\nelse if (b)\n    y();\n"), [])
        self.assertEqual(what("if (a)\n    x();\nelse if (b) y();\n"), [(3, "тело if")])

    def test_not_ifs(self):
        self.assertEqual(what('log("if (x) y; else z;");\nint elsewhere = diff(1);\n'), [])
        self.assertEqual(what("#define GUARD(x) if (!(x)) return\n#else\n"), [])


class Condition(unittest.TestCase):
    EXAMPLE = "if (lv_obj_get_content_width(cont) <= 0 || lv_obj_get_content_height(cont) <= 0)\n    stop();\n"

    def test_the_example_of_contributing_is_long(self):
        self.assertEqual(what(self.EXAMPLE), [(1, "длинное")])
        self.assertIn("75 знаков", sc.problems(self.EXAMPLE)[0][3])

    def test_the_limit(self):
        self.assertEqual(what("if (" + "a" * 60 + ")\n    b();\n"), [])
        self.assertEqual(what("if (" + "a" * 61 + ")\n    b();\n"), [(1, "длинное")])
        self.assertEqual(what("if (a  &&   b)\n    c();\n", long=6), [], "spaces run together")

    def test_a_condition_on_several_lines(self):
        self.assertEqual(what("if (a &&\n    b)\n    c();\n"), [(1, "условие")])
        self.assertEqual(what("if (a &&\n    b) c();\n"), [(1, "условие"), (2, "тело if")])

    def test_parentheses_inside(self):
        self.assertEqual(what("if (f(a, (b)) && g())\n    c();\n"), [])
        self.assertEqual(what("if (f(\")\"))\n    c();\n"), [])

    def test_the_variable_is_the_cure(self):
        text = ("const bool noRoom = lv_obj_get_content_width(cont) <= 0 || lv_obj_get_content_height(cont) <= 0;\n"
                "if (noRoom)\n    stop();\n")
        self.assertEqual(what(text), [])


class Names(unittest.TestCase):
    def test_two_files_with_one_name(self):
        self.assertEqual(sc.same_names(["src/ui/engine.cpp", "src/lua/engine.cpp", "src/ui/ui_engine.h"]),
                         {"engine.cpp": ["src/ui/engine.cpp", "src/lua/engine.cpp"]})

    def test_stubs_third_party_and_other_files(self):
        files = ["ci/stubs/Arduino.h", "sim/shim/Arduino.h", "lib/md4c/md4c.c", "src/md4c.c",
                 "README.md", "docs/README.md", "sim/third_party/tlsf/tlsf.c", "src/tlsf.c"]
        self.assertEqual(sc.same_names(files), {})


class Repository(unittest.TestCase):
    """check() and main() on a real git repository: only the lines the change adds count"""

    OLD = "void f(bool ok) {\n    if (ok) return;\n    int a = 1;\n}\n"

    def setUp(self):
        self.dir = tempfile.TemporaryDirectory()
        self.root = Path(self.dir.name)
        self.git("init", "-q", "-b", "future")
        (self.root / "src").mkdir()
        (self.root / "lib").mkdir()
        (self.root / "src/old.cpp").write_text(self.OLD)
        (self.root / "lib/vendor.c").write_text("void g(int x) { if (x) x++; }\n")
        self.git("add", ".")
        self.git("commit", "-q", "-m", "base")
        self.git("checkout", "-q", "-b", "pr")

    def tearDown(self):
        self.dir.cleanup()

    def git(self, *args):
        subprocess.run(["git", "-C", str(self.root), "-c", "user.name=t", "-c", "user.email=t@t", *args],
                       check=True, capture_output=True)

    def commit(self, path, text):
        (self.root / path).parent.mkdir(parents=True, exist_ok=True)
        (self.root / path).write_text(text)
        self.git("add", ".")
        self.git("commit", "-q", "-m", "pr")

    def test_old_code_untouched_is_not_the_pr_s(self):
        self.commit("src/old.cpp", self.OLD.replace("int a = 1;", "int a = 2;"))
        self.assertEqual(sc.check(self.root, "future"), [])

    def test_a_new_one_line_if_and_a_touched_old_one(self):
        self.commit("src/old.cpp", self.OLD.replace("return;", "return ;") + "void h(bool b) { if (b) g(); }\n")
        self.assertEqual(sc.check(self.root, "future"),
                         [("src/old.cpp", 2, sc.BODY), ("src/old.cpp", 5, sc.BODY)])

    def test_third_party_code_and_other_files(self):
        self.commit("lib/vendor.c", "void g(int x) { if (x) x--; }\n")
        self.commit("tools/x.py", "if x: y()\n")
        self.assertEqual(sc.check(self.root, "future"), [])

    def test_a_second_file_with_a_taken_name(self):
        self.commit("src/ui/old.cpp", "int u;\n")
        self.assertEqual([(p, at) for p, at, _ in sc.check(self.root, "future")],
                         [("src/old.cpp", None), ("src/ui/old.cpp", None)])

    def test_main_annotates_the_line_in_actions(self):
        self.commit("src/new.cpp", "void h(bool b) {\n    if (b) g();\n}\n")
        out = io.StringIO()
        with mock.patch.dict(os.environ, {"GITHUB_ACTIONS": "true"}), redirect_stdout(out):
            code = sc.main(["--base", "future", str(self.root)])
        self.assertEqual(code, 1)
        self.assertIn(f"src/new.cpp:2: {sc.BODY}", out.getvalue())
        self.assertIn(f"::error file=src/new.cpp,line=2::{sc.BODY}", out.getvalue())

    def test_main_on_clean_code(self):
        self.commit("src/new.cpp", "void h(bool b) {\n    if (b)\n        g();\n}\n")
        with redirect_stdout(io.StringIO()):
            self.assertEqual(sc.main(["--base", "future", str(self.root)]), 0)


if __name__ == "__main__":
    unittest.main()
