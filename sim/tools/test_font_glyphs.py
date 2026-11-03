"""The glyphs of the firmware fonts, read from their cmaps in src/font/*.c.

    python3 -m unittest sim/tools/test_font_glyphs.py

Ubuntu_*px keep their 213 glyphs; UbuntuExt_*px, their fallbacks (scripts/make_fonts.py), add the
other languages and draw nothing twice. Every letter of the html_tags «langs» page is in one of them.
"""

import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
FONTS = ROOT / "src" / "font"
SIZES = (16, 32, 48, 72)
LANGS_PAGE = ROOT / "sim" / "tests" / "fixtures" / "apps" / "html_tags" / "html_tags.bax"


def codepoints(path):
    """The codepoints of an lv_font_conv font, as LVGL finds them: a FORMAT0_TINY cmap is a range,
    FORMAT0_FULL a range where offset 0 past the first code is a missing glyph, SPARSE a list."""
    text = Path(path).read_text("utf-8")
    lists = {name: [int(v, 0) for v in re.findall(r"0x[0-9a-fA-F]+|\d+", body)]  # hex or decimal
             for name, body in re.findall(r"static const uint(?:8|16)_t ((?:unicode|glyph_id_ofs)_list_\d+)\[\] = \{(.*?)\};",
                                          text, re.S)}
    out = set()
    for start, length, ulist, ofs, kind in re.findall(
            r"\.range_start = (\d+), \.range_length = (\d+),.*?\.unicode_list = (\w+), \.glyph_id_ofs_list = (\w+),"
            r".*?\.type = (LV_FONT_FMT_TXT_CMAP_\w+)", text, re.S):
        start, length = int(start), int(length)
        if kind.endswith("FORMAT0_TINY"):
            out |= set(range(start, start + length))
        elif kind.endswith("FORMAT0_FULL"):
            out |= {start + i for i, o in enumerate(lists[ofs][:length]) if o or i == 0}
        else:
            out |= {start + d for d in lists[ulist]}
    return out


class FontGlyphs(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.main = {s: codepoints(FONTS / f"Ubuntu_{s}px.c") for s in SIZES}
        cls.ext = {s: codepoints(FONTS / f"UbuntuExt_{s}px.c") for s in SIZES}

    def test_main_fonts_keep_their_glyphs(self):
        for s in SIZES:
            self.assertEqual(len(self.main[s]), 213, s)
        self.assertTrue(all(self.main[s] == self.main[16] for s in SIZES))

    def test_every_size_has_the_same_languages(self):
        self.assertTrue(all(self.ext[s] == self.ext[16] for s in SIZES))
        self.assertGreater(len(self.ext[16]), 400)

    def test_nothing_is_drawn_twice(self):
        for s in SIZES:
            self.assertEqual(self.main[s] & self.ext[s], set(), s)

    def test_the_letters_of_european_languages(self):
        letters = "ÄäÖöÜüßÉéÈèÇçŒœÑñ¿¡ĄąĆćĘęŁłŃńŚśŹźŻżČčŘřŠšŽžŮůŤťĎďŇňĚěŐőŰűȘșȚțƏə" \
                  "ΑαΓγΔδΩωάέήίόύώ" "ҐґҚқӘәҰұӨөҢңҺһ" "№−≤≥≠≈∞√‰…™·"
        have = self.main[16] | self.ext[16]
        self.assertEqual("".join(ch for ch in letters if ord(ch) not in have), "")

    def test_the_langs_page_has_every_glyph(self):
        page = re.search(r'<page id="langs">(.*?)</page>', LANGS_PAGE.read_text("utf-8"), re.S).group(1)
        text = re.sub(r"<[^>]*>", "", page)
        have = self.main[16] | self.ext[16] | {0x0A, 0x20}
        self.assertEqual(sorted({ch for ch in text if ord(ch) not in have}), [])


if __name__ == "__main__":
    unittest.main()
