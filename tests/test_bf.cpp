/**
 * BF Engine Test — Real project code, only external deps mocked
 */
#include <cstdio>
#include <cstring>
#include <string>

#include "engines/bf/bf_engine.h"
#include "core/state_store.h"

// BF code for "Hello, World!" (generated, not hand-written)
const char* HELLO_WORLD_BF = 
    "++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++"  // H (72)
    ".++++++++++++++++++++++++++++++"  // e (101)
    ".+++++++..+++."  // llo
    "----------------------------------------------------------------------."  // , (44)
    "--------------------------------."  // space (32)
    "++++++++++++++++++++++++++++++++++++++++++++++++++++++++."  // W (87)
    "++++++++++++."  // o (111) 
    "+++.------."  // rl
    "--------."  // d
    "------------------------------------------------------------------------.";  // ! (33)

// Optimized Hello World (uses loops)
const char* HELLO_OPTIMIZED = 
    "++++++++[>++++[>++>+++>+++>+<<<<-]>+>+>->>+[<]<-]"
    ">>.>---.+++++++..+++.>>.<-.<.+++.------.--------.>>+.>++.";

int main() {
    printf("=== BF Engine Integration Test ===\n");
    printf("Testing REAL bf_engine.o + state_store.o + base_script_engine.o\n\n");
    
    BfEngine bf;
    bf.init();
    
    // Test 1: Simple Hello World
    printf("Test 1: Hello World (optimized BF)...\n");
    bf.execute(HELLO_OPTIMIZED);
    bf.call("run");
    
    P::String out = State::store().getAsString("_stdout");
    printf("  Output: '%s'\n", out.c_str());
    printf("  Expected: 'Hello World!\\n'\n");
    printf("  %s\n\n", (out == "Hello World!\n") ? "✓ PASS" : "✗ FAIL");
    
    // Test 2: Input/Output (cat program)
    printf("Test 2: Cat program (echo input)...\n");
    State::store().set("_stdin", "Test123");
    State::store().set("_stdout", "");
    bf.execute(",.,.,.,.,.,.,.");  // Read 7 chars, output each
    bf.call("run");
    
    out = State::store().getAsString("_stdout");
    printf("  Input:  'Test123'\n");
    printf("  Output: '%s'\n", out.c_str());
    printf("  %s\n\n", (out == "Test123") ? "✓ PASS" : "✗ FAIL");
    
    // Test 3: Computation (add two numbers)
    printf("Test 3: Add 3+5...\n");
    State::store().set("_stdin", "");
    State::store().set("_stdout", "");
    // Cell 0 = 3, Cell 1 = 5, add them, output ASCII digit
    bf.execute("+++>+++++[<+>-]<++++++++++++++++++++++++++++++++++++++++++++++++.");
    bf.call("run");
    
    out = State::store().getAsString("_stdout");
    printf("  Output: '%s'\n", out.c_str());
    printf("  Expected: '8'\n");
    printf("  %s\n\n", (out == "8") ? "✓ PASS" : "✗ FAIL");
    
    bf.shutdown();
    
    printf("=== Tests Complete ===\n");
    return 0;
}
