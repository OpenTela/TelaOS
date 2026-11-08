#!/usr/bin/env python3
"""Tests for judge_pages.py and the plain HTML pages of gen_pages.py (no simulator, no Chromium):
python3 -m unittest discover -s sim/tools -p 'test_*.py'"""

import json
import os
import re
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gen_pages  # noqa: E402
import judge  # noqa: E402
import judge_pages  # noqa: E402


class HtmlPages(unittest.TestCase):
    def test_page_n_of_a_seed_is_always_the_same_and_not_the_other_kind(self):
        self.assertEqual(gen_pages.page(1, 7, html=True), gen_pages.page(1, 7, html=True))
        self.assertNotEqual(gen_pages.page(1, 7, html=True), gen_pages.page(2, 7, html=True))
        self.assertNotEqual(gen_pages.page(1, 7, html=True), gen_pages.page(1, 7))

    def test_plain_html_only(self):
        tela = set(gen_pages.TELA_WIDGETS + gen_pages.TELA_CONTAINERS) - {"div", "table", "button"}
        for i in range(100):
            text = gen_pages.page(1, i, html=True)
            pages, state = judge.app_pages(text)
            self.assertEqual([p for p, _ in pages], ["main"])
            self.assertEqual(state, {})
            markup = pages[0][1]
            self.assertNotIn("{", markup.replace("{ ", "").replace(" }", ""), "no {var}")
            tags = set(re.findall(r"<([a-z][a-z0-9]*)", markup))
            self.assertFalse(tags & tela, f"page {i}: {tags & tela}")
            self.assertNotRegex(markup, r'\s(x|y|w|h|align|flex|bgcolor|onclick)="')

    def test_every_kind_of_block_and_rule_turns_up(self):
        text = "".join(gen_pages.page(1, i, html=True) for i in range(300))
        for tag in set(gen_pages.HTML_FLOW) | set(gen_pages.HTML_PHRASING):
            self.assertIn(f"<{tag}", text)
        for prop in gen_pages.CSS:
            self.assertIn(f"{prop}:", text)


class Documents(unittest.TestCase):
    def test_the_fonts_are_fetched_not_carried(self):
        with tempfile.TemporaryDirectory() as d:
            gen_pages.write(d, 3, 1, per_batch=2, html=True)
            docs = judge_pages.documents(d, 3, "/nowhere")
        self.assertEqual(sorted(docs), ["gen_0000", "gen_0001", "gen_0002"])
        doc = docs["gen_0001"]
        self.assertIn(f"url('{judge_pages.HOST}/fonts/Ubuntu-Regular.ttf')", doc)
        self.assertNotIn("data:font", doc)
        self.assertIn("<style>", doc)
        markup = judge.app_pages(gen_pages.page(1, 1, html=True))[0][0][1]
        self.assertIn(judge.standard(markup).strip()[:60], doc)


class Trees(unittest.TestCase):
    def test_the_page_main_of_each_drawn_tree(self):
        with tempfile.TemporaryDirectory() as d:
            tree = {"screen": {"w": 480, "h": 480}, "dom": [{"id": "main", "tag": "page", "children": []}]}
            Path(d, "gen_0000.json").write_text(json.dumps(tree))
            Path(d, "gen_0002.json").write_text(json.dumps({"screen": {"w": 480, "h": 480}}))  # no "dom"
            found, size = judge_pages.trees(d, ["gen_0000", "gen_0001", "gen_0002"])
        self.assertEqual(list(found), ["gen_0000"])
        self.assertEqual(size, (480, 480))


class Lines(unittest.TestCase):
    def result(self, agree, count):
        return {"app": "случайные", "agree": agree, "count": count, "boxes": 0,
                "dims": {"x": 0, "y": 0, "w": 0, "h": 0}, "skipped": {},
                "verdicts": [], "seed": 1, "drawn": 300, "of": 300}

    def test_the_title_says_how_to_make_the_pages_again(self):
        out = judge_pages.lines(self.result(660, 4190))
        self.assertTrue(out[0].startswith("**Случайные страницы против Chromium:** 300 страниц HTML и CSS "
                                          "(`gen_pages.py --html --seed 1`)"))
        self.assertIn("**660 из 4190, 16%**", out[0])

    def test_against_the_base(self):
        self.assertIn(", столько же", judge_pages.lines(self.result(660, 4190), self.result(660, 4190))[0])
        self.assertIn("(было 600 из 4190, 14%)", judge_pages.lines(self.result(660, 4190), self.result(600, 4190))[0])

    def test_nothing_without_a_result(self):
        self.assertEqual(judge_pages.lines(None, None), [])


if __name__ == "__main__":
    unittest.main()
