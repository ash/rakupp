#include "Profiler.h"
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace rakupp {
namespace prof {

bool on = false;
static std::string g_dest;

struct Entry {
    std::string name, file;
    uint64_t calls = 0;
    uint64_t inclNs = 0, exclNs = 0;
    uint32_t active = 0; // frames of this routine currently on the stack (recursion)
};
struct Frame {
    const void* key;
    uint64_t start, child; // child = ns spent in profiled callees (for exclusive time)
    const char* name;      // borrowed from the live Callable; copied on first leave
    const char* file;
};
struct TMap {
    std::unordered_map<const void*, Entry> agg;
    std::vector<Frame> stack;
};

// One aggregation map per thread, registered on first use and merged at
// report time — workers are joined before the report runs, so the merge
// walks quiet maps (the registry itself is the only shared write, locked).
static std::mutex g_regMu;
static std::vector<std::shared_ptr<TMap>>& registry() {
    static std::vector<std::shared_ptr<TMap>> r;
    return r;
}
static TMap& T() {
    static thread_local std::shared_ptr<TMap> t;
    if (!t) {
        t = std::make_shared<TMap>();
        std::lock_guard<std::mutex> lock(g_regMu);
        registry().push_back(t);
    }
    return *t;
}
static uint64_t nowNs() {
    return (uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

void setDest(const std::string& dest) {
    g_dest = dest;
    on = !dest.empty();
}

void enter(const void* key, const char* name, const char* file) {
    TMap& t = T();
    t.agg[key].active++;
    t.stack.push_back({key, nowNs(), 0, name, file});
}

void leave() {
    TMap& t = T();
    if (t.stack.empty()) return;
    Frame f = t.stack.back();
    t.stack.pop_back();
    uint64_t el = nowNs() - f.start;
    Entry& e = t.agg[f.key];
    if (e.calls == 0) {
        e.name = (f.name && *f.name) ? f.name : "<anon>";
        e.file = f.file ? f.file : "";
    }
    e.calls++;
    e.exclNs += el > f.child ? el - f.child : 0;
    if (e.active == 1) e.inclNs += el; // only the outermost frame of a recursion
    if (e.active > 0) e.active--;
    if (!t.stack.empty()) t.stack.back().child += el;
}

void report() {
    if (!on) return;
    // merge the per-thread maps (workers are already joined)
    std::unordered_map<const void*, Entry> merged;
    {
        std::lock_guard<std::mutex> lock(g_regMu);
        for (auto& tm : registry())
            for (auto& kv : tm->agg) {
                if (kv.second.calls == 0) continue; // enter with no leave (still active)
                Entry& m = merged[kv.first];
                if (m.calls == 0) { m.name = kv.second.name; m.file = kv.second.file; }
                m.calls += kv.second.calls;
                m.inclNs += kv.second.inclNs;
                m.exclNs += kv.second.exclNs;
            }
    }
    std::vector<const Entry*> rows;
    rows.reserve(merged.size());
    for (auto& kv : merged) rows.push_back(&kv.second);
    std::sort(rows.begin(), rows.end(),
              [](const Entry* a, const Entry* b) { return a->exclNs > b->exclNs; });

    bool json = g_dest.size() >= 5 && g_dest.compare(g_dest.size() - 5, 5, ".json") == 0;
    std::ofstream fout;
    std::ostream* out = &std::cerr;
    if (g_dest != "-") {
        fout.open(g_dest);
        if (!fout) { std::cerr << "Cannot write profile to " << g_dest << "\n"; return; }
        out = &fout;
    }
    char buf[64];
    if (json) {
        *out << "[\n";
        bool first = true;
        for (const Entry* e : rows) {
            if (!first) *out << ",\n";
            first = false;
            std::string nm;
            for (char c : e->name) { if (c == '"' || c == '\\') nm += '\\'; nm += c; }
            std::string fl;
            for (char c : e->file) { if (c == '"' || c == '\\') fl += '\\'; fl += c; }
            snprintf(buf, sizeof buf, "%.3f", e->inclNs / 1e6);
            *out << "  {\"name\": \"" << nm << "\", \"file\": \"" << fl
                 << "\", \"calls\": " << e->calls << ", \"incl_ms\": " << buf;
            snprintf(buf, sizeof buf, "%.3f", e->exclNs / 1e6);
            *out << ", \"excl_ms\": " << buf << "}";
        }
        *out << "\n]\n";
    }
    else {
        *out << "Profile — wall time; builtins are attributed to their caller\n";
        *out << "  excl(ms)   incl(ms)      calls  routine\n";
        for (const Entry* e : rows) {
            snprintf(buf, sizeof buf, "%10.3f %10.3f %10llu  ",
                     e->exclNs / 1e6, e->inclNs / 1e6, (unsigned long long)e->calls);
            *out << buf << e->name;
            if (!e->file.empty()) *out << " (" << e->file << ")";
            *out << "\n";
        }
        if (rows.empty()) *out << "  (no user routines were called)\n";
    }
}

} // namespace prof
} // namespace rakupp
