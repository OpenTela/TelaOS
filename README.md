# TelaOS

**Операционная система для умных часов на ESP32**

> Создавайте приложения для носимых устройств за 10 минут. Декларативный UI на HTML-подобном языке + Lua скриптинг. Без перекомпиляции прошивки.

---

## 🎯 Что это?

TelaOS превращает ESP32 в платформу для создания умных часов и носимых устройств, где:

- 📝 **Приложения пишутся на декларативном языке** - похоже на HTML + CSS
- 🔧 **Lua скриптинг для логики** - простой и мощный
- 🔄 **Горячая загрузка** - без перекомпиляции и перепрошивки
- 📱 **Мобильное приложение** - с AI-ассистентом для создания приложений голосом
- 💻 **Desktop IDE** - с real-time эмулятором и USB deployment
- 🌐 **BLE мост для интернета** - HTTP запросы через телефон

---

## ⚡ Быстрый старт

### Пример приложения

```html
<app>
  <ui default="/main">
    <page id="main">
      <label align="center" y="30%" font="72">{count}</label>
      <button x="10%" y="70%" w="35%" onclick="dec">-</button>
      <button x="55%" y="70%" w="35%" onclick="inc">+</button>
    </page>
  </ui>
  
  <state>
    <int name="count" default="0"/>
  </state>
  
  <script language="lua">
    function inc()
      state.count = state.count + 1
    end
    
    function dec()
      state.count = state.count - 1
    end
  </script>
</app>
```

**30 строк кода. 2 минуты работы. Готовое приложение!**

---

## 🚀 Возможности

### Декларативный UI

```html
<!-- Центрированный текст с биндингом -->
<label align="center" y="20%" font="48">{time}</label>

<!-- Кнопка с событием -->
<button y="60%" w="90%" onclick="startTimer">Start</button>

<!-- Input с автофокусом -->
<input bind="userName" onenter="submit" placeholder="Name"/>

<!-- Условная видимость -->
<label visible="{isLoading}">Loading...</label>
```

### CSS Стили

```html
<style>
  button {
    bgcolor: #0066ff;
    radius: 8;
  }
  
  button.primary {
    bgcolor: #00cc00;
  }
  
  button.danger {
    bgcolor: #ff0000;
  }
  
  label.title {
    font: 48;
    color: #ffffff;
  }
</style>

<button class="primary">OK</button>
<button class="danger">Cancel</button>
<label class="title">Settings</label>
```

### Lua API

```lua
-- Состояние
state.temperature = 23
state.isEnabled = true

-- Навигация
navigate("/settings")

-- UI управление
focus("inputField")
setAttr("button1", "bgcolor", "#ff0000")

-- Canvas рисование
canvas.clear("myCanvas", "#000000")
canvas.circle("myCanvas", 120, 120, 50, "#ff0000")
canvas.refresh("myCanvas")

-- HTTP через BLE мост
fetch({
  method = "GET",
  url = "https://api.weather.com/data"
}, function(response)
  state.temp = response.data.temp
end)

-- CSV для данных
local csv = CSV.load("log.csv")
csv:add({timestamp = os.date(), event = "Started"})
csv:save(true)

-- YAML для конфигов
local config = YAML.load("settings.yaml")
local theme = config:get("ui.theme")
config:set("ui.theme", "dark")
config:save()
```

---

## 🛠️ Экосистема

### 1. Мобильное приложение (FutureClock Companion)

- 🎤 **AI-ассистент** - создание приложений голосом
- 📱 **Эмулятор** - тестирование перед загрузкой
- 🔄 **Установка по Bluetooth** - без проводов
- 📺 **Screen mirroring** - трансляция экрана с touch control
- 🌐 **BLE мост** - интернет для часов через телефон

### 2. Desktop IDE

- 💻 **Редактор как VS Code** - подсветка синтаксиса, автодополнение
- ⚡ **Real-time эмулятор** - обновляется при наборе кода
- 🔌 **USB deployment** - отправка на устройство в один клик
- 🚀 **Лаунчер** - запуск всех приложений прямо в эмуляторе
- 🐛 **Отладка** - Lua исполняется в реальном времени

### 3. Само устройство

- ⌚ **ESP32** - dual-core, 240 MHz
- 📺 **LCD дисплей** - 240×240 (круглый или квадратный)
- 🔋 **Автономность** - оптимизация памяти (320KB RAM)
- 📲 **BLE** - связь с телефоном
- 💾 **Файловая система** - LittleFS для приложений

---

## 🏗️ Архитектура

```
┌─────────────────────────────────────┐
│         Приложения (.html)          │
│   Декларативный UI + Lua скрипты    │
├─────────────────────────────────────┤
│          UI Engine (C++)            │
│     HTML/CSS Parser → LVGL          │
├─────────────────────────────────────┤
│       Lua Script Engine             │
│   State, API, Events, Timers        │
├─────────────────────────────────────┤
│         Core System (C++)           │
│  App Manager, Console Protocol v3   │
│  BLE Bridge, Transport Layer        │
├─────────────────────────────────────┤
│      Hardware (ESP32 + LCD)         │
└─────────────────────────────────────┘
```

---

## 📡 Console Protocol v2.7

Унифицированный протокол для BLE и Serial:

```json
// Синхронизация при подключении
[1, "sys", "sync", ["2.7", "2026-02-20T12:00:00Z", "+03:00"]]

// Touch simulation
[2, "ui", "tap", ["120", "160"]]
[3, "ui", "swipe", ["left"]]
[4, "ui", "type", ["Hello World"]]

// Управление приложениями
[5, "app", "run", ["weather"]]
[6, "app", "push", ["timer", "app.html", 9924]]

// Скриншот
[7, "sys", "screen", ["rgb16", "2"]]
```

**Документация:** [CONSOLE_PROTOCOL_SPEC_v2_7.md](docs/CONSOLE_PROTOCOL_SPEC_v2_7.md)

---

## 📚 Примеры приложений

### Погода (с интернетом через BLE)

```html
<app>
  <system><bluetooth/></system>
  
  <ui default="/main">
    <page id="main">
      <label align="center" y="30%" font="72">{temp}°C</label>
      <label align="center" y="50%">{city}</label>
      <button y="80%" onclick="update">Refresh</button>
    </page>
  </ui>
  
  <state>
    <string name="temp" default="--"/>
    <string name="city" default="Loading..."/>
  </state>
  
  <script language="lua">
    function update()
      fetch({
        method = "GET",
        url = "https://api.openweathermap.org/data/2.5/weather?q=Moscow&appid=KEY"
      }, function(response)
        if response.status == 200 then
          local data = json.parse(response.body)
          state.temp = tostring(math.floor(data.main.temp - 273.15))
          state.city = data.name
        end
      end)
    end
    
    update()
  </script>
</app>
```

### Таймер с сохранением

```html
<app>
  <ui default="/main">
    <page id="main">
      <label align="center" y="40%" font="72">{time}</label>
      <button y="70%" w="42%" onclick="start">Start</button>
      <button x="52%" y="70%" w="42%" onclick="stop">Stop</button>
    </page>
  </ui>
  
  <state>
    <string name="time" default="00:00"/>
    <bool name="running" default="false"/>
  </state>
  
  <timer interval="1000" call="tick"/>
  
  <script language="lua">
    local seconds = 0
    
    function tick()
      if state.running then
        seconds = seconds + 1
        local m = math.floor(seconds / 60)
        local s = seconds % 60
        state.time = string.format("%02d:%02d", m, s)
      end
    end
    
    function start()
      state.running = true
    end
    
    function stop()
      state.running = false
      -- Сохранить в лог
      local csv = CSV.load("data/sessions.csv")
      csv:add({date = os.date("%Y-%m-%d"), duration = seconds})
      csv:save(true)
    end
  </script>
</app>
```

**Больше примеров:** [examples/](examples/)

---

## 🧪 Тестирование

```bash
# Сборка и запуск тестов
cd tests
make test
```

### Test Results

- ✅ **test_calc:** 11/11 - Calculator UI test
- ✅ **test_ids:** 7/7 - HTML ID processing
- ✅ **test_styles:** 9/9 - CSS styles parsing
- ✅ **test_bf:** 3/3 - Brainfuck engine

**Total: 30 assertions, ALL PASSED**

---

## 📦 Сборка

### Требования

- PlatformIO или Arduino IDE
- ESP32 board (ESP32-S3 рекомендуется)
- GC9A01 или ST7789 дисплей (240×240)

### Сборка через PlatformIO

```bash
git clone https://github.com/OpenTella/TelaOS.git
cd TelaOS
pio run
pio run --target upload
```

### Конфигурация

Отредактируйте `platformio.ini`:

```ini
[env:esp32]
platform = espressif32
board = esp32-s3-devkitc-1
framework = arduino

build_flags = 
    -DCORE_DEBUG_LEVEL=3
    -DBOARD_HAS_PSRAM
    
lib_deps = 
    lvgl/lvgl@^8.3.0
    bblanchon/ArduinoJson@^6.21.0
```

---

## 📖 Документация

### Спецификации

- [UI HTML Spec v0.3](docs/ui_html_spec.md) - Декларативный UI язык
- [Console Protocol v2.7](docs/CONSOLE_PROTOCOL_SPEC_v2_7.md) - BLE/Serial протокол
- [Project Rules](docs/PROJECT_RULES.md) - Структура и соглашения

### Гайды
(TODO)
- [Getting Started](docs/GETTING_STARTED.md) - Первое приложение за 5 минут
- [API Reference](docs/API_REFERENCE.md) - Полный справочник Lua API
- [BLE Bridge](docs/BLE_BRIDGE.md) - Интернет через телефон
- [CRTP Mappable](docs/CRTP_MAPPABLE_GUIDE.md) - C++ паттерн для CSV

---

## 🎯 Roadmap

### v1.0 (Текущая)

- [x] Декларативный UI движок
- [x] Lua скриптинг
- [x] BLE Protocol v2.7
- [x] Touch simulation
- [x] CSV/YAML API
- [x] Мобильное приложение
- [x] Desktop IDE

### v1.1 (Планируется)

- [ ] OTA обновления
- [ ] Возможность делиться приложениями друг с другом
- [ ] Больше виджетов
- [ ] Анимации и переходы
- [ ] WebSocket поддержка

### v2.0 (Будущее)

- [ ] Multi-tasking
- [ ] Уведомления от телефона
- [ ] Голосовой ассистент
- [ ] Watchface редактор

---

## 🤝 Участие в разработке

Мы приветствуем ваш вклад!

1. Fork проекта
2. Создайте feature branch (`git checkout -b feature/amazing-feature`)
3. Commit изменения (`git commit -m 'Add amazing feature'`)
4. Push в branch (`git push origin feature/amazing-feature`)
5. Откройте Pull Request

### Области для участия

- 📝 Документация и туториалы
- 🐛 Исправление багов
- ✨ Новые виджеты и фичи
- 🌍 Переводы
- 💡 Примеры приложений
- 🧪 Тесты

---

## 📄 Лицензия

LGPL License - см. [LICENSE](LICENSE.txt)

---

## 🙏 Благодарности

- **LVGL** - отличная UI библиотека
- **Lua** - мощный скриптовый язык
- **ArduinoJson** - JSON парсинг
- **Anthropic Claude** - ассистирование в разработке

---

## 📞 Контакты

- 💬 Telegram: [@username]
- 📧 Email: contact@telaos.org
- 🌐 Website: https://telaos.org
- 📺 YouTube: [TelaOS Channel]

---

## ⭐ Поддержите проект

Если TelaOS полезен для вас, поставьте звезду на GitHub!

---

**TelaOS - Операционная система, которая делает ESP32 умнее! 🚀**
