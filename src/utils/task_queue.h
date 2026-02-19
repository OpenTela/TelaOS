#pragma once

#include "ui/ui_task.h"
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <map>
#include <vector>
#include <memory>
#include "utils/psram_alloc.h"

/**
 * TaskQueue - потокобезопасная очередь задач с дедупликацией
 */
class TaskQueue {
private:
    P::Map<P::String, std::unique_ptr<ITask>> m_keyed;
    std::vector<std::unique_ptr<ITask>> m_unkeyed;
    SemaphoreHandle_t m_mutex = nullptr;
    
    static TaskQueue* s_instance;
    TaskQueue();
    
public:
    static TaskQueue& instance();
    static void destroy();
    
    void push(std::unique_ptr<ITask> task);
    void process();
    size_t size();
    void clear();
};

namespace UI {
    void postTask(std::unique_ptr<ITask> task);
    void updateLabel(const P::String& labelId, const P::String& value);
    void updateBinding(const P::String& varName, const P::String& value);
    void processTasks();
}
