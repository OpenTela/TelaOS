# Console Protocol Specification v2.4

Унифицированный протокол для Evolution OS / Blank OS - работает через BLE и Serial.

## Changelog

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

**Примеры (BLE):**
```json
[1, "ui", "set", ["temperature", "23"]]
[2, "ui", "get", ["temperature"]]
[3, "ui", "nav", ["settings"]]
[4, "ui", "call", ["updateWeather"]]
[5, "ui", "call", ["setBrightness", "50"]]
```

**Примеры (Serial):**
```
ui set temperature 23
ui get temperature
ui nav settings
ui call updateWeather
ui call setBrightness 50
```

---

### sys - Системные команды

| Команда | Аргументы | Ответ | Описание |
|---------|-----------|-------|----------|
| `ping` | — | `{}` | Проверка связи |
| `info` | — | `{"dram":N,"psram":N,"fs_used":N,"fs_total":N}` | Информация о системе |
| `screen` | color, scale | `{}` → BIN_CHAR | Захват скриншота |
| `bt` / `ble` | on / off | `{}` | Управление Bluetooth |
| `log` | [category] [level] | `{}` | Управление логированием |

**Примеры (BLE):**
```json
[1, "sys", "ping", []]
[2, "sys", "info", []]
[3, "sys", "screen", ["rgb16", "2"]]
[4, "sys", "ble", ["off"]]
[5, "sys", "log", ["ui", "verbose"]]
[6, "sys", "log", ["verbose"]]  // все категории
```

**Примеры (Serial):**
```
ping                    # сокращение для sys ping
info                    # сокращение для sys info
heap                    # алиас для sys info
mem                     # алиас для sys info
sys screen rgb16 2
ble off                 # сокращение для sys ble off
log ui verbose
log verbose             # все категории
```

---

### app - Управление приложениями

| Команда | Аргументы | Ответ | Описание |
|---------|-----------|-------|----------|
| `list` | — | `{"count":N}` → BIN_CHAR | Список приложений |
| `run` | name | `{}` | Запустить приложение |
| `info` | name | `{"title":"...","size":N,"files":[...]}` | Информация о приложении |
| `pull` | name, [file] | `{"size":N,"file":"..."}` → BIN_CHAR | Скачать файл |
| `push` | — | не реализовано | Загрузить файл |
| `rm` | name | `{}` | Удалить приложение |

**Примечания:**
- `app pull weather` - без указания файла отдаёт `app.html` (по умолчанию)
- `app pull weather icon.png` - явное указание файла
- `app push` не реализовано (пока)

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
3. **sys screen** - скриншот

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
→ [3, "sys", "screen", ["rgb16", "2"]]
```

**Ответ (JSON):**
```json
← [3, "ok", {"w": 240, "h": 240, "format": "rgb16"}]
```

**Binary data (BIN_CHAR):**
```
[0x00, 0x00][250 bytes pixel data]
[0x01, 0x00][250 bytes pixel data]
...
[0xFF, 0xFF]  // end marker
```

---

## Реализация

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
        if (args.size() != 2) {
            return Result::Error("Usage: sys screen <color> <scale>");
        }
        
        String color = args[0];  // rgb16, rgb8, gray, bw
        String scale = args[1];  // 0, 2, tiny
        
        // Захватываем скриншот
        std::vector<uint8_t> pixels = Screenshot::capture(color, scale);
        
        // Метаданные
        cJSON* meta = cJSON_CreateObject();
        cJSON_AddNumberToObject(meta, "w", Screenshot::width);
        cJSON_AddNumberToObject(meta, "h", Screenshot::height);
        cJSON_AddStringToObject(meta, "format", color.c_str());
        
        return Result::Binary(pixels, meta);
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
- [ ] app push через binary channel
- [ ] Компрессия LZ4 для app pull
- [ ] Events: lua_error (v2.4)
- [ ] Batch команды
- [ ] Streaming для больших файлов

---

**Console Protocol v2.4**