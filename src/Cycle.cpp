// The backup cycle collector — see Cycle.h for the shape of it, and
// docs/dev/plans/CYCLE-GC-PLAN.md for why it is built this way.
//
// THE SAFETY INVARIANT. A node is freed only when every reference its count
// records comes from a node in the garbage set. Every way the walk can be
// wrong falls on the safe side but one: a reference the walk cannot see (a
// C++ temporary, an Env slot, a std::function capture, another thread's copy)
// leaves the count above what the walk explains, so the node looks held from
// outside and nothing is freed. The ONE unsafe error is counting an edge that
// holds no reference. Every edge `forEachEdge` reports is a PayRef or PRef
// that owns exactly one count on its body — review it line by line, and run
// RAKUPP_CYCLE_POISON=1 after changing it. A count found BELOW what the walk
// explains is proof of such an error, and abandons the collection.
#include "Cycle.h"

#include "Interpreter.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace rakupp {

int g_cycleMode = 1;
bool g_cycleTrack = true;
std::atomic<size_t> g_cycleBufN{0};
std::atomic<size_t> g_cycleTrigger{1024};

namespace {

// --- configuration ---------------------------------------------------------
size_t g_triggerBase = 1024;               // candidates that start a collection (RAKUPP_CYCLE_TRIGGER)
constexpr size_t kTriggerMax = 1u << 20;   // …at most, after deferrals
constexpr size_t kBufCap = (1u << 24) - 1; // the index bits in pflags_
constexpr size_t kWalkCap = 1024;          // nodes one candidate's walk may add
constexpr size_t kBatchBase = 8192;        // nodes a whole collection may visit…
constexpr size_t kBatchPerCand = 64;       // …plus this much per candidate
constexpr int kCascadeBudget = 256;        // container bodies a dying slot's cascade may open
constexpr long kCascadeScan = 4096;        // …and the Values it may read in each
constexpr long kWalkScan = 16384;          // Values one candidate's walk may read

bool g_stats = false;
bool g_triggerFixed = false;   // RAKUPP_CYCLE_TRIGGER given: no backing off
bool g_poison = false;
Interpreter* g_interp = nullptr;
std::thread::id g_mainId;   // the attached interpreter's main thread
bool g_inCollect = false;

struct Stats {
    size_t notes = 0, buffered = 0, forgotten = 0, dropped = 0;
    size_t batches = 0, deferred = 0, cands = 0, candsDead = 0, walks = 0, walkCapped = 0;
    size_t batchCapped = 0, nodes = 0, edges = 0, maxWalk = 0, garbage = 0, freed = 0;
    size_t finalized = 0, resurrected = 0, poisoned = 0, inconsistent = 0;
    size_t noteKind[16] = {}, noteSole = 0, classified[2] = {}, kept = 0, released = 0;
    double secs = 0;
} S;

// --- the candidate buffer ----------------------------------------------------
// Any thread may add or forget a candidate (a scope exit or a last release can
// happen anywhere), so the buffer sits behind a spinlock: uncontended, it costs
// an exchange and a store, which a mutex doubles.
// Never destroyed: a body released during static destruction still asks it.
struct Cand { Payload* p; PK k; };
std::vector<Cand>& g_buf = *new std::vector<Cand>;
std::atomic_flag g_bufLock = ATOMIC_FLAG_INIT;
struct BufGuard {
    BufGuard() { while (g_bufLock.test_and_set(std::memory_order_acquire)) {} }
    ~BufGuard() { g_bufLock.clear(std::memory_order_release); }
};

// Set the buffered bit and the index together, keeping the other flag bits.
void setIndex(Payload* p, size_t idx) {
    uint32_t f = p->pflags_.load(std::memory_order_relaxed), nf;
    do nf = (f & ((1u << kPayCycleIdxShift) - 1)) | kPayCycleBuffered | (uint32_t(idx) << kPayCycleIdxShift);
    while (!p->pflags_.compare_exchange_weak(f, nf, std::memory_order_relaxed));
}
void clearIndex(Payload* p) {
    uint32_t f = p->pflags_.load(std::memory_order_relaxed), nf;
    do nf = f & ((1u << kPayCycleIdxShift) - 1) & ~uint32_t(kPayCycleBuffered);
    while (!p->pflags_.compare_exchange_weak(f, nf, std::memory_order_relaxed));
}

// A buffered object needs no more release notes: its tracked bit comes off
// while it waits (its releases stay on the inline path) and goes back on when
// a collection takes it out of the buffer alive.
void untrack(Payload* p, PK k) {
    if (k == PK::Obj && g_cycleTrack) p->refs_.fetch_and(~kRefTracked, std::memory_order_relaxed);
}
void retrack(Payload* p, PK k) {
    if (k == PK::Obj && g_cycleTrack && !(p->pflags_.load(std::memory_order_relaxed) & kPayCycleAcyclic))
        p->refs_.fetch_or(kRefTracked, std::memory_order_relaxed);
}

void bufferAdd(Payload* p, PK k) {
    if (p->pflags_.load(std::memory_order_relaxed) & kPayCycleBuffered) return;
    BufGuard g;
    if (p->pflags_.load(std::memory_order_relaxed) & kPayCycleBuffered) return;
    if (g_buf.size() >= kBufCap) { S.dropped++; return; }
    setIndex(p, g_buf.size());
    untrack(p, k);
    g_buf.push_back({p, k});
    g_cycleBufN.store(g_buf.size(), std::memory_order_relaxed);
    S.buffered++;
}

// --- the edges -------------------------------------------------------------
// A walk node is a payload body (an object, an array, a hash, a Pair or cell,
// a Match, a routine) or a SCOPE: an Env, which a closure holds through
// std::shared_ptr. An Env's count is that pointer's use_count, and an edge to
// it is one shared_ptr that shares the node's control block.
constexpr PK kPkEnv = static_cast<PK>(0xEE);

// A body of a Value payload is OWNING when its target is its own Value, and
// an ALIAS of a hash entry (ValueAliasBody) otherwise. An alias holds the
// entry through the hash's entry generation, not through a count the walk
// could explain, so it reports no edges.
Value* owningValue(Payload* p) {
    auto* vb = static_cast<ValueBodyBase*>(p);
    Value* own = &static_cast<Body<Value>*>(vb)->v;
    return vb->target == own ? own : nullptr;
}

// f(key, kind, env) once per OWNING handle: `env` is the shared_ptr for a
// scope edge, null for a payload edge.
// An empty array or hash is no edge worth a node: it holds nothing, so it is
// on no cycle, and an edge left out only makes its target look held.
inline bool emptyBody(const Value& v) {
    if (v.pk_ == PK::List) return static_cast<Body<ValueList>*>(v.p_.get())->v.empty();
    if (v.pk_ == PK::Hash) return static_cast<Body<ValueMap>*>(v.p_.get())->v.holdsNothing();
    return false;
}
template <class F>
inline void valueEdge(const Value& v, F& f) {
    if (((kCyclePkMask >> (unsigned)v.pk_) & 1u) && v.p_ && !emptyBody(v)) f((void*)v.p_.get(), v.pk_, nullptr);
}
template <class F>
inline void envEdge(const std::shared_ptr<Env>& e, F& f) {
    if (e) f((void*)e.get(), kPkEnv, &e);
}

// Every Value a hash holds a count through: dead entries keep their Values
// until clear(), and an object-keyed hash keeps its keys' originals.
template <class F>
inline void hashEdges(const ValueMap& h, F& f, bool& pin, long& scan) {
    if (h.entriesAliased()) pin = true;   // a live Pair reaches the entries from outside
    h.forEachHeld([&](const Value& v) {
        if (--scan < 0) { pin = true; return false; }
        valueEdge(v, f);
        return true;
    });
}

// A scope the walk does not enter: a program's, a module's, a package's or an
// EVAL's. The process holds those for good, and their slots are most of what
// a program has — walking them would be all cost.
bool envOpaque(const Env* e) {
    return !e->parent || e->unitFrame || e->packageFrame || e->evalFrame;
}

// Every owning handle in the node; `pin` is set when the node is reachable by
// a route the walk cannot count — or when `scan` (the Values this walk may
// still look at) runs out partway through it. A node left half-enumerated
// is safe pinned: the edges it did report are real, and the ones it did not
// only make their targets look held from outside. Without the budget, one
// live array of a million elements was read whole by every collection that
// reached it.
template <class F>
void forEachEdge(void* key, PK k, F&& f, bool& pin, long& scan) {
    auto values = [&](const auto& seq) {
        for (const Value& v : seq) {
            if (--scan < 0) { pin = true; return; }
            valueEdge(v, f);
        }
    };
    switch (k) {
        case PK::List:
            values(static_cast<Body<ValueList>*>(static_cast<Payload*>(key))->v);
            break;
        case PK::Hash:
            hashEdges(static_cast<Body<ValueMap>*>(static_cast<Payload*>(key))->v, f, pin, scan);
            break;
        case PK::Obj: {
            ObjectData& od = static_cast<Body<ObjectData>*>(static_cast<Payload*>(key))->v;
            hashEdges(od.attrs, f, pin, scan);
            if (od.boxedSlot.p && scan >= 0) valueEdge(*od.boxedSlot.p, f);
            break;
        }
        case PK::Match: {
            MatchData& md = static_cast<Body<MatchData>*>(static_cast<Payload*>(key))->v;
            if (md.pos) f((void*)md.pos.body(), PK::List, nullptr);
            if (md.named) f((void*)md.named.body(), PK::Hash, nullptr);
            if (md.made) f((void*)md.made.body(), PK::PairV, nullptr);
            break;
        }
        case PK::PairV:
        case PK::Cell:
            if (Value* v = owningValue(static_cast<Payload*>(key))) valueEdge(*v, f);
            break;
        case PK::Code: {
            // what a routine owns: its scope, its `state` scope, a multi's
            // candidates, a wrapped routine's wrappers, a candidate's proto.
            // (A builtin's std::function captures are invisible: safe.)
            Callable& c = static_cast<Body<Callable>*>(static_cast<Payload*>(key))->v;
            envEdge(c.closure, f);
            envEdge(c.state.env, f);
            values(c.candidates);
            values(c.wrappers);
            if (c.dispatcherC) f((void*)c.dispatcherC.body(), PK::Code, nullptr);
            break;
        }
        default:
            if (k == kPkEnv) {
                Env* e = static_cast<Env*>(key);
                if (envOpaque(e)) { pin = true; break; }
                for (const auto& kv : e->vars) {
                    if (--scan < 0) { pin = true; return; }
                    valueEdge(kv.second, f);
                }
                values(e->pad);
                envEdge(e->parent.sp(), f);
            }
            break;
    }
}

// Drop what the body holds. Only for a body the walk proved unreachable, and
// while the collector holds a count on every such body, so none of them is
// freed in the middle of another's clearing. A scope is never cleared: every
// cycle through one passes through a routine that holds it, and the scope
// goes when the routine lets go of it.
void breakBody(Payload* p, PK k) {
    switch (k) {
        case PK::List: static_cast<Body<ValueList>*>(p)->v.clear(); break;
        case PK::Hash: static_cast<Body<ValueMap>*>(p)->v.clear(); break;
        case PK::Obj: {
            ObjectData& od = static_cast<Body<ObjectData>*>(p)->v;
            od.attrs.clear();
            od.boxedSlot.p.reset();
            od.hasBoxed = false;
            break;
        }
        case PK::Match: {
            MatchData& md = static_cast<Body<MatchData>*>(p)->v;
            md.pos.reset(); md.named.reset(); md.made.reset();
            break;
        }
        case PK::PairV:
        case PK::Cell:
            if (Value* v = owningValue(p)) *v = Value();
            break;
        case PK::Code: {
            Callable& c = static_cast<Body<Callable>*>(p)->v;
            c.closure.reset();
            c.state.env.reset();
            c.candidates.clear();
            c.wrappers.clear();
            c.dispatcherC.reset();
            break;
        }
        default:
            break;
    }
}

// Poison mode: the object keeps its contents (so a wrong collection cannot
// crash), and its class becomes one with no methods, named for what it was —
// any later method call on it dies with that name in the message.
void poisonBody(Payload* p, PK k) {
    S.poisoned++;
    if (k != PK::Obj) return;
    ObjectData& od = static_cast<Body<ObjectData>*>(p)->v;
    auto ci = std::make_shared<ClassInfo>();
    ci->name = "CYCLE-COLLECTED[" + (od.cls ? od.cls->name : std::string("?")) + "]";
    od.cls = ci;
}

// Take a count on a body unless it is already dying (another thread's last
// release is between the decrement and cycleForget).
bool retainIfLive(Payload* p) {
    uint32_t c = p->refs_.load(std::memory_order_relaxed);
    while ((c & kRefMask) != 0)
        if (p->refs_.compare_exchange_weak(c, c + 1, std::memory_order_relaxed)) return true;
    return false;
}

// Give back a count the collector took, WITHOUT the release counting as a
// candidate: every live object a collection looked at would otherwise be back
// in the buffer the moment the collection let go of it.
void releaseQuiet(Payload* p) {
    uint32_t old = p->refs_.fetch_sub(1, std::memory_order_acq_rel);
    if ((old & kRefMask) == 1) {
        if (p->pflags_.load(std::memory_order_relaxed) & kPayCycleBuffered) cycleForget(p);
        p->destroySelf();
    }
}
uint32_t countOf(const Payload* p) { return p->refs_.load(std::memory_order_relaxed) & kRefMask; }

// --- the cascade at a dying slot ---------------------------------------------
void noteDrop(Payload* p, PK k, int& budget) {
    if (!((kCycleNoteMask >> (unsigned)k) & 1u)) return;   // an object is tracked: its own releases note it
    uint32_t c = countOf(p);
    if (c > 1) { bufferAdd(p, k); return; }
    // the slot is the body's last owner: the body dies with it and drops its
    // own handles, each of which may strand a group in turn
    if (c == 0 || --budget < 0) return;
    bool pin = false;
    long scan = kCascadeScan;
    forEachEdge((void*)p, k, [&](void* q, PK qk, const std::shared_ptr<Env>*) {
        if (qk != kPkEnv) noteDrop(static_cast<Payload*>(q), qk, budget);
    }, pin, scan);
}

// --- one collection ----------------------------------------------------------
struct Node {
    Node(void* key_, PK k_) : key(key_), k(k_) {}
    void* key;
    PK k;
    bool held = false;      // the collector holds a count on it
    bool pinned = false;    // reachable by a route the walk cannot count
    bool walked = false;    // its edges were enumerated
    bool live = false;
    uint32_t internal = 0;
    uint32_t e0 = 0, e1 = 0;  // its edges, in adj[e0, e1)
    std::shared_ptr<Env> env; // a scope node: the collector's own handle (held)
    Payload* p() const { return static_cast<Payload*>(key); }
    bool isEnv() const { return k == kPkEnv; }
    long count() const { return isEnv() ? env.use_count() : (long)countOf(p()); }
};

bool sameOwner(const std::shared_ptr<Env>& a, const std::shared_ptr<Env>& b) {
    return !a.owner_before(b) && !b.owner_before(a);
}

// Node index by address: open addressing, linear probing, half full at most.
// (std::unordered_map allocated a node per entry, which was most of what a
// walk cost.)
class PtrIndex {
    std::vector<std::pair<void*, uint32_t>> t_;
    size_t n_ = 0;
    static size_t hash(void* k) {
        uint64_t x = (uint64_t)(uintptr_t)k;
        x ^= x >> 33; x *= 0xff51afd7ed558ccdULL; x ^= x >> 33;
        return (size_t)x;
    }
    void grow() {
        std::vector<std::pair<void*, uint32_t>> old;
        old.swap(t_);
        t_.assign(old.empty() ? 1024 : old.size() * 2, {nullptr, 0});
        n_ = 0;
        for (auto& e : old) if (e.first) put(e.first, e.second);
    }
public:
    static constexpr uint32_t kNone = 0xffffffffu;
    uint32_t find(void* k) const {
        if (t_.empty()) return kNone;
        const size_t m = t_.size() - 1;
        for (size_t i = hash(k) & m;; i = (i + 1) & m) {
            if (t_[i].first == k) return t_[i].second;
            if (!t_[i].first) return kNone;
        }
    }
    void put(void* k, uint32_t v) {   // (k is not in the table)
        if ((n_ + 1) * 2 > t_.size()) grow();
        const size_t m = t_.size() - 1;
        size_t i = hash(k) & m;
        while (t_[i].first) i = (i + 1) & m;
        t_[i] = {k, v};
        n_++;
    }
};

struct Collection {
    std::vector<Node> nodes;
    PtrIndex at;
    std::vector<uint32_t> queue;   // (one walk's, reused)
    std::vector<uint32_t> adj;
    std::vector<Payload*> holds;   // one count per held body, given back at the end
    ~Collection() { releaseHolds(); }
    void releaseHolds() {
        std::vector<Payload*> h;
        h.swap(holds);
        for (Payload* p : h) releaseQuiet(p);
        for (Node& n : nodes) n.env.reset();   // (a scope may go here, its routines broken)
    }

    // The node for an edge, made on first sight; -1 for an edge that holds
    // no count the node can explain (a non-owning or foreign scope handle).
    static constexpr long kNoRoom = -2;   // (target: no new node allowed)
    long target(void* key, PK k, const std::shared_ptr<Env>* env, bool& isNew, bool mayAdd = true) {
        isNew = false;
        const uint32_t found = at.find(key);
        if (found == PtrIndex::kNone && !mayAdd) return kNoRoom;
        if (k == kPkEnv) {
            if (env->use_count() == 0) return -1;
            if (found != PtrIndex::kNone) return sameOwner(*env, nodes[found].env) ? (long)found : -1;
            Node n(key, k);
            n.env = *env;
            n.held = true;
            at.put(key, (uint32_t)nodes.size());
            nodes.push_back(std::move(n));
            isNew = true;
            return (long)nodes.size() - 1;
        }
        if (found != PtrIndex::kNone) return (long)found;
        at.put(key, (uint32_t)nodes.size());
        nodes.emplace_back(key, k);
        isNew = true;
        return (long)nodes.size() - 1;
    }
    uint32_t addCandidate(Payload* p, PK k) {
        bool isNew;
        return (uint32_t)target((void*)p, k, nullptr, isNew);
    }

    // Breadth-first from `root`, adding at most `cap` new nodes. A node the
    // cap leaves unenumerated is pinned: its outgoing references went
    // uncounted, which can only make their targets look held from outside,
    // and pinning it keeps the reasoning that simple.
    void walk(uint32_t root, size_t cap) {
        size_t added = 0;
        long scan = kWalkScan;
        queue.clear();
        queue.push_back(root);
        bool capped = false;
        for (size_t qi = 0; qi < queue.size(); qi++) {
            uint32_t i = queue[qi];
            if (capped) { nodes[i].pinned = true; continue; }
            bool pin = false;
            uint32_t e0 = (uint32_t)adj.size();
            void* key = nodes[i].key;
            PK k = nodes[i].k;
            forEachEdge(key, k, [&](void* q, PK qk, const std::shared_ptr<Env>* env) {
                bool isNew;
                // past the cap, an edge to a node not seen yet goes uncounted
                // (and pins this one), however many the node holds
                long j = target(q, qk, env, isNew, added <= cap);
                if (j == kNoRoom) { pin = true; return; }
                if (j < 0) return;
                if (isNew) { added++; queue.push_back((uint32_t)j); }
                adj.push_back((uint32_t)j);
                nodes[j].internal++;
            }, pin, scan);
            Node& n = nodes[i];
            n.e0 = e0; n.e1 = (uint32_t)adj.size();
            n.walked = true;
            n.pinned = n.pinned || pin;
            if ((added > cap || scan < 0) && !capped) { capped = true; S.walkCapped++; }
        }
        S.maxWalk = std::max(S.maxWalk, queue.size());
    }

    // What the count records beyond what the walk explains. Negative means
    // the walk counted a reference that does not exist.
    long external(const Node& n) const {
        long known = (long)n.internal + (n.held ? 1 : 0);
        if (n.k == PK::Obj && (n.p()->pflags_.load(std::memory_order_relaxed) & kPayInDestroyReg)) known++;
        return n.count() - known;
    }

    // Mark live every node held from outside or pinned, and all it reaches
    // through counted edges. False when a count is below what the walk
    // explains: then nothing may be freed.
    bool markLive(const std::vector<uint32_t>& set) {
        std::vector<uint32_t> stack;
        for (uint32_t i : set) {
            Node& n = nodes[i];
            long ext = external(n);
            if (ext < 0) {
                S.inconsistent++;
                std::fprintf(stderr, "rakupp: cycle collector: a count below its known references "
                                     "(kind %d, count %ld, known %u) — collection abandoned\n",
                             (int)n.k, n.count(), n.internal);
                return false;
            }
            if ((ext > 0 || n.pinned || !n.walked) && !n.live) { n.live = true; stack.push_back(i); }
        }
        while (!stack.empty()) {
            uint32_t i = stack.back(); stack.pop_back();
            for (uint32_t e = nodes[i].e0; e < nodes[i].e1; e++) {
                Node& t = nodes[adj[e]];
                if (!t.live) { t.live = true; stack.push_back(adj[e]); }
            }
        }
        return true;
    }

    void hold(uint32_t i) {
        if (nodes[i].held) return;
        if (!retainIfLive(nodes[i].p())) return;   // (quiescent: cannot fail)
        holds.push_back(nodes[i].p());
        nodes[i].held = true;
    }

    // After DESTROY ran: the garbage set's own edges again, from scratch —
    // a destructor may have stored `self` anywhere, or changed what the
    // garbage holds. Only edges between members of the set count now.
    bool recheck(std::vector<uint32_t>& garbage) {
        for (uint32_t i : garbage) { nodes[i].internal = 0; nodes[i].live = false; nodes[i].pinned = false; }
        std::vector<std::vector<uint32_t>> out(garbage.size());
        std::unordered_map<uint32_t, uint32_t> pos;
        for (uint32_t g = 0; g < garbage.size(); g++) pos[garbage[g]] = g;
        for (uint32_t g = 0; g < garbage.size(); g++) {
            Node& n = nodes[garbage[g]];
            bool pin = false;
            long scan = kWalkScan;   // (a garbage node was read whole once; a DESTROY that grew it past this pins it)
            forEachEdge(n.key, n.k, [&](void* q, PK, const std::shared_ptr<Env>* env) {
                const uint32_t ti = at.find(q);
                if (ti == PtrIndex::kNone) return;
                auto pg = pos.find(ti);
                if (pg == pos.end()) return;
                Node& t = nodes[ti];
                if (t.isEnv() && (!env || env->use_count() == 0 || !sameOwner(*env, t.env))) return;
                t.internal++;
                out[g].push_back(pg->second);
            }, pin, scan);
            nodes[garbage[g]].pinned = pin;
        }
        std::vector<uint32_t> stack;
        for (uint32_t g = 0; g < garbage.size(); g++) {
            Node& n = nodes[garbage[g]];
            long ext = external(n);
            if (ext < 0) { S.inconsistent++; return false; }
            if ((ext > 0 || n.pinned) && !n.live) { n.live = true; stack.push_back(g); }
        }
        while (!stack.empty()) {
            uint32_t g = stack.back(); stack.pop_back();
            for (uint32_t t : out[g]) {
                Node& m = nodes[garbage[t]];
                if (!m.live) { m.live = true; stack.push_back(t); }
            }
        }
        std::vector<uint32_t> still;
        for (uint32_t i : garbage) {
            if (nodes[i].live) S.resurrected++;
            else still.push_back(i);
        }
        garbage.swap(still);
        return true;
    }
};

size_t collect() {
    if (g_inCollect || !g_interp) return 0;
    g_inCollect = true;
    auto t0 = std::chrono::steady_clock::now();
    Collection c;
    std::vector<Cand> batch;
    {
        BufGuard g;
        batch.swap(g_buf);
        g_cycleBufN.store(0, std::memory_order_relaxed);
        for (Cand& cd : batch) { clearIndex(cd.p); retrack(cd.p, cd.k); }
        c.nodes.reserve(batch.size() * 2);
        // every candidate is alive here — a dying one took itself out under
        // this lock — but a release on another thread may be on its way
        for (Cand& cd : batch) {
            if (!retainIfLive(cd.p)) { cd.p = nullptr; continue; }
            uint32_t i = c.addCandidate(cd.p, cd.k);
            c.nodes[i].held = true;
            c.holds.push_back(cd.p);
        }
    }
    S.batches++;
    S.cands += batch.size();
    size_t freed = 0;

    // 1. the walk, one candidate at a time; a node an earlier walk in this
    //    collection reached is not walked again
    const size_t batchCap = kBatchBase + kBatchPerCand * batch.size();
    for (Cand& cd : batch) {
        if (!cd.p) { S.candsDead++; continue; }
        uint32_t i = c.at.find((void*)cd.p);
        if (c.nodes[i].walked) continue;
        if (countOf(cd.p) == 1) continue;   // ours alone: freed below
        if (c.nodes.size() > batchCap) { S.batchCapped++; break; }
        S.walks++;
        c.walk(i, kWalkCap);
    }
    S.nodes += c.nodes.size();
    S.edges += c.adj.size();

    // 2-4. internal counts were taken during the walk; what nothing outside
    //      holds, and nothing held from outside reaches, is garbage
    std::vector<uint32_t> all(c.nodes.size());
    for (uint32_t i = 0; i < all.size(); i++) all[i] = i;
    std::vector<uint32_t> garbage;
    if (c.markLive(all))
        for (uint32_t i : all)
            if (!c.nodes[i].live) garbage.push_back(i);
    S.garbage += garbage.size();

    if (!garbage.empty() && g_cycleMode == 1) {
        for (uint32_t i : garbage) if (!c.nodes[i].isEnv()) c.hold(i);
        // 5. DESTROY first, with every link intact, then look again
        std::vector<PRef<ObjectData>> fin;
        for (uint32_t i : garbage) {
            Node& n = c.nodes[i];
            if (n.k != PK::Obj) continue;
            uint32_t f = n.p()->pflags_.load(std::memory_order_relaxed);
            if (!(f & kPayInDestroyReg) || (f & kPayCycleFinalized)) continue;
            n.p()->pflags_.fetch_or(kPayCycleFinalized, std::memory_order_relaxed);
            fin.push_back(PRef<ObjectData>::fromSlot(PayRef(n.p(), false)));
        }
        bool ok = true;
        if (!fin.empty()) {
            for (auto& od : fin) { S.finalized++; g_interp->cycleFinalize(od); }
            fin.clear();
            ok = c.recheck(garbage);
        }
        // 6. break: the counts then fall to zero when the holds go
        if (ok) {
            size_t n = 0;
            for (uint32_t i : garbage) {
                if (c.nodes[i].isEnv()) continue;
                if (g_poison) poisonBody(c.nodes[i].p(), c.nodes[i].k);
                else breakBody(c.nodes[i].p(), c.nodes[i].k);
                n++;
            }
            freed = g_poison ? 0 : n;
            S.freed += freed;
        }
    }
    c.releaseHolds();
    // a collection that found nothing in a full buffer makes the next one
    // wait for more candidates; one that found garbage keeps the pace
    g_cycleTrigger.store(g_triggerFixed ? g_triggerBase
                         : garbage.empty() ? std::min(kTriggerMax, std::max(g_triggerBase, batch.size() * 2))
                                           : g_triggerBase,
                         std::memory_order_relaxed);
    S.secs += std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    g_inCollect = false;
    return freed;
}

void printStats() {
    std::fprintf(stderr,
                 "cycle collector (%s): %zu buffered %zu forgotten %zu dropped | "
                 "%zu collections (%zu deferred), %zu candidates (%zu dead by then), %zu walks "
                 "(%zu capped, %zu skipped by the collection cap), %zu nodes, %zu edges, largest walk %zu | "
                 "garbage %zu, freed %zu, finalized %zu, resurrected %zu, poisoned %zu, inconsistent %zu | %.3f s\n",
                 g_cycleMode == 2 ? "dry run" : g_cycleMode == 1 ? "on" : "off",
                 S.buffered, S.forgotten, S.dropped, S.batches, S.deferred, S.cands, S.candsDead, S.walks,
                 S.walkCapped, S.batchCapped, S.nodes, S.edges, S.maxWalk, S.garbage, S.freed, S.finalized,
                 S.resurrected, S.poisoned, S.inconsistent, S.secs);
    std::fprintf(stderr, "  scope-exit notes %zu (%zu sole owner): list %zu hash %zu pair %zu match %zu cell %zu\n",
                 S.notes, S.noteSole, S.noteKind[(int)PK::List], S.noteKind[(int)PK::Hash], S.noteKind[(int)PK::PairV],
                 S.noteKind[(int)PK::Match], S.noteKind[(int)PK::Cell]);
    std::fprintf(stderr, "  instances classified: %zu tracked, %zu acyclic by class; release notes %zu, kept-scope notes %zu\n",
                 S.classified[0], S.classified[1], S.released, S.kept);
}

struct Config {
    Config() {
        if (const char* m = std::getenv("RAKUPP_CYCLE")) {
            if (!std::strcmp(m, "0") || !std::strcmp(m, "off")) { g_cycleMode = 0; g_cycleTrack = false; }
            else if (!std::strcmp(m, "dry") || !std::strcmp(m, "2")) g_cycleMode = 2;
        }
        if (const char* p = std::getenv("RAKUPP_CYCLE_POISON")) g_poison = *p && *p != '0';
        if (const char* s = std::getenv("RAKUPP_CYCLE_STATS")) g_stats = *s && *s != '0';
        // the test knob: collect whenever the buffer holds N candidates (1 =
        // at nearly every scope exit), never backing off — so the poison run
        // exercises the collector on programs too short to fill a buffer
        if (const char* t = std::getenv("RAKUPP_CYCLE_TRIGGER")) {
            long n = std::atol(t);
            if (n > 0) { g_triggerBase = (size_t)n; g_triggerFixed = true; g_cycleTrigger.store(g_triggerBase); }
        }
        if (g_stats) std::atexit(printStats);
    }
} g_config;

} // namespace

void cycleForget(Payload* p) noexcept {
    BufGuard g;
    uint32_t f = p->pflags_.load(std::memory_order_relaxed);
    if (!(f & kPayCycleBuffered)) return;   // a collection took the batch first
    size_t idx = f >> kPayCycleIdxShift;
    clearIndex(p);
    if (idx >= g_buf.size() || g_buf[idx].p != p) return;
    if (idx + 1 != g_buf.size()) {
        g_buf[idx] = g_buf.back();
        setIndex(g_buf[idx].p, idx);
    }
    g_buf.pop_back();
    g_cycleBufN.store(g_buf.size(), std::memory_order_relaxed);
    S.forgotten++;
}

void cycleNoteRelease(Payload* p) noexcept {
    if (g_stats) S.released++;
    if (!g_cycleMode) return;
    // (only an object's body is tracked.) The first release that leaves it
    // alive is where its class is first known to the collector — every way
    // of making an object passes here, the construction protocol or not. A
    // class that cannot be on a cycle takes the tracking off for good. Only
    // at a quiet point: a mixin on another thread may be swapping the class.
    if (g_interp && g_interp->cycleQuiet()) {
        ObjectData& od = static_cast<Body<ObjectData>*>(p)->v;
        if (od.cls) {
            cycleClassify(p, &od);
            if (p->pflags_.load(std::memory_order_relaxed) & kPayCycleAcyclic) return;
        }
    }
    bufferAdd(p, PK::Obj);
}

namespace {

// --- classes that cannot be on a cycle ----------------------------------------
// An attribute type whose every value is a value: what it holds has no
// attributes, elements or scope of its own. (A subclass of one, `class A is
// Int { has $.x }`, does — so a class whose attributes are all typed Int and
// whose instances are such subclasses can form a cycle no member of which is
// tracked. That cycle leaks; nothing is ever freed early.)
bool valueTypeName(const std::string& t) {
    static const char* const kTypes[] = {
        "int", "int8", "int16", "int32", "int64", "uint", "uint8", "uint16", "uint32", "uint64",
        "byte", "atomicint", "num", "num32", "num64", "str", "bool",
        "Int", "UInt", "Num", "Str", "Bool", "Rat", "FatRat", "Complex", "Order",
    };
    for (const char* k : kTypes) if (t == k) return true;
    return false;
}

bool classAcyclic(const ClassInfo* ci, int depth) {
    if (!ci || depth > 32) return false;
    if (!ci->nativeParent.empty() && !valueTypeName(ci->nativeParent)) return false;   // `is Array`: a boxed container
    for (const ClassAttr& a : ci->attrs) {
        if (a.sigil != '$' || !a.doesRoles.empty() || a.inlined) return false;
        if (!valueTypeName(a.type)) return false;
    }
    if (ci->parent && !classAcyclic(ci->parent.get(), depth + 1)) return false;
    for (auto& p : ci->extraParents) if (p && !classAcyclic(p.get(), depth + 1)) return false;
    for (auto& r : ci->composedRoles) if (r && !classAcyclic(r.get(), depth + 1)) return false;
    return true;
}

} // namespace

void cycleClassify(Payload* p, ObjectData* od) noexcept {
    if (!g_cycleTrack || !p || !od) return;
    if (p->pflags_.load(std::memory_order_relaxed) & kPayCycleAcyclic) return;   // (decided already)
    ClassInfo* ci = od->cls.get();
    if (!ci) return;
    signed char v = ci->cycleSafe;
    if (v < 0) { v = classAcyclic(ci, 0) ? 1 : 0; ci->cycleSafe = v; }
    if (g_stats) S.classified[v == 1]++;
    if (v != 1) return;
    p->pflags_.fetch_or(kPayCycleAcyclic, std::memory_order_relaxed);
    p->refs_.fetch_and(~kRefTracked, std::memory_order_relaxed);
}

void cycleNoteKeptSlow(const Value& v) noexcept {
    if (g_stats) S.kept++;
    if (g_cycleMode && v.p_) bufferAdd(v.p_.get(), v.pk_);
}

void cycleNoteSlow(const Value& v) noexcept {
    if (!g_cycleMode || !v.p_) return;
    if (g_stats) { S.notes++; S.noteKind[(unsigned)v.pk_ & 15]++; if (v.p_ && countOf(v.p_.get()) == 1) S.noteSole++; }
    int budget = kCascadeBudget;
    noteDrop(v.p_.get(), v.pk_, budget);
}

void cycleCollectSlow() noexcept {
    if (!g_cycleMode || g_inCollect || !g_interp) return;
    // DESTROY runs from here: never while an exception unwinds through the
    // scope exit that got us here
    if (std::uncaught_exceptions() > 0 || std::this_thread::get_id() != g_mainId) return;
    if (!g_interp->cycleQuiet()) {
        // workers are live: wait for twice as many candidates before trying again
        S.deferred++;
        g_cycleTrigger.store(std::min(kTriggerMax, g_cycleTrigger.load(std::memory_order_relaxed) * 2),
                             std::memory_order_relaxed);
        return;
    }
    try { collect(); } catch (...) { g_inCollect = false; }
}

size_t cycleCollectNow() noexcept {
    if (!g_cycleMode || g_inCollect || !g_interp || !g_interp->cycleQuiet()) return 0;
    try { return collect(); } catch (...) { g_inCollect = false; return 0; }
}

// The OLDEST live interpreter collects: a scratch one made while the program
// runs (a check, a slang's host) never takes over, and one made before the
// program's own and gone again hands over to it.
// (Never destroyed: a compiled program's interpreter is a function-local
// static, and goes after this file's statics have.)
static auto& g_interps = *new std::vector<std::pair<Interpreter*, std::thread::id>>;
static auto& g_interpsMu = *new std::mutex;

void cycleAttach(Interpreter* in) noexcept {
    std::lock_guard<std::mutex> lk(g_interpsMu);
    try { g_interps.emplace_back(in, std::this_thread::get_id()); } catch (...) { return; }
    if (!g_interp) { g_interp = in; g_mainId = std::this_thread::get_id(); }
}

void cycleDetach(Interpreter* in) noexcept {
    std::lock_guard<std::mutex> lk(g_interpsMu);
    for (size_t i = 0; i < g_interps.size(); i++)
        if (g_interps[i].first == in) { g_interps.erase(g_interps.begin() + (long)i); break; }
    if (g_interp != in) return;
    // its candidates go with it: what they hold was that program's
    {
        BufGuard g;
        for (Cand& cd : g_buf) { clearIndex(cd.p); retrack(cd.p, cd.k); }
        g_buf.clear();
        g_cycleBufN.store(0, std::memory_order_relaxed);
    }
    g_interp = g_interps.empty() ? nullptr : g_interps.front().first;
    if (g_interp) g_mainId = g_interps.front().second;
}

void cycleDisable() noexcept { g_cycleMode = 0; }

} // namespace rakupp
