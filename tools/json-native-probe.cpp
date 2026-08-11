// json-native-probe.cpp — price a NATIVE from-json against the real `Value`.
//
// The question: rakupp interprets JSON::Fast's Raku source in ~470 ms where
// Rakudo's JIT runs it in ~36 ms (278 KB). Optimising the tree-walker cannot
// close 13x (see docs/dev/experiments/IR-EXPERIMENT.md). A native `from-json`
// can — but ONLY if building the result is cheap, and that is not obvious:
// rakupp's Hash is `std::map<std::string, Value>` and `sizeof(Value)` is 392
// bytes, so a 278 KB document means ~12,000 red-black-tree nodes each holding
// a 392-byte value. If construction dominates, a native parser buys much less
// than the scan-speed suggests and the plan is wrong.
//
// So this measures the two halves SEPARATELY:
//
//   A. scan only      — tokenize the document, build nothing
//   B. scan + build   — the real thing, producing the same Value tree the
//                       interpreter would hand back to Raku code
//
// B is the number that decides the plan. A says how much of B is the parser
// (which is easy to make fast) versus the data structure (which is not).
//
// Build & run (arm64 lib, NOT the default build/ which is x86_64 under Rosetta):
//
//   clang++ -std=c++17 -O2 -DNDEBUG -Isrc tools/json-native-probe.cpp \
//           build-arm64/librakupp_{rt,parse,ucd_names,ucd_coll,ucd_props}.a -o /tmp/json-native-probe && \
//   /tmp/json-native-probe tools/bench/diagnose/d800.json
//
// Deliberately NOT a complete JSON parser: no \u surrogate pairs, no error
// recovery, no JSONC. Those cost a constant per escape, not per byte, and this
// is a feasibility probe, not the implementation. It parses the diagnose corpus
// exactly, which is what makes the number comparable to json-parse.raku.
#include "Value.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>

using namespace rakupp;
using clk = std::chrono::steady_clock;

namespace {

struct Parser {
    const char* p;
    const char* end;
    bool build;          // false = scan only, discard everything
    long long nodes = 0; // counted either way, so both modes prove they ran

    void ws() {
        while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) ++p;
    }

    // A JSON string, decoding the escapes the corpus uses. Returns the decoded
    // text; in scan mode the caller throws it away but we still pay the decode,
    // because a scan that skips escape handling would flatter the parser.
    std::string str() {
        ++p; // opening quote
        std::string out;
        const char* run = p;
        while (p < end && *p != '"') {
            if (*p == '\\') {
                out.append(run, p - run);
                ++p;
                char c = *p++;
                switch (c) {
                    case 'n': out += '\n'; break;
                    case 't': out += '\t'; break;
                    case 'r': out += '\r'; break;
                    case 'b': out += '\b'; break;
                    case 'f': out += '\f'; break;
                    case '/': out += '/';  break;
                    case '"': out += '"';  break;
                    case '\\': out += '\\'; break;
                    case 'u': {
                        unsigned cp = (unsigned)strtoul(std::string(p, p + 4).c_str(), nullptr, 16);
                        p += 4;
                        // UTF-8 encode (BMP only — the corpus has no surrogates)
                        if (cp < 0x80) out += (char)cp;
                        else if (cp < 0x800) {
                            out += (char)(0xC0 | (cp >> 6));
                            out += (char)(0x80 | (cp & 0x3F));
                        } else {
                            out += (char)(0xE0 | (cp >> 12));
                            out += (char)(0x80 | ((cp >> 6) & 0x3F));
                            out += (char)(0x80 | (cp & 0x3F));
                        }
                        break;
                    }
                    default: out += c;
                }
                run = p;
            } else ++p;
        }
        out.append(run, p - run);
        ++p; // closing quote
        return out;
    }

    // JSON::Fast runs the token through .Numeric, so an integer becomes Int and
    // a decimal becomes Rat. Matching that matters for the plan (a native parser
    // that returned Num everywhere would be faster AND wrong), so it is priced.
    Value num() {
        const char* s = p;
        bool isInt = true;
        if (*p == '-') ++p;
        while (p < end && ((*p >= '0' && *p <= '9') || *p == '.' || *p == 'e' ||
                           *p == 'E' || *p == '+' || *p == '-')) {
            if (*p == '.' || *p == 'e' || *p == 'E') isInt = false;
            ++p;
        }
        if (!build) return Value::nil();
        std::string tok(s, p - s);
        if (isInt) return Value::integer(strtoll(tok.c_str(), nullptr, 10));
        // decimal -> Rat, the way .Numeric does: digits over a power of ten
        size_t dot = tok.find('.');
        if (dot != std::string::npos && tok.find('e') == std::string::npos &&
            tok.find('E') == std::string::npos) {
            std::string digits = tok.substr(0, dot) + tok.substr(dot + 1);
            long long scale = (long long)(tok.size() - dot - 1);
            BigInt den(1);
            for (long long k = 0; k < scale; k++) den = den * BigInt(10);
            return Value::rat(BigInt::fromString(digits), den);
        }
        return Value::number(strtod(tok.c_str(), nullptr));
    }

    Value thing() {
        ws();
        ++nodes;
        switch (*p) {
            case '{': {
                ++p;
                Value h = build ? Value::makeHash() : Value::nil();
                ws();
                if (*p == '}') { ++p; return h; }
                for (;;) {
                    ws();
                    std::string k = str();
                    ws();
                    ++p; // colon
                    Value v = thing();
                    if (build) (*h.hash)[k] = std::move(v);
                    ws();
                    if (*p == ',') { ++p; continue; }
                    ++p; // closing brace
                    return h;
                }
            }
            case '[': {
                ++p;
                Value a = build ? Value::array() : Value::nil();
                ws();
                if (*p == ']') { ++p; return a; }
                for (;;) {
                    Value v = thing();
                    if (build) a.arr->push_back(std::move(v));
                    ws();
                    if (*p == ',') { ++p; continue; }
                    ++p; // closing bracket
                    return a;
                }
            }
            case '"': { std::string s = str(); return build ? Value::str(std::move(s)) : Value::nil(); }
            case 't': p += 4; return Value::boolean(true);
            case 'f': p += 5; return Value::boolean(false);
            case 'n': p += 4; return Value::any();
            default:  return num();
        }
    }
};

double bench(const char* label, const std::string& text, bool build, long long* nodesOut) {
    double best = 1e18;
    for (int rep = 0; rep < 7; rep++) {
        auto t0 = clk::now();
        Parser ps{text.data(), text.data() + text.size(), build, 0};
        Value root = ps.thing();
        auto t1 = clk::now();
        double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
        if (rep && ms < best) best = ms;
        *nodesOut = ps.nodes;
        // root destroyed here, INSIDE the loop but OUTSIDE the timer — teardown
        // is a real cost of the design but the interpreter pays it lazily too,
        // so timing it here would not match json-parse.raku's measurement.
    }
    printf("  %-22s %8.2f ms\n", label, best);
    return best;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) { fprintf(stderr, "usage: json-native-probe FILE.json\n"); return 2; }
    std::ifstream f(argv[1]);
    if (!f) { fprintf(stderr, "cannot open %s\n", argv[1]); return 2; }
    std::stringstream ss; ss << f.rdbuf();
    std::string text = ss.str();

    printf("%s — %zu bytes, sizeof(Value)=%zu\n", argv[1], text.size(), sizeof(Value));
    long long n1 = 0, n2 = 0;
    double scan  = bench("A. scan only",      text, false, &n1);
    double built = bench("B. scan + build",   text, true,  &n2);
    printf("  %-22s %8lld / %lld\n", "nodes (A/B)", n1, n2);
    printf("  %-22s %8.2f ms  (%.0f%% of B)\n", "structure build", built - scan,
           100.0 * (built - scan) / built);
    printf("  %-22s %8.1f MB/s\n", "throughput (B)", text.size() / 1e6 / (built / 1e3));
    return 0;
}
