#!/usr/bin/env python3
"""make_fonts.py — the glyphs of other languages for the four firmware fonts.

    python3 scripts/make_fonts.py          # writes src/font/UbuntuExt_{16,32,48,72}px.c

Ubuntu_16px … Ubuntu_72px (src/font/) have 213 glyphs: ASCII, Russian and a few signs. The
glyphs of the other European languages come from the same face, Ubuntu Regular 0.83
(fonts/ubuntu/), as a second font of each size: UbuntuExt_*px. UI::Font makes it the fallback
of the first, so a glyph the first does not have is drawn from the second, and the 213 glyphs
stay as they are, pixel for pixel.

Not part of the build: the generated files are committed. Needs fontTools (pip) and
lv_font_conv 1.5 (npm); LV_FONT_CONV names the lv_font_conv to run (default: lv_font_conv).
"""

import os
import subprocess
import sys
from pathlib import Path

from fontTools.ttLib import TTFont

ROOT = Path(__file__).resolve().parent.parent
FACE = "fonts/ubuntu/Ubuntu-Regular.ttf"
SIZES = (16, 32, 48, 72)

# What Ubuntu_*px already have (their cmaps): not drawn twice
MAIN = (set(range(0x20, 0x7F))
        | {0xA3, 0xA9, 0xAB, 0xAE, 0xB0, 0xB1, 0xB2, 0xB3, 0xB4, 0xBB, 0xD7}
        | set(range(0x401, 0x460))
        | {0x2013, 0x2014, 0x2018, 0x2019, 0x201A, 0x201C, 0x201D, 0x201E, 0x2022, 0x2039, 0x203A, 0x20AC})

# What the second font is for; of each range, what the face has
WANT = (set(range(0xA0, 0x180))          # Latin-1 Supplement, Latin Extended-A
        | {0x18F, 0x259}                 # Ə ə: Azerbaijani
        | set(range(0x218, 0x21C))       # Ș ș Ț ț: Romanian
        | set(range(0x370, 0x400))       # Greek
        | set(range(0x400, 0x500))       # Cyrillic: Ѐ, Ґ, Kazakh, Tatar…
        | set(range(0x2000, 0x2070))     # punctuation: … ‰ ′ ″
        | set(range(0x20A0, 0x20D0))     # currency
        | set(range(0x2100, 0x2150))     # № ™
        | set(range(0x2200, 0x2300)))    # − ≈ ≠ ≤ ≥ ∞ √

# Letters the face does not have under their own code, drawn with the glyph of the same letter
BORROW = {0x259: 0x4D9}  # ə (Azerbaijani) — Cyrillic ә


def ranges(codes):
    """0xA0-0xA2,0xA4 for lv_font_conv --range."""
    out, start, prev = [], None, None
    for c in sorted(codes):
        if start is None:
            start = prev = c
        elif c == prev + 1:
            prev = c
        else:
            out.append(f"0x{start:X}" if start == prev else f"0x{start:X}-0x{prev:X}")
            start = prev = c
    if start is not None:
        out.append(f"0x{start:X}" if start == prev else f"0x{start:X}-0x{prev:X}")
    return ",".join(out)


def main():
    cmap = TTFont(ROOT / FACE).getBestCmap()
    new = {c for c in WANT if c in cmap} - MAIN
    borrowed = {code: glyph for code, glyph in BORROW.items() if code not in cmap and glyph in cmap}
    tool = os.environ.get("LV_FONT_CONV", "lv_font_conv")
    for size in SIZES:
        name = f"UbuntuExt_{size}px"
        cmd = [tool, "--font", FACE, "-r", ranges(new)]
        for code, glyph in sorted(borrowed.items()):
            cmd += ["-r", f"0x{glyph:X}=>0x{code:X}"]
        cmd += ["--size", str(size), "--bpp", "2", "--format", "lvgl", "--no-compress",
                "--lv-include", "lvgl.h", "--lv-font-name", name, "-o", f"src/font/{name}.c"]
        subprocess.run(cmd, cwd=ROOT, check=True)
    print(f"{len(new) + len(borrowed)} glyphs in {len(SIZES)} fonts: src/font/UbuntuExt_*px.c")
    return 0


if __name__ == "__main__":
    sys.exit(main())
