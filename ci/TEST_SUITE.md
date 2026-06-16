# TelaOS Test Suite v3.0

## Changelog

- **v3.0**: 44 теста (было 36), добавлены csv, yaml, lifecycle, timer_api, lua_errors, flatten, zindex
- **v2.0**: 36 тестов, VFS, консольные тесты
- **v1.0**: Начальная версия

---

**44 теста · ~930 проверок · 32/47 модулей покрыто (68%)**

```
$ python3 test.py /path/to/TelaOS

✓ ALL 44 TESTS PASSED
```

---

## Тесты по категориям

### Console (4 теста · 168 проверок)

| Тест | Проверок | Что тестирует |
|------|----------|---------------|
| `console/console` | 16 | `sys ping`, `ui render`, `app push` — парсинг аргументов, unknown commands, пустой ввод |
| `console/console_args` | 134 | `isIntStr`, `isFloat`, `isColor`, `parseArgs` — кавычки, экранирование, edge cases |
| `console/console_fs` | 9 | `app pull` (single + `*`), `app delete`, ошибки — **использует VFS** |
| `console/serial_console` | 9 | Serial transport: echo, prompt, побайтовый парсинг команд |

### Core (4 теста · 81 проверка)

| Тест | Проверок | Что тестирует |
|------|----------|---------------|
| `core/app_manager` | 22 | `scanApps` из VFS, `extractMeta`, `launch`, `systemConfig`, `queueLaunch`, `refreshApps` — **использует VFS** |
| `core/call_queue` | 13 | push/process, дедупликация, overflow (max 16), size tracking |
| `core/state_store` | 33 | define/get/set все типы, `has`/`count`, `getAsString`, `setFromString`, `reset`/`clear`, `onChange` callback |
| `core/yaml_config` | 13 | define + defaults, load/save VFS, roundtrip, auto-save on `set()`, комментарии — **использует VFS** |

### CSV (1 тест · 24 проверки)

| Тест | Проверок | Что тестирует |
|------|----------|---------------|
| `csv/csv_cpp` | 24 | CSV парсер/сериализатор: кавычки, экранирование, многострочные поля, пустые ячейки, roundtrip |

### E2E (6 тестов · ~149 проверок)

| Тест | Проверок | Что тестирует |
|------|----------|---------------|
| `e2e/bf` | ~5 | Brainfuck-движок: Hello World, арифметика, ввод/вывод |
| `e2e/calc_console` | 68 | Калькулятор через консольные команды: ввод цифр, операции, цепочки, clear, дисплей |
| `e2e/crossword` | 35 | Кроссворд через `ui call`/`ui click`: выбор ячейки, ввод букв, навигация, детекция победы |
| `e2e/lifecycle` | 9 | Жизненный цикл приложения: init → render → navigate → shutdown |
| `e2e/timer_api` | 20 | Декларативные таймеры: `<timer>`, интервалы, множественные таймеры, toggle, циклические данные |
| `e2e/ui_calculator` | ~12 | Калькулятор через прямые LVGL-клики |

### Lua (7 тестов · 123 проверки)

| Тест | Проверок | Что тестирует |
|------|----------|---------------|
| `lua/lua_csv` | 22 | Lua CSV API: `csv.parse`, `csv.stringify`, типы данных, edge cases |
| `lua/lua_errors` | 34 | Обработка ошибок: синтаксис, runtime, nil access, stack overflow, pcall |
| `lua/lua_fetch` | 8 | `fetch()` / `ble_connected()`, BLE-down error path, callback status=0 |
| `lua/lua_preprocess` | 9 | Forward declarations: `local function` ниже вызова, взаимные вызовы, рекурсия |
| `lua/lua_system` | 20 | `app_launch`/`app_home`, canvas API, `freezeUI`/`unfreezeUI` |
| `lua/lua_ui` | 9 | `focus()`/`getAttr()`/`navigate()`, `setAttr` visible/nonexistent |
| `lua/timers` | 21 | Таймеры: create/fire/cancel, `setInterval`/`clearInterval`, множественные |

### Parser (2 теста · ~25 проверок)

| Тест | Проверок | Что тестирует |
|------|----------|---------------|
| `parser/html_parser` | ~15 | Парсинг тегов, атрибуты, вложенность, self-closing |
| `parser/html_to_kdl` | ~10 | HTML → KDL конвертация: структура, атрибуты |

### UI (11 тестов · ~173 проверки)

| Тест | Проверок | Что тестирует |
|------|----------|---------------|
| `ui/align` | 13 | `align="center center"`, `valign`, `text-align`, `text-valign`, приоритет координат |
| `ui/canvas` | 20 | Canvas: `clear`/`rect`/`pixel`/`circle`/`line`, bounds clamping, error handling |
| `ui/css` | 49 | CSS: tag/class/id/tag.class селекторы, каскад, #RGB shorthand, zero values, composite rules |
| `ui/flatten` | 33 | Flatten: группы → плоский список страниц, вложенность, порядок |
| `ui/groups` | 9 | Группы страниц: ориентация, индикаторы, default страница, свайп |
| `ui/navigation` | 10 | `href`, `navigate()`, standalone страницы, переход между группами |
| `ui/qi_align` | 10 | Quick-integration: HTML → LVGL → KDL → UIQuery, проверка координат |
| `ui/ui_ids` | ~8 | Auto-generated и explicit ID, уникальность, `ui_get()` |
| `ui/ui_render` | ~3 | Базовый render: HTML → LVGL объекты |
| `ui/ui_styles` | ~10 | `<style>` блок, классы, наследование, переопределение |
| `ui/zindex` | 8 | Z-index: порядок рендеринга, `z-index` атрибут, перекрытие |

### Utils (5 тестов · 117 проверок)

| Тест | Проверок | Что тестирует |
|------|----------|---------------|
| `utils/log_config` | 18 | Уровни логов, set/get по категориям, `setAll`, `command()` парсинг |
| `utils/name_gen` | 23 | ASCII sanitize, Cyrillic→Latin, diacritics, edge cases, `generate()` |
| `utils/string_utils` | 39 | `trim`, `toLower`, `contains`, `toBool`, `equals`, `decodeEntities` (HTML entities) |
| `utils/task_queue` | 14 | Keyed дедупликация, ordering, clear, UpdateLabel/UpdateBinding tasks |
| `utils/vfs` | 23 | VFS: read/write, `available()`, dirs, `openNextFile`, remove, `usedBytes` |

### Widgets (3 теста · 30 проверок)

| Тест | Проверок | Что тестирует |
|------|----------|---------------|
| `widgets/binding` | 14 | `{var}` текстовый биндинг, шаблоны, two-way bind на slider/switch/input |
| `widgets/input` | 10 | Input: placeholder, `bind`, `onenter`, `onblur`, password mode |
| `widgets/visibility` | 6 | `visible="{var}"`, динамическое show/hide |

### YAML (1 тест · 37 проверок)

| Тест | Проверок | Что тестирует |
|------|----------|---------------|
| `yaml/lua_yaml` | 37 | Lua YAML API: parse, serialize, типы, вложенные структуры, roundtrip |

---

## Покрытие модулей

### Покрыто (32/47 = 68%)

```
✓ core/app_manager         ✓ core/call_queue          ✓ core/state_store
✓ core/script_engine       ✓ core/script_manager       ✓ core/yaml_config
✓ core/base_script_engine
✓ console/console
✓ engines/bf/bf_engine     ✓ engines/lua/lua_engine    ✓ engines/lua/lua_csv
✓ engines/lua/lua_fetch    ✓ engines/lua/lua_system    ✓ engines/lua/lua_timer
✓ engines/lua/lua_ui       ✓ engines/lua/lua_yaml
✓ ui/css_parser            ✓ ui/html_parser            ✓ ui/ui_canvas
✓ ui/ui_engine             ✓ ui/ui_html                ✓ ui/ui_page_group
✓ ui/ui_widget_builder     ✓ ui/ui_task                ✓ ui/ui_types
✓ ui/xml_utils
✓ utils/font               ✓ utils/log_config          ✓ utils/name_gen
✓ utils/string_utils       ✓ utils/task_queue
✓ widgets/widget_common
```

### Не покрыто — hardware-dependent (15/47)

```
✗ ble/ble_bridge           BLE GATT сервер
✗ ble/bin_receive          Приём файлов по BLE
✗ ble/bin_transfer         Отправка файлов по BLE
✗ hal/device               Абстракция оборудования
✗ hal/display_hal          Инициализация дисплея
✗ hal/boards/*             Драйверы плат (3 файла)
✗ ui/ui_launcher           Launcher, иконки, сетка
✗ ui/ui_shade              Шторка уведомлений
✗ ui/ui_touch              Обработка тач-ввода
✗ utils/esp_log            ESP-IDF log wrapper
✗ utils/screenshot         Захват фреймбуфера
✗ core/native_app          Реестр нативных приложений
✗ main.cpp                 Точка входа, FreeRTOS
```

---

## Инфраструктура

- **Без фреймворка** — plain C++ с макросами `TEST()`/`PASS()`/`FAIL()`, exit code
- **Реальный код** — компилируется из `TelaOS/src/` с mock HAL, не переписан
- **Реальный Lua VM** — `liblua54.a`, не стаб
- **VFS** — in-memory LittleFS для тестирования app_manager, yaml_config, console FS
- **LVGL mock** — объекты с parent-child деревом, стили, флаги, события — без рендеринга
- **Dep tracking** — `-MMD` флаги для инкрементальной пересборки
