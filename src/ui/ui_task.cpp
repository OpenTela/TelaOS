#include "ui/ui_task.h"

void* ITask::operator new(size_t size) {
    void* p = heap_caps_malloc(size, MALLOC_CAP_SPIRAM);
    if (!p)
        p = heap_caps_malloc(size, MALLOC_CAP_DEFAULT);
    if (!p)
        throw std::bad_alloc();
    return p;
}

void ITask::operator delete(void* p) noexcept { heap_caps_free(p); }

UpdateLabelTask::UpdateLabelTask(const P::String& labelId, const P::String& value)
    : m_labelId(labelId), m_value(value) {}

UpdateLabelTask::UpdateLabelTask(const char* labelId, const char* value)
    : m_labelId(labelId ? labelId : ""), m_value(value ? value : "") {}

void UpdateLabelTask::execute() {
    ui_set_text_internal(m_labelId.c_str(), m_value.c_str());
}

P::String UpdateLabelTask::key() const {
    return P::String("lbl:") + m_labelId.c_str();
}

UpdateBindingTask::UpdateBindingTask(const P::String& varName, const P::String& value)
    : m_varName(varName), m_value(value) {}

UpdateBindingTask::UpdateBindingTask(const char* varName, const char* value)
    : m_varName(varName ? varName : ""), m_value(value ? value : "") {}

void UpdateBindingTask::execute() {
    ui_update_bindings(m_varName.c_str(), m_value.c_str());
}

P::String UpdateBindingTask::key() const {
    return P::String("bind:") + m_varName.c_str();
}

std::unique_ptr<ITask> makeUpdateLabel(const P::String& labelId, const P::String& value) {
    return std::make_unique<UpdateLabelTask>(labelId, value);
}

std::unique_ptr<ITask> makeUpdateBinding(const P::String& varName, const P::String& value) {
    return std::make_unique<UpdateBindingTask>(varName, value);
}
