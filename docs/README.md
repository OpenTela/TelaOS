# Evolution OS

Декларативная платформа для создания приложений на ESP32 с HTML-подобным синтаксисом и Lua скриптами.

## Концепция

Evolution OS — операционная система для умных часов и носимых устройств на базе ESP32. Главная идея — **простота создания приложений**. Разработчик описывает интерфейс в декларативном XML-подобном формате, логику на Lua, а система автоматически:

- Парсит разметку и создаёт виджеты LVGL
- Связывает данные (state) с UI элементами
- Выполняет Lua скрипты в песочнице
- Обновляет интерфейс при изменении состояния

**Философия:** Писать приложение для часов должно быть так же просто, как создать HTML страницу.

## Архитектура

```
┌─────────────────────────────────────────────────────────────┐
│                      Launcher                                │
│              (часы, дата, иконки приложений)                 │
└─────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────┐
│                    App Manager                               │
│         (загрузка, запуск, выгрузка приложений)              │
└─────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────┐
│                    Application                               │
│                    ({name}.bax)                               │
├──────────┬──────────┬──────────┬──────────┬─────────────────┤
│   <ui>   │ <state>  │ <script> │ <style>  │    <timer>      │
└──────────┴──────────┴──────────┴──────────┴─────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────┐
│                      LVGL 9.x                                │
└─────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────┐
│                    ESP-IDF 5.x / ESP32(-S3)                    │
│              HAL: мультиплатформенная абстракция              │
└─────────────────────────────────────────────────────────────┘
```

## Структура проекта

```
TelaOS/
├── src/
│   ├── main.cpp              # Точка входа
│   ├── core/                 # Ядро: app_manager, state_store, script_manager
│   ├── ui/                   # UI движок: html_parser, css_parser, launcher
│   ├── widgets/              # Виджеты LVGL
│   ├── engines/lua/          # Lua: engine, timer, fetch, system, ui
│   ├── console/              # Протокол команд (Serial + BLE транспорт)
│   ├── ble/                  # BLE bridge, binary transfer/receive
│   ├── hal/                  # HAL абстракция
│   │   ├── device.h/cpp      # Абстрактный класс
│   │   ├── display_hal.h/cpp # Общий LVGL setup
│   │   └── boards/           # esp4848s040, esp8048w550, twatch2020
│   ├── native/               # Нативные компоненты (shade, etc.)
│   ├── utils/                # Утилиты: log, strings, fonts, screenshot
│   └── font/                 # Шрифты Ubuntu 16/32/48/72px
├── data/
│   ├── apps/                 # 27 приложений на LittleFS
│   │   ├── calculator/
│   │   │   ├── calculator.bax
│   │   │   └── icon.png
│   │   └── weather/
│   └── system/               # Системные ресурсы (иконки категорий)
├── include/                  # Конфиги: lv_conf.h, ui_layout.h
├── docs/                     # Документация
├── scripts/                  # Build-скрипты (resize_icons, embedded_icons)
├── tools/                    # BLE assistant (Python)
└── platformio.ini
```

## Формат приложения

Каждое приложение — файл `{name}.bax` в папке `/data/apps/{name}/`.

```xml
<app>
  <config>
    <network/>              <!-- опционально: доступ в сеть через BLE bridge -->
    <display buffer="small"/>
  </config>
  
  <ui default="/main">
    <!-- страницы и виджеты -->
  </ui>
  
  <state>
    <int name="count" default="0"/>
  </state>
  
  <timer interval="1000" call="tick"/>
  
  <script language="lua">
    function tick()
      state.count = state.count + 1
    end
  </script>
  
  <style>
    button { bgcolor: #333; radius: 8; }
  </style>
</app>
```

Полная спецификация виджетов, state, биндингов, событий и Lua API — в **ui_html_spec.md**.

### Секция `<config>`

Системные настройки.

**`<network/>`** — включить доступ в интернет (HTTP запросы через BLE bridge к телефону).

| Синтаксис | Результат |
|-----------|-----------|
| Нет тега | Сеть отключена, RAM свободна |
| `<network/>` | Включена сразу |
| `<network mode="always"/>` | То же самое |
| `<network mode="ondemand"/>` | Включится при первом `fetch()` |


**`<display buffer="..."/>`** — размер буфера отрисовки.

| Значение | Линии (480px) | Описание |
|----------|---------------|----------|
| `micro` | 15 (~14 KB) | Аварийный |
| `small` | 30 (~28 KB) | Компактный, для сетевых приложений |
| `optimal` | 60 (~56 KB) | **По умолчанию** |
| `max` | 120 (~112 KB) | Для тяжёлых анимаций |

## Примеры приложений

### Простой счётчик

```xml
<app>
  <ui default="/main">
    <page id="main">
      <label align="center" y="40%" color="#fff" font="72">{count}</label>
      <button x="20%" y="60%" w="60%" h="50" bgcolor="#3498db" onclick="increment">
        +1
      </button>
    </page>
  </ui>
  
  <state>
    <int name="count" default="0"/>
  </state>
  
  <script language="lua">
    function increment()
      state.count = state.count + 1
    end
  </script>
</app>
```

### Pomodoro таймер

```xml
<app>
  <ui default="/main">
    <group id="main" default="timer" orientation="horizontal" indicator="dots">
      <page id="timer">
        <label align="center" y="8%" color="#e74c3c" font="32">{mode}</label>
        <label align="center" y="25%" color="#fff" font="72">{timeDisplay}</label>
        <button x="5%" y="68%" w="28%" h="45" bgcolor="#2ecc71" onclick="startTimer">Start</button>
        <button x="36%" y="68%" w="28%" h="45" bgcolor="#f39c12" onclick="pauseTimer">Pause</button>
        <button x="67%" y="68%" w="28%" h="45" bgcolor="#e74c3c" onclick="resetTimer">Reset</button>
      </page>
      
      <page id="settings">
        <label align="center" y="5%" color="#fff" font="32">Settings</label>
        <label x="5%" y="18%" color="#888">Work: {workMins} min</label>
        <slider x="5%" y="25%" w="90%" min="1" max="60" bind="workMins"/>
      </page>
    </group>
  </ui>
  
  <state>
    <string name="mode" default="WORK"/>
    <string name="timeDisplay" default="25:00"/>
    <int name="workMins" default="25"/>
    <int name="timeLeft" default="1500"/>
    <bool name="running" default="false"/>
  </state>
  
  <timer interval="1000" call="tick"/>
  
  <script language="lua">
    function formatTime(seconds)
      local m = math.floor(seconds / 60)
      local s = seconds % 60
      return string.format('%02d:%02d', m, s)
    end
    
    function tick()
      if not state.running then return end
      state.timeLeft = state.timeLeft - 1
      state.timeDisplay = formatTime(state.timeLeft)
      if state.timeLeft <= 0 then
        state.mode = 'BREAK'
        state.timeLeft = 300
      end
    end
    
    function startTimer() state.running = true end
    function pauseTimer() state.running = false end
    
    function resetTimer()
      state.running = false
      state.mode = 'WORK'
      state.timeLeft = state.workMins * 60
      state.timeDisplay = formatTime(state.timeLeft)
    end
  </script>
</app>
```

### Canvas-игра (Flappy Bird)

```xml
<app>
  <ui default="/game">
    <page id="game" bgcolor="#87CEEB">
      <canvas id="screen" x="5%" y="10%" w="350" h="280"/>
      <label align="center" y="80%" color="#000">Score: {score}</label>
      <button x="5%" y="91%" w="43%" h="40" bgcolor="#f60" onclick="jump">JUMP</button>
      <button x="52%" y="91%" w="43%" h="40" bgcolor="#0a0" onclick="startGame">START</button>
    </page>
  </ui>
  
  <state>
    <int name="score" default="0"/>
    <float name="birdY" default="140"/>
    <float name="birdVel" default="0"/>
    <int name="pipeX" default="400"/>
    <int name="pipeGap" default="120"/>
    <bool name="running" default="false"/>
  </state>
  
  <timer interval="33" call="tick"/>
  
  <script language="lua">
    local W, H = 350, 280
    local GRAVITY, JUMP = 0.8, -10
    
    function render()
      canvas.clear("screen", 0x87CEEB)
      canvas.rect("screen", 0, H - 20, W, 20, 0x8B4513)
      local gy = state.pipeGap
      canvas.rect("screen", state.pipeX, 0, 50, gy - 40, 0x00AA00)
      canvas.rect("screen", state.pipeX, gy + 40, 50, H, 0x00AA00)
      canvas.rect("screen", 50, state.birdY - 10, 20, 20, 0xFFFF00)
    end
    
    function tick()
      if not state.running then return end
      state.birdVel = state.birdVel + GRAVITY
      state.birdY = state.birdY + state.birdVel
      state.pipeX = state.pipeX - 4
      if state.pipeX < -50 then
        state.pipeX = W
        state.pipeGap = math.random(60, H - 80)
        state.score = state.score + 1
      end
      if state.birdY < 10 or state.birdY > H - 30 then
        state.running = false
      end
      render()
    end
    
    function jump()
      if state.running then state.birdVel = JUMP end
    end
    
    function startGame()
      state.score = 0
      state.birdY = 140
      state.birdVel = 0
      state.pipeX = 400
      state.running = true
      render()
    end
    
    render()
  </script>
</app>
```

## Иконки приложений

Каждое приложение может иметь иконку `icon.png`:

- **Размер:** 64×64 пикселей (авторесайз build-скриптом)
- **Формат:** PNG с прозрачностью
- **Расположение:** `{app_folder}/icon.png`

Иконки встраиваются в прошивку при сборке (flash). Подробнее — в **BUILD_ICONS.md**.

## Ограничения

1. **Шрифты фиксированы** — только 16, 32, 48, 72px (Ubuntu)
2. **Нет анимаций** — только мгновенные изменения state
3. **Сеть через BLE** — HTTP запросы идут через телефон (BLE bridge), нет прямого WiFi
4. **Для игр используйте Canvas** — динамическое позиционирование через canvas API

## Сборка и загрузка

```bash
pio run                    # Сборка
pio run -t upload          # Загрузка прошивки
pio run -t uploadfs        # Загрузка приложений на LittleFS
pio device monitor         # Serial монитор
```

## Документация

| Документ | Описание |
|----------|----------|
| **ui_html_spec.md** | Полная спецификация UI: виджеты, state, биндинги, Lua API |
| **CONSOLE_PROTOCOL_SPEC_v2.7.md** | BLE/Serial протокол команд |
| **native_app_spec_v0.2.md** | Система виджетов для нативных приложений |
| **BUILD_ICONS.md** | Система иконок и build-скрипты |
| **COMPILER_README.md** | Компилятор и тесты без ESP32 |
| **AI_DEBUG_METHODOLOGY.md** | Методология отладки |
| **RULE_versioning.md** | Версионирование модулей |

## Roadmap

- [x] UI движок (HTML/CSS парсинг, LVGL рендеринг)
- [x] Lua скрипты в песочнице
- [x] State с двусторонними биндингами
- [x] Таймеры, динамические классы
- [x] Лаунчер с иконками
- [x] Canvas API (2D графика)
- [x] BLE bridge (HTTP через телефон)
- [x] HAL абстракция (мультиплатформенность)
- [ ] NVS для сохранения настроек
- [ ] OTA обновления

---

MIT License
