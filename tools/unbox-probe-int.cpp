// UNBOX-PLAN.md probe 1: what the -O lanes cost per statement, against the same
// loop run on C++ locals. Build:
//   c++ -std=c++17 -O2 -w -Isrc -Iinclude tools/unbox-probe-int.cpp \
//       build/librakupp_{rt,parse,ucd_names,ucd_coll,ucd_props,stubs}.a -o /tmp/u1 && /tmp/u1
#include "Interpreter.h"
#include "Value.h"
#include <cstdio>
#include <chrono>
using namespace rakupp;
static const long long N = 50000000;

// ---- what a kernel emits TODAY (verbatim from the cache, -O lanes) ---------
static void today(Value& v_si, Value& v_ss) {
    while (([&]() -> bool { do { if (!(rtIntBox(v_si))) break; return (v_si.i < N); } while (0);
                            return rtLtB(v_si, Value::integer(N)); }())) {
        { bool __lok1 = false; do {
            if (!(rtIntBox(v_ss) && rtIntBox(v_si))) break;
            long long __ln0; if (rakupp::add_ovf(v_ss.i, v_si.i, &__ln0)) break;
            if (rtIntSlot(v_ss)) v_ss.i = __ln0; else v_ss = Value::integer(__ln0); __lok1 = true;
          } while (0);
          if (!__lok1) { v_ss = rtAdd(v_ss, v_si); } }
        { bool __lok3 = false; do {
            if (!(rtIntBox(v_si))) break;
            long long __ln2; if (rakupp::add_ovf(v_si.i, 1LL, &__ln2)) break;
            if (rtIntSlot(v_si)) v_si.i = __ln2; else v_si = Value::integer(__ln2); __lok3 = true;
          } while (0);
          if (!__lok3) { v_si = rtAdd(v_si, Value::integer(1LL)); } }
    }
}

// ---- what P3 would emit: guard ONCE at entry, run on C++ locals, store back -
static void p3(Value& v_si, Value& v_ss) {
    if (rtIntSlot(v_si) && rtIntSlot(v_ss)) {          // one guard, at entry
        long long i = v_si.i, s = v_ss.i;              // unboxed for the loop's extent
        bool ok = true;
        while (i < N) {
            long long t;
            if (rakupp::add_ovf(s, i, &t)) { ok = false; break; }   s = t;
            if (rakupp::add_ovf(i, 1LL, &t)) { ok = false; break; } i = t;
        }
        v_si.i = i; v_ss.i = s;                        // store back where it escapes
        if (ok) return;
    }
    today(v_si, v_ss);                                 // deopt: the boxed lane finishes it
}

template <class F> double ms(F f) {
    Value i = Value::integer(0), s = Value::integer(0);
    auto t0 = std::chrono::steady_clock::now();
    f(i, s);
    auto t1 = std::chrono::steady_clock::now();
    if (s.i != 1249999975000000LL) printf("  WRONG: %lld\n", s.i);
    return std::chrono::duration<double, std::milli>(t1 - t0).count();
}
int main() {
    double a = 1e9, b = 1e9;
    for (int r = 0; r < 5; r++) { double x = ms(today); if (x < a) a = x;
                                  double y = ms(p3);    if (y < b) b = y; }
    printf("  50M iterations, best of 5\n");
    printf("  today's kernel (-O lanes) : %7.1f ms\n", a);
    printf("  P3, unboxed for the loop  : %7.1f ms   (%.1fx)\n", b, a / b);
}
