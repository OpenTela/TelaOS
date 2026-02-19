# UI HTML Specification v0.5

Декларативный язык разметки для создания интерфейсов на ESP32 с LVGL.

## Changelog

- **v0.5**: z-index (HTML атрибут, CSS, setAttr)

- **v0.4**: Тег `<configuration>` (legacy: `<system>`). Тег `<network/>` (legacy: `<bluetooth/>`)
- **v0.3**: focus(), onenter, onblur, биндинг bgcolor/color, setAttr/getAttr
- **v0.2**: canvas, image, ресурсы, иконки на кнопках
- **v0.1**: Начальная версия

## Структура файлов приложения

```
myapp/
├── app.html         # Обязательно: код приложения
├── icon.png         # Опционально: иконка в лаунчере (48x48)
└── resources/       # Опционально: ресурсы
    ├── plus.png
    └── minus.png
```

---

## Ресурсы (иконки, изображения)

### Пути к ресурсам

Относительные пути ищутся в `resources/`:

```html
<!-- Ищет: myapp/resources/plus.png -->
<button icon="plus.png"/>
<image src="icons/gear.png"/>
```

### Иконки на кнопках

```html
<button icon="plus.png" iconsize="48" onclick="add"/>
<button icon="save.png" iconsize="24">Сохранить</button>
```

| Атрибут | Описание | Default |
|---------|----------|---------|
| `icon` | Путь к иконке | — |
| `iconsize` | Размер в px | 24 |

**Формат:** PNG рекомендуется (поддержка прозрачности)

---

## Структура app.html

```html
<app>
  <system>
    <network/>  <!-- опционально: включить BLE -->
  </system>
  
  <ui default="/main">
    <!-- страницы и группы -->
  </ui>
  
  <state>
    <!-- переменные состояния -->
  </state>
  
  <timer interval="1000" call="functionName"/>
  
  <script language="lua">
    -- код скриптов
  </script>
</app>
```

---

## Навигация

### Standalone страницы

```html
<page id="settings">
  <!-- контент -->
</page>
```

Переход: `href="/settings"` или `navigate("/settings")` из Lua.

### Группы страниц (свайп)

```html
<group id="main" default="home" orientation="horizontal" indicator="dots">
  <page id="home">...</page>
  <page id="stats">...</page>
</group>
```

| Атрибут | Значения | Default | Описание |
|---------|----------|---------|----------|
| `id` | string | required | Идентификатор группы |
| `default` | string | первая | Страница по умолчанию |
| `orientation` | `horizontal`, `vertical`, `h`, `v` | `horizontal` | Направление свайпа |
| `indicator` | `scrollbar`, `dots`, `none` | `scrollbar` | Индикатор |

---

## Выравнивание — Единая система

### Концепция

Два типа выравнивания с симметричным API:

| | Элемент на странице | Текст внутри элемента |
|---|---|---|
| **Полная форма** | `align="h v"` | `text-align="h v"` |
| **Только H** | `align="h"` | `text-align="h"` |
| **Только V** | `valign="v"` | `text-valign="v"` |

**Значения:**
- Горизонталь (h): `left`, `center`, `right`
- Вертикаль (v): `top`, `center`, `bottom`

### Позиция элемента

```html
<!-- Центр экрана -->
<label align="center center">HELLO</label>

<!-- Правый нижний угол -->
<label align="right bottom">Corner</label>

<!-- Только горизонталь + y координата -->
<label align="center" y="10%">Top center</label>

<!-- Раздельные атрибуты -->
<label align="center" valign="bottom">Bottom center</label>
```

**Приоритет:** координаты `x`/`y` > атрибуты `align`/`valign`

### Текст внутри элемента

```html
<!-- Полная форма -->
<label w="200" h="100" text-align="center center">
  Centered text
</label>

<!-- Раздельные атрибуты -->
<label w="200" h="100" text-align="center" text-valign="bottom">
  Bottom center
</label>

<!-- Только вертикаль -->
<label w="200" h="100" text-valign="center">
  Vertically centered
</label>
```

**Важно:** `text-valign` работает только при заданной высоте (`h`).

---

## Виджеты

### label

```html
<label x="10" y="20" color="#fff">Static text</label>
<label align="center" y="5%" color="#0f0">{variable}</label>
<label x="10%" y="10%" w="80%" h="100" 
       bgcolor="#333" text-align="center center">
  Centered in box
</label>
```

| Атрибут | Описание |
|---------|----------|
| `x`, `y` | Позиция (px или %) |
| `w`, `h` | Размер (px или %) |
| `align` | Позиция элемента: `"h"` или `"h v"` |
| `valign` | Позиция элемента (вертикаль) |
| `text-align` | Текст внутри: `"h"` или `"h v"` |
| `text-valign` | Текст внутри (вертикаль) |
| `color` | Цвет текста (#RRGGBB или `{var}`) |
| `bgcolor` | Цвет фона (#RRGGBB или `{var}`) |
| `font` | Размер: 16, 32, 48, 72 |
| `radius` | Скругление углов (px) |
| `z-index` | Порядок наложения (>0 наверх, <0 назад) |
| `visible` | Видимость (`{var}`) |
| `class` | CSS класс |

### button

```html
<button x="5%" y="70%" w="90%" h="40" bgcolor="#06f" onclick="doSomething">
  Click me
</button>
<button href="/settings">Settings</button>
<button icon="icons/gear.png" w="50" h="50"/>
```

| Атрибут | Описание |
|---------|----------|
| `x`, `y`, `w`, `h` | Позиция и размер |
| `align`, `valign` | Позиция элемента |
| `onclick` | Lua функция при нажатии |
| `onhold` | Lua функция при удержании |
| `href` | Навигация на страницу |
| `icon` | Путь к иконке |
| `bgcolor` | Цвет фона (#RRGGBB или `{var}`) |
| `color` | Цвет текста (#RRGGBB или `{var}`) |
| `radius` | Скругление углов |
| `z-index` | Порядок наложения (>0 наверх, <0 назад) |
| `visible` | Видимость (`{var}`) |
| `class` | CSS класс |

### slider

```html
<slider x="5%" y="50%" w="90%" min="0" max="100" bind="brightness"/>
```

| Атрибут | Описание |
|---------|----------|
| `min`, `max` | Диапазон значений |
| `bind` | Привязка к state |
| `onchange` | Lua функция при изменении |
| `z-index` | Порядок наложения |

### switch

```html
<switch x="35%" y="34%" bind="enabled" onchange="onToggle"/>
```

| Атрибут | Описание |
|---------|----------|
| `bind` | Привязка к state ("true"/"false") |
| `onchange` | Lua функция при переключении |
| `z-index` | Порядок наложения |

### input

```html
<input x="5%" y="22%" w="90%" h="35" bind="userName" placeholder="Name"/>
```

| Атрибут | Описание |
|---------|----------|
| `bind` | Привязка к state |
| `placeholder` | Текст-подсказка |
| `password` | Маскировать ввод (true/false) |
| `onenter` | Lua функция при Enter |
| `onblur` | Lua функция при потере фокуса |
| `z-index` | Порядок наложения |

### canvas

```html
<canvas id="draw" x="0" y="0" w="100%" h="100%"/>
```

Рисование из Lua:
```lua
canvas.clear("draw", "#000000")
canvas.rect("draw", 10, 10, 50, 50, "#ff0000")
canvas.pixel("draw", 100, 100, "#00ff00")
canvas.line("draw", 0, 0, 100, 100, "#ffffff")
canvas.circle("draw", 120, 120, 30, "#0000ff")
canvas.refresh("draw")
```

### image

```html
<image x="10" y="10" src="icons/logo.png"/>
<image x="10" y="10" w="48" h="48" src="icons/icon.png"/>
```

---

## State (состояние)

```html
<state>
  <string name="userName" default=""/>
  <string name="status" default="Ready"/>
  <int name="count" default="0"/>
  <int name="brightness" default="50"/>
  <bool name="enabled" default="false"/>
  <float name="temperature" default="22.5"/>
</state>
```

**Типы переменных:**
| Тип | Lua | Default |
|-----|-----|---------|
| `string` | `state.name = "text"` | `""` |
| `int` | `state.count = state.count + 1` | `0` |
| `bool` | `state.enabled = true` | `false` |
| `float` | `state.temp = 22.5` | `0.0` |

Доступ из Lua: `state.variableName`

---

## Биндинг

### Текстовый биндинг

```html
<label>{time}</label>           <!-- полная замена -->
<label>Time: {time}</label>     <!-- шаблон -->
```

### Двусторонний биндинг

```html
<slider bind="brightness"/>     <!-- виджет ↔ state -->
<switch bind="enabled"/>
<input bind="userName"/>
```

### Биндинг атрибутов

```html
<button bgcolor="{btnColor}">Dynamic color</button>
<label color="{textColor}" bgcolor="{bgColor}">Styled</label>
<label visible="{isVisible}">Conditional</label>
<label class="{dynamicClass}">Styled</label>
```

---

## События

```html
<button onclick="myFunction">Click</button>
<button onhold="longPress">Hold me</button>
<slider onchange="onValueChange"/>
<switch onchange="onToggle"/>
<input onenter="onSubmit" onblur="onLostFocus"/>
```

---

## Таймеры

```html
<timer interval="1000" call="updateTime"/>
<timer interval="500" call="animate"/>
```

---

## Скрипты (Lua)

```html
<script language="lua">
  function updateTime()
    local h = os.date("%H")
    local m = os.date("%M")
    state.time = h .. ":" .. m
  end
  
  function onBrightnessChange()
    print("Brightness: " .. state.brightness)
  end
</script>
```

### Доступные API

**State:**
- `state.varName` — чтение/запись переменных

**Навигация:**
- `navigate("/page_id")` — переход на страницу

**UI:**
- `focus("widgetId")` — установить фокус на input
- `setAttr("id", "attr", "value")` — изменить атрибут
- `getAttr("id", "attr")` — получить атрибут

**setAttr/getAttr атрибуты:** `bgcolor`, `color`, `text`, `visible`, `x`, `y`, `w`, `h`, `z-index`

**Canvas:**
- `canvas.clear(id, color)`
- `canvas.rect(id, x, y, w, h, color)`
- `canvas.pixel(id, x, y, color)`
- `canvas.line(id, x1, y1, x2, y2, color)`
- `canvas.circle(id, cx, cy, r, color)`
- `canvas.refresh(id)`

**Система:**
- `print(...)` — вывод в консоль
- `app_launch(name)` — запустить приложение
- `app_home()` — вернуться в launcher

**Время:**
- `os.date()` — работает только при синхронизированном времени (NTP/RTC)
- Для счётчика используйте локальную переменную + таймер:
```lua
local sec = 0
function tick()
  sec = sec + 1
  state.time = string.format("%02d:%02d", sec / 60, sec % 60)
end
```

**Сеть (требует `<network/>`):**
- `fetch(options, callback)` — HTTP запрос через BLE bridge

---

## Стили (CSS-like)

```html
<style>
  button { bgcolor: #333; radius: 8; }
  button.primary { bgcolor: #0066ff; }
  label.title { font: 48; color: #fff; }
  label.overlay { z-index: 1; }
</style>

<button class="primary">OK</button>
<label class="title">Hello</label>
```

**Поддерживаемые CSS свойства:**

| CSS | Описание |
|-----|----------|
| `color` | Цвет текста |
| `bgcolor` / `background-color` | Цвет фона |
| `font` / `font-size` | Размер шрифта |
| `radius` / `border-radius` | Скругление |
| `z-index` | Порядок наложения |
| `width`, `height` | Размер |
| `left`, `top` | Позиция |
| `padding` | Отступ (+ `-left`, `-right`, `-top`, `-bottom`) |

---

## Полный пример

```html
<app>
  <ui default="/main">
    <group id="main" default="home" orientation="horizontal" indicator="dots">
      <page id="home">
        <!-- Заголовок по центру -->
        <label align="center" y="5%" color="#fff" font="48">{time}</label>
        <label align="center" y="20%" color="#888">{status}</label>
        
        <!-- Бокс с центрированным текстом -->
        <label x="10%" y="35%" w="80%" h="60" 
               bgcolor="#333" radius="8"
               text-align="center center" font="32">
          {temperature}°C
        </label>
        
        <!-- Слайдер -->
        <label x="5%" y="55%" color="#aaa">Brightness:</label>
        <slider x="5%" y="62%" w="90%" min="0" max="100" bind="brightness"/>
        
        <!-- Кнопки -->
        <button x="5%" y="80%" w="42%" h="40" bgcolor="#f00" onclick="reset">
          Reset
        </button>
        <button x="52%" y="80%" w="42%" h="40" bgcolor="#06f" href="/settings">
          Settings
        </button>
      </page>
      
      <page id="stats">
        <label align="center center" color="#fff" font="32">
          Statistics
        </label>
      </page>
    </group>
    
    <page id="settings">
      <label align="center" y="5%" color="#fff" font="32">Settings</label>
      
      <label x="5%" y="18%" color="#aaa">Name:</label>
      <input x="5%" y="24%" w="90%" h="40" bind="userName" 
             placeholder="Your name" onenter="saveName"/>
      
      <label x="5%" y="38%" color="#aaa">Notifications:</label>
      <switch x="75%" y="36%" bind="notifications"/>
      
      <button align="center" y="80%" w="50%" h="40" bgcolor="#06f" href="/main">
        Back
      </button>
    </page>
  </ui>
  
  <state>
    <string name="time" default="00:00"/>
    <string name="status" default="Ready"/>
    <int name="temperature" default="22"/>
    <int name="brightness" default="50"/>
    <string name="userName" default=""/>
    <bool name="notifications" default="true"/>
  </state>
  
  <timer interval="1000" call="tick"/>
  
  <script language="lua">
    local sec = 0
    
    function tick()
      sec = sec + 1
      local m = math.floor(sec / 60) % 60
      local s = sec % 60
      state.time = string.format("%02d:%02d", m, s)
    end
    
    function reset()
      state.brightness = 50
      state.status = "Reset!"
    end
    
    function saveName()
      state.status = "Hello, " .. state.userName
    end
  </script>
</app>
```

---

## Системные настройки

### Сеть

```html
<!-- Выключен (default) — экономия RAM -->
<app>
  <ui>...</ui>
</app>

<!-- Включен сразу -->
<app>
  <system>
    <network/>
  </system>
</app>

<!-- Включится при первом fetch() -->
<app>
  <system>
    <network mode="ondemand"/>
  </system>
</app>
```

---

## Roadmap (TODO)

### Виджеты

- `arc` — дуга/круговой прогресс
- `bar` — линейный прогресс-бар
- `spinner` — индикатор загрузки
- `chart` — графики
- `list` — список с прокруткой
- `roller` — барабанный селектор

### Фичи

- Вложенные группы
- Анимации
- Условный рендеринг (`visible="{!isLoading}"`)
