// value-pool-probe.cpp — what it would cost, and buy, to keep every Value in
// one pre-reserved pool and have a variable or an array element hold a 32-bit
// INDEX into it instead of the Value itself.
//
// docs/dev/findings/VALUE-POOL-INDEX-2026-11-10.md reads these numbers. The
// probe prices the representation on the real 32-byte rakupp::Value; it is not
// an engine measurement, and nothing in the engine is built this way.
//
// Three shapes of the same data:
//
//   inline   a Value where it is used: a ValueList of Values, a Value variable.
//            What rakupp does.
//   pool     a 32-bit handle to a slot in a pool of Values. The slot is
//            counted (a parallel array of counts), so a copy of a handle is a
//            count, and the last drop frees the slot. Every value is a slot.
//   pool+imm the same, and a handle whose low bit is set IS a 31-bit Int: no
//            slot, no count. This is the idea at its best: the commonest value
//            costs four bytes and nothing else.
//
// Each pooled shape is measured with an atomic count (what a threaded engine
// needs, and what Value's payloads pay today) and with a plain one (the
// ceiling).
//
//   c++ -std=c++20 -O2 -DNDEBUG -Isrc -Iinclude tools/value-pool-probe.cpp \
//       build/librakupp_{rt,parse,ucd_names,ucd_coll,ucd_props,stubs}.a \
//       -liconv -o /tmp/value-pool-probe && /tmp/value-pool-probe
#include "Value.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <string>
#include <sys/mman.h>
#include <vector>

using namespace rakupp;
using clk = std::chrono::steady_clock;
static double ms(clk::time_point a, clk::time_point b) {
    return std::chrono::duration<double, std::milli>(b - a).count();
}

// ---- the pool --------------------------------------------------------------
// Address space for 2^26 slots, touched as it is used: the "big pre-allocated
// array". Slot 0 is never handed out. One thread, so the free list is plain;
// a real one would be BodyPool's (thread-private lists over shared spans).
template <bool ATOMIC>
struct Pool {
    static constexpr size_t kCap = (size_t)1 << 26;
    inline static Value* slots = nullptr;
    inline static uint32_t* rc = nullptr;   // the count; a free slot's next-free index
    inline static uint32_t freeHead = 0, hi = 1, live = 0, peak = 0;
    static void init() {
        if (slots) return;
        slots = (Value*)mmap(nullptr, kCap * sizeof(Value), PROT_READ | PROT_WRITE,
                             MAP_PRIVATE | MAP_ANON | MAP_NORESERVE, -1, 0);
        rc = (uint32_t*)mmap(nullptr, kCap * sizeof(uint32_t), PROT_READ | PROT_WRITE,
                             MAP_PRIVATE | MAP_ANON | MAP_NORESERVE, -1, 0);
        if (slots == MAP_FAILED || rc == MAP_FAILED) abort();
    }
    static uint32_t alloc(Value&& v) {
        uint32_t s = freeHead;
        if (s) freeHead = rc[s];
        else { s = hi++; if (s >= kCap) abort(); }
        ::new (&slots[s]) Value(std::move(v));
        rc[s] = 1;
        if (++live > peak) peak = live;
        return s;
    }
    static void retain(uint32_t s) {
        if (ATOMIC) reinterpret_cast<std::atomic<uint32_t>*>(&rc[s])->fetch_add(1, std::memory_order_relaxed);
        else rc[s]++;
    }
    static void release(uint32_t s) {
        uint32_t left;
        if (ATOMIC) left = reinterpret_cast<std::atomic<uint32_t>*>(&rc[s])->fetch_sub(1, std::memory_order_acq_rel) - 1;
        else left = --rc[s];
        if (!left) {
            slots[s].~Value();
            rc[s] = freeHead;
            freeHead = s;
            live--;
        }
    }
    static bool sole(uint32_t s) { return rc[s] == 1; }
    // FRESH: nothing is live, so start over — slots are handed out in address
    // order, as in a program's first moments.
    static void reset() { if (live) abort(); freeHead = 0; hi = 1; peak = 0; }
    // AGED: n slots taken and given back in random order, so the free list
    // visits them at random — a pool after a program has run for a while.
    static void age(uint32_t n) {
        std::vector<uint32_t> ss;
        for (uint32_t k = 0; k < n; k++) ss.push_back(alloc(Value()));
        std::mt19937 g(7);
        std::shuffle(ss.begin(), ss.end(), g);
        for (uint32_t x : ss) release(x);
    }
};

// ---- the handle ------------------------------------------------------------
// IMM: a set low bit makes the other 31 a signed Int. Otherwise the handle is
// a slot index (shifted, with IMM, to leave the bit clear); 0 is no value.
template <bool IMM, bool ATOMIC>
struct H {
    using P = Pool<ATOMIC>;
    uint32_t h = 0;
    static constexpr long long kImmMax = (1ll << 30) - 1, kImmMin = -(1ll << 30);
    bool imm() const { return IMM && (h & 1); }
    uint32_t slot() const { return IMM ? h >> 1 : h; }
    static H ofSlot(uint32_t s) { H r; r.h = IMM ? s << 1 : s; return r; }
    H() = default;
    H(const H& o) : h(o.h) { if (h && !imm()) P::retain(slot()); }
    H(H&& o) noexcept : h(o.h) { o.h = 0; }
    H& operator=(const H& o) {
        if (h != o.h) { if (o.h && !o.imm()) P::retain(o.slot()); drop(); h = o.h; }
        return *this;
    }
    H& operator=(H&& o) noexcept { if (this != &o) { drop(); h = o.h; o.h = 0; } return *this; }
    ~H() { drop(); }
    void drop() { if (h && !imm()) P::release(slot()); }

    static H ofInt(long long x) {
        if (IMM && x >= kImmMin && x <= kImmMax) { H r; r.h = ((uint32_t)x << 1) | 1u; return r; }
        return ofSlot(P::alloc(Value::integer(x)));
    }
    static H ofValue(Value&& v) { return ofSlot(P::alloc(std::move(v))); }
    long long asInt() const { return imm() ? (long long)((int32_t)h >> 1) : P::slots[slot()].i; }
    double asNum() const { return P::slots[slot()].n; }
    const Value& val() const { return P::slots[slot()]; }
    // `$x = $x + 1` as the engine's general path does it: a new value, stored.
    // A slot nothing else holds is rewritten where it is — the best a counted
    // slot can do, and what CowStr does for a body.
    void addInt(long long d) {
        if (imm()) { *this = ofInt(asInt() + d); return; }
        if (P::sole(slot())) { P::slots[slot()].i += d; return; }
        *this = ofInt(asInt() + d);
    }
    void addNum(double d) {
        if (P::sole(slot())) { P::slots[slot()].n += d; return; }
        *this = ofValue(Value::number(asNum() + d));
    }
};

static const int N = 1000000;
static const int R = 7;   // best-of, inside one binary
static volatile long long g_sink;
static bool g_aged = false;

template <class F> static double best(F&& f) {
    double b = 1e18;
    for (int r = 0; r < R; r++) {
        auto t0 = clk::now();
        f();
        b = std::min(b, ms(t0, clk::now()));
    }
    return b;
}

struct Row { const char* what; double inl, p, pa, pi, pia; };
static void show(const Row& r) {
    auto x = [&](double v) { return v / r.inl; };
    printf("%-34s %8.2f | %8.2f %5.2fx %8.2f %5.2fx | %8.2f %5.2fx %8.2f %5.2fx\n", r.what, r.inl,
           r.p, x(r.p), r.pa, x(r.pa), r.pi, x(r.pi), r.pia, x(r.pia));
}

// One workload over a handle shape. `kind`: 0 small Ints, 1 Nums, 2 short Strs.
template <class HT> struct Work {
    using Vec = std::vector<HT>;
    static void prep() { HT::P::reset(); if (g_aged) HT::P::age(2 * N); }
    static HT make(int kind, int k) {
        if (kind == 0) return HT::ofInt(k);
        if (kind == 1) return HT::ofValue(Value::number(k + 0.5));
        char b[8]; int n = snprintf(b, sizeof b, "s%d", k % 99999);
        return HT::ofValue(Value::str(std::string_view(b, (size_t)n)));
    }
    static double build(int kind) {
        prep();
        return best([&] { Vec v; for (int k = 0; k < N; k++) v.push_back(make(kind, k)); g_sink = (long long)v.size(); });
    }
    static double copy(int kind) {
        prep();
        Vec v; for (int k = 0; k < N; k++) v.push_back(make(kind, k));
        return best([&] { Vec w(v); g_sink = (long long)w.size(); });
    }
    static double scan(int kind, bool shuffled) {
        prep();
        Vec v; for (int k = 0; k < N; k++) v.push_back(make(kind, k));
        if (shuffled) { std::mt19937 g(1); std::shuffle(v.begin(), v.end(), g); }
        return best([&] {
            long long t = 0;
            if (kind == 0) for (auto& e : v) t += e.asInt();
            else if (kind == 1) { double d = 0; for (auto& e : v) d += e.asNum(); t = (long long)d; }
            else for (auto& e : v) t += (long long)e.val().s.size();
            g_sink = t;
        });
    }
    static double sort(int kind) {
        prep();
        Vec v; for (int k = 0; k < N; k++) v.push_back(make(kind, (int)(((long long)k * 7919) % 1000003)));
        return best([&] {
            Vec w(v);
            if (kind == 0) std::sort(w.begin(), w.end(), [](const HT& a, const HT& b) { return a.asInt() < b.asInt(); });
            else std::sort(w.begin(), w.end(), [](const HT& a, const HT& b) { return a.asNum() < b.asNum(); });
            g_sink = (long long)w.size();
        });
    }
    static double incr(int kind) {
        prep();
        return best([&] {
            HT x = make(kind, 0);
            if (kind == 0) for (int k = 0; k < 20000000; k++) x.addInt(1);
            else for (int k = 0; k < 20000000; k++) x.addNum(1.0);
            g_sink = kind == 0 ? x.asInt() : (long long)x.asNum();
        });
    }
    // `$t = $t + $_` where the left value is ALSO held elsewhere each turn
    // (a closure, an array element, the caller's copy): no in-place write.
    static double incrShared(int kind) {
        prep();
        return best([&] {
            HT x = make(kind, 0);
            for (int k = 0; k < 20000000; k++) {
                HT keep = x;
                if (kind == 0) x.addInt(1); else x.addNum(1.0);
                g_sink = keep.h;
            }
        });
    }
    static double assign(int kind) {
        prep();
        HT a = make(kind, 7), b = make(kind, 9), c;
        return best([&] { for (int k = 0; k < 20000000; k++) { c = (k & 1) ? a : b; } g_sink = c.h; });
    }
    // An array of N references to 100 distinct values: what sharing buys.
    static double dedup(int kind) {
        prep();
        Vec base; for (int k = 0; k < 100; k++) base.push_back(make(kind, k * 1000003 + 1073741824));
        return best([&] { Vec v; for (int k = 0; k < N; k++) v.push_back(base[k % 100]); g_sink = (long long)v.size(); });
    }
};

// The same workloads on Values where they are used.
struct Inl {
    static Value make(int kind, int k) {
        if (kind == 0) return Value::integer(k);
        if (kind == 1) return Value::number(k + 0.5);
        char b[8]; int n = snprintf(b, sizeof b, "s%d", k % 99999);
        return Value::str(std::string_view(b, (size_t)n));
    }
    static double build(int kind) {
        return best([&] { ValueList v; for (int k = 0; k < N; k++) v.push_back(make(kind, k)); g_sink = (long long)v.size(); });
    }
    static double copy(int kind) {
        ValueList v; for (int k = 0; k < N; k++) v.push_back(make(kind, k));
        return best([&] { ValueList w(v); g_sink = (long long)w.size(); });
    }
    static double scan(int kind, bool shuffled) {
        ValueList v; for (int k = 0; k < N; k++) v.push_back(make(kind, k));
        if (shuffled) { std::mt19937 g(1); std::shuffle(v.begin(), v.end(), g); }
        return best([&] {
            long long t = 0;
            if (kind == 0) for (auto& e : v) t += e.i;
            else if (kind == 1) { double d = 0; for (auto& e : v) d += e.n; t = (long long)d; }
            else for (auto& e : v) t += (long long)e.s.size();
            g_sink = t;
        });
    }
    static double sort(int kind) {
        ValueList v; for (int k = 0; k < N; k++) v.push_back(make(kind, (int)(((long long)k * 7919) % 1000003)));
        return best([&] {
            ValueList w(v);
            if (kind == 0) std::sort(w.begin(), w.end(), [](const Value& a, const Value& b) { return a.i < b.i; });
            else std::sort(w.begin(), w.end(), [](const Value& a, const Value& b) { return a.n < b.n; });
            g_sink = (long long)w.size();
        });
    }
    static double incr(int kind) {
        return best([&] {
            Value x = make(kind, 0);
            if (kind == 0) for (int k = 0; k < 20000000; k++) x = Value::integer(x.i + 1);
            else for (int k = 0; k < 20000000; k++) x = Value::number(x.n + 1.0);
            g_sink = kind == 0 ? x.i : (long long)x.n;
        });
    }
    static double incrShared(int kind) {
        return best([&] {
            Value x = make(kind, 0);
            for (int k = 0; k < 20000000; k++) {
                Value keep = x;
                if (kind == 0) x = Value::integer(x.i + 1); else x = Value::number(x.n + 1.0);
                g_sink = keep.i;
            }
        });
    }
    static double assign(int kind) {
        Value a = make(kind, 7), b = make(kind, 9), c;
        return best([&] { for (int k = 0; k < 20000000; k++) { c = (k & 1) ? a : b; } g_sink = c.i; });
    }
    static double dedup(int kind) {
        ValueList base; for (int k = 0; k < 100; k++) base.push_back(make(kind, k * 1000003 + 1073741824));
        return best([&] { ValueList v; for (int k = 0; k < N; k++) v.push_back(base[k % 100]); g_sink = (long long)v.size(); });
    }
};

using HP = H<false, false>;    // pool, plain count
using HPA = H<false, true>;    // pool, atomic count
using HI = H<true, false>;     // pool + immediate Ints, plain count
using HIA = H<true, true>;     // pool + immediate Ints, atomic count

#define ROW(label, call) show(Row{label, Inl::call, Work<HP>::call, Work<HPA>::call, Work<HI>::call, Work<HIA>::call})

int main() {
    Pool<false>::init();
    Pool<true>::init();
    printf("sizeof: Value %zu, handle %zu; a pooled value is a handle + a %zu-byte slot + a 4-byte count\n",
           sizeof(Value), sizeof(HI), sizeof(Value));
    printf("times in ms, best of %d; x is against inline (under 1 is faster)\n\n", R);
    printf("%-34s %8s | %8s %6s %8s %6s | %8s %6s %8s %6s\n", "", "inline", "pool", "", "atomic", "", "pool+imm", "", "atomic", "");
    static const char* kinds[] = {"Int", "Num", "Str"};
    char lab[64];
    for (int aged = 0; aged < 2; aged++) {
    g_aged = aged;
    printf("---- %s pool ----\n", aged ? "AGED (free slots in random order)" : "FRESH (slots handed out in order)");
    for (int kind = 0; kind < 3; kind++) {
        snprintf(lab, sizeof lab, "build 1M %ss", kinds[kind]);            ROW(lab, build(kind));
        snprintf(lab, sizeof lab, "copy the array of 1M %ss", kinds[kind]); ROW(lab, copy(kind));
        snprintf(lab, sizeof lab, "read 1M %ss, in order", kinds[kind]);    ROW(lab, scan(kind, false));
        snprintf(lab, sizeof lab, "read 1M %ss, shuffled", kinds[kind]);    ROW(lab, scan(kind, true));
        if (kind < 2) {
            snprintf(lab, sizeof lab, "sort 1M %ss", kinds[kind]);              ROW(lab, sort(kind));
            snprintf(lab, sizeof lab, "$x = $x + 1, 20M, %s", kinds[kind]);     ROW(lab, incr(kind));
            snprintf(lab, sizeof lab, "  the same, $x also held (%s)", kinds[kind]); ROW(lab, incrShared(kind));
        }
        snprintf(lab, sizeof lab, "$c = $a or $b, 20M, %s", kinds[kind]);   ROW(lab, assign(kind));
        snprintf(lab, sizeof lab, "1M refs to 100 %ss", kinds[kind]);       ROW(lab, dedup(kind));
        printf("\n");
    }

    }
    // ---- memory: bytes an element costs, by what the array holds -----------
    // (1M refs to 100 values: the Ints there are too large to be immediate)
    printf("bytes per element of a 1M array (handle 4 + slot %zu + count 4 where a slot is used):\n", sizeof(Value));
    printf("  %-28s inline %3zu   pool %3zu   pool+imm %3zu\n", "distinct small Ints", sizeof(Value), 4 + sizeof(Value) + 4, (size_t)4);
    printf("  %-28s inline %3zu   pool %3zu   pool+imm %3zu\n", "distinct Nums or Strs", sizeof(Value), 4 + sizeof(Value) + 4, 4 + sizeof(Value) + 4);
    printf("  %-28s inline %3zu   pool %3zu   pool+imm %3zu\n", "1M refs to 100 values", sizeof(Value), (size_t)4, (size_t)4);
    return 0;
}
