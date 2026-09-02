// value32-probe.cpp — what is left to win by shrinking `Value` below 128 bytes,
// and what the two cheaper alternatives to shrinking it are worth.
//
// REPRESENTATION-PLAN phase 1 set out to reach <= 64 bytes and stopped at 128
// (batch 4). This probe prices the rest of that target — and, because the
// campaign's own history says the projection can be right for the wrong reason
// (phase 2), it prices the two things that turned out to be in the way:
//
//   * a `Value` is not trivially relocatable, so a `std::vector<Value>` move-
//     constructs every element on growth. That is 61% of the array build cost
//     and it has nothing to do with sizeof;
//   * `CowStr`'s promote threshold sits at 64, so a 23..63-byte string is a
//     heap std::string that mallocs on EVERY COPY. A 33-byte string is 1.8x
//     more expensive than a 68-byte one for that reason alone.
//
// Sections:
//   A  sizes
//   B  vector<T> build / copy / scan: real Value vs 32-byte vs 16-byte stand-ins
//   C  ValueHash build on the real JSON shape (800 hashes of 15)
//   D  a Str value's ctor + copies: CowStr today vs always-heap vs inline-in-value
//   E  payload copy: shared_ptr vs an intrusive refcount
//   F  vector<Value> growth vs reserve vs a memcpy-relocating vector
//   G  CowStr's kPromote threshold, swept by string length
//
// D is the section that decides the design: an always-heap Str is SLOWER than
// today, an inline-in-the-value Str is faster, and they differ by 6.6x. E is
// the counterintuitive one: the intrusive refcount buys size, not speed.
//
//   c++ -std=c++20 -O2 -DNDEBUG -Isrc -Iinclude tools/value32-probe.cpp \
//       build-arm64/librakupp_{rt,parse,ucd_names,ucd_coll,ucd_props,stubs}.a \
//       -o /tmp/value32-probe && /tmp/value32-probe
#include "Value.h"
#include "ValueHash.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using namespace rakupp;
using clk = std::chrono::steady_clock;
static double ms(clk::time_point a, clk::time_point b) {
    return std::chrono::duration<double, std::milli>(b - a).count();
}

// ---- stand-ins -------------------------------------------------------------

// 32 bytes: 16 bytes of immediate (int / double / short string), one intrusive
// refcounted body pointer, and a tag word. NOT a proposed layout — the right
// SIZE and the right COPY COST to price the idea.
struct Body32 {
    std::atomic<uint32_t> rc{1};
    virtual ~Body32() {}
};
struct Slim32 {
    uint64_t tag = 0;
    union { long long i; double n; char sso[16]; };
    Body32* p = nullptr;
    Slim32() : i(0) {}
    Slim32(const Slim32& o) : tag(o.tag), i(o.i), p(o.p) {
        if (p) p->rc.fetch_add(1, std::memory_order_relaxed);
    }
    Slim32& operator=(const Slim32& o) {
        Slim32 t(o);
        std::swap(tag, t.tag); std::swap(i, t.i); std::swap(p, t.p);
        return *this;
    }
    ~Slim32() { if (p && p->rc.fetch_sub(1, std::memory_order_acq_rel) == 1) delete p; }
};

// 16 bytes: tag + payload, nothing else — and NO refcount, so it is trivially
// copyable and trivially destructible. That is the ceiling a non-refcounted
// (GC'd) value would reach, not something reachable by repacking fields.
struct Slim16 {
    union { long long i; double n; Body32* p; };
    uint64_t tag = 0;
    Slim16() : i(0) {}
};

struct StrBodyA : Body32 {
    std::string t;
    explicit StrBodyA(std::string x) : t(std::move(x)) {}
};

// A vector that grows by memcpy, i.e. one that knows T is trivially
// relocatable. `Value` is: moving the bits is sound as long as the source is
// not then destroyed, which is exactly what a reallocation does.
template <class T> struct RelocVec {
    T* d = nullptr;
    size_t n = 0, c = 0;
    ~RelocVec() { for (size_t k = 0; k < n; k++) d[k].~T(); free(d); }
    void grow() {
        size_t nc = c ? c * 2 : 8;
        T* nd = (T*)malloc(nc * sizeof(T));
        memcpy(nd, d, n * sizeof(T));
        free(d); d = nd; c = nc;
    }
    void push_back(T&& v) { if (n == c) grow(); new (d + n) T(std::move(v)); n++; }
    size_t size() const { return n; }
};

static const int N = 1000000;
static const int R = 7;   // best-of; the probe compares shapes inside ONE
                          // binary, so machine drift moves both sides together

int main() {
    // ---- A. sizes ----------------------------------------------------------
    printf("A. sizeof: Value %zu  ValueExt %zu  CowStr %zu  MatchData %zu  ValueHash %zu"
           "  | Slim32 %zu  Slim16 %zu\n\n",
           sizeof(Value), sizeof(ValueExt), sizeof(CowStr), sizeof(MatchData),
           sizeof(ValueHash), sizeof(Slim32), sizeof(Slim16));

    // ---- B. the array path -------------------------------------------------
    double a = 1e18, b = 1e18, c = 1e18;
    for (int r = 0; r < R; r++) {
        auto t0 = clk::now();
        std::vector<Value> v;
        for (int k = 0; k < N; k++) v.push_back(Value::integer(k));
        auto t1 = clk::now();
        if (v.size() != (size_t)N) abort();
        a = std::min(a, ms(t0, t1));
    }
    for (int r = 0; r < R; r++) {
        auto t0 = clk::now();
        std::vector<Slim32> v;
        for (int k = 0; k < N; k++) { Slim32 s; s.tag = 3; s.i = k; v.push_back(s); }
        auto t1 = clk::now();
        if (v.size() != (size_t)N) abort();
        b = std::min(b, ms(t0, t1));
    }
    for (int r = 0; r < R; r++) {
        auto t0 = clk::now();
        std::vector<Slim16> v;
        for (int k = 0; k < N; k++) { Slim16 s; s.tag = 3; s.i = k; v.push_back(s); }
        auto t1 = clk::now();
        if (v.size() != (size_t)N) abort();
        c = std::min(c, ms(t0, t1));
    }
    printf("B. build vector<T> of %d Ints   Value %6.2f ms   Slim32 %6.2f (%.2fx)   Slim16 %6.2f (%.2fx)\n",
           N, a, b, a / b, c, a / c);
    {
        std::vector<Value> sv; sv.reserve(N);
        for (int k = 0; k < N; k++) sv.push_back(Value::integer(k));
        std::vector<Slim32> s2; s2.reserve(N);
        for (int k = 0; k < N; k++) { Slim32 s; s.tag = 3; s.i = k; s2.push_back(s); }

        double ca = 1e18, cb = 1e18;
        for (int r = 0; r < R; r++) { auto t0 = clk::now(); std::vector<Value>  x = sv; auto t1 = clk::now(); if (x.size() != sv.size()) abort(); ca = std::min(ca, ms(t0, t1)); }
        for (int r = 0; r < R; r++) { auto t0 = clk::now(); std::vector<Slim32> x = s2; auto t1 = clk::now(); if (x.size() != s2.size()) abort(); cb = std::min(cb, ms(t0, t1)); }
        printf("   copy  vector<T> of %d Ints   Value %6.2f ms   Slim32 %6.2f (%.2fx)\n", N, ca, cb, ca / cb);

        double sa = 1e18, sb = 1e18; long long acc = 0;
        for (int r = 0; r < R; r++) { auto t0 = clk::now(); long long s = 0; for (auto& v : sv) s += v.i; auto t1 = clk::now(); acc += s; sa = std::min(sa, ms(t0, t1)); }
        for (int r = 0; r < R; r++) { auto t0 = clk::now(); long long s = 0; for (auto& v : s2) s += v.i; auto t1 = clk::now(); acc += s; sb = std::min(sb, ms(t0, t1)); }
        printf("   scan  vector<T> of %d Ints   Value %6.2f ms   Slim32 %6.2f (%.2fx)   [acc %lld]\n",
               N, sa, sb, sa / sb, acc);
    }

    // ---- C. the hash path, on the shape a document actually has -------------
    {
        static const char* names[] = {"id","name","kind","active","score","count","tags","desc",
                                      "esc","meta","created","depth","a","b","c"};
        double best = 1e18;
        for (int r = 0; r < R; r++) {
            auto t0 = clk::now();
            std::vector<ValueHash> hs; hs.reserve(800);
            for (int h = 0; h < 800; h++) {
                ValueHash m;
                for (int k = 0; k < 15; k++) m[names[k]] = Value::integer(k);
                hs.push_back(std::move(m));
            }
            auto t1 = clk::now();
            best = std::min(best, ms(t0, t1));
        }
        printf("\nC. build 800 ValueHash of 15 (Int)   %6.2f ms  = %.0f ns/entry\n",
               best, best * 1e6 / 12000);
    }

    // ---- D. the Str trade --------------------------------------------------
    {
        const int SN = 2000000;
        printf("\nD. Str value, ctor + 3 copies, x%d\n", SN);
        for (const char* txt : {"name", "created_at_iso"}) {
            double bv = 1e18, bh = 1e18, bs = 1e18;
            for (int r = 0; r < R; r++) {          // today: CowStr, inline below 64 bytes
                auto t0 = clk::now(); size_t sink = 0;
                for (int k = 0; k < SN; k++) {
                    Value v = Value::str(txt); Value c1 = v, c2 = v, c3 = c1;
                    sink += c2.s.size() + c3.s.size();
                }
                auto t1 = clk::now(); bv = std::min(bv, ms(t0, t1)); if (!sink) abort();
            }
            for (int r = 0; r < R; r++) {          // always a heap body + atomic refcount
                auto t0 = clk::now(); size_t sink = 0;
                for (int k = 0; k < SN; k++) {
                    Slim32 v; v.tag = 5; v.p = new StrBodyA(txt);
                    Slim32 c1 = v, c2 = v, c3 = c1;
                    sink += ((StrBodyA*)c2.p)->t.size() + ((StrBodyA*)c3.p)->t.size();
                }
                auto t1 = clk::now(); bh = std::min(bh, ms(t0, t1)); if (!sink) abort();
            }
            for (int r = 0; r < R; r++) {          // bytes inline in the 32-byte value
                auto t0 = clk::now(); size_t sink = 0; size_t L = strlen(txt);
                for (int k = 0; k < SN; k++) {
                    Slim32 v; v.tag = 5 | (L << 8); memcpy(v.sso, txt, L);
                    Slim32 c1 = v, c2 = v, c3 = c1;
                    sink += (size_t)(c2.tag >> 8) + (size_t)(c3.tag >> 8);
                }
                auto t1 = clk::now(); bs = std::min(bs, ms(t0, t1)); if (!sink) abort();
            }
            printf("   \"%-14s\"  CowStr %6.2f ms   heap-body %6.2f (%.2fx)   inline %6.2f (%.2fx)\n",
                   txt, bv, bh, bv / bh, bs, bv / bs);
        }
    }

    // ---- E. what the intrusive refcount is actually worth -------------------
    {
        Value arr = Value::array();
        arr.arr()->push_back(Value::integer(1));
        Body32* body = new Body32();
        const int CN = 20000000;
        double bs = 1e18, bi = 1e18;
        for (int r = 0; r < R; r++) {
            auto t0 = clk::now(); long long sink = 0;
            for (int k = 0; k < CN; k++) { Value c = arr; sink += (long long)(size_t)c.arr(); }
            auto t1 = clk::now(); bs = std::min(bs, ms(t0, t1)); if (!sink) abort();
        }
        for (int r = 0; r < R; r++) {
            auto t0 = clk::now(); long long sink = 0;
            Slim32 a2; a2.tag = 6; a2.p = body; body->rc.fetch_add(1, std::memory_order_relaxed);
            for (int k = 0; k < CN; k++) { Slim32 c = a2; sink += (long long)(size_t)c.p; }
            auto t1 = clk::now(); bi = std::min(bi, ms(t0, t1)); if (!sink) abort();
        }
        printf("\nE. copy an Array Value %dx   shared_ptr %6.2f ms   intrusive %6.2f (%.2fx)\n",
               CN, bs, bi, bs / bi);
    }

    // ---- F. growth, which is not a sizeof problem ---------------------------
    {
        double g = 1e18, res = 1e18, rel = 1e18;
        for (int r = 0; r < R; r++) {
            auto t0 = clk::now(); std::vector<Value> v;
            for (int k = 0; k < N; k++) v.push_back(Value::integer(k));
            auto t1 = clk::now(); if (v.size() != (size_t)N) abort(); g = std::min(g, ms(t0, t1));
        }
        for (int r = 0; r < R; r++) {
            auto t0 = clk::now(); std::vector<Value> v; v.reserve(N);
            for (int k = 0; k < N; k++) v.push_back(Value::integer(k));
            auto t1 = clk::now(); if (v.size() != (size_t)N) abort(); res = std::min(res, ms(t0, t1));
        }
        for (int r = 0; r < R; r++) {
            auto t0 = clk::now(); RelocVec<Value> v;
            for (int k = 0; k < N; k++) v.push_back(Value::integer(k));
            auto t1 = clk::now(); if (v.size() != (size_t)N) abort(); rel = std::min(rel, ms(t0, t1));
        }
        printf("\nF. vector<Value> build %d Ints   grow %6.2f ms | reserve %6.2f | reloc-grow %6.2f (%.2fx vs grow)\n",
               N, g, res, rel, g / rel);

        double gs = 1e18, rs = 1e18;
        for (int r = 0; r < R; r++) {
            auto t0 = clk::now(); std::vector<Value> v;
            for (int k = 0; k < N; k++) v.push_back(Value::str("created_at"));
            auto t1 = clk::now(); if (v.size() != (size_t)N) abort(); gs = std::min(gs, ms(t0, t1));
        }
        for (int r = 0; r < R; r++) {
            auto t0 = clk::now(); RelocVec<Value> v;
            for (int k = 0; k < N; k++) v.push_back(Value::str("created_at"));
            auto t1 = clk::now(); if (v.size() != (size_t)N) abort(); rs = std::min(rs, ms(t0, t1));
        }
        printf("   vector<Value> build %d Strs   grow %6.2f ms |                 reloc-grow %6.2f (%.2fx)\n",
               N, gs, rs, gs / rs);
    }

    // ---- G. CowStr's promote threshold, by length ---------------------------
    {
        const int SN = 1000000;
        printf("\nG. Str value, ctor (+ copies), x%d — as built vs forced to a shared body\n", SN);
        printf("   len  copies   as-is(ms)   promoted(ms)   ratio\n");
        for (int len : {8, 16, 22, 24, 32, 48, 63, 80, 200}) {
            std::string t(len, 'x');
            for (int copies : {0, 2}) {
                double p = 1e18, q = 1e18;
                for (int r = 0; r < R; r++) {
                    auto t0 = clk::now(); size_t sink = 0;
                    for (int k = 0; k < SN; k++) {
                        Value v = Value::str(t); sink += v.s.size();
                        if (copies) { Value c1 = v, c2 = v; sink += c1.s.size() + c2.s.size(); }
                    }
                    auto t1 = clk::now(); p = std::min(p, ms(t0, t1)); if (!sink) abort();
                }
                for (int r = 0; r < R; r++) {
                    auto t0 = clk::now(); size_t sink = 0;
                    for (int k = 0; k < SN; k++) {
                        Value v = Value::str(t); v.s.promote(); sink += v.s.size();
                        if (copies) { Value c1 = v, c2 = v; sink += c1.s.size() + c2.s.size(); }
                    }
                    auto t1 = clk::now(); q = std::min(q, ms(t0, t1)); if (!sink) abort();
                }
                printf("   %3d  %6d   %9.2f   %12.2f   %5.2fx\n", len, copies, p, q, p / q);
            }
        }
    }
    return 0;
}
