#include "utils/task_queue.h"
#include "utils/log_config.h"

static const char* TAG = "TaskQueue";
static const char* VERSION = "1.2.0";

TaskQueue* TaskQueue::s_instance = nullptr;

TaskQueue::TaskQueue() {
    m_mutex = xSemaphoreCreateMutex();
}

TaskQueue& TaskQueue::instance() {
    if (!s_instance) {
        s_instance = new TaskQueue();
        LOG_I(Log::APP, "TaskQueue v%s initialized", VERSION);
    }
    return *s_instance;
}

void TaskQueue::push(std::unique_ptr<ITask> task) {
    if (!task) return;
    
    P::String k = task->key();
    
    xSemaphoreTake(m_mutex, portMAX_DELAY);
    if (k.empty()) {
        m_unkeyed.push_back(std::move(task));
    } else {
        m_keyed[k] = std::move(task);
    }
    xSemaphoreGive(m_mutex);
}

void TaskQueue::process() {
    P::Map<P::String, std::unique_ptr<ITask>> keyed;
    std::vector<std::unique_ptr<ITask>> unkeyed;
    
    xSemaphoreTake(m_mutex, portMAX_DELAY);
    keyed.swap(m_keyed);
    unkeyed.swap(m_unkeyed);
    xSemaphoreGive(m_mutex);
    
    for (auto& [key, task] : keyed) {
        if (task) task->execute();
    }
    for (auto& task : unkeyed) {
        if (task) task->execute();
    }
}

size_t TaskQueue::size() {
    xSemaphoreTake(m_mutex, portMAX_DELAY);
    size_t sz = m_keyed.size() + m_unkeyed.size();
    xSemaphoreGive(m_mutex);
    return sz;
}

void TaskQueue::clear() {
    xSemaphoreTake(m_mutex, portMAX_DELAY);
    m_keyed.clear();
    m_unkeyed.clear();
    xSemaphoreGive(m_mutex);
}

void TaskQueue::destroy() {
    if (s_instance) {
        s_instance->clear();
        vSemaphoreDelete(s_instance->m_mutex);
        delete s_instance;
        s_instance = nullptr;
    }
}

namespace UI {

void postTask(std::unique_ptr<ITask> task) {
    TaskQueue::instance().push(std::move(task));
}

void updateLabel(const P::String& labelId, const P::String& value) {
    TaskQueue::instance().push(std::make_unique<UpdateLabelTask>(labelId, value));
}

void updateBinding(const P::String& varName, const P::String& value) {
    TaskQueue::instance().push(std::make_unique<UpdateBindingTask>(varName, value));
}

void processTasks() {
    TaskQueue::instance().process();
}

} // namespace UI
