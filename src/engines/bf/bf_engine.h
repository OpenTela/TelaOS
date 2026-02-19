#pragma once

#include "core/base_script_engine.h"
#include <vector>

/**
 * BfEngine - Brainfuck interpreter for EvolutionOS.
 * 
 * Output ('.' command) writes to state._stdout.
 * Input (',' command) reads from state._stdin (default: zeros).
 */
class BfEngine : public BaseScriptEngine {
public:
    bool init() override;
    bool execute(const char* code) override;
    bool call(const char* func) override;
    const char* name() const override;
    void shutdown() override;

private:
    static constexpr int TAPE_SIZE = 4096;
    static constexpr int MAX_STEPS = 500000;
    
    P::String m_code;    // parsed BF instructions
    P::String m_output;  // accumulated output
    
    bool run();
};
