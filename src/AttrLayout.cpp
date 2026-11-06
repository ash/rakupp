// AttrLayout.cpp — an object's attribute keys, laid out by its class
// (OBJECT-SLOTS-PLAN.md batch 1). See AttrLayout.h.
#include "AttrLayout.h"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <set>
#ifdef _WIN32
#include <process.h>
static long processId() { return (long)_getpid(); }
#else
#include <unistd.h>
static long processId() { return (long)getpid(); }
#endif

namespace rakupp {

static bool layoutMatches(const AttrLayout& l, const AttrChainView& chain) {
    if (l.built.size() != chain.n) return false;
    for (size_t i = 0; i < chain.n; i++)
        if (l.built[i].first != chain.at(i) || l.built[i].second != chain.at(i)->attrs.size()) return false;
    return true;
}

std::shared_ptr<const AttrLayout> attrLayoutFor(ClassInfo* ci, const AttrChainView& chain) {
    std::shared_ptr<const AttrLayout> cur = std::atomic_load(&ci->attrLayout);
    if (cur && layoutMatches(*cur, chain)) return cur;
    // built under one lock (rare: once per class version), published atomically:
    // objects of one class are made on worker threads too
    static std::mutex mu;
    std::lock_guard<std::mutex> lk(mu);
    cur = std::atomic_load(&ci->attrLayout);
    if (cur && layoutMatches(*cur, chain)) return cur;
    auto l = std::make_shared<AttrLayout>();
    for (size_t i = 0; i < chain.n; i++) l->built.emplace_back(chain.at(i), chain.at(i)->attrs.size());
    // runAttrDefaults' walk: least derived level first, each level's attributes
    // in declaration order; a key's place is its first store
    std::set<std::string> have;
    std::string buf;
    for (size_t lvlIx = chain.n; lvlIx-- > 0;)
        for (auto& at : chain.at(lvlIx)->attrs) {
            const std::string& k = attrStoreKey(chain, lvlIx, at, buf);
            if (have.insert(k).second) l->keys.push_back(k);
        }
    cur = l;
    std::atomic_store(&ci->attrLayout, cur);
    return cur;
}

// --- the check (RAKUPP_ATTR_LAYOUT_CHECK) ------------------------------------

const char* const g_attrLayoutCheck = [] {
    const char* p = std::getenv("RAKUPP_ATTR_LAYOUT_CHECK");
    return p && *p ? p : nullptr;
}();

namespace {
struct CheckLog {
    std::mutex mu;
    std::set<std::string> seen;   // each distinct line once per process
    long checked = 0, differed = 0;
    // one line per write, appended: every process of a Roast run shares the file
    void append(const std::string& line) {
        if (std::FILE* f = std::fopen(g_attrLayoutCheck, "a")) {
            std::fwrite(line.data(), 1, line.size(), f);
            std::fclose(f);
        }
    }
    ~CheckLog() {
        if (checked)
            append(std::to_string(processId()) + "\tsummary\tchecked=" + std::to_string(checked) +
                   "\tdiffered=" + std::to_string(differed) + "\n");
    }
};
CheckLog& checkLog() { static CheckLog l; return l; }
std::string shown(const std::vector<std::string>& ks) {
    std::string out;
    for (auto& k : ks) {
        if (!out.empty()) out += ",";
        for (char c : k) {
            if (c == '\x01') out += "\\x01";
            else out += c;
        }
    }
    return out;
}
}  // namespace

void attrLayoutCheck(const ObjectData& od, ClassInfo* ci, const AttrChainView& chain, bool wasEmpty) {
    std::shared_ptr<const AttrLayout> layout = attrLayoutFor(ci, chain);
    std::vector<std::string> stored, inLayout;
    bool beyond = false;
    std::set<std::string> lk(layout->keys.begin(), layout->keys.end());
    for (const auto& kv : od.attrs) {
        stored.push_back(kv.first);
        if (lk.count(kv.first)) inLayout.push_back(kv.first);
        else beyond = true;
    }
    // what differs: the table held keys before the walk (nonempty), a layout key
    // was never stored (missing), the layout's keys were stored in another order
    // (order), or a key the layout lacks was stored (beyond)
    std::string kinds;
    auto add = [&](const char* k) { kinds += kinds.empty() ? k : std::string(",") + k; };
    if (!wasEmpty) add("nonempty");
    if (inLayout.size() != layout->keys.size()) add("missing");
    else if (inLayout != layout->keys) add("order");
    if (beyond) add("beyond");
    CheckLog& L = checkLog();
    std::lock_guard<std::mutex> g(L.mu);
    L.checked++;
    if (kinds.empty()) return;
    L.differed++;
    std::string line = kinds + "\t" + ci->name + "\tstored=" + shown(stored) + "\tlayout=" + shown(layout->keys);
    if (!L.seen.insert(line).second) return;
    L.append(std::to_string(processId()) + "\t" + line + "\n");
}

}  // namespace rakupp
