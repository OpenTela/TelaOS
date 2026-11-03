# Icons v1.0

Иконки в тексте TelaOS: разметка Google, какие имена есть, откуда шрифт и как добавить иконку.
Иконки приложений в лаунчере — другое, см. `docs/BUILD_ICONS.md`.

## Changelog

- **v1.0**: 179 иконок Material Symbols по имени и 57 знаков Unicode в тексте. Шрифты иконок —
  файлы на LittleFS, не код прошивки.

---

## Коротко

- **Разметка как у Google:** `<span class="material-symbols-outlined">home</span>`. Имя становится
  иконкой. Неизвестное имя остаётся текстом — как в браузере без шрифта.
- **Классы:** `material-symbols-outlined`, `material-symbols-rounded`, `material-symbols-sharp`,
  `material-icons` (и `-outlined`, `-round`, `-sharp`). Все рисуются набором Outlined: другого на
  часах нет.
- **Размер** — 24px, как в таблице стилей Google, пока страница не задаст свой `font-size` (правилом
  или `style`). Шрифты идут ступенями 16/32/48/72, поэтому 24px рисуется 32-м (`docs/CSS.md`).
- **Цвет** — цвет текста, `color` работает.
- **Имя из переменной:** `<p><span class="material-symbols-outlined">{sky}</span></p>` — иконка
  меняется вместе с `sky`.
- **Внутри кнопки:** `<button><span class="material-symbols-outlined">add</span></button>`.
- **Знаки Unicode пишутся прямо в тексте:** `✓ ← → ↑ ↓ ★ ☆ ♥ ♡ ⚙ ☀ ☁ ❄ ▶ ⏸` и другие (список ниже).
  Их рисует тот же шрифт иконок. Залитые — там, где Unicode рисует знак залитым: `★` BLACK STAR
  залита, `☆` WHITE STAR — контур.

```html
<p>Заряд <span class="material-symbols-outlined" style="font-size: 16px">battery_full</span> 80%</p>
<p>Оценка: ★★★☆☆</p>
```

## Где лежат

- `data/system/fonts/Icons_16px.bin` … `Icons_72px.bin` — шрифты LVGL того же размера, что шрифты
  текста. `data/system/fonts/Icons.txt` — имена и коды, по строке: `home e9b2`.
- **Это файлы на LittleFS, не код:** попадают на часы с `pio run -t uploadfs`. Добавить иконку можно
  без сборки прошивки.
- **При старте** прошивка грузит их в PSRAM (около 300 КБ, DRAM не тратится) и ставит между шрифтом
  текста и шрифтом других языков того же размера: `Ubuntu_*px` → `Icons_*px` → `UbuntuExt_*px`.
  В логе: `font: Icons_16px.bin in N ms`.
- **Шрифт** — Material Symbols Outlined (Google), лицензия Apache 2.0:
  `fonts/material-symbols/Apache-2.0.txt`.
- **Без файлов** (прошивка залита, а `uploadfs` нет) всё остальное работает как раньше: имена иконок
  видны словами, знаки — прямоугольниками. В логе: `font: no /system/fonts/Icons_16px.bin`.

## Как добавить иконку

1. Найти имя на [fonts.google.com/icons](https://fonts.google.com/icons).
2. Дописать его в `ICONS` в `scripts/make_fonts.py`.
3. Скачать шрифт по ссылке `MATERIAL_URL` из скрипта (10 МБ, в репозиторий не кладётся) и его
   `.codepoints` из той же папки, затем:
   `MATERIAL_SYMBOLS=<путь к .ttf> python3 scripts/make_fonts.py icons`.
   Коды берутся из файла той же версии: между версиями Material коды меняются, имена — нет.
4. `pio run -t uploadfs`.

## Отличия от браузера

- Rounded и Sharp рисуются как Outlined.
- `font-variation-settings` (`FILL`, `wght`, `GRAD`, `opsz`) не работает: иконки по имени всегда
  контурные, как у Google по умолчанию.
- `textContent` элемента с иконкой отдаёт её код, а не имя.

## Список

| Группа | Имена |
|---|---|
| Навигация и интерфейс (22) | `home` `arrow_back` `arrow_forward` `arrow_upward` `arrow_downward` `chevron_left` `chevron_right` `expand_less` `expand_more` `close` `menu` `more_vert` `more_horiz` `apps` `search` `settings` `tune` `refresh` `sync` `undo` `redo` `open_in_new` |
| Действия и состояния (25) | `check` `check_circle` `add` `remove` `edit` `delete` `save` `share` `download` `upload` `content_copy` `content_paste` `content_cut` `info` `help` `warning` `error` `block` `lock` `lock_open` `visibility` `visibility_off` `star` `bookmark` `flag` |
| Формы и ввод (11) | `check_box` `check_box_outline_blank` `radio_button_checked` `radio_button_unchecked` `toggle_on` `toggle_off` `keyboard` `backspace` `keyboard_return` `mic` `mic_off` |
| Статус часов (30) | `battery_full` `battery_5_bar` `battery_3_bar` `battery_1_bar` `battery_0_bar` `battery_charging_full` `battery_alert` `bolt` `bluetooth` `bluetooth_connected` `bluetooth_disabled` `wifi` `wifi_off` `signal_cellular_alt` `notifications` `notifications_active` `notifications_off` `do_not_disturb_on` `power_settings_new` `restart_alt` `light_mode` `dark_mode` `brightness_medium` `flashlight_on` `mobile_vibrate` `usb` `sd_card` `hard_drive` `update` `watch` |
| Время (13) | `schedule` `alarm` `alarm_on` `alarm_off` `alarm_add` `timer` `timer_off` `hourglass_empty` `hourglass_top` `calendar_today` `calendar_month` `event` `history` |
| Медиа (18) | `play_arrow` `pause` `stop` `skip_next` `skip_previous` `fast_forward` `fast_rewind` `eject` `volume_up` `volume_down` `volume_off` `shuffle` `repeat` `music_note` `headphones` `movie` `photo_camera` `image` |
| Связь и люди (12) | `call` `call_end` `mail` `chat` `sms` `send` `person` `group` `account_circle` `contacts` `language` `translate` |
| Здоровье и спорт (10) | `favorite` `monitor_heart` `directions_walk` `directions_run` `directions_bike` `footprint` `local_fire_department` `fitness_center` `bedtime` `water_drop` |
| Погода (11) | `sunny` `partly_cloudy_day` `cloud` `rainy` `thunderstorm` `weather_snowy` `foggy` `air` `clear_night` `thermostat` `umbrella` |
| Места и дорога (11) | `location_on` `my_location` `near_me` `navigation` `explore` `map` `directions_car` `flight` `train` `restaurant` `local_cafe` |
| Файлы и данные (9) | `folder` `description` `list` `grid_view` `edit_note` `attach_file` `qr_code` `cloud_download` `cloud_upload` |
| Деньги и игры (7) | `trending_up` `trending_down` `currency_bitcoin` `payments` `casino` `sports_esports` `extension` |

**Знаки в тексте** (какой иконкой рисуется каждый — `SIGNS` в `scripts/make_fonts.py`):

- контурные (36): ⌂ ← → ↑ ↓ ✕ ☰ ⋮ ⋯ ↻ ↶ ↷ ✓ ✎ ℹ ⚠ ☆ ☑ ☐ ◉ ○ ⌨ ⌫ ↵ ⊖ ⏻ ☼ ☾ ⌚ ⏰ ⏱ ⏳ ✉ ♡ ❄ ₿
- залитые (21): ⚙ ★ ⚑ ⚡ ⌛ ▶ ⏸ ⏹ ⏭ ⏮ ⏩ ⏪ ⏏ ♪ ☎ ♥ ☀ ⛅ ☁ ☂ ✈

`−` (минус) у шрифта текста есть свой, его иконка не перекрывает.
