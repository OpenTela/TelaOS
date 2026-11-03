# CSS v0.2

CSS в TelaOS: что из CSS имеет смысл на экране часов, что уже работает и во что оно превращается
в LVGL. Браузер без JS — всё остальное должно быть: HTML (`docs/HTML_TAGS.md`) и CSS.

## Changelog

- **v0.2**: `<label>` и `<button>` с HTML внутри — предки этого HTML: `.x i {}` красит `<i>`
  в `<label class="x">`.
- **v0.1**: Список CSS для часов. Селекторы и каскад: списки `a, b`, потомок `nav a`, дочерний
  `ul > li`, `*`, атрибуты `[a=v]` (и `~= |= ^= $= *=`, флаг `i`), страница как предок,
  специфичность, `!important`, `display: none` из листа, правила для строчных элементов.
  Разбор: `;;` и объявление без `:` больше не вешают часы (прежний разбор на них зацикливался),
  `;` в кавычках не обрывает объявление, комментарии внутри блока пропускаются.

---

## Где живут стили

- `<style>` в `<app>` — для всего приложения; `<style>` на странице — для того, что после него.
- `style="…"` у элемента — после всех листов.
- Атрибуты TelaOS (`bgcolor=`, `color=`, `font=`…) — после CSS: виджет ставит их сам.
- Движок: `src/ui/css_parser.cpp`. Предки элемента для селекторов — `UI::CssScope`: страница,
  `<div>`, таблицы, вкладки, `<label>` и `<button>` с HTML внутри и элементы HTML, пока строится их содержимое.

## Работает: 24 из 65

Знаки: ✅ работает, ½ частично (не считается), — нет.

### Селекторы и каскад (16)

| | Что | Как |
|---|-----|-----|
| ✅ | `p`, `.class`, `#id` | |
| ✅ | составные: `button.primary#ok[type]` | раньше только `tag.class` |
| ✅ | `*` | |
| ✅ | списки: `h1, h2, h3` | селектор, который движок не понимает (`a:hover`), выпадает, остальные в списке работают — как в браузере |
| ✅ | потомок: `nav a` | предки — страница, `<div>`, таблицы, вкладки, `<label>` и `<button>`, блоки и строчные элементы HTML |
| ✅ | дочерний: `ul > li` | |
| ✅ | атрибуты: `[a]`, `[a=v]`, `[a~=v]`, `[a\|=v]`, `[a^=v]`, `[a$=v]`, `[a*=v]`, `[a=v i]` | |
| ✅ | страница как предок: `#settings button` | |
| ✅ | специфичность: id > классы и атрибуты > теги, затем порядок | раньше четыре уровня: тег < класс < тег.класс < id |
| ✅ | `!important` | лист `!important` бьёт `style=""`, `style="… !important"` — всё |
| ✅ | `style=""` после листов | |
| ✅ | наследование цвета и шрифта | LVGL наследует `text_color`, `text_font` |
| — | соседи: `a + b`, `a ~ b` | |
| — | `:first-child`, `:last-child`, `:nth-child()` | |
| — | `:active`, `:hover`, `:focus`, `:checked`, `:disabled` | состояния LVGL: `PRESSED`, `FOCUSED`, `CHECKED`, `DISABLED` |
| — | `:not()`, `:is()`, `::before`, `::after`, `::placeholder` | |

### Значения (8)

| | Что | Как |
|---|-----|-----|
| ✅ | числа и `px` | |
| ½ | `%` | у `width`, `height`, `left`, `top`; у `padding` — нет |
| — | `em`, `rem` | от 16px |
| — | `vw`, `vh` | от экрана |
| ✅ | `#RGB`, `#RRGGBB`, все 148 имён | |
| — | `rgb()`, `rgba()`, `hsl()`, `#RRGGBBAA` | альфа — `bg_opa`, `text_opa` |
| — | 2–4 значения: `padding: 4px 8px` | |
| — | `var(--x)`, `calc()` | |

### Блок (11)

| | Что | Как |
|---|-----|-----|
| ✅ | `width`, `height`, `min-*`, `max-*` | |
| ½ | `padding`, `padding-*` | одно число |
| — | `margin`, `margin-*` | `margin_*` в LVGL 9 |
| — | `border`, `border-*` | `border_width`, `border_color`, `border_side` |
| ✅ | `border-radius` | |
| — | `outline` | `outline_*` |
| — | `box-shadow` | `shadow_*` |
| ✅ | `background`, `background-color` | цветом |
| — | `linear-gradient()` | `bg_grad` |
| ✅ | `opacity` | |
| — | `overflow` | прокрутка и обрезка |

### Отображение и положение (7)

| | Что | Как |
|---|-----|-----|
| ✅ | `display: none` | из листа и из `style=""` |
| — | `display: block`, `inline`, `inline-block` | |
| — | `visibility: hidden` | |
| ✅ | `left`, `top` | |
| — | `right`, `bottom` | |
| — | `position: relative`, `absolute`, `fixed` | |
| ✅ | `z-index` | |

### Flex (5)

| | Что | Как |
|---|-----|-----|
| — | `display: flex`, `flex-direction`, `flex-wrap` | у TelaOS `<div flex=…>` есть, из CSS — нет |
| — | `justify-content`, `align-items`, `align-content` | |
| ½ | `gap` | `row-gap`, `column-gap` — нет |
| — | `flex-grow`, `flex` | |
| — | `order` | |

### Текст (12)

| | Что | Как |
|---|-----|-----|
| ✅ | `color` | |
| ✅ | `font-size` | ближайший из шрифтов прошивки: 16, 32, 48, 72 |
| ✅ | `text-align` | |
| ½ | `text-decoration` | у строчных элементов (`a`, `span`); у блоков — нет |
| — | `font-weight`, `font-style` | в прошивке нет жирного и курсива |
| — | `line-height` | `text_line_space` |
| — | `letter-spacing` | `text_letter_space` |
| — | `text-transform` | |
| — | `white-space` | |
| — | `text-overflow: ellipsis` | `LV_LABEL_LONG_DOT` |
| — | `vertical-align` | |
| — | `word-break`, `overflow-wrap` | |

### Движение (3)

| | Что | Как |
|---|-----|-----|
| — | `transition` | переходы стилей LVGL |
| — | `transform`: `translate`, `rotate`, `scale` | `transform_*`, `translate_*` |
| — | `@keyframes`, `animation` | анимации LVGL |

### At-правила (3)

| | Что | Как |
|---|-----|-----|
| — | `@media` | пропускается целиком |
| — | `@import` | пропускается |
| — | `@font-face` | только шрифты прошивки |

## Строчные элементы

У слов внутри абзаца (`<a>`, `<span>`, `<b>`) нет своих объектов LVGL: это куски одной надписи или
группы спанов. Правила для них (`nav a`, `.menu a`, `a.btn`) дают то, что умеет кусок текста:
`color`, `font-size`, `text-decoration`.

## Ограничения

- Правило с потомком действует на элементы, которые строятся после `<style>`, как и любое правило
  на странице.
- Когда `class="{var}"` меняется, перечитываются только правила из одних классов — как и раньше.
- `<th>` строится как `<td>`: правила `th` к нему не применяются.

## Тесты

- `ci/tests/ui/test_css.cpp` — простые селекторы и прежний каскад (CI, mock LVGL).
- `ci/tests/ui/test_css_selectors.cpp` — селекторы и каскад этой версии.
- Симулятор (`docs/SIMULATOR.md`): приложение-образец `sim/tests/fixtures/apps/css_tags`,
  сценарий `sim/tests/tela_css.sim`. Цвет каждой строки на снимке говорит, сработало ли её правило.
