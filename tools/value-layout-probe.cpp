// value-layout-probe.cpp — phase 1 of REPRESENTATION-PLAN.md, before any layout
// is chosen. `value-build-probe.cpp` showed that shrinking `Value` from 392
// bytes to 24 is worth 1.83x on Hash building. 24 bytes is not reachable — a
// Raku value needs a tag, a native scalar, a string and at least one container
// pointer inline or every Str and Array pays an indirection.
//
// So the question this answers is: WHERE does the 392-byte cost come from?
//
//   * raw SIZE — the memcpy and the cache lines, or
//   * the NON-TRIVIAL MEMBERS — 11 shared_ptr destructors (each a branch and a
//     possible atomic decrement) and 4 std::string ctor/dtor pairs, all run for
//     a value that is an Int.
//
// They point at different designs. If it is size, the layout must union the
// mutually-exclusive pointers. If it is the members, a fat-but-POD Value with
// the cold fields behind ONE pointer is enough, and that is a far smaller and
// safer change.
//
// P392 is the discriminator: same bytes as today, zero non-trivial members.
//
//   clang++ -std=c++17 -O2 -DNDEBUG -Isrc tools/value-layout-probe.cpp \
//           build-arm64/librakupp_{rt,parse,ucd_names,ucd_coll,ucd_props}.a -o /tmp/value-layout-probe && /tmp/value-layout-probe
#include "Value.h"
#include <chrono>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <vector>

using namespace rakupp;
using clk = std::chrono::steady_clock;

static const int N = 12000;

// Same 392 bytes as Value, but trivially copyable — isolates size from members.
struct P392 { unsigned char tag; char pad[391]; };

// The shape phase 1 would actually produce: tag + scalars + CowStr + the two
// hot container pointers + ONE pointer to everything cold.
struct Slim96 {
    unsigned char tag = 0;
    bool flags[7] = {};
    long long i = 0;
    double n = 0;
    CowStr s;                            // 40 — Str is too hot to indirect
    std::shared_ptr<void> arr;           // Array
    std::shared_ptr<void> ext;           // everything cold, allocated only when used
};

// The aggressive variant: containers also behind the one pointer, string too.
struct Slim32 {
    unsigned char tag = 0;
    bool flags[7] = {};
    long long i = 0;
    std::shared_ptr<void> ext;
};

// 24-byte POD, the stand-in value-build-probe.cpp used.
struct P24 { unsigned char tag; long long i; void* p; };

static std::vector<std::string> keys() {
    std::vector<std::string> k; k.reserve(N);
    static const char* names[] = {"id","name","kind","active","score","count",
                                  "tags","desc","esc","meta","created","depth","a","b"};
    for (int i = 0; i < N; i++) k.push_back(std::string(names[i % 14]) + std::to_string(i / 14));
    return k;
}

template <typename F> static double bench(F&& body) {
    double best = 1e18;
    for (int rep = 0; rep < 7; rep++) {
        auto t0 = clk::now();
        body();
        double ms = std::chrono::duration<double, std::milli>(clk::now() - t0).count();
        if (rep && ms < best) best = ms;
    }
    return best;
}

template <typename T>
static void row(const char* label, const std::vector<std::string>& K) {
    double m = bench([&] { std::map<std::string, T> h; for (int i = 0; i < N; i++) h[K[i]] = T(); });
    double v = bench([&] { std::vector<T> a; a.reserve(N); for (int i = 0; i < N; i++) a.push_back(T()); });
    printf("  %-26s %4zu B  %8.2f ms  %8.2f ms\n", label, sizeof(T), m, v);
}

int main() {
    auto K = keys();
    printf("%-28s %6s  %11s  %11s\n", "", "size", "map insert", "vec push");
    row<Value>  ("Value  (today)",        K);
    row<P392>   ("P392   POD, same size", K);
    row<Slim96> ("Slim96 phase-1 shape",  K);
    row<Slim32> ("Slim32 aggressive",     K);
    row<P24>    ("P24    POD stand-in",   K);
    return 0;
}
