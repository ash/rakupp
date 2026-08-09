// value-build-probe.cpp — why does building 19,201 result nodes cost 11.6 ms?
//
// json-native-probe.cpp found that a NATIVE from-json spends 95% of its time
// constructing the `Value` tree, not parsing — 600 ns per node. That is the
// real ceiling on rakupp's JSON throughput, and it is nothing to do with the
// tree-walker. This probe splits that 600 ns into its causes, because the two
// candidates need completely different work:
//
//   * `sizeof(Value) == 392`, with 4 std::strings and 11 shared_ptrs — every
//     construct/copy/destroy touches all of them.
//   * `Hash` is `std::map<std::string, Value>` — a red-black tree with one
//     malloc per entry and no locality, where Rakudo uses a hash table.
//
// Shapes, each building the SAME 12,000 key/value pairs:
//
//   A. std::map<std::string, Value>            — today's Hash
//   B. std::unordered_map<std::string, Value>  — same fat Value, hashed
//   C. std::map<std::string, small>            — today's container, 16-byte value
//   D. std::unordered_map<std::string, small>  — both changes
//   E. std::vector<Value>                      — the Array path, for reference
//   F. std::vector<small>                      — same, slim
//
// B-vs-A prices the container. C-vs-A prices sizeof(Value). D says what both
// together are worth, which is the number that decides whether a native
// from-json can beat Rakudo by the margin Rakudo beats us by today.
//
//   clang++ -std=c++17 -O2 -DNDEBUG -Isrc tools/value-build-probe.cpp \
//           build-arm64/librakupp_rt.a -o /tmp/value-build-probe && /tmp/value-build-probe
#include "Value.h"
#include <chrono>
#include <cstdio>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

using namespace rakupp;
using clk = std::chrono::steady_clock;

// A stand-in for a slimmed Value: tag + payload + one pointer for the rare
// cases. NOT a proposal for the layout, just the right SIZE to price the idea.
struct SmallValue {
    unsigned char tag = 0;
    union { long long i; double n; };
    void* ext = nullptr;
    SmallValue() : i(0) {}
};

static const int NKEYS = 12000;

static std::vector<std::string> keys() {
    std::vector<std::string> k;
    k.reserve(NKEYS);
    // realistic JSON key shapes: short, repeated across records
    static const char* names[] = {"id", "name", "kind", "active", "score",
                                  "count", "tags", "desc", "esc", "meta",
                                  "created", "depth", "a", "b"};
    for (int i = 0; i < NKEYS; i++)
        k.push_back(std::string(names[i % 14]) + std::to_string(i / 14));
    return k;
}

template <typename F>
static double bench(const char* label, F&& body) {
    double best = 1e18;
    for (int rep = 0; rep < 7; rep++) {
        auto t0 = clk::now();
        body();
        double ms = std::chrono::duration<double, std::milli>(clk::now() - t0).count();
        if (rep && ms < best) best = ms;
    }
    printf("  %-38s %8.2f ms   %6.0f ns/entry\n", label, best, best * 1e6 / NKEYS);
    return best;
}

int main() {
    auto K = keys();
    printf("sizeof(Value)=%zu  sizeof(SmallValue)=%zu  entries=%d\n\n",
           sizeof(Value), sizeof(SmallValue), NKEYS);

    double a = bench("A. map<string,Value>        (today)", [&] {
        std::map<std::string, Value> m;
        for (int i = 0; i < NKEYS; i++) m[K[i]] = Value::integer(i);
    });
    double b = bench("B. unordered_map<string,Value>", [&] {
        std::unordered_map<std::string, Value> m;
        m.reserve(NKEYS);
        for (int i = 0; i < NKEYS; i++) m[K[i]] = Value::integer(i);
    });
    double c = bench("C. map<string,SmallValue>", [&] {
        std::map<std::string, SmallValue> m;
        for (int i = 0; i < NKEYS; i++) { SmallValue v; v.i = i; m[K[i]] = v; }
    });
    double d = bench("D. unordered_map<string,SmallValue>", [&] {
        std::unordered_map<std::string, SmallValue> m;
        m.reserve(NKEYS);
        for (int i = 0; i < NKEYS; i++) { SmallValue v; v.i = i; m[K[i]] = v; }
    });
    bench("E. vector<Value>            (Array today)", [&] {
        std::vector<Value> v; v.reserve(NKEYS);
        for (int i = 0; i < NKEYS; i++) v.push_back(Value::integer(i));
    });
    bench("F. vector<SmallValue>", [&] {
        std::vector<SmallValue> v; v.reserve(NKEYS);
        for (int i = 0; i < NKEYS; i++) { SmallValue s; s.i = i; v.push_back(s); }
    });

    printf("\n  container alone (A/B)  %.2fx\n", a / b);
    printf("  Value size alone (A/C) %.2fx\n", a / c);
    printf("  both      (A/D)        %.2fx\n", a / d);

    // The rows above build ONE map of 12,000 entries. That is not the shape a
    // document has: d800.json is ~800 records of ~15 fields, i.e. 800 SMALL
    // hashes. The distinction matters — a hash table amortises its bucket array
    // over many entries, and at 15 entries there is little to amortise, while a
    // red-black tree's per-node malloc is the same either way. Measuring the
    // wrong shape is how a container change gets adopted on a number it will
    // never deliver.
    printf("\n  same 12,000 entries as 800 hashes of 15 (the real shape):\n");
    const int kHashes = 800, kFields = 15;
    double sa = bench("A2. map, 800 x 15", [&] {
        for (int h = 0; h < kHashes; h++) {
            std::map<std::string, Value> m;
            for (int i = 0; i < kFields; i++) m[K[h * kFields + i]] = Value::integer(i);
        }
    });
    double sb = bench("B2. unordered_map, 800 x 15", [&] {
        for (int h = 0; h < kHashes; h++) {
            std::unordered_map<std::string, Value> m;
            for (int i = 0; i < kFields; i++) m[K[h * kFields + i]] = Value::integer(i);
        }
    });
    printf("\n  container, ONE big hash (A/B)  %.2fx\n", a / b);
    printf("  container, REAL shape (A2/B2)  %.2fx\n", sa / sb);
    return 0;
}
