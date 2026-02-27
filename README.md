# TelaOS

**Smartwatch OS where apps are just HTML + Lua.**

Write a complete app in a single `.bax` file — declarative UI, reactive state, scripting — and it runs natively on an ESP32 with LVGL rendering.

```xml
<app>
  <ui default="/main">
    <page id="main">
      <label align="center" y="30%" color="#fff" font="72">{count}</label>
      <button align="center" y="55%" w="60%" h="50" bgcolor="#3498db" onclick="add">+1</button>
    </page>
  </ui>

  <state>
    <int name="count" default="0"/>
  </state>

  <script language="lua">
    function add()
      state.count = state.count + 1
    end
  </script>
</app>
```

That's a working app. Deploy it over BLE, it appears in the launcher with an icon.

---

## What's inside

**27 apps** ship with the OS: calculator, weather, paint, snake, flappy bird, pomodoro, excel spreadsheet, crosswords, crypto tracker, dice, and more — all written in the same HTML+Lua format.

**The stack:**

```
  .bax app (HTML + CSS + Lua)
         ↓
  UI Engine ── parses markup, builds widget tree
  State    ── reactive bindings, two-way sync
  Lua VM   ── sandboxed scripts, timers, canvas API
  CSS      ── tag/class/id selectors, cascade
         ↓
  LVGL 9.2 ── native rendering, 60fps
         ↓
  ESP32-S3 ── 240MHz, 8MB PSRAM, 480×480 touch display
```

## Hardware

Runs on ESP32 boards with LVGL-compatible displays:

| Board | Display | Touch | Status |
|-------|---------|-------|--------|
| ESP-4848S040 | 480×480 RGB, ST7701 | GT911 | Primary |
| ESP-8048W550 | 800×480 RGB | GT911 | Supported |
| T-Watch 2020 | 240×240 SPI | FT6336 | Supported |

Adding a new board = one HAL file (~100 lines). Display, touch, backlight, buttons.

## App format

A `.bax` file is self-contained — UI, state, logic, styles:

```xml
<app>
  <system>
    <bluetooth/>           <!-- optional: internet via BLE bridge -->
  </system>

  <ui default="/main">
    <group id="main" orientation="horizontal" indicator="dots">
      <page id="home">...</page>
      <page id="settings">...</page>
    </group>
  </ui>

  <state>
    <string name="city" default="Moscow"/>
    <int name="temp" default="0"/>
    <bool name="loading" default="false"/>
  </state>

  <timer interval="1000" call="tick"/>

  <script language="lua">
    function tick()
      -- your logic here
    end
  </script>

  <style>
    button { bgcolor: #333; radius: 8; }
    .primary { bgcolor: #0066ff; color: #fff; }
  </style>
</app>
```

### Widgets

`label`, `button`, `slider`, `switch`, `input`, `canvas`, `image` — with positioning (`x`, `y`, `w`, `h` in px or %), alignment (`align="center center"`), colors, fonts (16/32/48/72px).

### Bindings

Reactive — change `state.count` and every `{count}` in the UI updates automatically. Works in text content, `bgcolor`, `color`, `visible`, and `class` attributes.

```xml
<label color="{statusColor}">{statusText}</label>
<button class="{btnClass}" onclick="toggle">{btnLabel}</button>
<label visible="{isLoading}">Loading...</label>
```

### Lua API

```lua
state.varName = "value"          -- reactive state
navigate("/settings")            -- page navigation
focus("inputId")                 -- keyboard focus
setAttr("btn1", "bgcolor", "#f00")  -- imperative styling
canvas.rect("c", 10, 10, 50, 50, "#ff0000")  -- 2D drawing
fetch({url="https://..."}, function(r) ... end) -- HTTP via BLE
```

### CSS

Tag, class, compound, and ID selectors with specificity cascade:

```css
button { bgcolor: #333; radius: 8; }        /* tag */
.primary { bgcolor: #06f; color: #fff; }    /* class */
button.danger { bgcolor: #e74c3c; }         /* tag.class */
#header { font: 48; }                       /* id */
```

## Connectivity

Apps access the internet through a BLE bridge — a Python script on your phone or PC proxies HTTP requests:

```lua
fetch({
  method = "GET",
  url = "https://api.openweathermap.org/data/2.5/weather?q=Moscow"
}, function(response)
  local data = json.parse(response.body)
  state.temp = math.floor(data.main.temp - 273)
end)
```

The bridge also enables remote control — deploy apps, take screenshots, simulate touch, manage state — all over BLE or Serial with a unified command protocol.

## Build & deploy

```bash
pio run                    # build firmware
pio run -t upload          # flash firmware
pio run -t uploadfs        # flash apps to LittleFS
```

Deploy individual apps over BLE without reflashing:

```bash
python tools/ble_assistant.py
assistant> app push myapp
```

## Project structure

```
src/
├── core/           # app manager, state store, script engine
├── ui/             # HTML parser, CSS parser, widget builder, launcher
├── engines/lua/    # Lua VM, timers, fetch, canvas bindings
├── console/        # command protocol (Serial + BLE transport)
├── ble/            # BLE bridge, binary transfer
├── hal/            # hardware abstraction (per-board drivers)
├── widgets/        # LVGL widget wrappers
└── utils/          # logging, fonts, screenshots

data/apps/          # 27 bundled apps
docs/               # specs & guides
tools/              # BLE assistant (Python)
scripts/            # build scripts (icons, resources)
```

~12K lines of C++. ~18K with apps.

## Memory architecture

LVGL objects live in PSRAM (8MB). DRAM is reserved for fast draw operations (custom allocator, 512B threshold). Display buffer auto-sizes at boot.

```
DRAM (320KB total)
  ├── draw temps, masks (<512B allocs)
  ├── display buffer (28-112KB adaptive)
  └── BLE stack (~40KB when active)

PSRAM (8MB)
  ├── LVGL objects, styles, text
  ├── app parsing buffers
  └── screenshot buffer
```

## Examples

<details>
<summary><b>Counter</b> — minimal app, 15 lines</summary>

```xml
<app>
  <ui default="/main">
    <page id="main">
      <label align="center" y="30%" color="#fff" font="72">{count}</label>
      <button align="center" y="55%" w="60%" h="50" bgcolor="#3498db" onclick="add">+1</button>
    </page>
  </ui>

  <state>
    <int name="count" default="0"/>
  </state>

  <script language="lua">
    function add()
      state.count = state.count + 1
    end
  </script>
</app>
```
</details>

<details>
<summary><b>Pomodoro timer</b> — multi-page, dynamic CSS classes, sliders</summary>

```xml
<app>
  <ui default="/main">
    <group id="main" default="timer" orientation="horizontal" indicator="dots">
      <page id="timer">
        <label class="{modeClass}" align="center" y="8%">{mode}</label>
        <label align="center" y="25%" color="#fff" font="72">{timeDisplay}</label>
        <button class="{startBtnClass}" x="10%" y="68%" w="38%" h="45" onclick="toggleTimer">
          {startBtnText}
        </button>
        <button class="btn btn-stop" x="52%" y="68%" w="38%" h="45" onclick="resetTimer">
          Reset
        </button>
      </page>

      <page id="settings">
        <label align="center" y="5%" color="#fff" font="32">Settings</label>
        <label x="5%" y="18%" color="#888">Work: {workMins} min</label>
        <slider x="5%" y="25%" w="90%" min="1" max="60" bind="workMins"/>
        <label x="5%" y="38%" color="#888">Break: {breakMins} min</label>
        <slider x="5%" y="45%" w="90%" min="1" max="30" bind="breakMins"/>
      </page>
    </group>
  </ui>

  <state>
    <string name="mode" default="WORK"/>
    <string name="modeClass" default="mode-work"/>
    <string name="timeDisplay" default="25:00"/>
    <string name="startBtnText" default="Start"/>
    <string name="startBtnClass" default="btn btn-start"/>
    <int name="workMins" default="25"/>
    <int name="breakMins" default="5"/>
    <int name="timeLeft" default="1500"/>
    <bool name="running" default="false"/>
  </state>

  <timer interval="1000" call="tick"/>

  <script language="lua">
    function formatTime(s)
      return string.format('%02d:%02d', math.floor(s/60), s%60)
    end

    function tick()
      if not state.running then return end
      state.timeLeft = state.timeLeft - 1
      state.timeDisplay = formatTime(state.timeLeft)
      if state.timeLeft <= 0 then
        state.mode = 'BREAK'
        state.modeClass = 'mode-break'
        state.timeLeft = state.breakMins * 60
      end
    end

    function toggleTimer()
      state.running = not state.running
      state.startBtnText = state.running and 'Pause' or 'Start'
      state.startBtnClass = state.running and 'btn btn-pause' or 'btn btn-start'
    end

    function resetTimer()
      state.running = false
      state.mode = 'WORK'
      state.modeClass = 'mode-work'
      state.timeLeft = state.workMins * 60
      state.timeDisplay = formatTime(state.timeLeft)
      state.startBtnText = 'Start'
      state.startBtnClass = 'btn btn-start'
    end
  </script>

  <style>
    .mode-work { color: #e74c3c; font: 32; }
    .mode-break { color: #2ecc71; font: 32; }
    .btn { radius: 8; }
    .btn-start { background: #2ecc71; color: #fff; }
    .btn-pause { background: #f39c12; color: #000; }
    .btn-stop { background: #e74c3c; color: #fff; }
  </style>
</app>
```
</details>

<details>
<summary><b>Weather</b> — network fetch, JSON parsing, BLE bridge</summary>

```xml
<app>
  <config>
    <network/>
  </config>

  <ui default="/main">
    <page id="main">
      <label align="center" y="10%" color="#fff" font="48">{city}</label>
      <label align="center" y="30%" color="#fff" font="72">{temp}°C</label>
      <label align="center" y="50%" color="#888">{description}</label>
      <label visible="{isLoading}" align="center" y="70%" color="#ff0">Loading...</label>
      <button align="center" y="80%" w="50%" h="40" bgcolor="#06f" onclick="refresh">
        Refresh
      </button>
    </page>
  </ui>

  <state>
    <string name="city" default="Moscow"/>
    <string name="temp" default="--"/>
    <string name="description" default=""/>
    <string name="isLoading" default="false"/>
  </state>

  <script language="lua">
    local API_KEY = "YOUR_KEY"

    function refresh()
      state.isLoading = "true"
      fetch({
        method = "GET",
        url = "https://api.openweathermap.org/data/2.5/weather?q="
              .. state.city .. "&appid=" .. API_KEY .. "&units=metric"
      }, function(r)
        state.isLoading = "false"
        if r.status == 200 then
          local data = json.parse(r.body)
          state.temp = tostring(math.floor(data.main.temp))
          state.description = data.weather[1].description
        end
      end)
    end

    refresh()
  </script>
</app>
```
</details>

<details>
<summary><b>Snake game</b> — canvas rendering, touch input, game loop</summary>

```xml
<app>
  <ui default="/game">
    <page id="game" bgcolor="#1a1a1a">
      <canvas id="c" x="5%" y="5%" w="350" h="350" ontap="handleTap"/>
      <label align="center" y="88%" color="#fff">Score: {score}</label>
      <button x="5%" y="93%" w="43%" h="30" bgcolor="#2ecc71" onclick="startGame">START</button>
      <button x="52%" y="93%" w="43%" h="30" bgcolor="#e74c3c" onclick="stopGame">STOP</button>
    </page>
  </ui>

  <state>
    <int name="score" default="0"/>
  </state>

  <timer interval="150" call="tick"/>

  <script language="lua">
    local W, H, CELL = 350, 350, 14
    local snake, food, dir, running = {}, {}, {}, false

    function startGame()
      snake = {{x=10,y=10},{x=9,y=10},{x=8,y=10}}
      dir = {x=1, y=0}
      state.score = 0
      running = true
      placeFood()
    end

    function stopGame() running = false end

    function placeFood()
      food = {x=math.random(0,W/CELL-1), y=math.random(0,H/CELL-1)}
    end

    function handleTap(x, y)
      if not running then return end
      local cx = W / 2
      local cy = H / 2
      local dx, dy = x - cx, y - cy
      if math.abs(dx) > math.abs(dy) then
        dir = dx > 0 and {x=1,y=0} or {x=-1,y=0}
      else
        dir = dy > 0 and {x=0,y=1} or {x=0,y=-1}
      end
    end

    function tick()
      if not running then return end
      local head = {x=snake[1].x+dir.x, y=snake[1].y+dir.y}
      if head.x<0 or head.y<0 or head.x>=W/CELL or head.y>=H/CELL then
        running = false; return
      end
      table.insert(snake, 1, head)
      if head.x==food.x and head.y==food.y then
        state.score = state.score + 1
        placeFood()
      else
        table.remove(snake)
      end
      render()
    end

    function render()
      canvas.clear("c", "#1a1a1a")
      canvas.rect("c", food.x*CELL, food.y*CELL, CELL-1, CELL-1, "#e74c3c")
      for i, s in ipairs(snake) do
        local color = i == 1 and "#2ecc71" or "#27ae60"
        canvas.rect("c", s.x*CELL, s.y*CELL, CELL-1, CELL-1, color)
      end
      canvas.refresh("c")
    end

    render()
  </script>
</app>
```
</details>

<details>
<summary><b>Flappy Bird</b> — physics, collision, canvas animation</summary>

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
    local GRAVITY, JUMP_VEL = 0.8, -10

    function render()
      canvas.clear("screen", 0x87CEEB)
      canvas.rect("screen", 0, H-20, W, 20, 0x8B4513)
      local gy = state.pipeGap
      canvas.rect("screen", state.pipeX, 0, 50, gy-40, 0x00AA00)
      canvas.rect("screen", state.pipeX, gy+40, 50, H, 0x00AA00)
      canvas.rect("screen", 50, state.birdY-10, 20, 20, 0xFFFF00)
      canvas.refresh("screen")
    end

    function tick()
      if not state.running then return end
      state.birdVel = state.birdVel + GRAVITY
      state.birdY = state.birdY + state.birdVel
      state.pipeX = state.pipeX - 4
      if state.pipeX < -50 then
        state.pipeX = W
        state.pipeGap = math.random(60, H-80)
        state.score = state.score + 1
      end
      if state.birdY < 10 or state.birdY > H-30 then
        state.running = false
      end
      render()
    end

    function jump()
      if state.running then state.birdVel = JUMP_VEL end
    end

    function startGame()
      state.score = 0; state.birdY = 140; state.birdVel = 0
      state.pipeX = 400; state.running = true
      render()
    end

    render()
  </script>
</app>
```
</details>

## Docs

| Document | What's in it |
|----------|-------------|
| [UI_HTML_SPEC.md](docs/UI_HTML_SPEC.md) | Full widget/state/binding/Lua API reference |
| [CONSOLE_PROTOCOL_SPEC.md](docs/CONSOLE_PROTOCOL_SPEC.md) | BLE & Serial command protocol |
| [BUILD_ICONS.md](docs/BUILD_ICONS.md) | Icon pipeline & embedding |

## License

MIT
