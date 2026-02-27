# Console Protocol v2.7 — Справка для эмулятора

Единый набор команд, два транспорта. Эмулятор должен реализовать оба.

---

## Транспорты

### BLE (Web Bluetooth)

**GATT Service:** `12345678-1234-5678-1234-56789abcdef0`

| Characteristic | UUID | Direction | Тип |
|---|---|---|---|
| TX | `...def1` | device → client | Notify (JSON text) |
| RX | `...def2` | client → device | Write (JSON text) |
| BIN | `...def3` | device ↔ client | Notify + Write (binary chunks) |

**Запрос (client → RX):**
```json
[id, "subsystem", "cmd", ["arg1", "arg2"]]
```

**Ответ OK (TX → client):**
```json
[id, "ok", {"key": "value", ...}]
```

**Ответ Error (TX → client):**
```json
[id, "error", {"code": "not_found", "message": "App not found"}]
```

**Ответ с payload (TX + BIN):**
```json
[id, "ok", {"bytes": 14320, "type": "binary"}]
```
Затем данные идут через BIN characteristic чанками.

**Fetch от устройства (TX → client, HTTP proxy):**
```json
{"id": 5, "method": "GET", "url": "https://api.example.com/data"}
```
Клиент выполняет HTTP, отвечает:
```json
{"id": 5, "status": 200, "body": "..."}
```

### Serial (USB / WebSerial)

**Baud:** 115200

**Запрос:**
```
subsystem cmd arg1 arg2 "quoted arg with spaces"
```

Подсистема `sys` — по умолчанию. Одиночные команды без префикса идут в `sys`:
```
ping          → sys ping
info          → sys info
reboot        → sys reboot
```

`log` — отдельная подсистема, работает напрямую:
```
log verbose   → log verbose    (subsystem=log, cmd=verbose)
log ui debug  → log ui debug   (subsystem=log, cmd=ui, arg=debug)
```

**Ответ OK:**
```
[id] OK
  key1 = value1
  key2 = "string value"
```

**Ответ Error:**
```
[id] ERROR [code]: message
```

**Ответ с binary payload:**
```
[id] OK
  bytes = 14320
  [binary: 14320 bytes]
  base64: iVBORw0KGgo...
```

**Ответ с string payload (binary `\0`-separated → отображается как `\n`):**
```
[id] OK
  count = 19
  [string: 140 bytes]
anki
calculator
clicker
...
```

---

## BIN Channel (Binary Transfer)

Используется для больших данных: скриншоты, файлы приложений, списки.

### Device → Client (BinTransfer)

**Chunk format:** `[2B chunk_id LE][до 250B data]`

- `chunk_id` — uint16 little-endian, начинается с 0, инкрементируется
- Максимум 250 байт данных в чанке (MTU 252 - 2B header)
- Размер данных известен из JSON ответа (поле `bytes`)
- Трансфер завершён когда сумма полученных байт == `bytes`

**Пример:** скриншот 14320 bytes → 58 чанков по 250B + последний 70B

### Client → Device (BinReceive)

Тот же формат чанков. Используется для `app push`.

- Клиент сначала отправляет команду push через RX
- Устройство готовит буфер, отвечает OK
- Клиент шлёт чанки через BIN_CHAR
- Устройство валидирует и сохраняет

---

## Команды

### ui — управление приложением

| Команда | Аргументы | Ответ data | Описание |
|---|---|---|---|
| `set` | var, value | `{}` | Установить state переменную |
| `get` | var | `{"name": "...", "value": "..."}` | Получить значение |
| `nav` | page | `{}` | Навигация на страницу |
| `text` | widgetId, value | `{}` | Установить текст виджета |
| `call` | funcName, [args...] | `{}` | Вызвать Lua функцию |
| `notify` | title, message | `{}` | Показать уведомление |
| `tap` | x, y | `{}` | Тап по координатам |
| `tap` | widgetId | `{}` | Тап по виджету (onclick → fallback координаты) |
| `hold` | x, y, [ms] | `{}` | Удержание (default 500ms) |
| `hold` | widgetId, [ms] | `{}` | Удержание по виджету |
| `swipe` | direction, [speed] | `{}` | Свайп: left/right/up/down (l/r/u/d), speed: fast/slow |
| `swipe` | x1, y1, x2, y2, [ms] | `{}` | Свайп по координатам (default 300ms) |
| `type` | [widgetId], text | `{}` | Ввести текст в input (widgetId опционален) |

Алиас: `click` = `tap`

### sys — системные

| Команда | Аргументы | Ответ data | Описание |
|---|---|---|---|
| `ping` | — | `{}` | Проверка связи |
| `info` | — | heap, psram, chip, freq, buf_lines, ble | Системная информация |
| `reboot` | — | — | Перезагрузка |
| `screen` | [color], [scale], [mode] | `{w, h, color, format, raw_size}` + BIN | Скриншот |
| `time` | [epoch_seconds] | `{time}` | Получить/установить время |
| `sync` | protocol_ver, datetime_iso, timezone | `{protocol, os}` | Синхронизация времени и версий |

**screen** аргументы:
- `color`: `rgb16` (default), `bw`, `gray`, `pal`
- `scale`: 0 = без масштабирования (default), 2, 4, ...
- `mode`: `fixed` (default) — фиксированный scale; `tiny` — автоподбор scale чтобы влезть в один BLE фрейм
- **Формат данных: LZ4** — payload всегда сжат LZ4. `raw_size` = размер до сжатия, `bytes` = размер после
- BLE: данные через BIN characteristic
- Serial: base64

### app — приложения

| Команда | Аргументы | Ответ data | Описание |
|---|---|---|---|
| `list` | — | `{count}` + binary payload | Список имён (`\0`-separated) |
| `info` | name | `{name, size, files}` | Информация: файлы и размеры (`files: {"file": size}`) |
| `pull` | name, [file] | `{name, file, size}` + BIN | Скачать файл (default: {name}.bax) |
| `pull` | name, `*` | `{name, files}` + BIN | Скачать все файлы (конкатенированные) |
| `push` | name, [file], size | `{name, file, size}` | Загрузить файл (file default: {name}.bax) |
| `push` | name, `*`, files_json | `{name, files, size}` | Multi-file push |
| `delete` / `rm` | name | `{}` | Удалить приложение |
| `launch` / `run` | name | `{}` | Запустить приложение |
| `home` | — | `{}` | Вернуться в launcher |

**app list** — binary payload содержит имена, каждое завершается `\0` (null-terminated strings):
```
anki\0calculator\0clicker\0...
```

**app pull** — без аргумента отдаёт `{name}.bax`:
```
app pull weather
```
Ответ: `{name: "weather", file: "weather.bax", size: 1984}` + binary payload.

С конкретным файлом:
```
app pull weather icon.png
```
Ответ: `{name: "weather", file: "icon.png", size: 512}` + binary payload.

С `*` — все файлы приложения, конкатенированные:
```
app pull weather *
```
Ответ: `{name: "weather", files: {"weather.bax": 1984, "icon.png": 512}}` + binary blob.

**app push (single file):**
```json
[1, "app", "push", ["myapp", "1984"]]
```
Ответ: `[1, "ok", {"name":"myapp", "file":"myapp.bax", "size":1984}]` → клиент шлёт чанки через BIN.

С явным именем файла:
```json
[1, "app", "push", ["myapp", "calc.bax", "1984"]]
```

Порядок file/size гибкий — код определяет по наличию цифр и расширения.

**app push (multi-file):**
```json
[1, "app", "push", ["myapp", "*", "{\"myapp.bax\":1984,\"icon.png\":512}"]]
```
Ответ: `[1, "ok", {"name":"myapp", "files":{"myapp.bax":1984,"icon.png":512}, "size":2496}]`

Клиент конкатенирует все файлы в один blob и шлёт чанками.

### log — логирование

| Команда | Аргументы | Ответ | Описание |
|---|---|---|---|
| `show` | — | текущие уровни | Показать уровни |
| `<level>` | — | `{}` | Все категории → level |
| `<category>` `<level>` | — | `{}` | Конкретная категория |

**Категории:** `ui`, `lua`, `state`, `app`, `ble`
**Уровни:** `disabled`, `error`, `warn`, `info`, `debug`, `verbose`

---

## Коды ошибок

| Code | Описание |
|---|---|
| `invalid` | Неизвестная команда, плохие аргументы |
| `not_found` | Приложение/переменная не найдена |
| `offline` | Нет интернета |
| `timeout` | Сервер не ответил |
| `server` | HTTP 5xx |
| `denied` | HTTP 401/403 |
| `busy` | Ресурс занят |
| `memory` | Нет памяти |

---

## Пример: полная сессия

### BLE

```
→ [1, "sys", "ping", []]
← [1, "ok", {}]

→ [2, "app", "list", []]
← [2, "ok", {"count": 19, "bytes": 140, "type": "binary"}]
← BIN: [0x00,0x00, "anki\0calculator\0clicker\0..."]

→ [3, "sys", "info", []]
← [3, "ok", {"heap_free":207904, "heap_min":70232, "psram_free":7845855, "psram_total":8375831, "chip":"ESP32-S3", "freq":240, "buf_lines":120, "ble":"on"}]

→ [4, "app", "run", ["pomodoro"]]
← [4, "ok", {}]

→ [5, "ui", "set", ["brightness", "80"]]
← [5, "ok", {}]

→ [6, "ui", "tap", ["120", "160"]]
← [6, "ok", {}]

→ [7, "sys", "screen", ["rgb16", "2"]]
← [7, "ok", {"w":240, "h":240, "color":"rgb16", "format":"lz4", "raw_size":115200, "bytes":14400, "type":"binary"}]
← BIN: 58 chunks × 250B
```

### Serial (те же команды)

```
> sys ping
[1] OK

> app list
[2] OK
  count = 19
  [string: 140 bytes]
anki
calculator
clicker
...

> sys info
[3] OK
  heap_free = 207904
  heap_min = 70232
  psram_free = 7845855
  ...

> app run pomodoro
[4] OK

> ui set brightness 80
[5] OK

> ui tap 120 160
[6] OK
```

---

## Для эмулятора: что реализовать

### Минимум (MVP)
1. **WebSerial** — подключение по USB, отправка текстовых команд, парсинг ответов
2. **Web Bluetooth** — подключение по BLE, отправка JSON массивов, получение ответов + BIN chunks
3. **BinTransfer receive** — сборка чанков в буфер по chunk_id, завершение по достижению `bytes`
4. **Базовые команды:** `sys ping`, `sys info`, `app list`, `app run`, `app home`, `ui set`/`get`

### Полная реализация
5. **BinReceive (push)** — отправка чанков для `app push`
6. **Screenshot** — `sys screen` + LZ4 декомпрессия + декодирование (RGB565/BW/gray/palette)
7. **Touch simulation** — `ui tap`, `ui swipe`, `ui hold`
8. **HTTP proxy** — перехват fetch-запросов от устройства, выполнение на стороне клиента
