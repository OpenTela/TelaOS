#!/usr/bin/env python3
"""Tests for gen_pages.py:  python3 -m unittest discover -s sim/tools -p 'test_*.py'"""

import os
import re
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gen_pages  # noqa: E402

SRC = Path(__file__).resolve().parents[2] / "src"


def engine_tags():
    """Tag names the engine knows: the HTML layer's table and the TelaOS element names"""
    html = set(re.findall(r'\{"([a-z0-9-]+)", Kind::', (SRC / "ui" / "ui_html_flow.cpp").read_text("utf-8")))
    types = (SRC / "ui" / "ui_types.h").read_text("utf-8")
    element = types[types.index("namespace Element"):]
    tela = set(re.findall(r'constexpr const char\* \w+ = "([a-z]+)";', element[:element.index("}")]))
    return html | tela


class GenPages(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.pages = [gen_pages.page(1, i) for i in range(300)]  # the CI count
        cls.text = "\n".join(cls.pages).lower()

    def test_page_n_of_a_seed_is_always_the_same(self):
        self.assertEqual(gen_pages.page(1, 7), gen_pages.page(1, 7))
        self.assertNotEqual(gen_pages.page(1, 7), gen_pages.page(2, 7))
        with tempfile.TemporaryDirectory() as d:
            gen_pages.write(d, 3, seed=1, per_batch=2)
            self.assertEqual((Path(d) / "batch_1/apps/gen_0002/gen_0002.bax").read_text("utf-8"), gen_pages.page(1, 2))

    def test_every_page_is_an_app_with_one_page(self):
        for p in self.pages:
            self.assertTrue(p.startswith("<app ") and p.rstrip().endswith("</app>"))
            self.assertEqual(p.count('<page id="main">'), 1)

    def test_every_tag_the_engine_knows_turns_up(self):
        tags = engine_tags()
        self.assertGreater(len(tags), 120)  # both tables were found
        missing = sorted(t for t in tags if not re.search(rf"<{re.escape(t)}[\s>/]", self.text))
        self.assertEqual(missing, [])

    def test_the_markup_the_walker_must_survive_turns_up(self):
        for piece in ["<!-- <label>hidden</label> > still a comment -->", "</div>", "<!doctype html>", " a < b ",
                      "<" + "x" * 32, 'title="a>b"', "{nothing}", 'os="2.0"', "<card>", '<template id="card">',
                      "@for("]:
            self.assertIn(piece.lower(), self.text, piece)
        self.assertRegex(self.text, r"<li>[^<]*<li>")  # an implied </li>
        self.assertRegex("\n".join(self.pages), r"<[A-Z]{2,}[\s>/]")  # an upper-case tag
        self.assertRegex(self.text, r"<\w+\s+(hidden|disabled|open|checked) \w+=")  # a valueless attribute first


if __name__ == "__main__":
    unittest.main()
