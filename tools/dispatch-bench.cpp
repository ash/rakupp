// dispatch-bench.cpp — size the cost of each call-dispatch shape used by
// rakupp's --exe codegen, against the real runtime. The numbers behind
// docs/dev/DISPATCH.md. Build & run:
//
//   clang++ -std=c++17 -O2 -DNDEBUG -Isrc tools/dispatch-bench.cpp \
//           build/librakupp_rt.a -o /tmp/dispatch-bench && /tmp/dispatch-bench
//
//   A. RT.callBuiltin("chr", args)      — string + hash + map find + std::function (today)
//   B. cached BuiltinFn call            — pointer resolved once, std::function call (proposed)
//   C. direct C++ function call         — what user subs get (u_square style)
//   D. applyArith("+", a, b)            — string-dispatch operator (plain --exe today)
//   E. rtAdd(a, b)                      — inline int fast path (-O lanes today)
//   F. applyArith("x", a, b)-style late op — worst-case string chain (measured via "x")
//
// Methodology: N iterations per shape, 7 reps, report min (matches BENCHMARKS.md).
#include "Interpreter.h"
#include "Value.h"
#include <chrono>
#include <cstdio>
#include <functional>
#include <string>
#include <unordered_map>

using namespace rakupp;
using clk = std::chrono::steady_clock;

static Interpreter RT;

// C: a "user sub"-shaped direct function (mirrors u_square's ValueList form)
static Value u_chrlike(ValueList __a) {
    long long c = __a.empty() ? 0 : __a[0].toInt();
    return Value::integer(c + 1);
}

// B-sim: a map identical in shape to builtins_, so we can hold a cached pointer
static std::unordered_map<std::string, BuiltinFn> simMap;

template <typename F>
static double bench(const char* label, long long n, F&& body) {
    double best = 1e18;
    for (int rep = 0; rep < 7; rep++) {
        auto t0 = clk::now();
        body(n);
        double ms = std::chrono::duration<double, std::milli>(clk::now() - t0).count();
        if (rep > 0 && ms < best) best = ms;  // discard first rep as warm-up
    }
    printf("%-44s %9.2f ms   (%6.2f ns/call)\n", label, best, best * 1e6 / n);
    return best;
}

int main() {
    const long long N = 2'000'000;
    long long sink = 0;

    // real builtin sanity: is "chr" in builtins_?
    { Value r = RT.callBuiltin("chr", ValueList{Value::integer(65)});
      printf("sanity: callBuiltin(\"chr\", 65) = %s\n\n", r.toStr().c_str()); }

    simMap["chr"] = [](Interpreter&, ValueList& a) -> Value {
        long long c = a.empty() ? 0 : a[0].toInt();
        return Value::integer(c + 1);
    };
    const BuiltinFn* cached = &simMap.find("chr")->second;

    // ---- builtin-call shapes (identical ValueList construction in all) ----
    bench("A  RT.callBuiltin(\"chr\", {v})  [today]", N, [&](long long n) {
        for (long long i = 0; i < n; i++)
            sink += RT.callBuiltin("chr", ValueList{Value::integer(i & 0xFF)}).i;
    });
    bench("A' by-name find on same map    [isolated]", N, [&](long long n) {
        for (long long i = 0; i < n; i++) {
            ValueList a{Value::integer(i & 0xFF)};
            auto it = simMap.find("chr");
            sink += it->second(RT, a).i;
        }
    });
    bench("B  cached BuiltinFn ptr call   [proposed]", N, [&](long long n) {
        for (long long i = 0; i < n; i++) {
            ValueList a{Value::integer(i & 0xFF)};
            sink += (*cached)(RT, a).i;
        }
    });
    bench("C  direct C++ fn (user-sub shape)", N, [&](long long n) {
        for (long long i = 0; i < n; i++)
            sink += u_chrlike(ValueList{Value::integer(i & 0xFF)}).i;
    });

    printf("\n");

    // ---- operator shapes ----
    Value a = Value::integer(7), b = Value::integer(9);
    bench("D  applyArith(\"+\", a, b)      [plain --exe]", N, [&](long long n) {
        for (long long i = 0; i < n; i++) sink += applyArith("+", a, b).i;
    });
    bench("E  rtAdd(a, b)                 [-O lane]", N, [&](long long n) {
        for (long long i = 0; i < n; i++) sink += rtAdd(a, b).i;
    });
    Value s1 = Value::str("ab"), s2 = Value::str("cd");
    bench("F  applyArith(\"~\", s1, s2)    [string op]", N / 4, [&](long long n) {
        for (long long i = 0; i < n; i++) sink += (long long)applyArith("~", s1, s2).s.size();
    });
    bench("G  rtConcat(s1, s2)            [direct]", N / 4, [&](long long n) {
        for (long long i = 0; i < n; i++) sink += (long long)rtConcat(s1, s2).s.size();
    });

    printf("\n(sink=%lld)\n", sink);
    return 0;
}
