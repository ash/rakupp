// Stackful coroutines — see Coro.h. Three pieces:
//
//  · the context switch: a few lines of assembly per architecture that save
//    the callee-saved registers on the current stack, record the stack
//    pointer, load another one and restore ITS registers (Windows: Fibers,
//    which do the same inside the OS);
//  · stacks: reserved with mmap, a guard page at the bottom, committed by the
//    kernel only as they are touched, and pooled per thread so that a gather
//    does not pay a system call to start;
//  · the loop each stack runs: take the coroutine this stack was handed, run
//    its entry, switch back, and wait there to be handed the next one. A
//    pooled stack is parked inside that loop, so reusing it is one switch.
#include "Coro.h"

#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <vector>

#if RAKUPP_HAVE_CORO

#if defined(_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#else
#  include <sys/mman.h>
#  include <unistd.h>
#endif

// AddressSanitizer has to be told about every stack switch, or it reads the
// coroutine's frames as overflows of the thread's stack (and a fake-stack
// build loses track of which frames are live). A no-op in normal builds.
#if defined(__SANITIZE_ADDRESS__)
#  define RAKUPP_CORO_ASAN 1
#elif defined(__has_feature)
#  if __has_feature(address_sanitizer)
#    define RAKUPP_CORO_ASAN 1
#  endif
#endif
#ifdef RAKUPP_CORO_ASAN
extern "C" {
void __sanitizer_start_switch_fiber(void** fake_stack_save, const void* bottom, size_t size);
void __sanitizer_finish_switch_fiber(void* fake_stack_save, const void** bottom_old, size_t* size_old);
}
#endif

namespace rakupp {

// 256 MiB of address space per stack, as a worker thread gets: deep recursion
// inside a gather's block must not hit the recursion guard much earlier than
// it would outside one. Only the pages actually touched are ever committed.
static constexpr size_t kReserve = size_t(256) << 20;
// A stack the pool hands out again keeps whatever it had committed, so the
// pool stays small.
static constexpr size_t kPoolMax = 16;

struct CoroStack {
    Coro* current = nullptr;   // what this stack runs, or last ran
    void* asanFake = nullptr;  // AddressSanitizer's fake-stack handle for this stack
#if defined(_WIN32)
    void* fiber = nullptr;
    char* top = nullptr;       // the fiber's first frame, noted when it starts
#else
    char* map = nullptr;       // the mapping; its lowest pages are the guard
    size_t mapSize = 0;
    void* sp = nullptr;        // this stack's saved context while it is not running
#endif
};

void coroRunLoop(CoroStack* s);

#if !defined(_WIN32)

// ---- the switch ------------------------------------------------------------
// void* rakupp_coro_switch(void** save, void* to, void* arg)
//   Pushes the callee-saved registers, stores the stack pointer to *save,
//   switches to `to`, pops that context's registers and returns into it with
//   `arg` as the return value.
// rakupp_coro_trampoline: where a fresh stack's first switch lands. The
//   initial frame (coroFreshContext) leaves the CoroStack* and the address of
//   rakupp_coro_boot in callee-saved registers for it.
extern "C" void* rakupp_coro_switch(void** save, void* to, void* arg);
extern "C" void rakupp_coro_trampoline();
extern "C" void rakupp_coro_boot(CoroStack* s) { coroRunLoop(s); }

#if defined(__APPLE__)
#  define RKC_SYM(x) "_" #x
#  define RKC_HIDE(x) ".private_extern _" #x "\n"
#  define RKC_TYPE(x) ""
#else
#  define RKC_SYM(x) #x
#  define RKC_HIDE(x) ".hidden " #x "\n"
#  define RKC_TYPE(x) ".type " #x ", %function\n"
#endif

#if defined(__aarch64__)
// AAPCS64: x19–x29, x30 (the return address) and the low halves of v8–v15
// survive a call. 160 bytes keeps sp 16-aligned. x18 is the platform register
// on Apple and is left alone.
__asm__(
    ".text\n"
    ".p2align 2\n"
    ".globl " RKC_SYM(rakupp_coro_switch) "\n"
    RKC_HIDE(rakupp_coro_switch)
    RKC_TYPE(rakupp_coro_switch)
    RKC_SYM(rakupp_coro_switch) ":\n"
    "    sub  sp, sp, #160\n"
    "    stp  x19, x20, [sp, #0]\n"
    "    stp  x21, x22, [sp, #16]\n"
    "    stp  x23, x24, [sp, #32]\n"
    "    stp  x25, x26, [sp, #48]\n"
    "    stp  x27, x28, [sp, #64]\n"
    "    stp  x29, x30, [sp, #80]\n"
    "    stp  d8,  d9,  [sp, #96]\n"
    "    stp  d10, d11, [sp, #112]\n"
    "    stp  d12, d13, [sp, #128]\n"
    "    stp  d14, d15, [sp, #144]\n"
    "    mov  x9, sp\n"
    "    str  x9, [x0]\n"
    "    mov  sp, x1\n"
    "    ldp  x19, x20, [sp, #0]\n"
    "    ldp  x21, x22, [sp, #16]\n"
    "    ldp  x23, x24, [sp, #32]\n"
    "    ldp  x25, x26, [sp, #48]\n"
    "    ldp  x27, x28, [sp, #64]\n"
    "    ldp  x29, x30, [sp, #80]\n"
    "    ldp  d8,  d9,  [sp, #96]\n"
    "    ldp  d10, d11, [sp, #112]\n"
    "    ldp  d12, d13, [sp, #128]\n"
    "    ldp  d14, d15, [sp, #144]\n"
    "    add  sp, sp, #160\n"
    "    mov  x0, x2\n"
    "    ret\n"
    ".p2align 2\n"
    ".globl " RKC_SYM(rakupp_coro_trampoline) "\n"
    RKC_HIDE(rakupp_coro_trampoline)
    RKC_TYPE(rakupp_coro_trampoline)
    RKC_SYM(rakupp_coro_trampoline) ":\n"
    "    mov  x0, x19\n"
    "    blr  x20\n"
    "    brk  #1\n"
);

// The frame a fresh stack's first switch pops: x19 = the stack, x20 = the
// boot function, x29 = 0 (ends the frame-pointer chain for unwinders and
// debuggers), x30 = the trampoline, which `ret` jumps to.
static void* coroFreshContext(char* top, CoroStack* s) {
    auto* f = reinterpret_cast<uint64_t*>(top - 160);
    for (int i = 0; i < 20; i++) f[i] = 0;
    f[0] = reinterpret_cast<uint64_t>(s);                        // x19
    f[1] = reinterpret_cast<uint64_t>(&rakupp_coro_boot);        // x20
    f[10] = 0;                                                   // x29
    f[11] = reinterpret_cast<uint64_t>(&rakupp_coro_trampoline); // x30
    return f;
}

#elif defined(__x86_64__)
// System V x86-64: rbx, rbp, r12–r15 survive a call, and so do the MXCSR
// control bits and the x87 control word.
__asm__(
    ".text\n"
    ".p2align 4\n"
    ".globl " RKC_SYM(rakupp_coro_switch) "\n"
    RKC_HIDE(rakupp_coro_switch)
    RKC_TYPE(rakupp_coro_switch)
    RKC_SYM(rakupp_coro_switch) ":\n"
    "    pushq %rbp\n"
    "    pushq %rbx\n"
    "    pushq %r12\n"
    "    pushq %r13\n"
    "    pushq %r14\n"
    "    pushq %r15\n"
    "    subq  $8, %rsp\n"
    "    stmxcsr (%rsp)\n"
    "    fnstcw  4(%rsp)\n"
    "    movq  %rsp, (%rdi)\n"
    "    movq  %rsi, %rsp\n"
    "    ldmxcsr (%rsp)\n"
    "    fldcw   4(%rsp)\n"
    "    addq  $8, %rsp\n"
    "    popq  %r15\n"
    "    popq  %r14\n"
    "    popq  %r13\n"
    "    popq  %r12\n"
    "    popq  %rbx\n"
    "    popq  %rbp\n"
    "    movq  %rdx, %rax\n"
    "    ret\n"
    ".p2align 4\n"
    ".globl " RKC_SYM(rakupp_coro_trampoline) "\n"
    RKC_HIDE(rakupp_coro_trampoline)
    RKC_TYPE(rakupp_coro_trampoline)
    RKC_SYM(rakupp_coro_trampoline) ":\n"
    "    movq  %r12, %rdi\n"
    "    callq *%r13\n"
    "    ud2\n"
);

// The frame a fresh stack's first switch pops (lowest address first): the
// MXCSR and x87 control word at their ABI defaults, r15..r12, rbx, rbp = 0,
// then the trampoline's address for `ret`. The return slot sits 24 bytes
// under the 16-aligned top, so the trampoline's own call is made with the
// stack aligned as the ABI requires.
static void* coroFreshContext(char* top, CoroStack* s) {
    auto* f = reinterpret_cast<uint64_t*>(top - 80);
    for (int i = 0; i < 10; i++) f[i] = 0;
    auto* ctl = reinterpret_cast<uint32_t*>(f);
    ctl[0] = 0x1F80;                                            // MXCSR
    ctl[1] = 0x037F;                                            // x87 control word
    // f[1] r15, f[2] r14: zero
    f[3] = reinterpret_cast<uint64_t>(&rakupp_coro_boot);       // r13
    f[4] = reinterpret_cast<uint64_t>(s);                       // r12
    f[5] = 0;                                                   // rbx
    f[6] = 0;                                                   // rbp
    f[7] = reinterpret_cast<uint64_t>(&rakupp_coro_trampoline); // return address
    return f;
}
#endif

static size_t pageSize() {
    static const size_t ps = [] { long p = sysconf(_SC_PAGESIZE); return p > 0 ? size_t(p) : size_t(4096); }();
    return ps;
}
static size_t guardSize() { return pageSize() * (pageSize() >= 16384 ? 4 : 16); }  // 64 KiB either way

static CoroStack* stackNew() {
    int flags = MAP_PRIVATE | MAP_ANON;
#ifdef MAP_NORESERVE
    flags |= MAP_NORESERVE;
#endif
#ifdef MAP_STACK
    flags |= MAP_STACK;
#endif
    void* m = mmap(nullptr, kReserve, PROT_READ | PROT_WRITE, flags, -1, 0);
    if (m == MAP_FAILED) return nullptr;
    mprotect(m, guardSize(), PROT_NONE);
    auto* s = new CoroStack;
    s->map = static_cast<char*>(m);
    s->mapSize = kReserve;
    char* top = s->map + s->mapSize;
    top = reinterpret_cast<char*>(reinterpret_cast<uintptr_t>(top) & ~uintptr_t(15));
    s->sp = coroFreshContext(top, s);
    return s;
}
static void stackFree(CoroStack* s) {
    if (!s) return;
    munmap(s->map, s->mapSize);
    delete s;
}
static char* stackTopOf(const CoroStack* s) {
    return reinterpret_cast<char*>(reinterpret_cast<uintptr_t>(s->map + s->mapSize) & ~uintptr_t(15));
}
static size_t stackUsableOf(const CoroStack* s) { return s->mapSize - guardSize(); }
[[maybe_unused]] static const void* stackBottomOf(const CoroStack* s) { return s->map + guardSize(); }

#else  // _WIN32 --------------------------------------------------------------

static VOID CALLBACK coroFiberProc(PVOID p) {
    auto* s = static_cast<CoroStack*>(p);
    char here;
    s->top = &here;
    coroRunLoop(s);
}
static CoroStack* stackNew() {
    auto* s = new CoroStack;
    s->fiber = CreateFiberEx(64 * 1024, kReserve, FIBER_FLAG_FLOAT_SWITCH, &coroFiberProc, s);
    if (!s->fiber) { delete s; return nullptr; }
    return s;
}
static void stackFree(CoroStack* s) {
    if (!s) return;
    if (s->fiber) DeleteFiber(s->fiber);
    delete s;
}
static char* stackTopOf(const CoroStack* s) { return s->top; }
static size_t stackUsableOf(const CoroStack*) { return kReserve - 256 * 1024; }
[[maybe_unused]] static const void* stackBottomOf(const CoroStack* s) {
    return s->top ? s->top - stackUsableOf(s) : nullptr;
}
static void ensureThreadIsFiber() {
    if (!IsThreadAFiber()) ConvertThreadToFiber(nullptr);
}
#endif

// ---- the per-thread pool ---------------------------------------------------
namespace {
struct StackPool {
    std::vector<CoroStack*> free;
    ~StackPool() { for (auto* s : free) stackFree(s); }
};
thread_local StackPool t_pool;
}

static CoroStack* stackAcquire() {
    if (!t_pool.free.empty()) {
        CoroStack* s = t_pool.free.back();
        t_pool.free.pop_back();
        return s;
    }
    CoroStack* s = stackNew();
    if (!s) {
        std::fprintf(stderr, "rakupp: cannot allocate a coroutine stack\n");
        std::abort();
    }
    return s;
}
// Only a stack parked in the run loop (its coroutine finished) can be reused.
static void stackRelease(CoroStack* s) {
    s->current = nullptr;
    if (t_pool.free.size() < kPoolMax) t_pool.free.push_back(s);
    else stackFree(s);
}

size_t coroPooledStacks() { return t_pool.free.size(); }
size_t coroStackReserve() { return kReserve; }

// ---- the loop each stack runs ----------------------------------------------
void coroRunLoop(CoroStack* s) {
    for (;;) {
        Coro* c = s->current;
#ifdef RAKUPP_CORO_ASAN
        __sanitizer_finish_switch_fiber(s->asanFake, &c->asanCallerBottom_, &c->asanCallerSize_);
#endif
        c->entry_(c->arg_);
        c->finished_ = true;
        c->running_ = false;
        // Back to the resumer. This stack is resumed again only when the pool
        // hands it to another coroutine, which the loop then runs.
#ifdef RAKUPP_CORO_ASAN
        __sanitizer_start_switch_fiber(&s->asanFake, c->asanCallerBottom_, c->asanCallerSize_);
#endif
#if defined(_WIN32)
        SwitchToFiber(c->callerCtx_);
#else
        rakupp_coro_switch(&s->sp, c->callerCtx_, nullptr);
#endif
    }
}

// ---- Coro ------------------------------------------------------------------
Coro::~Coro() {
    if (!stack_) return;
    if (finished_) stackRelease(stack_);
    else stackFree(stack_);   // suspended: its frames are abandoned (see Coro.h)
    stack_ = nullptr;
}

void Coro::resume() {
    if (finished_ || running_) return;
    if (!started_) {
        stack_ = stackAcquire();
        stack_->current = this;
        started_ = true;
        owner_ = std::this_thread::get_id();
    }
    running_ = true;
#ifdef RAKUPP_CORO_ASAN
    __sanitizer_start_switch_fiber(&asanCallerFake_, stackBottomOf(stack_), stackUsableOf(stack_));
#endif
#if defined(_WIN32)
    ensureThreadIsFiber();
    callerCtx_ = GetCurrentFiber();
    SwitchToFiber(stack_->fiber);
#else
    rakupp_coro_switch(&callerCtx_, stack_->sp, nullptr);
#endif
#ifdef RAKUPP_CORO_ASAN
    __sanitizer_finish_switch_fiber(asanCallerFake_, nullptr, nullptr);
#endif
}

void Coro::yield() {
    running_ = false;
#ifdef RAKUPP_CORO_ASAN
    __sanitizer_start_switch_fiber(&stack_->asanFake, asanCallerBottom_, asanCallerSize_);
#endif
#if defined(_WIN32)
    SwitchToFiber(callerCtx_);
#else
    rakupp_coro_switch(&stack_->sp, callerCtx_, nullptr);
#endif
#ifdef RAKUPP_CORO_ASAN
    __sanitizer_finish_switch_fiber(stack_->asanFake, &asanCallerBottom_, &asanCallerSize_);
#endif
    running_ = true;
}

char* Coro::stackTop() const { return stack_ ? stackTopOf(stack_) : nullptr; }
size_t Coro::stackUsable() const { return stack_ ? stackUsableOf(stack_) : 0; }

} // namespace rakupp

#else  // !RAKUPP_HAVE_CORO ----------------------------------------------------

namespace rakupp {
// No context switch on this target: gather keeps its re-running form and
// never constructs a Coro. These keep the link complete.
struct CoroStack {};
void coroRunLoop(CoroStack*) {}
Coro::~Coro() {}
void Coro::resume() { std::fprintf(stderr, "rakupp: no coroutines on this platform\n"); std::abort(); }
void Coro::yield() {}
char* Coro::stackTop() const { return nullptr; }
size_t Coro::stackUsable() const { return 0; }
size_t coroPooledStacks() { return 0; }
size_t coroStackReserve() { return 0; }
}

#endif
