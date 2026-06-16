# TelaOS Test Suite — Compiler & Test Framework v3.1

## Changelog

- **v3.1**: 53 теста (~1050 проверок). Тесты шаблонов (`<template>`, `@for`, вложенные, slash в атрибутах), canvas, coords, type coercion. Stubs: lvgl pct range fix.
- **v3.0**: Переименование EOS → TelaOS, убран esp.zip из `--pack`, актуализация документации
- **v2.3**: Исходники ckdl вложены в репо, Lua-стабы убраны (всегда реальная Lua 5.4), единый пайплайн тестов
- **v2.2**: Тесты с реальным Lua VM, forward declaration preprocessor
- **v2.1**: CSS парсер, группы страниц, KDL-тесты
- **v2.0**: Mock LVGL, build.py/test.py, первые UI-тесты
- **v1.0**: Начальная версия

---

Компиляция и тестирование [TelaOS](https://github.com/OpenTela/TelaOS) на хосте без ESP32.

**53 теста · ~1050 проверок · 13K LOC тестов · 2K LOC стабов**

```bash
python3 build.py /path/to/TelaOS --mock    # компиляция
python3 test.py  /path/to/TelaOS           # все тесты
python3 test.py  /path/to/TelaOS -q        # быстрая проверка (9 ключевых)
```

---

## Требования

- **g++** с поддержкой C++20 (`-std=gnu++2a`)
- **Python 3.6+**

```bash
# Ubuntu/Debian
sudo apt install g++ python3
```

---

## Структура

```
telaci/
├── build.py              # Компиляция проекта (mock .o файлы)
├── test.py               # Запуск тестов
├── validate_archives.py  # Валидация архивов
├── README.md             # Этот файл
├── TEST_SUITE.md         # Описание всех тестов
│
├── stubs/                # Заглушки для ESP32/LVGL/Arduino
│   ├── lvgl.h            # LVGL 9 API
│   ├── lvgl_mock.h       # Mock-виджеты для тестов
│   ├── Arduino.h         # Arduino framework
│   ├── ArduinoJson.h     # JSON
│   ├── NimBLEDevice.h    # BLE
│   ├── LittleFS.h        # Файловая система (VFS in-memory)
│   ├── esp_timer.h       # ESP таймеры
│   ├── esp_log.h         # ESP логирование
│   ├── esp_heap_caps.h   # ESP память
│   ├── esp_system.h      # ESP система
│   ├── freertos/         # FreeRTOS стабы
│   ├── ui_layout.h       # UI layout helpers
│   ├── ui_query.h        # UI query helpers
│   └── ...
│
├── lib/                  # Внешние библиотеки
│   ├── build_libs.sh     # Скрипт сборки
│   ├── ckdl/             # KDL парсер (исходники + libkdl.a)
│   └── lua54/            # Lua 5.4 (include + liblua54.a)
│
├── tests/                # 53 теста, ~1050 проверок
│   ├── console/          # 4 файла, 168 проверок — консольное API, команды, парсинг
│   ├── core/             # 5 файлов, 81 проверка — state store, приведение типов, app meta
│   ├── csv/              # 1 файл, 24 проверки — CSV парсер
│   ├── e2e/              # 6 файлов, 132 проверки — калькулятор, кроссворд, lifecycle, Brainfuck
│   ├── lua/              # 7 файлов, 123 проверки — таймеры, fetch, ошибки, sandbox
│   ├── parser/           # 3 файла, 23 проверки — HTML парсер, HTML→KDL
│   ├── ui/               # 16 файлов, 253 проверки — CSS, шаблоны, @for, canvas, coords, z-index
│   ├── utils/            # 5 файлов, 117 проверок — строки, транслитерация, имена
│   ├── widgets/          # 5 файлов, 44 проверки — binding, button, input, visibility, escaping
│   ├── yaml/             # 1 файл, 37 проверок — YAML парсер + Lua API
│   ├── externals.cpp     # Общие external definitions
│   └── bin/              # Скомпилированные бинарники (gitignore)
│
└── build/
    └── mock/             # Объектные файлы (gitignore)
```

---

## build.py — Компиляция

Компилирует все `.cpp` из `src/` проекта в объектные файлы с mock-окружением.

```bash
python3 build.py <project_path> --mock [--rebuild]
```

| Аргумент | Описание |
|----------|----------|
| `project_path` | Путь к проекту TelaOS с `src/` |
| `--mock` | Mock-режим (`-DLVGL_MOCK_ENABLED`) |
| `--rebuild` | Пересобрать все файлы |

Инкрементальная сборка: `-MMD` отслеживает зависимости, перекомпилируются только изменённые файлы.

---

## test.py — Запуск тестов

```bash
python3 test.py <project_path> [options] [test_names...]
```

| Опция | Описание |
|-------|----------|
| (без аргументов) | Все 53 теста |
| `-q, --quick` | 9 ключевых тестов (~5x быстрее), при фейле — авторасширение |
| `--no-expand` | Не расширять quick при фейле |
| `css` | Тесты содержащие "css" в имени |
| `-e css` | Точно `test_css` |
| `-l, --list` | Список тестов (с `-q` — только quick set) |
| `-c, --clean` | Очистить bin/ перед запуском |
| `--validate` | Проверить структуру архивов |
| `--pack` | Создать compiler.zip в /outputs |

### Quick mode

9 тестов, покрывающих все основные подсистемы:

`state_store` · `calc_console` · `crossword` · `lua_errors` · `css` · `binding` · `lifecycle` · `console_args` · `html_parser`

При фейле автоматически запускаются остальные 44 теста (отключается `--no-expand`).

---

## Стабы (stubs/)

| Стаб | Что эмулирует | Уровень |
|------|---------------|---------|
| `lvgl.h` + `lvgl_mock.h` | LVGL 9: виджеты, стили, объекты, mock-дерево | Полный |
| `LittleFS.h` | VFS: in-memory файловая система | Полный |
| `Arduino.h` | Serial, millis, String, Wire | Базовый |
| `ArduinoJson.h` | JSON документы, массивы, десериализация | Частичный |
| `NimBLEDevice.h` | BLE стек | Только компиляция |
| `esp_timer.h` | `esp_timer_get_time()` | Стаб (возвращает 0) |
| `esp_log.h` | ESP_LOGI/W/E макросы | printf-обёртка |
| `esp_heap_caps.h` | `heap_caps_malloc` | malloc-обёртка |
| `esp_system.h` | `esp_restart` | Стаб |
| `freertos/` | Mutex, semaphore, timers | Стабы |
| `ui_layout.h` / `ui_query.h` | UI helpers | Рабочие |

---

## Библиотеки (lib/)

```bash
./lib/build_libs.sh          # собрать всё
./lib/build_libs.sh ckdl     # только ckdl
./lib/build_libs.sh lua      # только Lua (скачивает с lua.org)
./lib/build_libs.sh clean    # удалить .a файлы
```

| Библиотека | Описание |
|-----------|----------|
| `ckdl` | KDL парсер v1.0 — исходники в `lib/ckdl/src/`, сборка локальная |
| `lua54` | Lua 5.4.7 — скачивается при первом запуске, реальный VM (не стаб) |

---

## Написание тестов

```cpp
#include <cstdio>
#include "lvgl.h"
#include "lvgl_mock.h"
#include "ui/ui_engine.h"
#include "core/state_store.h"

#define TEST(name) printf("  %-50s ", name); total++;
#define PASS() do { printf("✓\n"); passed++; } while(0)
#define FAIL(msg) do { printf("✗ %s\n", msg); } while(0)

int main() {
    printf("=== My Tests ===\n\n");
    int passed = 0, total = 0;

    LvglMock::create_screen(480, 480);
    State::store().clear();
    UI::Engine::instance().init();
    UI::Engine::instance().render(MY_HTML);

    TEST("something works") {
        if (/* check */) PASS();
        else FAIL("nope");
    }

    printf("\n");
    if (passed == total) {
        printf("=== ALL %d TESTS PASSED ===\n", total);
        return 0;
    } else {
        printf("=== %d/%d FAILED ===\n", total - passed, total);
        return 1;
    }
}
```

Тест считается пройденным если exit code = 0 и в stdout есть `PASSED`/`PASS`/`Complete` без `FAILED`/`FAIL`.

---

## Troubleshooting

| Проблема | Решение |
|----------|---------|
| `liblua54.a not found` | `./lib/build_libs.sh` |
| `build/mock/ not found` | `python3 build.py <path> --mock` |
| `undefined reference` | Добавить стаб в `stubs/` или в `tests/externals.cpp` |
| Тест упал после изменения кода | `build.py --mock` → `test.py -c` |