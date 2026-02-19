# Console Protocol Specification v2.7

Унифицированный протокол для TelaOS - работает через BLE и Serial.

## Changelog

- **v2.7**: Touch Simulation: widget ID автодетект, `click` алиас, `type` с widget ID
- **v2.6**: Touch Simulation (tap, hold, swipe, type) + sys sync (синхронизация при подключении)
- **v2.5**: Screenshot: палитровый формат (`pal`), wire format, LZ4 compression, nearest-neighbor downscale
- **v2.4**: Events — unsolicited сообщения от устройства (lua_error) [TODO]
- **v2.1**: Добавлены коды ошибок (offline, timeout, server, etc.)
- **v2.0**: Унифицированный протокол BLE + Serial
---

## Обзор

### Два транспорта - один протокол

**BLE Transport:**
- Формат: JSON массивы `[id, subsystem, cmd, [args]]`
- Binary channel для больших данных
- Асинхронные ответы с ID

**Serial Transport:**
- Формат: текстовые команды `subsystem cmd arg1 arg2`
- Синхронные ответы
- Удобно для CLI и отладки

**Общая логика:**
- 3 subsystem: `ui`, `sys`, `app`
- Одинаковые команды
- Одинаковые ответы

---

## BLE Transport

### Формат команды:

```json
[id, subsystem, cmd, [arg1, arg2, ...]]
```

**Параметры:**
- `id` (int) - уникальный ID запроса
- `subsystem` (string) - "ui" | "sys" | "app"
- `cmd` (string) - команда
- `args` (array) - массив строковых аргументов

**Примеры:**
```json
[1, "sys", "ping", []]
[2, "ui", "set", ["temperature", "23"]]
[3, "app", "run", ["weather"]]
[4, "app", "pull", ["weather", "icon.png"]]
```

---

### Формат ответа:

**Успех:**
```json
[id, "ok", {data}]
```

**Ошибка:**
```json
[id, "error", {"code": "error_code", "message": "Human readable message", "http": 404}]
```

Поле `http` опционально — присутствует только для HTTP-ошибок.

**Коды ошибок:**

| Код | Описание | HTTP коды | Когда возникает |
|-----|----------|-----------|-----------------|
| `invalid` | Неверный запрос | — | Неизвестная команда, неверные аргументы |
| `not_found` | Не найдено | 404 | Приложение/файл/ресурс не существует |
| `offline` | Нет интернета | — | Fetch не может выполниться из-за отсутствия сети |
| `timeout` | Таймаут | 408, 504 | Сервер не ответил вовремя |
| `server` | Ошибка сервера | 500-599 | Внутренняя ошибка сервера |
| `denied` | Доступ запрещён | 401, 403 | Нет авторизации или прав доступа |
| `busy` | Занято | — | Ресурс занят другой операцией |
| `memory` | Нет памяти | — | Недостаточно RAM/PSRAM |

**Примеры:**
```json
[1, "ok", {}]
[2, "ok", {"value": "23"}]
[3, "error", {"code": "not_found", "message": "App not found: weather2"}]
[4, "error", {"code": "offline", "message": "No internet connection"}]
[5, "error", {"code": "server", "message": "Internal Server Error", "http": 500}]
[6, "error", {"code": "denied", "message": "Unauthorized", "http": 401}]
[7, "error", {"code": "not_found", "message": "Not Found", "http": 404}]
```

**Обработка на клиенте:**
```javascript
if (response[1] === "ok") {
    handleData(response[2]);
} else {
    const {code, message, http} = response[2];
    if (code === "offline") {
        showOfflineMessage();
    } else if (code === "timeout") {
        offerRetry();
    } else if (http === 401) {
        promptLogin();
    } else if (http === 404) {
        showNotFound(message);
    } else {
        showError(`${message} (${http || code})`);
    }
}
```

---

### Обратная совместимость (v1):

Старый формат автоматически преобразуется в v2:

**Старый формат (v1):**
```json
{
  "cmd": "setState",
  "name": "temperature",
  "value": "23"
}
```

**Маппинг в v2:**
```json
[auto_id, "ui", "set", ["temperature", "23"]]
```

**Поддерживаемые команды v1:**
- `{"cmd": "setState", ...}` → `["ui", "set", ...]`
- `{"cmd": "getState", ...}` → `["ui", "get", ...]`
- `{"cmd": "navigate", ...}` → `["ui", "nav", ...]`
- `{"cmd": "call", ...}` → `["ui", "call", ...]`

---

## Serial Transport

### Формат команды:

```
subsystem cmd arg1 arg2 "quoted arg with spaces"
```

**Правила:**
- Пробелы разделяют аргументы
- Кавычки `"..."` для аргументов с пробелами
- Без ID (команды синхронные)

**Примеры:**
```
app list
app info weather
ui set userName "Иван Петров"
sys ping
```

---

### Сокращения для sys:

Команды `sys` можно писать без префикса:

```
ping          =  sys ping
info          =  sys info
heap          =  sys info
mem           =  sys info
log verbose   =  sys log verbose
ble on        =  sys ble on
```

**Примеры:**
```
ping                    # sys ping
info                    # sys info
heap                    # sys info (алиас)
log ui verbose          # sys log ui verbose
ble off                 # sys ble off
```

---

### Формат ответа:

**Текстовый вывод:**
```
OK: {"count": 3}
ERROR: App not found
```

**Для binary transfer:**
```
OK: {"size": 2048, "transfer_id": 100}
<binary data следует>
```

---

## Команды


### ui - Управление состоянием приложения

| Команда | Аргументы | Ответ | Описание |
|---------|-----------|-------|----------|
| `set` | var, val | `{}` | Установить переменную state |
| `get` | var | `{"value":"..."}` | Получить значение переменной |
| `nav` | page | `{}` | Переход на страницу |
| `call` | func, [args...] | `{}` | Вызвать Lua функцию |
| `tap` | x, y **ИЛИ** widgetId | `{}` | Тап (touch simulation) |
| `click` | — | — | Алиас для `tap` |
| `hold` | x, y, [ms] **ИЛИ** widgetId, [ms] | `{}` | Длительное нажатие (default 500ms) |
| `swipe` | direction, [speed] | `{}` | Свайп по направлению (left/right/up/down) |
| `swipe` | x1, y1, x2, y2, [ms] | `{}` | Свайп по координатам |
| `type` | text **ИЛИ** widgetId, text | `{}` | Ввод текста (focus + ввод) |

Первый аргумент `tap`, `hold`, `type` — **автодетект**: число → координаты, строка → widget ID.

**Примеры (BLE):**
```json
[1, "ui", "set", ["temperature", "23"]]
[2, "ui", "get", ["temperature"]]
[3, "ui", "nav", ["settings"]]
[4, "ui", "call", ["updateWeather"]]
[5, "ui", "call", ["setBrightness", "50"]]
[6, "ui", "tap", ["120", "160"]]
[7, "ui", "click", ["btnPlus"]]
[8, "ui", "swipe", ["left"]]
[9, "ui", "type", ["editInput", "Hello World"]]
```

**Примеры (Serial):**
```
ui set temperature 23
ui get temperature
ui nav settings
ui call updateWeather
ui call setBrightness 50
ui tap 120 160
ui click btnPlus
ui swipe left
ui type editInput "Hello World"
```

---

### ui - Touch Simulation (удалённое управление экраном)

Команды для симуляции touch событий - позволяют управлять устройством удалённо.

#### `ui tap` / `ui click`

Одиночный тап. Автодетект: число → координаты, строка → widget ID.

`click` — полный алиас `tap`, более естественный для виджетов.

**Форматы:**
- `ui tap <x> <y>` — по координатам
- `ui tap <widgetId>` — по центру виджета
- `ui click <widgetId>` — то же самое

**Примеры (BLE):**
```json
[1, "ui", "tap", ["120", "160"]]     // координаты
[2, "ui", "click", ["btnPlus"]]      // widget ID
[3, "ui", "tap", ["btnSettings"]]    // tap по widget тоже работает
```

**Примеры (Serial):**
```
ui tap 120 160
ui click btnPlus
ui tap btnSettings
```

**Поведение:**
- Widget ID: вычисляет центр виджета через `lv_obj_get_coords()` (экранные координаты)
- Генерирует события: `TOUCH_DOWN` → wait 50ms → `TOUCH_UP`
- Вызывает `onclick` если попали на виджет
- Координаты вне экрана → error `invalid`
- Widget не найден → error `invalid`

---

#### `ui hold`

Длительное нажатие. Автодетект: число → координаты, строка → widget ID.

**Форматы:**
- `ui hold <x> <y> [ms]` — по координатам
- `ui hold <widgetId> [ms]` — по центру виджета

**Примеры (BLE):**
```json
[1, "ui", "hold", ["120", "160"]]           // координаты, 500ms
[2, "ui", "hold", ["120", "160", "1000"]]   // координаты, 1 секунда
[3, "ui", "hold", ["btnMenu"]]              // widget, 500ms
[4, "ui", "hold", ["btnMenu", "1000"]]      // widget, 1 секунда
```

**Примеры (Serial):**
```
ui hold 120 160
ui hold 120 160 1000
ui hold btnMenu
ui hold btnMenu 1000
```

**Поведение:**
- `TOUCH_DOWN` → wait duration → `TOUCH_UP`
- Вызывает `onhold` если попали на виджет

---

#### `ui swipe direction [speed]`

Свайп по направлению.

**Направления:**
- `left`, `l` - свайп влево
- `right`, `r` - свайп вправо  
- `up`, `u` - свайп вверх
- `down`, `d` - свайп вниз

**Speed:**
- `fast` - 150ms
- `normal` - 300ms (default)
- `slow` - 500ms

**Примеры (BLE):**
```json
[1, "ui", "swipe", ["left"]]              // свайп влево
[2, "ui", "swipe", ["right", "fast"]]     // свайп вправо быстро
[3, "ui", "swipe", ["l"]]                 // сокращение
```

**Примеры (Serial):**
```
ui swipe left
ui swipe right fast
ui swipe l
```

**Поведение:**
- Автоматически рассчитывает координаты (80% от размера экрана)
- `left`: (80%, 50%) → (20%, 50%)
- `right`: (20%, 50%) → (80%, 50%)
- `up`: (50%, 80%) → (50%, 20%)
- `down`: (50%, 20%) → (50%, 80%)

---

#### `ui swipe x1 y1 x2 y2 [duration_ms]`

Свайп по координатам.

**Параметры:**
- `x1, y1` (int) - начальная точка
- `x2, y2` (int) - конечная точка
- `duration_ms` (int, optional) - длительность (default: 300)

**Примеры (BLE):**
```json
[1, "ui", "swipe", ["0", "120", "240", "120"]]        // горизонтальный
[2, "ui", "swipe", ["120", "0", "120", "240", "500"]] // вертикальный медленный
```

**Примеры (Serial):**
```
ui swipe 0 120 240 120
ui swipe 120 0 120 240 500
```

**Поведение:**
- Интерполирует промежуточные точки (~16ms шаг)
- `TOUCH_DOWN` → последовательность `TOUCH_MOVE` → `TOUCH_UP`

---

#### `ui type`

Ввод текста. С widget ID — фокусирует input и вводит текст за одну команду.

**Форматы:**
- `ui type <text>` — ввод в текущий focused input
- `ui type <widgetId> <text>` — фокус на виджет + ввод

**Примеры (BLE):**
```json
[1, "ui", "type", ["Hello World"]]
[2, "ui", "type", ["editInput", "Hello World"]]
[3, "ui", "type", ["emailField", "user@example.com"]]
```

**Примеры (Serial):**
```
ui type "Hello World"
ui type editInput "Hello World"
ui type emailField "user@example.com"
```

**Поведение:**
- С widget ID: вызывает `focusInput(widgetId)`, затем вводит текст
- Без widget ID: вводит в текущий focused input
- Обновляет привязанную переменную state
- Вызывает события onchange

**Ошибки:**
```json
[1, "error", {"code": "invalid", "message": "No input widget in focus"}]
[2, "error", {"code": "invalid", "message": "Widget not found or not an input"}]
```

---

#### Автодетект: координаты vs widget ID

Команды `tap`/`click`, `hold`, `type` автоматически определяют тип первого аргумента:

| Первый аргумент | Пример | Интерпретация |
|-----------------|--------|---------------|
| Число | `"120"` | Координата X, следующий аргумент — Y |
| Строка | `"btnPlus"` | Widget ID, координаты вычисляются автоматически |

**Вычисление координат:** центр виджета в экранных координатах через `lv_obj_get_coords()`. Учитывает позицию на текущей странице, вложенность в контейнеры и скролл.

---

#### Координатная система

```
(0,0) ──────────► X
  │
  │    240×240
  │    дисплей
  │
  ▼ Y
```

- X: 0 (левый край) → WIDTH-1 (правый край)
- Y: 0 (верхний край) → HEIGHT-1 (нижний край)
- Координаты вне границ → error `invalid`

---

#### Примеры сценариев Touch Simulation

**Навигация (по widget ID):**
```bash
ui click btnSettings     # открыть settings
ui swipe down            # прокрутить вниз
ui click btnOk           # нажать кнопку OK
```

**Заполнение формы (widget ID + type):**
```bash
ui type inputName "John Doe"       # фокус + ввод за одну команду
ui type inputEmail "john@example.com"
ui click btnSubmit
```

**Координаты (когда ID неизвестен):**
```bash
ui tap 120 80        # тап по координатам
ui type "John Doe"   # ввод в текущий focused input
ui tap 120 200       # другое поле
```

**Переключение страниц:**
```bash
ui swipe left        # следующая страница
ui swipe left        # ещё одна
ui swipe right       # назад
```

**Автоматизация (Python):**
```python
device.send([1, "ui", "click", ["btnSettings"]])
device.send([2, "ui", "type", ["inputEmail", "test@example.com"]])
device.send([3, "ui", "click", ["btnSubmit"]])
device.send([4, "ui", "swipe", ["left"]])
```


### sys - Системные команды

| Команда | Аргументы | Ответ | Описание |
|---------|-----------|-------|----------|
| `ping` | — | `{}` | Проверка связи |
| `info` | — | `{"dram":N,"psram":N,"fs_used":N,"fs_total":N}` | Информация о системе |
| `sync` | protocol, datetime, timezone | `{"protocol":"2.6","os":"0.3.5"}` | Синхронизация при подключении |
| `screen` | [color], [scale], [mode] | `{w,h,color,format,raw_size}` → BIN_CHAR | Захват скриншота |
| `bt` / `ble` | on / off | `{}` | Управление Bluetooth |
| `log` | [category] [level] | `{}` | Управление логированием |

**Примеры (BLE):**
```json
[1, "sys", "ping", []]
[2, "sys", "info", []]
[3, "sys", "sync", ["2.6", "2026-02-15T12:34:56Z", "+03:00"]]
[4, "sys", "screen", ["rgb16", "2"]]
[5, "sys", "ble", ["off"]]
[6, "sys", "log", ["ui", "verbose"]]
[7, "sys", "log", ["verbose"]]  // все категории
```

**Примеры (Serial):**
```
ping                    # сокращение для sys ping
info                    # сокращение для sys info
heap                    # алиас для sys info
mem                     # алиас для sys info
sys sync 2.6 "2026-02-15T12:34:56Z" "+03:00"
sys screen rgb16 2
ble off                 # сокращение для sys ble off
log ui verbose
log verbose             # все категории
```

---

### sys sync - Синхронизация при подключении

Первая команда после BLE connect - синхронизирует время и обменивается версиями.

#### `sys sync protocol_version datetime timezone`

**Параметры (от телефона/клиента):**
1. `protocol_version` (string) - версия протокола клиента
2. `datetime` (string, ISO 8601) - текущее время UTC
3. `timezone` (string) - смещение от UTC (формат: ±HH:MM)

**Ответ (от устройства):**
```json
{
  "protocol": "2.6",
  "os": "0.3.5"
}
```

1. `protocol` (string) - версия протокола устройства
2. `os` (string) - версия TelaOS/прошивки

---

#### Примеры sys sync

**BLE:**

Запрос:
```json
[1, "sys", "sync", ["2.6", "2026-02-15T12:34:56Z", "+03:00"]]
```

Ответ:
```json
[1, "ok", {
  "protocol": "2.6",
  "os": "0.3.5"
}]
```

**Serial:**

Запрос:
```
sys sync 2.6 "2026-02-15T12:34:56Z" "+03:00"
```

Ответ:
```
OK: {"protocol":"2.6","os":"0.3.5"}
```

---

#### Timezone - примеры

| Timezone | Описание | Пример |
|----------|----------|--------|
| `+00:00` | UTC, Лондон | `sys sync 2.6 "2026-02-15T12:00:00Z" "+00:00"` |
| `+03:00` | Москва (MSK) | `sys sync 2.6 "2026-02-15T12:00:00Z" "+03:00"` |
| `-05:00` | Нью-Йорк (EST) | `sys sync 2.6 "2026-02-15T12:00:00Z" "-05:00"` |
| `+05:30` | Индия (IST) | `sys sync 2.6 "2026-02-15T12:00:00Z" "+05:30"` |
| `+09:00` | Токио (JST) | `sys sync 2.6 "2026-02-15T12:00:00Z" "+09:00"` |

---

#### Когда вызывать sync

**Обязательно:**
- При первом BLE подключении
- При переподключении (reconnect)

**Опционально:**
- Когда время на телефоне изменилось
- Когда изменилась timezone (путешествие)
- Периодически для проверки синхронизации (раз в час)

---

#### Обработка версий протокола

**Совпадают версии:**

Запрос:
```json
[1, "sys", "sync", ["2.6", "2026-02-15T12:34:56Z", "+03:00"]]
```

Ответ:
```json
[1, "ok", {"protocol": "2.6", "os": "0.3.5"}]
```

→ Клиент продолжает работу

---

**Версии отличаются (minor):**

Запрос:
```json
[1, "sys", "sync", ["2.6", "2026-02-15T12:34:56Z", "+03:00"]]
```

Ответ:
```json
[1, "ok", {"protocol": "2.4", "os": "0.3.1"}]
```

**Действия клиента:**
- ✅ Продолжить работу (v2.x обратно совместимы)
- ⚠️ Показать предупреждение: "Обновите прошивку для новых функций"
- ⚠️ Отключить функции недоступные в v2.4

---

**Версии отличаются (major):**

Запрос:
```json
[1, "sys", "sync", ["2.6", "2026-02-15T12:34:56Z", "+03:00"]]
```

Ответ:
```json
[1, "ok", {"protocol": "3.0", "os": "1.0.0"}]
```

**Действия клиента:**
- ❌ Показать ошибку: "Несовместимая версия протокола"
- ❌ Предложить обновить приложение
- ❌ Отключиться

---

#### Пример flow подключения

```
1. Телефон подключается по BLE
   → onConnect()
   
2. Телефон отправляет sync
   → [1, "sys", "sync", ["2.6", "2026-02-15T12:34:56Z", "+03:00"]]
   
3. Устройство обрабатывает:
   → Устанавливает время: 12:34:56 UTC
   → Устанавливает timezone: +03:00
   → Локальное время: 15:34:56
   
4. Устройство отвечает:
   ← [1, "ok", {"protocol":"2.6","os":"0.3.5"}]
   
5. Телефон проверяет совместимость:
   → protocol: 2.6 == 2.6 ✅
   → Продолжает работу
   
6. Теперь можно использовать остальные команды
```


---
### app - Управление приложениями

| Команда | Аргументы | Ответ | Описание |
|---------|-----------|-------|----------|
| `list` | — | `{"count":N}` → BIN_CHAR | Список приложений |
| `run` | name | `{}` | Запустить приложение |
| `info` | name | `{"title":"...","size":N,"files":[...]}` | Информация о приложении |
| `pull` | name, [file] | `{"size":N,"file":"..."}` → BIN_CHAR | Скачать файл |
| `push` | name, [file], size | `{}` ← BIN_CHAR | Загрузить файл на устройство |
| `rm` | name | `{}` | Удалить приложение |

**Примечания:**
- `app pull weather` - без указания файла отдаёт `app.html` (по умолчанию)
- `app pull weather icon.png` - явное указание файла
- `app push weather app.html 9924` — загрузить файл (ожидает binary data через BIN_CHAR)
- `app push weather * {"app.html":9924,"icon.png":512}` — multi-file upload

**Примеры (BLE):**
```json
[1, "app", "list", []]
[2, "app", "run", ["weather"]]
[3, "app", "info", ["weather"]]
[4, "app", "pull", ["weather"]]
[5, "app", "pull", ["weather", "icon.png"]]
[6, "app", "rm", ["weather"]]
```

**Примеры (Serial):**
```
app list
app run weather
app info weather
app pull weather
app pull weather icon.png
app rm weather
```

---

## BIN_CHAR - Binary Channel

### Назначение:

Отдельная BLE характеристика для передачи данных, не влезающих в JSON.

**UUID:** `...def3` (BIN_CHAR)  
**Тип:** Notify only (от устройства к ПК)

### Формат chunk:

```
[2B chunk_id LE][до 250B данных]
```

**Параметры:**
- `chunk_id` (uint16 LE) - номер чанка (0, 1, 2, ...)
- Данные: до 250 байт (зависит от MTU)

**End marker:**
```
[0xFF 0xFF]
```

### Применяется в:

1. **app list** - список имён приложений
2. **app pull** - содержимое файла
3. **sys screen** - скриншот (LZ4-сжатый)

---

### Пример: app list

**Команда (BLE):**
```json
→ [1, "app", "list", []]
```

**Ответ (JSON):**
```json
← [1, "ok", {"count": 3}]
```

**Binary data (BIN_CHAR):**
```
[0x00, 0x00][weather\0calculator\0timer\0]
[0xFF, 0xFF]  // end marker
```

**Парсинг:**
```cpp
String data = "weather\0calculator\0timer\0";
std::vector<String> apps = data.split('\0');
// ["weather", "calculator", "timer"]
```

---

### Пример: app pull

**Команда (BLE):**
```json
→ [2, "app", "pull", ["weather", "icon.png"]]
```

**Ответ (JSON):**
```json
← [2, "ok", {"size": 2048, "file": "icon.png"}]
```

**Binary data (BIN_CHAR):**
```
[0x00, 0x00][250 bytes of PNG data]
[0x01, 0x00][250 bytes of PNG data]
[0x02, 0x00][250 bytes of PNG data]
...
[0x08, 0x00][48 bytes of PNG data]
[0xFF, 0xFF]  // end marker
```

---

### Пример: sys screen

**Команда (BLE):**
```json
→ [3, "sys", "screen", ["pal", "2"]]
```

**Ответ (JSON):**
```json
← [3, "ok", {"w": 240, "h": 240, "color": "pal", "format": "lz4", "raw_size": 28821}]
```

**Binary data (BIN_CHAR):** LZ4-сжатые данные скриншота.

Подробности формата — см. раздел **Screenshot** ниже.

---

## Screenshot — формат скриншота

### Команда

```
sys screen [color] [scale] [mode]
```

| Аргумент | Default | Описание |
|----------|---------|----------|
| `color` | `rgb16` | Цветовой формат (см. таблицу) |
| `scale` | `0` | Масштаб: 0 = полный, 2-32 = уменьшение |
| `mode` | `fixed` | `fixed` или `tiny` (auto-fit в один BLE фрейм) |

**Примеры (BLE):**
```json
[1, "sys", "screen", []]                    // rgb16, full size
[2, "sys", "screen", ["pal", "2"]]          // palette, scale 2
[3, "sys", "screen", ["gray", "2"]]         // grayscale, scale 2
[4, "sys", "screen", ["rgb16", "0", "tiny"]] // auto-fit tiny preview
```

**Примеры (Serial):**
```
sys screen
sys screen pal 2
sys screen gray 2
sys screen rgb16 0 tiny
```

---

### Ответ (JSON)

```json
[id, "ok", {
  "w": 240,
  "h": 240,
  "color": "pal",
  "format": "lz4",
  "raw_size": 28821
}]
```

| Поле | Описание |
|------|----------|
| `w`, `h` | Размер изображения после масштабирования |
| `color` | Фактический цветовой формат (может отличаться от запроса при fallback) |
| `format` | Всегда `"lz4"` — данные LZ4-сжаты |
| `raw_size` | Размер данных до LZ4 сжатия |

**Важно:** `color` в ответе может не совпадать с запросом. Если запрошен `pal`, но на экране >256 цветов, устройство автоматически переключается на `rgb16` и отвечает `"color": "rgb16"`. Клиент **всегда** должен использовать `color` из ответа для декодирования.

---

### Binary data (BIN_CHAR)

После JSON ответа данные передаются через BIN_CHAR чанками:

```
[0x00, 0x00][до 250 bytes LZ4 data]
[0x01, 0x00][до 250 bytes LZ4 data]
...
[0xFF, 0xFF]  // end marker
```

Клиент собирает чанки, получает LZ4-сжатый буфер, декомпрессирует в `raw_size` байт. Формат декомпрессированных данных зависит от `color`.

---

### Цветовые форматы

| Формат | bpp | Описание | Потери |
|--------|-----|----------|--------|
| `rgb16` | 16 | RGB565 little-endian | нет |
| `rgb8` | 8 | RGB332 | да |
| `gray` | 8 | Grayscale (ITU-R BT.601) | да |
| `bw` | 1 | Black/White, порог 128 | да |
| `pal` | 4 или 8 | Indexed palette | нет |

---

### Wire format по форматам

#### rgb16

Массив пикселей RGB565, 2 байта на пиксель, little-endian. Строки сверху вниз, слева направо.

```
Размер = w × h × 2
```

#### rgb8

Массив пикселей RGB332, 1 байт на пиксель.

```
Бит: RRRGGGBB
Размер = w × h
```

#### gray

Массив пикселей яркости, 1 байт на пиксель (0 = чёрный, 255 = белый).

```
gray = (R×77 + G×150 + B×29) >> 8
Размер = w × h
```

#### bw

Packed bits, 8 пикселей на байт. MSB = первый пиксель.

```
Размер = ceil(w × h / 8)
```

#### pal — палитровый формат

Самый компактный lossless формат для UI-экранов.

```
[1 байт]   N — количество цветов в палитре (1-256)
[N×2 байт] палитра: RGB565 little-endian
[пиксели]  индексы в палитру
```

**Пиксели:**
- N ≤ 16: **4 bpp** — два пикселя на байт, high nibble = первый пиксель
- N > 16: **8 bpp** — один байт на пиксель

**Пример — 10 цветов, 240×240:**
```
Палитра:   1 + 10×2 = 21 байт
Пиксели:   240×240 / 2 = 28800 байт (4bpp)
Raw total:  28821 байт
vs RGB565:  115200 байт
Экономия:   75% (до LZ4)
```

**Декодирование (pseudo-code):**
```javascript
function decodePalette(raw, w, h) {
    let pos = 0;
    const paletteSize = raw[pos++];
    
    // Читаем палитру RGB565
    const palette = [];
    for (let i = 0; i < paletteSize; i++) {
        palette.push(raw[pos] | (raw[pos+1] << 8));
        pos += 2;
    }
    
    // Читаем индексы
    const pixels = new Uint16Array(w * h);
    if (paletteSize <= 16) {
        // 4bpp
        for (let i = 0; i < w * h; i += 2) {
            const byte = raw[pos++];
            pixels[i]     = palette[byte >> 4];
            pixels[i + 1] = palette[byte & 0x0F];
        }
    } else {
        // 8bpp
        for (let i = 0; i < w * h; i++) {
            pixels[i] = palette[raw[pos++]];
        }
    }
    
    return pixels; // массив RGB565
}
```

---

### Fallback

| Запрос | Условие | Фактический формат |
|--------|---------|-------------------|
| `pal` | ≤16 уникальных цветов | `pal` (4bpp) |
| `pal` | 17-256 уникальных цветов | `pal` (8bpp) |
| `pal` | >256 уникальных цветов | `rgb16` (fallback) |
| остальные | всегда | как запрошено |

---

### Downscale

При `scale > 1` изображение уменьшается перед кодированием:

| Формат | Алгоритм | Почему |
|--------|----------|--------|
| `pal` | **Nearest-neighbor** | Сохраняет точные цвета LVGL, не создаёт промежуточных |
| остальные | **Box averaging** | Сглаживание, лучше качество при потерях |

**Nearest-neighbor:** берёт центральный пиксель из каждого блока scale×scale.

**Box averaging:** усредняет все пиксели блока (R, G, B каналы отдельно). Создаёт промежуточные цвета на границах — для палитры это превращает 10 цветов в 50+, поэтому для `pal` не используется.

---

### Типичные размеры (UI-экран, 10 цветов)

| Параметры | Raw | + LZ4 (оценка) |
|-----------|-----|-----------------|
| `rgb16` (480×480) | 460 800 | ~30-80 KB |
| `rgb16 2` (240×240) | 115 200 | ~8-20 KB |
| `gray 2` (240×240) | 57 600 | ~4-10 KB |
| `pal` (480×480) | ~115 KB | ~5-15 KB |
| `pal 2` (240×240) | ~29 KB | **~1-4 KB** |

`pal 2` — оптимальный выбор для стриминга превью по BLE.

---

### Структура файлов:

```
src/
├── core/
│   ├── console.h          # Result struct, exec() API
│   └── console.cpp        # Диспетчер: execUi, execSys, execApp
│
├── ble/
│   ├── ble_bridge.cpp     # BLE парсинг v2/v1, маппинг
│   ├── bin_transfer.h     # Универсальный чанк-отправщик
│   └── bin_transfer.cpp   # Реализация BinTransfer
│
└── main.cpp               # Serial обработчик, pump BinTransfer
```

---

### console.h - Result struct

```cpp
struct Result {
    bool ok;              // true = success, false = error
    String error;         // error message (если ok = false)
    cJSON* data;          // данные ответа (если ok = true)
    
    // Для binary transfer
    bool hasBinary;       // true если нужен binary transfer
    std::vector<uint8_t> binaryData;  // данные для отправки
    
    Result() : ok(true), error(""), data(nullptr), hasBinary(false) {}
    
    static Result Ok(cJSON* data = nullptr);
    static Result Error(const String& msg);
    static Result Binary(const std::vector<uint8_t>& data, cJSON* meta = nullptr);
};
```

---

### console.cpp - Диспетчер

```cpp
class Console {
public:
    static Result exec(const String& subsystem, const String& cmd, const std::vector<String>& args);
    
private:
    static Result execUi(const String& cmd, const std::vector<String>& args);
    static Result execSys(const String& cmd, const std::vector<String>& args);
    static Result execApp(const String& cmd, const std::vector<String>& args);
};

Result Console::exec(const String& subsystem, const String& cmd, const std::vector<String>& args) {
    if (subsystem == "ui") {
        return execUi(cmd, args);
    }
    else if (subsystem == "sys") {
        return execSys(cmd, args);
    }
    else if (subsystem == "app") {
        return execApp(cmd, args);
    }
    else {
        return Result::Error("Unknown subsystem: " + subsystem);
    }
}
```

---

### execUi - Реализация

```cpp
Result Console::execUi(const String& cmd, const std::vector<String>& args) {
    if (cmd == "set") {
        if (args.size() != 2) {
            return Result::Error("Usage: ui set <var> <value>");
        }
        
        State::store().set(args[0], args[1]);
        return Result::Ok();
    }
    else if (cmd == "get") {
        if (args.size() != 1) {
            return Result::Error("Usage: ui get <var>");
        }
        
        String value = State::store().get(args[0]);
        
        cJSON* data = cJSON_CreateObject();
        cJSON_AddStringToObject(data, "value", value.c_str());
        
        return Result::Ok(data);
    }
    else if (cmd == "nav") {
        if (args.size() != 1) {
            return Result::Error("Usage: ui nav <page>");
        }
        
        UI::Engine::instance().showPage(args[0]);
        return Result::Ok();
    }
    else if (cmd == "call") {
        if (args.size() < 1) {
            return Result::Error("Usage: ui call <function> [args...]");
        }
        
        String func = args[0];
        std::vector<String> funcArgs(args.begin() + 1, args.end());
        
        ScriptEngine::instance().call(func, funcArgs);
        return Result::Ok();
    }
    else {
        return Result::Error("Unknown ui command: " + cmd);
    }
}
```

---

### execSys - Реализация

```cpp
Result Console::execSys(const String& cmd, const std::vector<String>& args) {
    if (cmd == "ping") {
        return Result::Ok();
    }
    else if (cmd == "info") {
        cJSON* data = cJSON_CreateObject();
        cJSON_AddNumberToObject(data, "dram", ESP.getFreeHeap());
        cJSON_AddNumberToObject(data, "psram", ESP.getFreePsram());
        cJSON_AddNumberToObject(data, "fs_used", LittleFS.usedBytes());
        cJSON_AddNumberToObject(data, "fs_total", LittleFS.totalBytes());
        
        return Result::Ok(data);
    }
    else if (cmd == "screen") {
        // sys screen [color] [scale] [mode]
        String color = args.size() > 0 ? args[0] : "rgb16";  // rgb16, rgb8, gray, bw, pal
        int scale = args.size() > 1 ? args[1].toInt() : 0;    // 0=full, 2-32
        String mode = args.size() > 2 ? args[2] : "fixed";    // fixed, tiny
        
        Screenshot::CaptureResult cap;
        if (!Screenshot::capture(cap, mode, scale, color)) {
            return Result::Error("Screenshot capture failed");
        }
        
        // Метаданные — color может отличаться от запроса (pal → rgb16 fallback)
        cJSON* meta = cJSON_CreateObject();
        cJSON_AddNumberToObject(meta, "w", cap.width);
        cJSON_AddNumberToObject(meta, "h", cap.height);
        cJSON_AddStringToObject(meta, "color", cap.color);  // фактический формат
        cJSON_AddStringToObject(meta, "format", "lz4");
        cJSON_AddNumberToObject(meta, "raw_size", cap.rawSize);
        
        return Result::Binary(cap.buffer, cap.size, meta);
    }
    else if (cmd == "ble" || cmd == "bt") {
        if (args.size() != 1) {
            return Result::Error("Usage: sys ble <on|off>");
        }
        
        bool enable = (args[0] == "on" || args[0] == "1" || args[0] == "true");
        
        if (enable) {
            BLE::start();
        } else {
            BLE::stop();
        }
        
        return Result::Ok();
    }
    else if (cmd == "log") {
        // sys log [category] [level]
        // sys log verbose          - все категории
        // sys log ui verbose       - только UI
        
        if (args.size() == 1) {
            // Все категории
            String level = args[0];
            Log::setAllLevels(level);
        }
        else if (args.size() == 2) {
            // Конкретная категория
            String category = args[0];
            String level = args[1];
            Log::setLevel(category, level);
        }
        else {
            return Result::Error("Usage: sys log [category] <level>");
        }
        
        return Result::Ok();
    }
    else {
        return Result::Error("Unknown sys command: " + cmd);
    }
}
```

---

### execApp - Реализация

```cpp
Result Console::execApp(const String& cmd, const std::vector<String>& args) {
    if (cmd == "list") {
        // Получаем список приложений
        std::vector<String> apps = AppManager::list();
        
        // Формируем binary data: "app1\0app2\0app3\0"
        String joined;
        for (const String& app : apps) {
            joined += app + '\0';
        }
        
        std::vector<uint8_t> binaryData(joined.begin(), joined.end());
        
        // Метаданные
        cJSON* meta = cJSON_CreateObject();
        cJSON_AddNumberToObject(meta, "count", apps.size());
        
        return Result::Binary(binaryData, meta);
    }
    else if (cmd == "run") {
        if (args.size() != 1) {
            return Result::Error("Usage: app run <name>");
        }
        
        AppManager::launch(args[0]);
        return Result::Ok();
    }
    else if (cmd == "info") {
        if (args.size() != 1) {
            return Result::Error("Usage: app info <name>");
        }
        
        String name = args[0];
        AppInfo info = AppManager::getInfo(name);
        
        if (!info.exists) {
            return Result::Error("App not found: " + name);
        }
        
        cJSON* data = cJSON_CreateObject();
        cJSON_AddStringToObject(data, "title", info.title.c_str());
        cJSON_AddNumberToObject(data, "size", info.size);
        
        cJSON* files = cJSON_CreateArray();
        for (const String& file : info.files) {
            cJSON_AddItemToArray(files, cJSON_CreateString(file.c_str()));
        }
        cJSON_AddItemToObject(data, "files", files);
        
        return Result::Ok(data);
    }
    else if (cmd == "pull") {
        if (args.size() < 1 || args.size() > 2) {
            return Result::Error("Usage: app pull <name> [file]");
        }
        
        String name = args[0];
        String file = (args.size() == 2) ? args[1] : "app.html";  // default
        
        String path = "/apps/" + name + "/" + file;
        
        if (!LittleFS.exists(path.c_str())) {
            return Result::Error("File not found: " + path);
        }
        
        // Читаем файл
        File f = LittleFS.open(path.c_str(), "r");
        std::vector<uint8_t> content;
        
        while (f.available()) {
            content.push_back(f.read());
        }
        f.close();
        
        // Метаданные
        cJSON* meta = cJSON_CreateObject();
        cJSON_AddNumberToObject(meta, "size", content.size());
        cJSON_AddStringToObject(meta, "file", file.c_str());
        
        return Result::Binary(content, meta);
    }
    else if (cmd == "rm") {
        if (args.size() != 1) {
            return Result::Error("Usage: app rm <name>");
        }
        
        AppManager::remove(args[0]);
        return Result::Ok();
    }
    else {
        return Result::Error("Unknown app command: " + cmd);
    }
}
```

---

### BLE Handler

```cpp
void handleBleCommand(const String& json) {
    cJSON* root = cJSON_Parse(json.c_str());
    
    // Парсим [id, subsystem, cmd, [args]]
    int id = cJSON_GetArrayItem(root, 0)->valueint;
    String subsystem = cJSON_GetArrayItem(root, 1)->valuestring;
    String cmd = cJSON_GetArrayItem(root, 2)->valuestring;
    
    cJSON* argsArray = cJSON_GetArrayItem(root, 3);
    std::vector<String> args;
    
    for (int i = 0; i < cJSON_GetArraySize(argsArray); i++) {
        args.push_back(cJSON_GetArrayItem(argsArray, i)->valuestring);
    }
    
    // Выполняем команду
    Result result = Console::exec(subsystem, cmd, args);
    
    // Формируем ответ
    cJSON* response = cJSON_CreateArray();
    cJSON_AddItemToArray(response, cJSON_CreateNumber(id));
    
    if (result.ok) {
        cJSON_AddItemToArray(response, cJSON_CreateString("ok"));
        cJSON_AddItemToArray(response, result.data ? result.data : cJSON_CreateObject());
    } else {
        cJSON_AddItemToArray(response, cJSON_CreateString("error"));
        cJSON_AddItemToArray(response, cJSON_CreateString(result.error.c_str()));
    }
    
    // Отправляем JSON ответ
    char* json_str = cJSON_Print(response);
    BLE::send(TX_CHAR, json_str);
    free(json_str);
    
    cJSON_Delete(response);
    cJSON_Delete(root);
    
    // Если есть binary data - отправляем через BIN_CHAR
    if (result.hasBinary) {
        BinTransfer::send(result.binaryData);
    }
}
```

---

### Serial Handler

```cpp
void handleSerialCommand(const String& line) {
    // Парсим "subsystem cmd arg1 arg2 "quoted arg""
    std::vector<String> tokens = parseCommandLine(line);
    
    if (tokens.size() < 2) {
        Serial.println("ERROR: Invalid command");
        return;
    }
    
    String subsystem = tokens[0];
    String cmd = tokens[1];
    std::vector<String> args(tokens.begin() + 2, tokens.end());
    
    // Сокращения для sys
    if (subsystem == "ping" || subsystem == "info" || 
        subsystem == "heap" || subsystem == "mem" ||
        subsystem == "log" || subsystem == "ble") {
        
        // Маппинг алиасов
        if (subsystem == "heap" || subsystem == "mem") {
            cmd = "info";
        } else {
            cmd = subsystem;
        }
        
        subsystem = "sys";
        // args остаются как есть
    }
    
    // Выполняем команду
    Result result = Console::exec(subsystem, cmd, args);
    
    // Выводим ответ
    if (result.ok) {
        if (result.data) {
            char* json_str = cJSON_Print(result.data);
            Serial.print("OK: ");
            Serial.println(json_str);
            free(json_str);
        } else {
            Serial.println("OK");
        }
        
        // Если есть binary - отправляем через Serial
        if (result.hasBinary) {
            Serial.write(result.binaryData.data(), result.binaryData.size());
        }
    } else {
        Serial.print("ERROR: ");
        Serial.println(result.error);
    }
}
```

---

### BinTransfer - Универсальный отправщик

```cpp
class BinTransfer {
public:
    static void send(const std::vector<uint8_t>& data);
    static void pump();  // вызывается в loop()
    
private:
    static std::queue<Chunk> queue;
    static bool isSending;
};

void BinTransfer::send(const std::vector<uint8_t>& data) {
    const size_t CHUNK_SIZE = 250;
    uint16_t chunk_id = 0;
    
    // Разбиваем на чанки
    for (size_t i = 0; i < data.size(); i += CHUNK_SIZE) {
        size_t size = std::min(CHUNK_SIZE, data.size() - i);
        
        Chunk chunk;
        chunk.id = chunk_id++;
        chunk.data.insert(chunk.data.end(), data.begin() + i, data.begin() + i + size);
        
        queue.push(chunk);
    }
    
    // End marker
    Chunk end;
    end.id = 0xFFFF;
    queue.push(end);
}

void BinTransfer::pump() {
    if (queue.empty() || isSending) {
        return;
    }
    
    isSending = true;
    
    Chunk chunk = queue.front();
    queue.pop();
    
    // Формируем пакет [chunk_id_low, chunk_id_high, data...]
    uint8_t packet[252];
    packet[0] = chunk.id & 0xFF;
    packet[1] = (chunk.id >> 8) & 0xFF;
    memcpy(packet + 2, chunk.data.data(), chunk.data.size());
    
    BLE::notify(BIN_CHAR, packet, chunk.data.size() + 2);
    
    isSending = false;
}
```

---

## Примеры использования

### BLE - Список приложений

```python
# Python
device.send([1, "app", "list", []])

# Ответ:
[1, "ok", {"count": 3}]

# Binary data:
b"weather\0calculator\0timer\0"
```

---

### Serial - Список приложений

```bash
# Команда:
app list

# Ответ:
OK: {"count": 3}
weather
calculator
timer
```

---

### BLE - Установка переменной

```python
device.send([2, "ui", "set", ["temperature", "23"]])

# Ответ:
[2, "ok", {}]
```

---

### Serial - Установка переменной

```bash
# Команда:
ui set temperature 23

# Ответ:
OK
```

---

### BLE - Системная информация

```python
device.send([3, "sys", "info", []])

# Ответ:
[3, "ok", {
  "dram": 245760,
  "psram": 4194304,
  "fs_used": 102400,
  "fs_total": 1048576
}]
```

---

### Serial - Системная информация (с алиасами)

```bash
# Команды (все эквивалентны):
sys info
info
heap
mem

# Ответ:
OK: {"dram":245760,"psram":4194304,"fs_used":102400,"fs_total":1048576}
```

---

## Events — Unsolicited сообщения [TODO]

Устройство может отправлять сообщения без запроса от клиента. `id = 0` — всегда, это не ответ на запрос.

Формат общий для всех типов ошибок — не только Lua. Любой скриптовый движок (Lua, Brainfuck, будущие) использует тот же формат.

---

### Script error

Отправляется при ошибке скриптового движка. Только если BLE подключен.

```json
← [0, "error", "lua", {
    "app": "nano",
    "msg": "[string \"...\"]:12: attempt to call a nil value",
    "trace": "[string \"...\"]:12: in function 'helper'\n[string \"...\"]:5: in function 'tick'"
  }]
```

Третий элемент — идентификатор движка: `"lua"`, `"bf"`, и т.д.

**Поля:**

| Поле | Обязательно | Описание |
|------|-------------|----------|
| `app` | да | Имя приложения |
| `msg` | да | Сообщение об ошибке (включает номер строки) |
| `trace` | нет | Stack traceback, снизу вверх. Отсутствует если не влез |

---

### Алгоритм формирования сообщения

`MAX_SIZE = 252` (NimBLE max notify payload).

**Шаг 1.** Формируем JSON без `trace`:

```json
[0,"error","lua",{"app":"nano","msg":"[string \"...\"]:12: attempt to call a nil value"}]
```

**Шаг 2.** Измеряем длину `base_len`. Если `base_len > MAX_SIZE` — обрезаем `msg` до 120 байт с `…` на конце. Повторяем замер.

**Шаг 3.** Вычисляем бюджет на trace:

```
budget = MAX_SIZE - base_len - 11
```

`11` = `,"trace":"` (10) + `"` (1) — обёртка поля в JSON.

Если `budget < 30` — отправляем без `trace`.

**Шаг 4.** Заполняем trace в пределах `budget`.

Trace от движка — массив строк от вершины стека к точке входа:

```
[0] [string "..."]:12: in function 'helper'    ← вершина (где упало)
[1] [string "..."]:8: in function 'process'
[2] [string "..."]:5: in function 'tick'        ← точка входа
```

Порядок включения:

1. **Последняя строка** (точка входа) — включаем всегда
2. **Первая строка** (где упало) — включаем если влезает
3. **Остальные от конца к началу** — пока влезают
4. Если пропущены строки в середине — вставляем `\n...` между включёнными

**Пример — всё влезло:**
```
[string "..."]:12: in function 'helper'
[string "..."]:8: in function 'process'
[string "..."]:5: in function 'tick'
```

**Пример — середина не влезла:**
```
[string "..."]:12: in function 'helper'
...
[string "..."]:5: in function 'tick'
```

**Пример — влезла только точка входа:**
```
[string "..."]:5: in function 'tick'
```

**Шаг 5.** Собираем финальный JSON, проверяем `≤ MAX_SIZE`, отправляем.

**Serial:** `EVENT: lua_error {"app":"nano","msg":"..."}`

---


## Roadmap

- [x] v2.0: Унифицированный протокол BLE + Serial
- [x] v2.1: Коды ошибок
- [x] v2.5: Screenshot LZ4 + palette + color formats
- [x] v2.6: Touch Simulation (tap, hold, swipe, type) + sys sync
- [x] v2.7: Widget ID автодетект, click алиас, type с widget ID
- [x] app push через binary channel
- [ ] Компрессия LZ4 для app pull
- [ ] Events: lua_error (v2.4)
- [ ] Batch команды
- [ ] Streaming для больших файлов

---

**Console Protocol v2.7**
