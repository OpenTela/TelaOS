"""The glyphs of the firmware fonts, read from their cmaps: src/font/*.c and the fonts kept as
files, data/system/fonts/*.bin.

    python3 -m unittest sim/tools/test_font_glyphs.py

Ubuntu_*px keep their 213 glyphs; UbuntuExt_*px, their fallbacks (scripts/make_fonts.py), add the
other languages and draw nothing twice. Every letter of the html_tags «langs» page is in one of them.
Icons_*px (loaded from LittleFS) have every icon of Icons.txt and the signs apps write, and are in
the format the firmware reads; every character of the apps and fixtures has a glyph. The sizes
between the four (Ubuntu_<px>px.bin, LittleFS too) are the sizes UI::Font draws, and draw what
the four draw. UbuntuBold_*px, UbuntuItalic_*px and UbuntuBoldItalic_*px draw what the regular fonts
draw. Every text font has the line of a browser (line-height: normal, from the face's hhea): the
same for every face of a size, taller for a larger size. Icons and the faces come in every size.
"""

import math
import re
import struct
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
FONTS = ROOT / "src" / "font"
FILE_FONTS = ROOT / "data" / "system" / "fonts"
SIZES = (16, 32, 48, 72)  # in flash
FIXTURES = ROOT / "sim" / "tests" / "fixtures"
LANGS_PAGE = FIXTURES / "apps" / "html_tags" / "html_tags.bax"
# font_header_bin_t of lib/lvgl/src/font/lv_binfont_loader.c, after the "head" label
BIN_HEAD = struct.Struct("<IHHHhHhHhhHHBBBBBBBBBBhH")
BIN_HEAD_FIELDS = ("version tables font_size ascent descent typo_ascent typo_descent typo_line_gap min_y max_y "
                   "default_advance_width kerning_scale index_to_loc_format glyph_id_format advance_width_format "
                   "bits_per_pixel xy_bits wh_bits advance_width_bits compression_id subpixels_mode padding "
                   "underline_position underline_thickness").split()


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


def bin_font(path):
    """(header fields, codepoints, cmap subtables) of an lv_font_conv binary font, read as
    lv_binfont_create() reads it: the "head" table, then "cmap" with its subtables."""
    data = Path(path).read_bytes()

    def table(at, label):
        length, = struct.unpack_from("<I", data, at)
        assert data[at + 4:at + 8] == label, (path, label)
        return length

    head_length = table(0, b"head")
    head = dict(zip(BIN_HEAD_FIELDS, BIN_HEAD.unpack_from(data, 8)))
    cmaps = head_length
    table(cmaps, b"cmap")
    count, = struct.unpack_from("<I", data, cmaps + 8)
    codes, subtables = set(), []
    for i in range(count):
        offset, start, length, _gid, entries, kind, _ = struct.unpack_from("<IIHHHBB", data, cmaps + 12 + 16 * i)
        subtables.append((kind, length, entries))
        body = cmaps + offset
        if kind == 2:  # FORMAT0_TINY: a range
            codes |= set(range(start, start + length))
        elif kind == 0:  # FORMAT0_FULL: offset 0 past the first code is a missing glyph
            codes |= {start + j for j, o in enumerate(data[body:body + entries][:length]) if o or j == 0}
        else:  # SPARSE_FULL, SPARSE_TINY: a list of offsets from start
            codes |= {start + d for d in struct.unpack_from(f"<{entries}H", data, body)}
    return head, codes, subtables


def between_sizes():
    """BETWEEN of scripts/make_fonts.py: the sizes kept as files."""
    text = (ROOT / "scripts" / "make_fonts.py").read_text("utf-8")
    return tuple(int(v) for v in re.search(r"^BETWEEN = \(([\d, ]+)\)", text, re.M).group(1).split(","))


def firmware_sizes():
    """kSizes of src/utils/font.cpp: the sizes UI::Font draws."""
    text = (ROOT / "src" / "utils" / "font.cpp").read_text("utf-8")
    body = re.search(r"constexpr int kSizes\[\] = \{(.*?)\};", text, re.S).group(1)
    named = {"Font::SMALL": 16, "Font::MEDIUM": 32, "Font::LARGE": 48, "Font::XLARGE": 72}
    return tuple(named[v] if v in named else int(v) for v in (s.strip() for s in body.split(",")))


BETWEEN = between_sizes()
ALL = tuple(sorted(SIZES + BETWEEN))


def flash_line(size):
    """(line_height, base_line) of Ubuntu_<size>px.c"""
    c = (FONTS / f"Ubuntu_{size}px.c").read_text("utf-8")
    return (int(re.search(r"\.line_height = (\d+)", c).group(1)), int(re.search(r"\.base_line = (\d+)", c).group(1)))


def bin_line(head):
    """(line_height, base_line) LVGL takes from a binary font (lv_binfont_loader.c)"""
    return head["ascent"] - head["descent"], -head["descent"]


def browser_line(size, face=ROOT / "fonts" / "ubuntu" / "Ubuntu-Regular.ttf"):
    """(line_height, base_line) of a browser's line-height: normal, read from the .ttf itself:
    ascent, descent and line gap of hhea at the size, each rounded half up as Chromium does; half the
    gap above the text, the rest below"""
    data = Path(face).read_bytes()
    tables = {data[12 + 16 * i:16 + 16 * i]: struct.unpack_from(">I", data, 20 + 16 * i)[0]
              for i in range(struct.unpack_from(">H", data, 4)[0])}
    upem, = struct.unpack_from(">H", data, tables[b"head"] + 18)
    ascent, descent, gap = struct.unpack_from(">hhh", data, tables[b"hhea"] + 4)
    a, d, g = (math.floor(v * size / upem + 0.5) for v in (ascent, -descent, gap))
    return a + d + g, d + g - g // 2


def icon_names():
    """{name: code} of Icons.txt, the names UI::Font::iconCode() looks up."""
    out = {}
    for line in (FILE_FONTS / "Icons.txt").read_text("utf-8").splitlines():
        name, code = line.split()
        out[name] = int(code, 16)
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


class IconFonts(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.fonts = {s: bin_font(FILE_FONTS / f"Icons_{s}px.bin") for s in ALL}
        cls.names = icon_names()
        cls.text = codepoints(FONTS / "Ubuntu_16px.c") | codepoints(FONTS / "UbuntuExt_16px.c")

    def test_the_firmware_can_read_them(self):
        # include/lv_conf.h: LV_USE_FONT_COMPRESSED 0. lv_binfont_loader.c of LVGL 9.2 keeps the
        # length of a FORMAT0_FULL subtable in 8 bits: a longer one would be read wrong.
        for s, (head, _, subtables) in self.fonts.items():
            self.assertEqual((head["font_size"], head["bits_per_pixel"], head["compression_id"]), (s, 2, 0))
            self.assertEqual([t for t in subtables if t[0] == 0 and t[2] > 255], [], s)

    def test_every_size_has_every_icon(self):
        self.assertGreaterEqual(len(self.names), 179)
        for s, (_, codes, _) in self.fonts.items():
            self.assertEqual(sorted(n for n, c in self.names.items() if c not in codes), [], s)
            self.assertEqual(codes, self.fonts[16][1], s)

    def test_icons_draw_nothing_the_text_fonts_have(self):
        self.assertEqual(sorted(hex(c) for c in self.fonts[16][1] & self.text), [])

    def test_the_signs_text_writes(self):
        _, codes, _ = self.fonts[16]
        signs = "✓←→↑↓⚙★♥☀☁❄☰✕⌂▶⏸"
        self.assertEqual("".join(ch for ch in signs if ord(ch) not in codes), "")

    def test_every_character_of_the_apps_has_a_glyph(self):
        have = self.text | self.fonts[16][1] | {0x09, 0x0A, 0x0D, 0x20}
        missing = {}
        for p in sorted(list((ROOT / "data").rglob("*.bax")) + list(FIXTURES.rglob("*.bax"))):
            for ch in p.read_text("utf-8"):
                if ord(ch) not in have and ch != "﻿":
                    missing.setdefault(ch, p.relative_to(ROOT).as_posix())
        self.assertEqual(missing, {})


class SizeFonts(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.fonts = {s: bin_font(FILE_FONTS / f"Ubuntu_{s}px.bin") for s in BETWEEN}
        cls.text = codepoints(FONTS / "Ubuntu_16px.c") | codepoints(FONTS / "UbuntuExt_16px.c")

    def test_the_firmware_draws_the_sizes_the_script_makes(self):
        self.assertEqual(firmware_sizes(), ALL)
        self.assertEqual(set(SIZES) & set(BETWEEN), set())

    def test_the_firmware_can_read_them(self):
        for s, (head, _, subtables) in self.fonts.items():
            self.assertEqual((head["font_size"], head["bits_per_pixel"], head["compression_id"]), (s, 2, 0))
            self.assertEqual([t for t in subtables if t[0] == 0 and t[2] > 255], [], s)

    def test_they_draw_what_the_four_draw(self):
        for s, (_, codes, _) in self.fonts.items():
            self.assertEqual(sorted(hex(c) for c in self.text ^ codes), [], s)

    def test_lines_grow_with_the_size(self):
        lines = [flash_line(s)[0] if s in SIZES else bin_line(self.fonts[s][0])[0] for s in ALL]
        self.assertEqual(lines, sorted(set(lines)), list(zip(ALL, lines)))

    def test_lines_are_a_browsers(self):
        # What Chromium lays out for <p style="font-size: Npx"> in Ubuntu (sim/tools/judge.py)
        self.assertEqual([browser_line(s)[0] for s in ALL], [13, 16, 18, 21, 24, 28, 32, 37, 42, 46, 55, 83])
        for s in ALL:
            mine = flash_line(s) if s in SIZES else bin_line(self.fonts[s][0])
            self.assertEqual(mine, browser_line(s), s)
        for s in SIZES:
            ext = (FONTS / f"UbuntuExt_{s}px.c").read_text("utf-8")
            self.assertEqual((int(re.search(r"\.line_height = (\d+)", ext).group(1)),
                              int(re.search(r"\.base_line = (\d+)", ext).group(1))), browser_line(s), s)
        for face in ("Bold", "Italic", "BoldItalic"):  # the faces have the regular's metrics
            self.assertEqual([browser_line(s, ROOT / "fonts" / "ubuntu" / f"Ubuntu-{face}.ttf") for s in ALL],
                             [browser_line(s) for s in ALL], face)


# The faces beside the regular one, files of every size (scripts/make_fonts.py bold, italic)
FACES = ("UbuntuBold", "UbuntuItalic", "UbuntuBoldItalic")


class FaceFonts(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.fonts = {(face, s): bin_font(FILE_FONTS / f"{face}_{s}px.bin") for face in FACES for s in ALL}

    def test_the_firmware_can_read_them(self):
        for (face, s), (head, _, subtables) in self.fonts.items():
            self.assertEqual((head["font_size"], head["bits_per_pixel"], head["compression_id"]), (s, 2, 0), face)
            self.assertEqual([t for t in subtables if t[0] == 0 and t[2] > 255], [], (face, s))

    def test_they_draw_what_the_regular_faces_draw(self):
        for (face, s), (_, codes, _) in self.fonts.items():
            if s in SIZES:
                regular = codepoints(FONTS / f"Ubuntu_{s}px.c") | codepoints(FONTS / f"UbuntuExt_{s}px.c")
            else:
                regular = bin_font(FILE_FONTS / f"Ubuntu_{s}px.bin")[1]
            self.assertEqual(sorted(hex(c) for c in regular ^ codes), [], (face, s))

    def test_text_changing_face_keeps_its_lines(self):
        for (face, s), (head, _, _) in self.fonts.items():
            regular = flash_line(s) if s in SIZES else bin_line(bin_font(FILE_FONTS / f"Ubuntu_{s}px.bin")[0])
            self.assertEqual(bin_line(head), regular, (face, s))


if __name__ == "__main__":
    unittest.main()
