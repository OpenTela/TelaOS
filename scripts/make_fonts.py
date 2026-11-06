#!/usr/bin/env python3
"""make_fonts.py — the fonts beside the four firmware fonts.

    python3 scripts/make_fonts.py [ext] [icons] [bold]     # all three by default

ext — src/font/UbuntuExt_{16,32,48,72}px.c, the glyphs of other languages. Ubuntu_16px …
Ubuntu_72px (src/font/) have 213 glyphs: ASCII, Russian and a few signs. The glyphs of the other
European languages come from the same face, Ubuntu Regular 0.83 (fonts/ubuntu/), as a second font
of each size: UbuntuExt_*px. UI::Font makes it the fallback of the first, so a glyph the first
does not have is drawn from the second, and the 213 glyphs stay as they are, pixel for pixel.

icons — data/system/fonts/Icons_{16,32,48,72}px.bin and Icons.txt: Material Symbols Outlined
(Google, Apache 2.0: fonts/material-symbols/) — the icons of docs/ICONS.md, under the codes of the
font, and Unicode signs text writes as they are (✓ ← ★ ♥) drawn with the icon of the same
meaning, filled where Unicode draws the sign solid. They are files on LittleFS, not code: UI::Font
loads them at start and puts them between the two fonts of the same size. Icons.txt (name, code)
is what <span class="material-symbols-outlined">home</span> looks up.

bold — data/system/fonts/UbuntuBold_{16,32,48,72}px.bin: Ubuntu Bold 0.83 (fonts/ubuntu/), the
same 650 glyphs Ubuntu_*px and UbuntuExt_*px draw together, in one font of each size. Files on
LittleFS too: UI::Font loads the one of a size when text of that size first asks for bold (<b>,
<strong>, headings, font-weight), and puts the icons of the size behind it.

Not part of the build: the generated files are committed. Needs fontTools (pip) and lv_font_conv
1.5 (npm); LV_FONT_CONV names the lv_font_conv to run (default: lv_font_conv). icons also needs
the font it is made from, 10 MB and not in the repository: MATERIAL_SYMBOLS names its .ttf, with
its .codepoints beside it, both from MATERIAL_URL.
"""

import hashlib
import os
import re
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

from fontTools import subset
from fontTools.ttLib import TTFont
from fontTools.varLib import instancer

ROOT = Path(__file__).resolve().parent.parent
FACE = "fonts/ubuntu/Ubuntu-Regular.ttf"
BOLD = "fonts/ubuntu/Ubuntu-Bold.ttf"
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

# The icon font: google/material-design-icons at a fixed commit, variablefont/. The codes of the
# same icon differ between versions (home: e88a, now e9b2), the names do not: names come from
# docs/ICONS.md, codes from the .codepoints of this very font.
MATERIAL_URL = ("https://raw.githubusercontent.com/google/material-design-icons/"
                "10ecc14790612ca8fd164e3f4ffe9c16fc50d5b7/variablefont/"
                "MaterialSymbolsOutlined%5BFILL%2CGRAD%2Copsz%2Cwght%5D.ttf")
MATERIAL_SHA256 = "f7837ba8573d9a7a74931618ced8307bc81f96d49af296bf6c8052c772ed5282"

# By group as in docs/ICONS.md: navigation, actions, forms, watch status, time, media, people,
# health, weather, places, files, money and games
ICONS = """
home arrow_back arrow_forward arrow_upward arrow_downward chevron_left chevron_right
expand_less expand_more close menu more_vert more_horiz apps search settings tune refresh sync
undo redo open_in_new
check check_circle add remove edit delete save share download upload content_copy content_paste
content_cut info help warning error block lock lock_open visibility visibility_off star
bookmark flag
check_box check_box_outline_blank radio_button_checked radio_button_unchecked toggle_on
toggle_off keyboard backspace keyboard_return mic mic_off
battery_full battery_5_bar battery_3_bar battery_1_bar battery_0_bar battery_charging_full
battery_alert bolt bluetooth bluetooth_connected bluetooth_disabled wifi wifi_off
signal_cellular_alt notifications notifications_active notifications_off do_not_disturb_on
power_settings_new restart_alt light_mode dark_mode brightness_medium flashlight_on
mobile_vibrate usb sd_card hard_drive update watch
schedule alarm alarm_on alarm_off alarm_add timer timer_off hourglass_empty hourglass_top
calendar_today calendar_month event history
play_arrow pause stop skip_next skip_previous fast_forward fast_rewind eject volume_up
volume_down volume_off shuffle repeat music_note headphones movie photo_camera image
call call_end mail chat sms send person group account_circle contacts language translate
favorite monitor_heart directions_walk directions_run directions_bike footprint
local_fire_department fitness_center bedtime water_drop
sunny partly_cloudy_day cloud rainy thunderstorm weather_snowy foggy air clear_night thermostat
umbrella
location_on my_location near_me navigation explore map directions_car flight train restaurant
local_cafe
folder description list grid_view edit_note attach_file qr_code cloud_download cloud_upload
trending_up trending_down currency_bitcoin payments casino sports_esports extension
""".split()

# Signs of Unicode that text writes as they are, and the icon that draws each. A sign the text
# face has (−) stays the face's.
SIGNS = {
    "⌂": "home", "←": "arrow_back", "→": "arrow_forward", "↑": "arrow_upward",
    "↓": "arrow_downward", "✕": "close", "☰": "menu", "⋮": "more_vert", "⋯": "more_horiz",
    "⚙": "settings", "↻": "refresh", "↶": "undo", "↷": "redo", "✓": "check", "−": "remove",
    "✎": "edit", "ℹ": "info", "⚠": "warning", "★": "star", "☆": "star", "⚑": "flag",
    "☑": "check_box", "☐": "check_box_outline_blank", "◉": "radio_button_checked",
    "○": "radio_button_unchecked", "⌨": "keyboard", "⌫": "backspace", "↵": "keyboard_return",
    "⚡": "bolt", "⊖": "do_not_disturb_on", "⏻": "power_settings_new", "☼": "light_mode",
    "☾": "dark_mode", "⌚": "watch", "⏰": "alarm", "⏱": "timer", "⌛": "hourglass_empty",
    "⏳": "hourglass_top", "▶": "play_arrow", "⏸": "pause", "⏹": "stop", "⏭": "skip_next",
    "⏮": "skip_previous", "⏩": "fast_forward", "⏪": "fast_rewind", "⏏": "eject",
    "♪": "music_note", "☎": "call", "✉": "mail", "♥": "favorite", "♡": "favorite",
    "☀": "sunny", "⛅": "partly_cloudy_day", "☁": "cloud", "❄": "ac_unit", "☂": "umbrella",
    "✈": "flight", "₿": "currency_bitcoin",
}
# Of them, the ones Unicode draws solid (BLACK STAR, BLACK HEART SUIT…): from the filled icon,
# FILL 1 of the font. The rest, and the icons by name, are outlined: FILL 0, as Google's default.
FILLED = set("⚙★⚑⚡⌛▶⏸⏹⏭⏮⏩⏪⏏♪☎♥☀⛅☁☂✈")


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


def ext(tool):
    cmap = TTFont(ROOT / FACE).getBestCmap()
    new = {c for c in WANT if c in cmap} - MAIN
    borrowed = {code: glyph for code, glyph in BORROW.items() if code not in cmap and glyph in cmap}
    for size in SIZES:
        name = f"UbuntuExt_{size}px"
        cmd = [tool, "--font", FACE, "-r", ranges(new)]
        for code, glyph in sorted(borrowed.items()):
            cmd += ["-r", f"0x{glyph:X}=>0x{code:X}"]
        cmd += ["--size", str(size), "--bpp", "2", "--format", "lvgl", "--no-compress",
                "--lv-include", "lvgl.h", "--lv-font-name", name, "-o", f"src/font/{name}.c"]
        subprocess.run(cmd, cwd=ROOT, check=True)
    print(f"{len(new) + len(borrowed)} glyphs in {len(SIZES)} fonts: src/font/UbuntuExt_*px.c")


def icons(tool):
    font = os.environ.get("MATERIAL_SYMBOLS")
    if not font:
        sys.exit(f"icons: MATERIAL_SYMBOLS=<the .ttf>; the font and its .codepoints: {MATERIAL_URL}")
    font = Path(font).resolve()
    if hashlib.sha256(font.read_bytes()).hexdigest() != MATERIAL_SHA256:
        sys.exit(f"icons: {font.name} is not the font of MATERIAL_URL: its codes may differ")
    codes = {}
    for line in font.with_suffix(".codepoints").read_text().splitlines():
        name, code = line.split()
        codes[name] = int(code, 16)
    unknown = [n for n in ICONS + list(SIGNS.values()) if n not in codes]
    if unknown:
        sys.exit(f"icons: not in the font: {' '.join(unknown)}")
    text = TTFont(ROOT / FACE).getBestCmap()
    signs = {sign: name for sign, name in SIGNS.items() if ord(sign) not in text}
    glyphs = {codes[n] for n in ICONS}
    out = ROOT / "data/system/fonts"
    out.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory() as tmp:
        # The variable font pinned to one look each, outlined and filled, with only what is drawn
        cut = [static(font, glyphs | {codes[n] for n in signs.values()}, fill, Path(tmp)) for fill in (0, 1)]
        for size in SIZES:
            cmd = [tool, "--font", str(cut[0]), "-r", ranges(glyphs)]
            for fill in (0, 1):
                if fill:
                    cmd += ["--font", str(cut[1])]
                for sign, name in sorted(signs.items()):
                    if (sign in FILLED) == bool(fill):
                        cmd += ["-r", f"0x{codes[name]:X}=>0x{ord(sign):X}"]
            cmd += ["--size", str(size), "--bpp", "2", "--format", "bin", "--no-compress", "--no-kerning",
                    "-o", str(out / f"Icons_{size}px.bin")]
            subprocess.run(cmd, cwd=ROOT, check=True)
    (out / "Icons.txt").write_text("".join(f"{n} {codes[n]:x}\n" for n in sorted(ICONS)))
    print(f"{len(ICONS)} icons and {len(signs)} signs in {len(SIZES)} fonts: data/system/fonts/")


def static(font, codes, fill, tmp):
    """The font cut to `codes`, its axes pinned: FILL `fill`, the rest at their defaults."""
    f = TTFont(font)
    options = subset.Options()
    options.layout_features = []  # the ligatures that make names icons in a browser: not used here
    subsetter = subset.Subsetter(options)
    subsetter.populate(unicodes=codes)
    subsetter.subset(f)
    axes = {a.axisTag: a.defaultValue for a in f["fvar"].axes}
    axes["FILL"] = fill
    path = tmp / f"fill{fill}.ttf"
    instancer.instantiateVariableFont(f, axes).save(path)
    return path


def bold(tool):
    regular = TTFont(ROOT / FACE).getBestCmap()
    cmap = TTFont(ROOT / BOLD).getBestCmap()
    text = {c for c in MAIN | WANT if c in regular}  # what Ubuntu_*px and UbuntuExt_*px draw
    codes = {c for c in text if c in cmap}
    borrowed = {code: glyph for code, glyph in BORROW.items() if code not in cmap and glyph in cmap}
    missing = text - codes - set(borrowed)
    if missing:
        sys.exit("bold: the face has no " + " ".join(f"U+{c:04X}" for c in sorted(missing)))
    out = ROOT / "data/system/fonts"
    out.mkdir(parents=True, exist_ok=True)
    for size in SIZES:
        cmd = [tool, "--font", BOLD, "-r", ranges(codes)]
        for code, glyph in sorted(borrowed.items()):
            cmd += ["-r", f"0x{glyph:X}=>0x{code:X}"]
        cmd += ["--size", str(size), "--bpp", "2", "--format", "bin", "--no-compress",
                "-o", str(out / f"UbuntuBold_{size}px.bin")]
        subprocess.run(cmd, cwd=ROOT, check=True)
        same_line(out / f"UbuntuBold_{size}px.bin", size)
    print(f"{len(codes) + len(borrowed)} glyphs in {len(SIZES)} fonts: data/system/fonts/UbuntuBold_*px.bin")


def same_line(path, size):
    """The line of the regular face of the size (line_height and base_line of Ubuntu_*px), written
    into the head of a binary font: lv_font_conv takes the line from the glyphs it draws, and text
    turning bold must not move its lines."""
    c = (ROOT / f"src/font/Ubuntu_{size}px.c").read_text()
    line = int(re.search(r"\.line_height = (\d+)", c).group(1))
    base = int(re.search(r"\.base_line = (\d+)", c).group(1))
    data = bytearray(path.read_bytes())
    assert data[4:8] == b"head", path
    struct.pack_into("<Hh", data, 16, line - base, -base)  # ascent, descent (lv_binfont_loader.c)
    path.write_bytes(bytes(data))


def main(argv):
    parts = argv[1:] or ["ext", "icons", "bold"]
    if any(p not in ("ext", "icons", "bold") for p in parts):
        print(__doc__.strip().splitlines()[2].strip())
        return 2
    tool = os.environ.get("LV_FONT_CONV", "lv_font_conv")
    if "ext" in parts:
        ext(tool)
    if "icons" in parts:
        icons(tool)
    if "bold" in parts:
        bold(tool)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
