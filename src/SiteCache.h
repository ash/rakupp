// SiteCache.h — what one place in the program has learned about the classes
// that reached it
//
// `$!x` in a method body, `$obj.name` at a call: the same node sees the same
// class (or two or three) over and over, and what it has to find out about
// that class each time — is the attribute stored under its bare name, does a
// method of the name exist, which routine answers — changes only when a
// symbol table does. A SiteCache keeps those facts on the node, per class, for
// one symbol generation (g_symbolGen, Interpreter.cpp).
//
// Up to kWays classes a site; one that sees more stays on the general path for
// the rest. An entry is made once and then updated in place, so a site costs
// at most kWays small blocks however often the generation moves. The entries
// are the node's and are freed with it; a copy of the node starts with none.
//
// Readers take no lock. An entry's class and serial are fixed before it is
// published, and an entry is never unlinked while its node lives. Its facts
// are atomics a writer changes with the generation set to 0 around them, so a
// reader loads the generation, then the facts, then the generation again
// (current): the facts it read are that generation's, both of them.
#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>

namespace rakupp {

struct ClassInfo;

struct SiteEntry {
    const ClassInfo* cls = nullptr;   // the class, and its ClassInfo::serial:
    uint64_t serial = 0;              // an address is reused, a serial is not
    std::atomic<uint64_t> gen{0};     // the generation + 1 the facts hold under (0: none yet)
    std::atomic<uint64_t> a{0}, b{0}; // the facts, as the site reads them
    SiteEntry* next = nullptr;
};

struct SiteCache {
    static constexpr int kWays = 4;
    std::atomic<SiteEntry*> head{nullptr};

    SiteCache() = default;
    // (a copied node learns for itself: the entries are its original's to free)
    SiteCache(const SiteCache&) {}
    SiteCache& operator=(const SiteCache&) { return *this; }
    ~SiteCache() {
        for (SiteEntry* e = head.load(std::memory_order_relaxed); e;) { SiteEntry* n = e->next; delete e; e = n; }
    }

    SiteEntry* find(const ClassInfo* ci, uint64_t serial) const {
        for (SiteEntry* e = head.load(std::memory_order_acquire); e; e = e->next)
            if (e->cls == ci && e->serial == serial) return e;
        return nullptr;
    }
    // A new entry for the class, or null when the site has seen kWays already.
    SiteEntry* add(const ClassInfo* ci, uint64_t serial) {
        static std::mutex mu;   // (one for every site: an entry is added a handful of times a program)
        std::lock_guard<std::mutex> lk(mu);
        int n = 0;
        SiteEntry* h = head.load(std::memory_order_acquire);
        for (SiteEntry* e = h; e; e = e->next) {
            if (e->cls == ci && e->serial == serial) return e;   // another thread's, just now
            n++;
        }
        if (n >= kWays) return nullptr;
        SiteEntry* e = new SiteEntry;
        e->cls = ci;
        e->serial = serial;
        e->next = h;
        head.store(e, std::memory_order_release);
        return e;
    }
    // The facts an entry holds under `gen`, both or neither: false while a
    // writer is between them (the caller takes the general path that once).
    static bool read(const SiteEntry& e, uint64_t gen, uint64_t& a, uint64_t& b) {
        if (e.gen.load(std::memory_order_acquire) != gen) return false;
        a = e.a.load(std::memory_order_relaxed);
        b = e.b.load(std::memory_order_relaxed);
        return e.gen.load(std::memory_order_acquire) == gen;
    }
    // The facts for this generation, published.
    static void publish(SiteEntry& e, uint64_t gen, uint64_t a, uint64_t b = 0) {
        e.gen.store(0, std::memory_order_release);
        e.a.store(a, std::memory_order_relaxed);
        e.b.store(b, std::memory_order_relaxed);
        e.gen.store(gen, std::memory_order_release);
    }
};

// ClassInfo::serial
inline uint64_t nextClassSerial() {
    static std::atomic<uint64_t> n{0};
    return n.fetch_add(1, std::memory_order_relaxed) + 1;
}

}  // namespace rakupp
