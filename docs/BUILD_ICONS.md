# Build Configuration Guide v3.0

## Changelog

- **v3.0**: Единый скрипт `create_embedded_icons.py`, авто-определение `USE_BUILTIN_ICONS`, staleness detection
- **v2.0**: Раздельные скрипты для app/system иконок
- **v1.0**: Ручная генерация

---

## Icon System

### Overview

Иконки встраиваются в прошивку автоматически при сборке. Скрипт `create_embedded_icons.py` сканирует все `icon.png` в `data/apps/*/` и системные иконки в `data/system/resources/icons/`, конвертирует в RGB565 и генерирует C-хедер.

**Staleness detection:** каждая встроенная иконка хранит размер исходного PNG. Если файл на LittleFS отличается по размеру (пользователь залил новую иконку без пересборки), используется FS-версия.

### Источники иконок

| Директория | Назначение |
|-----------|---------|
| `data/apps/{name}/icon.png` | Иконки приложений |
| `data/system/resources/icons/` | Системные/категорийные иконки (game.png, puzzle-game.png) |

### Build Flags

| Flag | Описание |
|------|----------|
| `USE_BUILTIN_ICONS` | **Авто-определяется** скриптом когда иконки найдены. Не нужно задавать вручную |
| `NO_BUILTIN_ICONS` | Принудительно отключить встроенные иконки |
| `NO_PNG_ICONS` | Отключить загрузку PNG с файловой системы (только embedded) |
| `PRELOAD_ICONS_TO_PSRAM` | Предзагрузка PNG в PSRAM при старте |

### Icon Loading Priority

1. **Builtin app icon** — `findBuiltinEntry(appName)` → staleness check → если не stale, используется
2. **Builtin system icon** — для путей `C:/system/resources/icons/` → staleness check
3. **PSRAM preloaded** — если `PRELOAD_ICONS_TO_PSRAM` включён
4. **Filesystem icon** — `app.iconPath` из LittleFS
5. **Letter fallback** — первая буква названия

### Scripts

#### `scripts/resize_icons.py` (pre-build)

Нормализует все `icon.png` до целевого размера (64×64 по умолчанию).

#### `scripts/create_embedded_icons.py` (pre-build)

Конвертирует PNG → RGB565 C-массивы. Генерирует `include/_generated_icons.h`.

**PlatformIO integration:**
```ini
[env]
extra_scripts = 
    pre:scripts/resize_icons.py
    pre:scripts/create_embedded_icons.py
```

**Когда перегенерирует:**
- Хедер отсутствует
- Версия скрипта изменилась
- Размер иконок (`custom_icon_size`) изменился
- Любой `icon.png` новее хедера

### Output

`include/_generated_icons.h` предоставляет:

```cpp
struct BuiltinIcon {
    const char* name;
    const lv_image_dsc_t* icon;
    uint32_t png_size;  // для staleness detection
};

const BuiltinIcon* findBuiltinEntry(const char* name);   // полная запись
const BuiltinIcon* findSystemEntry(const char* name);     // системная иконка
const lv_image_dsc_t* findBuiltinIcon(const char* name);  // только дескриптор
const lv_image_dsc_t* findSystemIcon(const char* name);
const lv_image_dsc_t* findCategoryIcon(const char* category);
```

---

## Device Configurations

### Small Devices (TWatch, 240×240)

Ограниченная RAM, всё embedded, без PNG:

```ini
[env:twatch2020]
custom_icon_size = 48

build_flags =
    -DNO_PNG_ICONS         # Только embedded иконки
```

### Large Devices (ESP4848, 480×480)

Гибридный подход: embedded + FS fallback:

```ini
[env:esp4848s040]
# custom_icon_size не задан → 64 (default)

build_flags =
    # USE_BUILTIN_ICONS определяется автоматически
    # FS иконки доступны как fallback
```

---

## Memory Considerations

### Icon Size Impact

| Icon Size | Pixels | RGB565 Bytes | 10 Icons |
|-----------|--------|--------------|----------|
| 32×32 | 1,024 | 2 KB | 20 KB |
| 48×48 | 2,304 | 4.5 KB | 45 KB |
| 64×64 | 4,096 | 8 KB | 80 KB |

### Recommendations

| Device | Подход |
|--------|--------|
| < 320 KB RAM | `NO_PNG_ICONS`, 48px |
| > 512 KB RAM | Default (hybrid), 64px |
| С PSRAM | `PRELOAD_ICONS_TO_PSRAM` |

---

## Troubleshooting

### Иконки показывают `[fs]` вместо `[embed]`
1. Проверь что `extra_scripts` настроены в `platformio.ini`
2. При сборке должно быть `=== Embedded Icons 64x64 ===` в выводе
3. Если `[stale->fs]` — размеры PNG на FS не совпадают с embedded. Перезалей FS: `pio run -t uploadfs`

### Иконки не показываются
1. Проверь что `icon.png` существует в папке приложения
2. Для системных: файл в `data/system/resources/icons/`
3. `NO_BUILTIN_ICONS` не должен быть задан

### Сборка слишком большая
- Уменьши `custom_icon_size` (48 или 32)
- Удали неиспользуемые иконки
