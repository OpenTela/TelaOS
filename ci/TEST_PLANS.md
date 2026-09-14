# Test Plans

Что нужно покрыть тестами.

## Высокий приоритет

### onenter / onblur callbacks
- `<input onenter="fn"/>` — вызов Lua при Enter
- `<input onblur="fn"/>` — вызов Lua при потере фокуса
- onenter + onblur на одном input — оба срабатывают
- onenter без onblur — не крашится
- Callback получает актуальное значение state

### setAttr / getAttr юнит-тест
- `setAttr("id", "bgcolor", "#ff0000")` — меняет цвет
- `setAttr("id", "text", "new text")` — меняет текст
- `setAttr("id", "visible", "false")` — скрывает
- `getAttr("id", "text")` — возвращает текст
- Несуществующий виджет — false, не крашится
- Неизвестный атрибут — false, не крашится

## Средний приоритет

### image виджет
- `<image src="icon.png"/>` — создаётся
- Размеры `w`/`h` — применяются
- Отсутствующий файл — не крашится

### markdown виджет
- `<markdown>` — создаётся как spangroup
- CSS применяется

### canvas через Lua
- `canvas.rect()` из Lua скрипта, не из C++
- `canvas.clear()` + `canvas.refresh()` через Lua

## Низкий приоритет

### BLE протокол
- JSON парсинг команд: setText, setState, navigate, call
- JSON формирование ответов
- Требует нормальный ArduinoJson стаб

### fetch() end-to-end
- Mock HTTP → BLE → callback → state update
- Ошибка сети → callback с status=0
