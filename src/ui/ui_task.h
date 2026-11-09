#pragma once

#include <memory>
#include "utils/psram_alloc.h"

// Forward declarations - реализованы в ui_html.cpp
void ui_set_text_internal(const char* id, const char* text);
void ui_update_bindings(const char* varname, const char* value);

/**
 * ITask - базовый интерфейс для задач UI
 */
class ITask {
public:
    virtual ~ITask() = default;
    virtual void execute() = 0;
    virtual P::String key() const { return ""; }

    // Tasks live in PSRAM. An app's start queues one for each bound variable (excel: over a
    // hundred), and plain new is malloc's, which puts what is under 4 KB in DRAM
    static void* operator new(size_t size);
    static void operator delete(void* p) noexcept;
};

/**
 * UpdateLabelTask - обновление текста label по id
 */
class UpdateLabelTask : public ITask {
    P::String m_labelId;
    P::String m_value;
    
public:
    UpdateLabelTask(const P::String& labelId, const P::String& value);
    UpdateLabelTask(const char* labelId, const char* value);
    
    void execute() override;
    P::String key() const override;
};

/**
 * UpdateBindingTask - обновление всех элементов с {varname}
 */
class UpdateBindingTask : public ITask {
    P::String m_varName;
    P::String m_value;
    
public:
    UpdateBindingTask(const P::String& varName, const P::String& value);
    UpdateBindingTask(const char* varName, const char* value);
    
    void execute() override;
    P::String key() const override;
};

std::unique_ptr<ITask> makeUpdateLabel(const P::String& labelId, const P::String& value);
std::unique_ptr<ITask> makeUpdateBinding(const P::String& varName, const P::String& value);
