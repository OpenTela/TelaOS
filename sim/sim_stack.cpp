// sim_stack.cpp — the firmware's stack, 4/5 of the watch's loop task (sim_stack.h).
//
// The firmware runs in a context of its own (ucontext): its stack is an mmap'ed block of exactly
// that size, painted, with unmapped pages right below it. The simulator switches to it for every
// setup() and loop() and back. A call deeper than that stack touches those pages: SIGSEGV,
// and the handler, on a stack of its own, prints the frames that went so deep and stops.
#include "sim_stack.h"

#include "freertos/task.h"

#include <alloca.h>
#include <dlfcn.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <ucontext.h>
#include <unistd.h>
#include <unwind.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

// The watch's loop task, unless the firmware sets its own (SET_LOOP_TASK_STACK_SIZE in main.cpp):
// CONFIG_ARDUINO_LOOP_STACK_SIZE of Arduino-ESP32 2.0.17
__attribute__((weak)) size_t getArduinoLoopTaskStackSize() { return 8192; }

namespace {

constexpr uint8_t kPaint = 0xA5;       // the stack not used yet
constexpr size_t kPage = 4096;
constexpr size_t kGuard = 16 * kPage;  // unmapped below the stack: a frame would have to be larger to jump it
constexpr size_t kEntry = 4 * kPage;   // above it: the simulator's own way onto the stack
constexpr int kMaxFrames = 128;

ucontext_t g_host;
ucontext_t g_fw;
const std::function<void()>* g_job = nullptr;
const std::function<void()>* g_hostJob = nullptr;
bool g_done = false;
bool g_inside = false;
uint8_t* g_map = nullptr;
uint8_t* g_bottom = nullptr;  // the lowest byte the firmware may use
uint8_t* g_top = nullptr;     // its first frame starts here
size_t g_size = 0;
size_t g_watch = 0;  // the watch's loop task
char g_exe[1024];
alignas(16) uint8_t g_signalStack[64 * 1024];

// The firmware's side: one job, then back to the simulator
__attribute__((noinline)) void serve() {
    for (;;) {
        g_inside = true;
        (*g_job)();
        g_inside = false;
        g_done = true;
        swapcontext(&g_fw, &g_host);
    }
}

// Onto the stack: the room above g_top is the simulator's, so the firmware has exactly g_size
void entry() {
    uint8_t here = 0;
    const uintptr_t sp = reinterpret_cast<uintptr_t>(&here);
    const uintptr_t top = reinterpret_cast<uintptr_t>(g_top);
    const size_t above = sp > top ? sp - top : 0;
    volatile uint8_t* room = static_cast<volatile uint8_t*>(alloca(above));
    if (above)
        room[0] = 0;  // keep it: serve() starts below
    serve();
}

// --- The overflow: what went so deep ---

struct Frame {
    uintptr_t ip;
    uintptr_t cfa;  // the caller's stack pointer at the call: frame sizes are the differences
};
Frame g_frames[kMaxFrames];
int g_count = 0;

_Unwind_Reason_Code collect(_Unwind_Context* c, void*) {
    if (g_count >= kMaxFrames)
        return _URC_END_OF_STACK;
    int before = 0;
    uintptr_t ip = _Unwind_GetIPInfo(c, &before);
    if (!before && ip)
        --ip;  // a return address: the call is the instruction before it
    g_frames[g_count].ip = ip;
    g_frames[g_count].cfa = _Unwind_GetCFA(c);
    ++g_count;
    return _URC_NO_REASON;
}

void say(const char* s) {
    const size_t n = std::strlen(s);
    if (write(1, s, n) < 0)
        _exit(4);
}

// "function at file:line" for each address, by addr2line on the simulator's own file
int symbolize(const uintptr_t* offsets, int n, char* out, size_t cap) {
    static char text[kMaxFrames][24];
    static char* argv[kMaxFrames + 8];
    int a = 0;
    argv[a++] = const_cast<char*>("addr2line");
    argv[a++] = const_cast<char*>("-f");
    argv[a++] = const_cast<char*>("-C");
    argv[a++] = const_cast<char*>("-p");
    argv[a++] = const_cast<char*>("-s");
    argv[a++] = const_cast<char*>("-e");
    argv[a++] = g_exe;
    for (int i = 0; i < n; ++i) {
        std::snprintf(text[i], sizeof text[i], "0x%lx", static_cast<unsigned long>(offsets[i]));
        argv[a++] = text[i];
    }
    argv[a] = nullptr;
    int fd[2];
    if (pipe(fd) != 0)
        return 0;
    const pid_t pid = fork();
    if (pid == 0) {
        dup2(fd[1], 1);
        close(fd[0]);
        execvp("addr2line", argv);
        _exit(127);
    }
    close(fd[1]);
    size_t len = 0;
    ssize_t got = 0;
    while (len + 1 < cap && (got = read(fd[0], out + len, cap - 1 - len)) > 0)
        len += static_cast<size_t>(got);
    out[len] = '\0';
    close(fd[0]);
    int status = 0;
    waitpid(pid, &status, 0);
    return static_cast<int>(len);
}

void onFault(int sig, siginfo_t* info, void*) {
    const uintptr_t at = reinterpret_cast<uintptr_t>(info->si_addr);
    const uintptr_t map = reinterpret_cast<uintptr_t>(g_map);
    const bool ours = g_map && at >= map && at < reinterpret_cast<uintptr_t>(g_bottom);
    if (!ours) {
        signal(sig, SIG_DFL);  // not the stack: crash as before
        return;
    }
    std::fflush(stdout);
    static char line[512];
    std::snprintf(line, sizeof line,
                  "[sim] STACK OVERFLOW: the firmware went deeper than its %zu bytes of stack (the watch's loop "
                  "task has %zu). Out of it the watch reboots: \"Stack canary watchpoint triggered (loopTask)\"\n"
                  "[sim] stack, deepest first: bytes of each frame, function\n",
                  g_size, g_watch);
    say(line);

    g_count = 0;
    _Unwind_Backtrace(collect, nullptr);
    // The frames on the firmware's stack, deepest first. libgcc gives each frame the CFA of the
    // one below it, which is where this frame ends: the first one's is the stack at the fault.
    // A frame takes up to where the next one ends; serve() ends the firmware's last
    Dl_info self;
    const bool selfKnown = dladdr(reinterpret_cast<void*>(&onFault), &self) && self.dli_fbase;
    const uintptr_t base = selfKnown ? reinterpret_cast<uintptr_t>(self.dli_fbase) : 0;
    static uintptr_t offsets[kMaxFrames];
    static uintptr_t ends[kMaxFrames + 1];
    static const char* libs[kMaxFrames];
    const uintptr_t top = reinterpret_cast<uintptr_t>(g_top);
    const uintptr_t serveAt = reinterpret_cast<uintptr_t>(&serve);
    int n = 0, named = 0;
    ends[0] = top;
    for (int i = 0; i < g_count && n < kMaxFrames; ++i) {
        const Frame& f = g_frames[i];
        const bool onStack = f.cfa > map && f.cfa <= top + kEntry;
        if (!onStack)
            continue;
        ends[n] = f.cfa;
        const bool inServe = f.ip >= serveAt && f.ip < serveAt + 512;
        if (inServe)
            break;  // the simulator's, above the firmware's frames
        Dl_info dl;
        const bool known = dladdr(reinterpret_cast<void*>(f.ip), &dl) && dl.dli_fbase;
        const bool inSim = known && reinterpret_cast<uintptr_t>(dl.dli_fbase) == base;
        const char* lib = known && dl.dli_fname ? std::strrchr(dl.dli_fname, '/') : nullptr;
        const char* sym = known && dl.dli_sname ? dl.dli_sname : nullptr;
        libs[n] = inSim ? nullptr : sym ? sym : lib ? lib + 1 : "?";
        if (inSim)
            offsets[named++] = f.ip - base;
        ++n;
        ends[n] = top;
    }
    static char names[kMaxFrames * 160];
    symbolize(offsets, named, names, sizeof names);
    char* p = names;
    static uintptr_t sizes[kMaxFrames];
    for (int i = 0; i < n; ++i)
        sizes[i] = ends[i + 1] - ends[i];
    for (int i = 0; i < n; ++i) {
        const char* name = libs[i];
        if (!name) {
            char* end = std::strchr(p, '\n');
            if (end)
                *end = '\0';
            char* at_ = std::strstr(p, " at ");  // no line numbers without -g: the function is enough
            if (at_)
                *at_ = '\0';
            name = *p ? p : "?";
            p = end ? end + 1 : p + std::strlen(p);
        }
        std::snprintf(line, sizeof line, "[sim] %6lu  %s\n", static_cast<unsigned long>(sizes[i]), name);
        say(line);
    }
    std::snprintf(line, sizeof line, "[sim] stack: at least %lu bytes, %d frames\n",
                  static_cast<unsigned long>(top - at), n);
    say(line);
    _exit(3);
}

}  // namespace

namespace SimStack {

void init(size_t bytes, size_t watch) {
    if (!bytes || g_map)
        return;
    g_size = bytes;
    g_watch = watch;
    const size_t span = kGuard + ((bytes + kPage - 1) / kPage) * kPage + kEntry;
    void* m = mmap(nullptr, span, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (m == MAP_FAILED) {
        std::fprintf(stderr, "[sim] no memory for the firmware's stack\n");
        std::exit(1);
    }
    g_map = static_cast<uint8_t*>(m);
    mprotect(g_map, kGuard, PROT_NONE);
    g_bottom = g_map + kGuard;
    g_top = g_bottom + bytes;
    std::memset(g_bottom, kPaint, bytes);

    const ssize_t len = readlink("/proc/self/exe", g_exe, sizeof g_exe - 1);
    g_exe[len > 0 ? len : 0] = '\0';
    g_count = 0;
    _Unwind_Backtrace(collect, nullptr);  // the unwinder loaded now, not first in the handler

    stack_t ss{};
    ss.ss_sp = g_signalStack;
    ss.ss_size = sizeof g_signalStack;
    sigaltstack(&ss, nullptr);
    struct sigaction sa{};
    sa.sa_sigaction = onFault;
    sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGSEGV, &sa, nullptr);
    sigaction(SIGBUS, &sa, nullptr);

    getcontext(&g_fw);
    g_fw.uc_stack.ss_sp = g_bottom;
    g_fw.uc_stack.ss_size = static_cast<size_t>(g_map + span - g_bottom);
    g_fw.uc_link = nullptr;
    makecontext(&g_fw, entry, 0);
}

void run(const std::function<void()>& job) {
    if (!g_map || g_inside) {
        job();
        return;
    }
    g_job = &job;
    g_done = false;
    for (;;) {
        swapcontext(&g_host, &g_fw);
        if (g_done)
            break;
        const std::function<void()>* h = g_hostJob;
        g_hostJob = nullptr;
        if (h)
            (*h)();
    }
    g_job = nullptr;
}

void onHost(const std::function<void()>& job) {
    if (!g_inside) {
        job();
        return;
    }
    g_hostJob = &job;
    g_inside = false;
    swapcontext(&g_fw, &g_host);
    g_inside = true;
}

size_t size() { return g_map ? g_size : 0; }

size_t used() {
    if (!g_map)
        return 0;
    const uint8_t* p = g_bottom;
    while (p < g_top && *p == kPaint)
        ++p;
    return static_cast<size_t>(g_top - p);
}

size_t watch() { return g_watch; }

std::string json() {
    char buf[96];
    std::snprintf(buf, sizeof buf, "{\"size\": %zu, \"used\": %zu, \"watch\": %zu}", size(), used(), g_watch);
    return buf;
}

}  // namespace SimStack

// `sys info` on the watch: the loop task's least free stack since boot (freertos/task.h)
UBaseType_t uxTaskGetStackHighWaterMark(TaskHandle_t) {
    return static_cast<UBaseType_t>(SimStack::size() - SimStack::used());
}
