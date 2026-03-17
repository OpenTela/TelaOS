# UI HTML Specification v0.7

Декларативный язык разметки для создания интерфейсов на ESP32 с LVGL.

## Changelog

- **v0.7**: `oninput` для input. Canvas: `ondraw`, `ontap`, `onhold`. `onclick` с аргументами. `overflow` для label.
- **v0.6**: Namespaces (`ui.*`, `app.*`, `net.*`, `timer.*`). Templates + `@for`. `<table>`/`<tr>`/`<td>`. setTimeout порядок аргументов. CSV/YAML API.
- **v0.5**: z-index (HTML атрибут, CSS, setAttr)
- **v0.4**: Тег `<config>`. Тег `<network/>`
- **v0.3**: focus(), onenter, onblur, биндинг bgcolor/color, setAttr/getAttr
- **v0.2**: canvas, image, ресурсы, иконки на кнопках
- **v0.1**: Начальная версия

## Структура файлов приложения

```
myapp/
├── myapp.bax        # Обязательно: код приложения
├── icon.png         # Опционально: иконка в лаунчере (64x64)
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

## Структура .bax файла

```html
<app os="1.0" title="My App" version="1.0" category="game" icon="system:puzzle-game">
  <config>
    <network/>  <!-- опционально: включить BLE -->
  </config>

  <templates>
    <!-- определения шаблонов -->
  </templates>
  
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
  
  <style>
    /* CSS-подобные стили */
  </style>
</app>
```

### Атрибуты `<app>`

| Атрибут | Описание | Default |
|---------|----------|---------|
| `os` | Минимальная версия TelaOS | — |
| `title` | Название в лаунчере | Имя папки с заглавной |
| `version` | Версия приложения | — |
| `category` | Категория (`game`, `study`, ...) | — |
| `icon` | Иконка (см. ниже) | Авто из `icon.png` или первая буква |

**Иконки (приоритет):**
1. Файл `icon.png` в папке приложения — используется автоматически, атрибут не нужен
2. `icon="system:puzzle-game"` — системная из `/system/resources/icons/puzzle-game.png`
3. Без иконки и без `icon.png` — отображается первая буква title

---

## Навигация

### Standalone страницы

```html
<page id="settings">
  <!-- контент -->
</page>
```

Переход: `href="/settings"` или `ui.navigate("/settings")` из Lua.

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
| `class` | CSS класс (поддерживает `{var}`) |
| `overflow` | `ellipsis`, `clip`, `scroll` |

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
| `onclick` | Lua выражение при нажатии |
| `onhold` | Lua функция при удержании |
| `href` | Навигация на страницу |
| `icon` | Путь к иконке |
| `iconsize` | Размер иконки (default: 24) |
| `bgcolor` | Цвет фона (#RRGGBB или `{var}`) |
| `color` | Цвет текста (#RRGGBB или `{var}`) |
| `radius` | Скругление углов |
| `z-index` | Порядок наложения (>0 наверх, <0 назад) |
| `visible` | Видимость (`{var}`) |
| `class` | CSS класс (поддерживает `{var}`) |

**onclick с аргументами:** если содержит `(`, выполняется как Lua-выражение:
```html
<button onclick="appendDigit('7')">7</button>
<button onclick="doTap(2,3)">Cell</button>
```

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
| `oninput` | Lua функция при каждом нажатии клавиши |
| `onblur` | Lua функция при потере фокуса |
| `z-index` | Порядок наложения |

### canvas

```html
<canvas id="draw" x="0" y="0" w="100%" h="100%"/>
```

**События canvas:**
```html
<canvas id="c" ontap="onTap" onhold="onHold" ondraw="onDraw"/>
```

| Атрибут | Описание | Callback |
|---------|----------|----------|
| `ontap` | Одиночный тап | `function(x, y)` — координаты тапа |
| `onhold` | Удержание | `function(x, y)` — координаты |
| `ondraw` | Рисование (continuous) | вызывается при перемещении |

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

## Table Layout

```html
<table x="0" y="10%" w="100%" h="60%" cellspacing="2%" bgcolor="#000">
  <tr h="12%">
    <td w="20%"><label>Header</label></td>
    <td><button onclick="doSomething">Click</button></td>
  </tr>
</table>
```

| Тег | Атрибуты | Описание |
|-----|----------|----------|
| `<table>` | `x`, `y`, `w`, `h`, `cellspacing`/`gap`, `bgcolor` | Flex-column контейнер |
| `<tr>` | `h`, `visible`, `bgcolor` | Flex-row (строка) |
| `<td>` | `w`, `visible`, `bgcolor` | Flex-cell (ячейка). Без `w` → равные доли |

Дочерние виджеты внутри `<td>` заполняют ячейку целиком.

---

## Templates

### Определение

```html
<templates>
  <template id="Num">
    <td><button class="btn" onclick="appendDigit('{n}')">{n}</button></td>
  </template>
</templates>
```

Правила:
- `id` — PascalCase (начинается с заглавной)
- `{param}` подставляются из атрибутов вызова

### Вызов

```html
<Num n="7"/>
```

### Вложенные шаблоны

```html
<template id="Cell">
  <td><button id="{col}{row}" bgcolor="{bg{col}{row}}">{v{col}{row}}</button></td>
</template>

<template id="DataRow">
  <tr>
    <Cell col="A" row="{row}"/>
    <Cell col="B" row="{row}"/>
  </tr>
</template>
```

Multi-pass: `{bg{col}{row}}` → `{bgA1}` → state binding.

---

## @for Directive

```
@for(variable in start..end [step N]) {
  body
}
```

### Примеры

```html
@for(i in 0..9) {
  <button id="b_{i}">{i}</button>
}

@for(r in 1..8) {
  <DataRow row="{r}"/>
}
```

### Вложенные циклы

```html
@for(r in 1..3) {
  <tr>
    @for(c in 0..5) {
      <td><button>R{r}C{c}</button></td>
    }
  </tr>
}
```

### Pipeline обработки

```
Raw HTML → parse_templates() → цикл (max 8 проходов):
  ├── expand @for
  ├── expand <PascalCase/>
  └── если стабильно → стоп
→ Expanded HTML → создание виджетов
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
<button onclick="doTap(1,2)">With args</button>
<button onhold="longPress">Hold me</button>
<slider onchange="onValueChange"/>
<switch onchange="onToggle"/>
<input onenter="onSubmit" oninput="onKeystroke" onblur="onLostFocus"/>
<canvas id="c" ondraw="paint" ontap="tap" onhold="hold"/>
```

**onclick с аргументами:** если значение содержит `(`, вызывается `execute()` (полное Lua-выражение), иначе `call()` (имя функции).

---

## Таймеры

```html
<timer interval="1000" call="updateTime"/>
<timer interval="500" call="animate"/>
```

---

## Lua API

```html
<script language="lua">
  function updateTime()
    local h = os.date("%H")
    local m = os.date("%M")
    state.time = h .. ":" .. m
  end
</script>
```

### Namespaces

Все функции доступны через namespace и как глобальные алиасы для обратной совместимости.

#### ui.* — управление интерфейсом

| Функция | Описание |
|---------|----------|
| `ui.navigate("/page")` | Переход на страницу |
| `ui.focus("widgetId")` | Фокус на input |
| `ui.setAttr("id", "attr", "value")` | Изменить атрибут |
| `ui.getAttr("id", "attr")` | Получить атрибут |
| `ui.freeze()` | Заморозить обновления UI |
| `ui.unfreeze()` | Разморозить обновления UI |

**Алиасы:** `navigate()`, `focus()`, `setAttr()`, `getAttr()`

**setAttr/getAttr атрибуты:** `bgcolor`, `color`, `text`, `visible`, `x`, `y`, `w`, `h`, `z-index`

#### app.* — управление приложением

| Функция | Описание |
|---------|----------|
| `app.exit([code[, msg]])` | Выйти в launcher |
| `app.launch(name)` | Запустить другое приложение |

**Алиас:** `exit()`

#### timer.* — таймеры из Lua

| Функция | Описание |
|---------|----------|
| `timer.once(callback, ms)` | Однократный таймер |
| `timer.interval(callback, ms)` | Повторяющийся таймер |
| `timer.clear(name)` | Отменить таймер по имени |

`callback` — строка `"funcName"` или function reference.

**Алиас:** `setTimeout(callback, ms)` = `timer.once()`

**⚠️ Порядок аргументов:** callback первый, ms второй.

```lua
timer.once("gameTick", 200)            -- string callback
timer.once(function() doStuff() end, 500) -- function callback
setTimeout("gameTick", 200)            -- alias
```

#### net.* — сеть

| Функция | Описание |
|---------|----------|
| `net.fetch(opts, callback)` | HTTP запрос через BLE bridge |
| `net.connected()` | Проверка BLE подключения (bool) |

**Алиас:** `fetch()`

**Опции fetch:**

| Поле | Описание | Default |
|------|----------|---------|
| `url` | URL запроса | required |
| `method` | HTTP метод | `"GET"` |
| `body` | Тело запроса | — |
| `format` | `"json"` — body как Lua таблица | — |
| `authorize` | `true` — bridge подставит API ключи | `false` |
| `fields` | Список полей для выборки из JSON | — |

**Ответ callback(r):**

| Поле | Описание |
|------|----------|
| `r.status` | HTTP код (200, 404...) |
| `r.body` | Тело ответа (string или table) |
| `r.ok` | `true` если status 200-299 |
| `r.error` | Текст ошибки |

**Пример:**
```lua
fetch({
  url = "https://api.example.com/data",
  authorize = true,
  format = "json",
  fields = {"name", "value"}
}, function(r)
  if r.ok then
    state.result = r.body.name
  end
end)
```

#### canvas.* — рисование

| Функция | Описание |
|---------|----------|
| `canvas.clear(id, color)` | Очистить |
| `canvas.rect(id, x, y, w, h, color)` | Прямоугольник |
| `canvas.pixel(id, x, y, color)` | Пиксель |
| `canvas.line(id, x1, y1, x2, y2, color)` | Линия |
| `canvas.circle(id, cx, cy, r, color)` | Круг |
| `canvas.refresh(id)` | Обновить экран |

#### os.* — время

| Функция | Описание |
|---------|----------|
| `os.date(fmt)` | Форматированная дата |
| `os.date("*t")` | Таблица `{year,month,day,hour,min,sec,wday,yday}` |
| `os.time()` | Unix timestamp (секунды) |
| `os.time({year,month,day,...})` | Timestamp из таблицы |

**Счётчик без RTC:** локальная переменная + таймер:
```lua
local sec = 0
function tick()
  sec = sec + 1
  state.time = string.format("%02d:%02d", sec / 60, sec % 60)
end
```

#### json.* — JSON

| Функция | Описание |
|---------|----------|
| `json.parse(s)` / `json.decode(s)` | JSON string → Lua table |
| `json.stringify(t)` / `json.encode(t)` | Lua table → JSON string |

#### CSV.* — работа с CSV

| Функция | Описание |
|---------|----------|
| `CSV.load(filename)` | Загрузить из файла |
| `CSV.loadText(text)` | Парсить из строки |
| `csv:records(count?)` | Получить записи как dict'ы |
| `csv:rows(count?)` | Получить записи как массивы |
| `csv:add(record)` | Добавить запись |
| `csv:save(onlyNew?)` | Сохранить в файл |

#### YAML.* — работа с YAML

| Функция | Описание |
|---------|----------|
| `YAML.load(filename)` | Загрузить из файла |
| `YAML.loadText(text)` | Парсить из строки |
| `yaml:get("a.b.c")` | Получить значение |
| `yaml:set("a.b.c", v)` | Установить значение |
| `yaml:tree()` | Lua table (reference) |
| `yaml:save(filename?)` | Сохранить в файл |

#### Прочее

| Функция | Описание |
|---------|----------|
| `print(...)` | Вывод в консоль |
| `state.varName` | Чтение/запись переменных |

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
| `bgcolor` / `background` / `background-color` | Цвет фона |
| `font` / `font-size` | Размер шрифта |
| `radius` / `border-radius` | Скругление |
| `z-index` | Порядок наложения |
| `width`, `height` | Размер |
| `left`, `top` | Позиция |
| `padding` | Отступ (+ `-left`, `-right`, `-top`, `-bottom`) |
| `opacity` | Прозрачность |
| `text-align` | Выравнивание текста |

**Селекторы:**
- `button` — тег
- `.primary` — класс
- `button.primary` — compound (тег + класс)
- `.a, .b` — группировка

**Специфичность:** tag(1) < class(10) < tag.class(11) < tag.class1.class2(21)

**Динамический класс:** `class="{var}"` — CSS пересчитывается при изменении state.

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
  <config>
    <network/>
  </config>
</app>

<!-- Включится при первом fetch() -->
<app>
  <config>
    <network mode="ondemand"/>
  </config>
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
