#pragma once

#include <functional>
#include "utils/psram_alloc.h"

/**
 * CallQueue — thread-safe queue for deferred script calls.
 * 
 * Any thread can push(), only main loop calls process().
 * Transport-agnostic: used by timers, onclick, BLE commands, Serial console.
 */
namespace CallQueue {

using Handler = std::function<void(const P::String& funcName)>;

void init();
void shutdown();

/// Set the handler that executes queued calls (typically routes to IScriptEngine)
void setHandler(Handler handler);

/// Queue a function call (thread-safe, deduplicated)
void push(const P::String& funcName);

/// Process all queued calls on the main thread
void process();

bool hasPending();
size_t size();
uint32_t droppedCount();

} // namespace CallQueue
