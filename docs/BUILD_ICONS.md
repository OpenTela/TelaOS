# Build Configuration Guide

## Icon System

### Overview

Icons can be loaded from two sources:
1. **Embedded** — compiled into firmware as C arrays (fast, no FS access)
2. **Filesystem** — loaded from LittleFS at runtime (flexible, can update without reflash)

### Icon Directories

| Directory | Purpose |
|-----------|---------|
| `resources/icons/apps/` | App icons (by app name, for `--all` mode) |
| `data/system/resources/icons/` | System icons + category fallbacks (game.png, tool.png, etc.) |

### Build Flags

| Flag | Description |
|------|-------------|
| `USE_BUILTIN_ICONS` | Enable embedded icons support |
| `EMBED_ALL_ICONS` | Embed app + category + system icons (for small devices) |
| `NO_PNG_ICONS` | Disable PNG loading from filesystem |
| `PRELOAD_ICONS_TO_PSRAM` | Preload PNG icons to PSRAM at boot (uses RAM, faster runtime) |

### Icon Loading Priority

When displaying an app icon, the system tries these sources in order:

1. **Builtin app icon** — `findBuiltinIcon(appName)`
2. **PSRAM preloaded** — if `PRELOAD_ICONS_TO_PSRAM` enabled
3. **Builtin system icon** — if `app.iconPath` starts with `C:/system/resources/icons/`
4. **Filesystem icon** — `app.iconPath` from LittleFS
5. **Builtin category icon** — `findCategoryIcon(category)`
6. **Filesystem category icon** — `/system/resources/icons/{category}.png`
7. **Letter fallback** — first letter of app title

### Scripts

#### `scripts/embed_icons.py`

Converts PNG icons to C arrays for embedding.

**Standalone usage:**
```bash
# Embed only system icons (default)
python scripts/embed_icons.py --system
python scripts/embed_icons.py

# Embed all icons (app + category + system)
python scripts/embed_icons.py --all
```

**PlatformIO integration:**
```ini
[env]
extra_scripts = 
    pre:scripts/embed_icons.py
```

Mode is controlled by `EMBED_ALL_ICONS` build flag.

#### `scripts/resize_icons.py`

Resizes icons to target size before embedding.

**PlatformIO integration:**
```ini
[env:myboard]
custom_icon_size = 48  # Target icon size in pixels
```

### Output

Both scripts output to `include/_generated_icons.h` which provides:

```cpp
// Lookup functions
const lv_image_dsc_t* findBuiltinIcon(const char* name);
const lv_image_dsc_t* findCategoryIcon(const char* category);
const lv_image_dsc_t* findSystemIcon(const char* name);
```

---

## Device Configurations

### Small Devices (TWatch, 240x240)

Limited RAM, embed everything:

```ini
[env:twatch2020]
custom_icon_size = 48

build_flags =
    -DUSE_BUILTIN_ICONS
    -DEMBED_ALL_ICONS      # All icons in firmware
    -DNO_PNG_ICONS         # Don't load from FS
    -DLV_MEM_SIZE=64000    # Smaller LVGL heap
```

### Large Devices (ESP4848, 480x480)

More RAM, hybrid approach:

```ini
[env:esp4848s040]
custom_icon_size = 48

build_flags =
    -DUSE_BUILTIN_ICONS
    # No EMBED_ALL_ICONS — only system icons embedded
    # No NO_PNG_ICONS — can load app icons from FS
    -DLV_MEM_SIZE=128000
```

---

## App Icon Declaration

Apps can specify their icon in `app.html`:

```html
<!-- System icon (embedded, fast) -->
<app title="Puzzle" icon="system:puzzle-game">

<!-- Custom icon (from app folder) -->
<app title="My App" icon="/apps/myapp/icon.png">

<!-- No icon specified — uses category fallback or letter -->
<app title="Game" category="game">
```

The `system:` prefix resolves to `C:/system/resources/icons/{name}.png` and uses embedded icon if available.

---

## Memory Considerations

### Icon Size Impact

| Icon Size | Pixels | RGB565 Bytes | 10 Icons |
|-----------|--------|--------------|----------|
| 32x32 | 1,024 | 2 KB | 20 KB |
| 48x48 | 2,304 | 4.5 KB | 45 KB |
| 64x64 | 4,096 | 8 KB | 80 KB |

### Recommendations

| Device RAM | Recommended Approach |
|------------|---------------------|
| < 320 KB | `EMBED_ALL_ICONS` + `NO_PNG_ICONS`, 32px icons |
| 320-512 KB | `EMBED_ALL_ICONS` + `NO_PNG_ICONS`, 48px icons |
| > 512 KB | System icons only embedded, app icons from FS |
| With PSRAM | `PRELOAD_ICONS_TO_PSRAM` for best of both worlds |

---

## Troubleshooting

### "unknown driver letter" error
Icon path missing `C:` prefix. System icons should resolve to `C:/system/resources/icons/...`

### Icons not showing
1. Check `USE_BUILTIN_ICONS` is defined
2. Run `python scripts/embed_icons.py` before build
3. Verify PNG files exist in source directories

### Build too large
- Reduce `custom_icon_size`
- Use `--system` mode instead of `--all`
- Remove unused icons from source directories
