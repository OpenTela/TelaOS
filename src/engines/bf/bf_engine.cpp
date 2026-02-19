#include "engines/bf/bf_engine.h"
#include "core/state_store.h"
#include "utils/log_config.h"
#include <cstring>
#include <vector>

static const char* TAG = "BfEngine";

bool BfEngine::init() {
    LOG_I(Log::LUA, "BfEngine::init()");
    m_code = "";
    m_output = "";
    return true;
}

bool BfEngine::execute(const char* code) {
    if (!code || !code[0]) return false;
    
    size_t codeLen = strlen(code);
    LOG_I(Log::LUA, "BfEngine::execute(%d bytes)", (int)codeLen);
    
    // Strip non-BF characters and store
    m_code = "";
    m_code.reserve(codeLen);
    for (const char* p = code; *p; p++) {
        char c = *p;
        if (c == '+' || c == '-' || c == '>' || c == '<' ||
            c == '.' || c == ',' || c == '[' || c == ']') {
            m_code += c;
        }
    }
    
    LOG_I(Log::LUA, "BF: %d instructions stored", (int)m_code.size());
    return true;
}

bool BfEngine::call(const char* func) {
    if (m_code.empty()) {
        LOG_W(Log::LUA, "BfEngine::call('%s') — no code loaded", func ? func : "");
        return false;
    }
    
    LOG_I(Log::LUA, "BfEngine::call('%s') — running BF", func ? func : "");
    return run();
}

bool BfEngine::run() {
    int len = m_code.size();
    
    // Precompute bracket pairs
    std::vector<int> jumps(len, -1);
    std::vector<int> stack;
    
    for (int i = 0; i < len; i++) {
        if (m_code[i] == '[') {
            stack.push_back(i);
        } else if (m_code[i] == ']') {
            if (stack.empty()) {
                LOG_E(Log::LUA, "BF: unmatched ']' at %d", i);
                return false;
            }
            int j = stack.back();
            stack.pop_back();
            jumps[j] = i;
            jumps[i] = j;
        }
    }
    
    if (!stack.empty()) {
        LOG_E(Log::LUA, "BF: unmatched '[' at %d", stack.back());
        return false;
    }
    
    // Read stdin from state
    P::String stdinBuf = State::store().getAsString("_stdin");
    int stdinPos = 0;
    
    // Execute
    // Static tape — 4KB on stack would overflow ESP32
    static uint8_t tape[TAPE_SIZE];
    memset(tape, 0, TAPE_SIZE);
    int ptr = 0;
    int pc = 0;
    int steps = 0;
    m_output = "";
    
    while (pc < len && steps < MAX_STEPS) {
        char c = m_code[pc];
        
        switch (c) {
            case '+': tape[ptr]++; break;
            case '-': tape[ptr]--; break;
            
            case '>':
                if (++ptr >= TAPE_SIZE) { ptr = TAPE_SIZE - 1; }
                break;
            case '<':
                if (--ptr < 0) { ptr = 0; }
                break;
                
            case '.':
                m_output += (char)tape[ptr];
                break;
            case ',':
                tape[ptr] = (stdinPos < (int)stdinBuf.size())
                    ? (uint8_t)stdinBuf[stdinPos++] : 0;
                break;
                
            case '[':
                if (tape[ptr] == 0) pc = jumps[pc];
                break;
            case ']':
                if (tape[ptr] != 0) pc = jumps[pc];
                break;
        }
        
        pc++;
        steps++;
    }
    
    if (steps >= MAX_STEPS) {
        LOG_W(Log::LUA, "BF: hit step limit (%d)", MAX_STEPS);
        m_output += "\n[limit]";
    }
    
    LOG_I(Log::LUA, "BF: done in %d steps, output %d bytes", steps, (int)m_output.size());
    
    // Write output to state._stdout and notify UI
    state_store_set("_stdout", m_output.c_str());
    notifyState("_stdout", m_output.c_str());
    
    return true;
}

const char* BfEngine::name() const {
    return "BfEngine";
}

void BfEngine::shutdown() {
    LOG_I(Log::LUA, "BfEngine::shutdown()");
    m_code = "";
    m_output = "";
}
