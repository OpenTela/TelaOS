# Native App Specification v0.2

Единая система виджетов для FutureClock. Используется и HTML-движком, и нативными приложениями.

## Changelog

- **v0.2**: Единая система виджетов (без дублирования HTML/Native). Без макросов для полей — честное повторение + concept-валидация. Callback cleanup через WidgetCallbacks.
- **v0.1**: Начальная версия

---

## Архитектура

```
┌─────────────────────────────────────────────┐
│              widgets/                        │
│  Label, Button, Slider, Switch, Input, Image │
│  w_common.h — типы, Align, хелперы          │
│  w_build.h  — ui::build()                    │
├──────────────────────┬──────────────────────┤
│   HTML engine        │   NativeApp          │
│   html_parser →      │   designated init →  │
│   заполняет поля →   │   заполняет поля →   │
│   widget.create()    │   widget.create()    │
│                      │                      │
│   + bindings         │   + прямые вызовы    │
│   + templates        │     setText()        │
│   + State::store()   │     setValue()        │
└──────────────────────┴──────────────────────┘
              │                │
              ▼                ▼
         ┌────────────────────────┐
         │        LVGL v9         │
         └────────────────────────┘
```

Один `create()` на виджет. HTML или C++ — только способ заполнения полей.

---

## Виджеты

Каждый виджет — aggregate struct (C++20 designated init ✓). Поля повторяются (не наследуются) — это требование стандарта.

### Label

```cpp
struct Label {
    const char* text     = "";
    int         font     = 0;
    uint32_t    color    = 0xFFFFFF;
    uint32_t    bgcolor  = NO_COLOR;
    int         radius   = 0;
    lv_align_t  align    = LV_ALIGN_TOP_LEFT;
    int         x = 0, y = 0, w = 0, h = 0;
    bool        visible  = true;
    lv_obj_t*   _        = nullptr;

    Label& create(lv_obj_t* parent);
    Label& setText(const char* t);
    Label& setColor(uint32_t c);
    Label& setBgColor(uint32_t c);
    Label& setVisible(bool v);
};
```

### Button

```cpp
struct Button {
    const char* text     = "";
    int         font     = 0;
    Action      onClick  = nullptr;
    Action      onHold   = nullptr;
    uint32_t    color    = 0xFFFFFF;
    uint32_t    bgcolor  = 0x333333;
    int         radius   = 0;
    lv_align_t  align    = LV_ALIGN_TOP_LEFT;
    int         x = 0, y = 0, w = 0, h = 0;
    bool        visible  = true;
    lv_obj_t*   _        = nullptr;

    Button& create(lv_obj_t* parent);
    Button& setText(const char* t);
    Button& setEnabled(bool e);
    Button& setVisible(bool v);
};
```

### Slider

```cpp
struct Slider {
    int         min      = 0;
    int         max      = 100;
    int*        bind     = nullptr;    // LVGL → variable
    Action      onChange = nullptr;
    uint32_t    color    = NO_COLOR;
    uint32_t    bgcolor  = NO_COLOR;
    lv_align_t  align    = LV_ALIGN_TOP_LEFT;
    int         x = 0, y = 0, w = 0, h = 0;
    bool        visible  = true;
    lv_obj_t*   _        = nullptr;

    Slider& create(lv_obj_t* parent);
    Slider& setValue(int v);
    int     getValue() const;
};
```

### Switch

```cpp
struct Switch {
    bool*       bind     = nullptr;    // LVGL → variable
    Action      onChange = nullptr;
    uint32_t    bgcolor  = NO_COLOR;
    lv_align_t  align    = LV_ALIGN_TOP_LEFT;
    int         x = 0, y = 0, w = 0, h = 0;
    bool        visible  = true;
    lv_obj_t*   _        = nullptr;

    Switch& create(lv_obj_t* parent);
    Switch& setValue(bool v);
    bool    getValue() const;
};
```

### Input

```cpp
struct Input {
    const char* placeholder = "";
    bool        password    = false;
    Action      onEnter     = nullptr;
    Action      onBlur      = nullptr;
    uint32_t    color       = 0xFFFFFF;
    uint32_t    bgcolor     = NO_COLOR;
    lv_align_t  align       = LV_ALIGN_TOP_LEFT;
    int         x = 0, y = 0, w = 0, h = 0;
    bool        visible     = true;
    lv_obj_t*   _           = nullptr;

    Input& create(lv_obj_t* parent);
    Input& setText(const char* t);
    const char* getText() const;
    Input& focus();
};
```

### Image

```cpp
struct Image {
    const lv_img_dsc_t* src     = nullptr;
    const char*         srcPath = nullptr;
    lv_align_t  align    = LV_ALIGN_TOP_LEFT;
    int         x = 0, y = 0, w = 0, h = 0;
    bool        visible  = true;
    lv_obj_t*   _        = nullptr;

    Image& create(lv_obj_t* parent);
    Image& setSrc(const lv_img_dsc_t* s);
};
```

---

## Типы и константы

```cpp
using Action = std::function<void()>;

constexpr uint32_t NO_COLOR = UINT32_MAX;    // "не устанавливать фон"

constexpr lv_align_t top_left     = LV_ALIGN_TOP_LEFT;
constexpr lv_align_t top_mid      = LV_ALIGN_TOP_MID;
constexpr lv_align_t top_right    = LV_ALIGN_TOP_RIGHT;
constexpr lv_align_t left_mid     = LV_ALIGN_LEFT_MID;
constexpr lv_align_t center       = LV_ALIGN_CENTER;
constexpr lv_align_t right_mid    = LV_ALIGN_RIGHT_MID;
constexpr lv_align_t bottom_left  = LV_ALIGN_BOTTOM_LEFT;
constexpr lv_align_t bottom_mid   = LV_ALIGN_BOTTOM_MID;
constexpr lv_align_t bottom_right = LV_ALIGN_BOTTOM_RIGHT;
```

---

## ui::build()

```cpp
// С фоном
ui::build(parent, 0x1A1A2E,
    title, temp, btnRefresh
);

// Без фона
ui::build(parent,
    label1, label2
);

// Смешанные (именованные + анонимные)
ui::build(page(), 0x000000,
    Label{ .text = "Static", .color = 0x888888, .align = top_mid },
    dynamicLabel,
    myButton
);
```

---

## NativeApp

```cpp
class NativeApp {
public:
    NativeApp(const char* name, const char* title,
              const lv_img_dsc_t* icon = nullptr);

    virtual void onCreate()  {}    // создать UI
    virtual void onDestroy() {}    // cleanup
    virtual void onTick()    {}    // периодический вызов

    lv_obj_t* page() const;        // корневой LVGL объект
    void goHome();                  // вернуться в лаунчер

    const char* name() const;
    const char* title() const;
    const lv_img_dsc_t* icon() const;

    // Registry
    static void registerApp(NativeApp* app);
    static NativeApp* find(const char* name);
    static const std::vector<NativeApp*>& all();
};

#define REGISTER_NATIVE_APP(ClassName)  // создаёт static instance + авторегистрация
```

---

## Callbacks

Все callbacks (onClick, onChange, onEnter...) хранятся через `WidgetCallbacks::bind()` — heap-allocated Action, привязанная к LVGL event. Безопасно для анонимных и именованных виджетов.

При выходе из приложения вызывается `WidgetCallbacks::cleanup()` — освобождает все stored Actions.

---

## Файловая структура

```
src/
├── widgets/                  # ЕДИНАЯ система виджетов
│   ├── w_common.h            # Action, Align, NO_COLOR, хелперы, WidgetCallbacks
│   ├── w_label.h             # Label
│   ├── w_button.h            # Button
│   ├── w_slider.h            # Slider
│   ├── w_switch.h            # Switch
│   ├── w_input.h             # Input
│   ├── w_image.h             # Image
│   ├── w_build.h             # ui::build()
│   └── widgets.h             # master include
│
├── core/
│   ├── native_app.h          # NativeApp + REGISTER_NATIVE_APP
│   ├── native_app.cpp        # Registry, page(), goHome()
│   ├── app_manager.h         # + native support
│   └── app_manager.cpp       # + scanNative, launchNative
│
├── apps/                      # Нативные приложения
│   └── counter_app.h         # Тестовый пример
│
├── ui/                        # HTML-специфичное (существующее)
│   ├── html_parser.h          # Фаза 2: будет заполнять widget structs
│   ├── ui_engine.h            # Без изменений
│   └── ui_html.cpp            # Фаза 2: widget.create() вместо raw LVGL
...
```

---

## План миграции

### Фаза 1 — Добавляем, не ломаем ✓

Новые файлы: `widgets/`, `native_app.h/cpp`. HTML-код не трогаем. Нативные приложения работают рядом с HTML.

### Фаза 2 — Мигрируем ui_html.cpp по одному виджету

Порядок: Label → Image → Button → Switch → Slider → Input → Canvas

Каждый шаг: html_parser заполняет struct → вызывает create() → тестируем все HTML-приложения.

### Фаза 3 — Убираем дублирование

Удаляем `UI::Widget` (старый widget.h), `UI::Element` становится тонкой обёрткой поверх виджетов (только bindings/templates).

---

## Интеграция с App::Manager

Минимальные изменения в существующем коде:

```cpp
// app_manager.h — AppInfo расширяется:
struct AppInfo {
    std::string name;
    std::string title;
    enum Source { HTML, Native } source = HTML;
    NativeApp* nativePtr = nullptr;
    // ...
};

// app_manager.cpp — scanApps() добавляет нативные:
void Manager::scanApps() {
    // 1. Нативные из registry
    for (auto* app : NativeApp::all()) {
        AppInfo info;
        info.name = app->name();
        info.title = app->title();
        info.source = AppInfo::Native;
        info.nativePtr = app;
        m_apps.push_back(info);
    }

    // 2. HTML из /apps/ (как сейчас)
    // ...
}

// app_manager.cpp — launch() проверяет тип:
bool Manager::launch(const std::string& name) {
    for (auto& app : m_apps) {
        if (app.name == name) {
            if (app.source == AppInfo::Native)
                return launchNative(app.nativePtr);
            else
                return loadApp(app.path());
        }
    }
    return false;
}

// app_manager.cpp — returnToLauncher() чистит callbacks:
void Manager::returnToLauncher() {
    if (m_currentNative) {
        m_currentNative->onDestroy();
        m_currentNative = nullptr;
    }
    WidgetCallbacks::cleanup();
    // ... existing cleanup ...
}
```
