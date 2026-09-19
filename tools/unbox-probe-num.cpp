// UNBOX-PLAN.md probe 2: the float shape. `-O` has no floating-point lane, so
// every Num operator reaches applyArith and is dispatched on an operator STRING
// — which is what the first column measures. Build:
//   c++ -std=c++17 -O2 -w -Isrc -Iinclude tools/unbox-probe-num.cpp \
//       build/librakupp_{rt,parse,ucd_names,ucd_coll,ucd_props,stubs}.a -o /tmp/u2 && /tmp/u2
#include "Interpreter.h"
#include "Value.h"
#include <cstdio>
#include <chrono>
using namespace rakupp;
// the float shape: mandel's inner loop, Num literals (2e0), 200k entries
static const int W = 300, H = 260;
static long long today_() {
    Value zr = Value::number(0), zi = Value::number(0), t = Value::number(0);
    Value cr = Value::number(0), ci = Value::number(0); Value k = Value::integer(0);
    long long sum = 0;
    for (int y = 0; y < H; y++) { ci = Value::number(y * 4.0 / H - 2.0);
      for (int x = 0; x < W; x++) { cr = Value::number(x * 4.0 / W - 2.0);
        zr = Value::number(0); zi = Value::number(0); k = Value::integer(0);
        while (rtLtB(k, Value::integer(112))) {
            t  = applyArith("+", applyArith("-", applyArith("*", zr, zr), applyArith("*", zi, zi)), cr);
            zi = applyArith("+", applyArith("*", applyArith("*", Value::number(2), zr), zi), ci);
            zr = t;
            if (rtGtB(applyArith("+", applyArith("*", zr, zr), applyArith("*", zi, zi)), Value::number(10))) break;
            k = rtAdd(k, Value::integer(1));
        }
        sum += k.i; } }
    return sum;
}
static long long p3_() {
    long long sum = 0;
    for (int y = 0; y < H; y++) { double ci = y * 4.0 / H - 2.0;
      for (int x = 0; x < W; x++) { double cr = x * 4.0 / W - 2.0, zr = 0, zi = 0; long long k = 0;
        while (k < 112) { double t = zr*zr - zi*zi + cr; zi = 2.0*zr*zi + ci; zr = t;
                          if (zr*zr + zi*zi > 10.0) break; k++; }
        sum += k; } }
    return sum;
}
template <class F> double ms(F f, long long& out) {
    auto t0 = std::chrono::steady_clock::now(); out = f();
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
}
int main() {
    double a = 1e9, b = 1e9; long long ra = 0, rb = 0;
    for (int r = 0; r < 3; r++) { double x = ms(today_, ra); if (x < a) a = x;
                                  double y = ms(p3_, rb);    if (y < b) b = y; }
    printf("  float loop, %d x %d, best of 3   (checksums %lld / %lld %s)\n", W, H, ra, rb, ra == rb ? "agree" : "DIFFER");
    printf("  boxed through the runtime : %8.1f ms\n", a);
    printf("  P3, unboxed doubles       : %8.1f ms   (%.1fx)\n", b, a / b);
}
