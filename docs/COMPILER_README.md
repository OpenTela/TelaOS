# EOS Compiler & Test Framework

Инструменты для компиляции и тестирования проектов Evolution OS без реального ESP32.

## Структура

```
compiler/
├── build.py          # Компилятор (mock build)
├── test.py           # Запуск тестов
├── stubs/            # Заглушки для ESP32/LVGL/Arduino
├── lib/              # Внешние библиотеки
│   ├── ckdl/         # KDL парсер (~130KB)
│   └── lua54/        # Lua 5.4 VM (~501KB)
├── tests/            # Тесты (test_*.cpp)
│   ├── bin/          # Скомпилированные тесты
│   └── externals.cpp # Общие external definitions
└── build/
    └── mock/         # Объектные файлы (.o)
```

## Требования

- **g++** с поддержкой C++20 (`-std=gnu++2a`)
- **Python 3.6+**

```bash
# Ubuntu/Debian
sudo apt install g++ python3

# macOS
xcode-select --install
```

---

## build.py — Компилятор

Компилирует проект EOS в режиме mock (без ESP32).

### Использование

```bash
python3 build.py <project_path> --mock
```

### Аргументы

| Аргумент | Описание |
|----------|----------|
| `project_path` | Путь к проекту с директорией `src/` |
| `--mock` | Режим mock-компиляции (обязателен) |

### Примеры

```bash
# Компиляция проекта
python3 build.py /path/to/eos --mock

# Повторная компиляция (только изменённые файлы)
python3 build.py /path/to/eos --mock
```

### Вывод

```
Project: /path/to/eos
Found 42 .cpp files in 10 folders
Mode: --mock (LVGL_MOCK_ENABLED)
Build dir: /home/user/compiler/build/mock
Compiling 42 files...

[1/42] ✓ bf/bf_engine.cpp
[2/42] ✓ ble/bin_receive.cpp
...
[42/42] ✓ widgets/widget_common.cpp

✓ All 42 files compiled successfully!
```

### Ошибки

При ошибке компиляции показывается файл и сообщение:

```
[5/42] ✗ console/console.cpp

--- console/console.cpp ---
error: 'foo' was not declared in this scope
```

---

## test.py — Запуск тестов

Компилирует и запускает тесты из директории `tests/`.

### Использование

```bash
python3 test.py <project_path> [options] [test_names...]
```

### Аргументы

| Аргумент | Описание |
|----------|----------|
| `project_path` | Путь к проекту (обязателен) |
| `test_names` | Имена тестов для запуска (опционально) |

### Опции

| Опция | Описание |
|-------|----------|
| `-e, --exact` | Точное совпадение имени теста |
| `-l, --list` | Показать список тестов |
| `-c, --clean` | Очистить бинарники перед запуском |
| `-h, --help` | Справка |

### Примеры

```bash
# Запуск всех тестов
python3 test.py /path/to/eos

# Список доступных тестов
python3 test.py /path/to/eos --list

# Запуск тестов содержащих "align"
python3 test.py /path/to/eos align

# Запуск точно test_console
python3 test.py /path/to/eos -e console

# Несколько тестов
python3 test.py /path/to/eos bf widgets css

# Очистка и запуск
python3 test.py /path/to/eos -c
```

### Вывод

```
Project: /path/to/eos
Running 16 test(s), 42 objects

[1/16] align: === ALL 13 TESTS PASSED ===
[2/16] bf: === Tests Complete ===
[3/16] binding: === ALL 11 TESTS PASSED ===
...
[16/16] widgets: === ALL 10 TESTS PASSED ===

✓ ALL 16 TESTS PASSED
```

### Статусы

| Статус | Значение |
|--------|----------|
| `=== ... PASSED ===` | Тест прошёл |
| `✓` | Тест прошёл (без подробностей) |
| `COMPILE ERROR` | Ошибка компиляции теста |
| `✗ FAILED` | Тест не прошёл |
| `TIMEOUT` | Тест завис (>30 сек) |

---

## Написание тестов

### Структура теста

Файл `tests/test_<name>.cpp`:

```cpp
#include <cstdio>
#include "ui/ui_html.h"  // includes из src/

#define TEST(name) printf("  %-40s ", name); total++;
#define PASS() do { printf("✓\n"); passed++; } while(0)
#define FAIL(msg) printf("✗ %s\n", msg)

int main() {
    printf("=== My Tests ===\n\n");
    int passed = 0;
    int total = 0;
    
    TEST("widget creates successfully");
    {
        auto w = create_widget();
        if (w != nullptr) PASS();
        else FAIL("widget is null");
    }
    
    TEST("binding works");
    {
        State::store().set("foo", "bar");
        if (State::store().get("foo") == "bar") PASS();
        else FAIL("value mismatch");
    }
    
    // Summary
    printf("\n");
    if (passed == total) {
        printf("=== ALL %d TESTS PASSED ===\n", total);
        return 0;
    } else {
        printf("=== %d/%d TESTS FAILED ===\n", total - passed, total);
        return 1;
    }
}
```

### Маркеры успеха

Тест считается пройденным если в выводе есть:
- `PASSED`
- `PASS`
- `Complete`

И НЕТ:
- `FAILED`
- `FAIL`

### Доступные includes

Тесты имеют доступ к:
- Всем headers из `src/` проекта
- Стабам из `stubs/` (Arduino, LVGL, ESP32, etc.)
- Библиотеке KDL из `lib/ckdl/`

---

## Стабы (stubs/)

Заглушки эмулируют ESP32/Arduino/LVGL API для компиляции на хосте.

| Файл | Эмулирует |
|------|-----------|
| `Arduino.h` | Arduino framework |
| `ArduinoJson.h` | ArduinoJson библиотека |
| `lvgl.h` | LVGL graphics library |
| `lvgl_mock.h` | Mock для захвата LVGL вызовов |
| `NimBLEDevice.h` | BLE библиотека |
| `LittleFS.h` | Файловая система |
| `esp_heap_caps.h` | ESP32 memory allocation |
| `lua.h`, `lauxlib.h`, `lualib.h` | Lua API |

### Mock виджеты

`lvgl_mock.h` предоставляет `MockWidget` для тестирования UI:

```cpp
// В тесте
auto* mock = lv_obj_get_mock(lv_widget);
ASSERT(mock->text == "Hello");
ASSERT(mock->x == 100);
ASSERT(mock->bgcolor == 0xFF0000);
```

---

## Типичный workflow

```bash
# 1. Компиляция проекта
python3 build.py /path/to/eos --mock

# 2. Запуск всех тестов
python3 test.py /path/to/eos

# 3. При изменении кода — только перекомпиляция изменённых файлов
python3 build.py /path/to/eos --mock

# 4. Запуск конкретного теста
python3 test.py /path/to/eos -e console

# 5. Полная пересборка (при проблемах)
rm -rf build/mock tests/bin
python3 build.py /path/to/eos --mock
python3 test.py /path/to/eos -c
```

---

## Текущие тесты

| Тест | Описание |
|------|----------|
| `align` | Выравнивание виджетов |
| `bf` | Brainfuck интерпретатор |
| `binding` | Биндинг переменных state ↔ UI |
| `console` | Console API (exec, subsystems) |
| `css` | CSS парсер |
| `groups` | Группы страниц (свайп) |
| `html_parser` | HTML парсер |
| `html_to_kdl` | Конвертация HTML → KDL |
| `navigation` | Навигация между страницами |
| `qi_align` | Quick-init выравнивание |
| `ui_calculator` | UI калькулятора |
| `ui_ids` | ID виджетов |
| `ui_render` | Рендеринг UI |
| `ui_styles` | CSS стили |
| `visibility` | Видимость виджетов |
| `widgets` | Базовые виджеты |

---

## Troubleshooting

### Build directory not found

```
ERROR: Build directory not found: .../build/mock
Run: python3 build.py <project_path> --mock
```

**Решение:** Сначала запустите `build.py`

### No src/ directory

```
ERROR: No src/ directory in project: /path
```

**Решение:** Укажите правильный путь к проекту

### Undefined reference

```
undefined reference to `SomeFunction'
```

**Решение:** Добавьте стаб в `stubs/` или `tests/externals.cpp`

### Test fails after code change

**Решение:** Пересоберите с очисткой:
```bash
python3 build.py /path/to/eos --mock
python3 test.py /path/to/eos -c
```
