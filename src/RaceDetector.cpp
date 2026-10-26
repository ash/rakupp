// The happens-before race detector (docs/dev/plans/RACE-DETECTOR-PLAN.md): P1,
// thread records, vector clocks and the sync edges; P2, locations and the
// report. The hooks are listed in RaceDetector.h.
//
// A thread's clock is touched only by that thread (a fork reads the spawner's
// on the spawner), so it needs no lock. A sync object's clock has a mutex of
// its own, a leaf: nothing is locked while it is held. The registries are
// leaked on purpose, as workers may still be finishing during static
// destruction.
#include "RaceDetector.h"
#include "Interpreter.h"   // t_hot, currentStmtLine
#include "InterpreterParts.h"   // btDisplayPath

#include <cctype>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <map>
#include <tuple>
#include <string>
#include <unordered_map>
#include <vector>

namespace rakupp {

namespace {
bool envOn(const char* k) { const char* v = std::getenv(k); return v && *v && std::strcmp(v, "0") != 0; }
// RAKUPP_RACE_TRACE=edges prints the sync edges; =accesses prints every access
// the location hooks see (or skip, and why); =edges,accesses both.
bool traceHas(const char* what) { const char* v = std::getenv("RAKUPP_RACE_TRACE"); return v && std::strstr(v, what); }
const bool g_raceTrace = traceHas("edges");
const bool g_raceTraceAcc = traceHas("access");
}
const bool g_race = envOn("RAKUPP_RACE") || g_raceTrace || g_raceTraceAcc;

using VC = std::vector<uint32_t>;

struct RaceSync { std::mutex m; VC vc; };
struct RaceSnapData { VC vc; };
struct RaceThread {
    uint32_t tid;
    RaceEdge kind;        // what spawned it
    int line;             // the statement that spawned it (0: the main thread)
    RaceEdge forkTag = RaceEdge::Start;
    VC vc;
    std::vector<std::tuple<int, int, int, int>> lastSites;   // the races its last access found (raceNameLast names them)
    int quiet = 0;                               // RaceQuiet: an engine read is under way
    // the container locations lvalue() resolved last, for the store that
    // follows (raceNoted); a few, because a right-hand side may resolve its own
    struct Note { const void* addr = nullptr; uint8_t kind = 0; const void* c = nullptr; uint64_t sub = 0; std::string key; };
    Note notes[4];
    unsigned noteAt = 0;
    std::string fileText;   // the file its last access was in, and that file's index (raceSite)
    int fileIdx = -1;
};

namespace {
const char* edgeName(RaceEdge e) {
    static const char* const names[] = {
        "start", "then", "hyper", "timer", "worker",
        "promise", "lock", "lock-async", "semaphore", "channel", "channel-close", "supply", "react", "atomic",
    };
    return names[(int)e];
}

std::mutex g_traceM;
void trace(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    std::lock_guard<std::mutex> lk(g_traceM);
    std::fputs("race-trace: ", stderr);
    std::vfprintf(stderr, fmt, ap);
    std::fputc('\n', stderr);
    va_end(ap);
}

std::mutex g_threadsM;
auto& g_threads = *new std::vector<std::unique_ptr<RaceThread>>;
std::atomic<bool> g_mainSeen{false};
std::atomic<bool> g_forked{false};   // container locations are recorded from the first spawn on

RaceThread* newThread(RaceEdge kind, int line) {
    std::lock_guard<std::mutex> lk(g_threadsM);
    auto t = std::make_unique<RaceThread>();
    t->tid = (uint32_t)g_threads.size();
    t->kind = kind;
    t->line = line;
    g_threads.push_back(std::move(t));
    return g_threads.back().get();
}

// The calling thread's record. The main thread makes its own at its first
// edge; so does a thread no spawn hook saw, which the trace points out (its
// clock starts empty, so it is ordered with nothing: a missed fork edge shows
// up as reports, never as silence).
RaceThread* self() {
    RaceThread* t = t_hot.race;
    if (!t) {
        const bool unseen = g_mainSeen.exchange(true);
        t = newThread(RaceEdge::Start, 0);
        if (unseen && g_raceTrace) trace("unregistered thread T%u", t->tid);
        t_hot.race = t;
    }
    if (t->vc.size() <= t->tid) { t->vc.resize(t->tid + 1); t->vc[t->tid] = 1; }
    return t;
}

void join(VC& into, const VC& from) {
    if (into.size() < from.size()) into.resize(from.size());
    for (size_t i = 0; i < from.size(); i++)
        if (from[i] > into[i]) into[i] = from[i];
}

void release(RaceThread* t, VC& into, std::mutex* m, RaceEdge e) {
    if (m) { std::lock_guard<std::mutex> lk(*m); join(into, t->vc); }
    else join(into, t->vc);
    if (g_raceTrace) trace("release %s T%u@%u", edgeName(e), t->tid, t->vc[t->tid]);
    t->vc[t->tid]++;
}

// The trace prints an acquire only when it orders something new: the
// components that grew, as `T<id>@<clock>`.
void acquire(const VC& from, std::mutex* m, RaceEdge e) {
    RaceThread* t = self();
    std::string learned;
    auto take = [&] {
        if (t->vc.size() < from.size()) t->vc.resize(from.size());
        for (size_t i = 0; i < from.size(); i++)
            if (from[i] > t->vc[i]) {
                t->vc[i] = from[i];
                if (g_raceTrace) learned += " T" + std::to_string(i) + "@" + std::to_string(from[i]);
            }
    };
    if (m) { std::lock_guard<std::mutex> lk(*m); take(); }
    else take();
    if (!learned.empty()) trace("acquire %s T%u <-%s", edgeName(e), t->tid, learned.c_str());
}

RaceSync& slotSync(RaceSlot& s) {
    RaceSync* p = s.p.load(std::memory_order_acquire);
    if (p) return *p;
    auto* fresh = new RaceSync;
    if (s.p.compare_exchange_strong(p, fresh, std::memory_order_acq_rel)) return *fresh;
    delete fresh;
    return *p;
}

constexpr int kEdgeKinds = (int)RaceEdge::Atomic + 1;
std::mutex g_sideM;
auto* const g_side = new std::unordered_map<const void*, std::unique_ptr<RaceSync>>[kEdgeKinds];
RaceSync& sideSync(const void* k, RaceEdge e) {
    std::lock_guard<std::mutex> lk(g_sideM);
    auto& p = g_side[(int)e][k];
    if (!p) p = std::make_unique<RaceSync>();
    return *p;
}

struct ChanShadow { std::deque<VC> items; VC all, closed; };
std::mutex g_chanM;
auto& g_chans = *new std::unordered_map<const void*, ChanShadow>;
} // namespace

RaceSlot::~RaceSlot() { delete p.load(std::memory_order_relaxed); }

void raceReleaseSlot(RaceSlot& s, RaceEdge e) { RaceSync& y = slotSync(s); release(self(), y.vc, &y.m, e); }
void raceAcquireSlot(RaceSlot& s, RaceEdge e) {
    RaceSync* y = s.p.load(std::memory_order_acquire);
    if (y) acquire(y->vc, &y->m, e);   // (nothing released into it yet: nothing to learn)
}
void raceReleaseKey(const void* k, RaceEdge e) { RaceSync& y = sideSync(k, e); release(self(), y.vc, &y.m, e); }
void raceAcquireKey(const void* k, RaceEdge e) { RaceSync& y = sideSync(k, e); acquire(y.vc, &y.m, e); }

RaceSnap raceSnapshot(RaceEdge e) {
    auto s = std::make_shared<RaceSnapData>();
    release(self(), s->vc, nullptr, e);
    return s;
}
void raceAcquireSnapImpl(const RaceSnap& s, RaceEdge e) { acquire(s->vc, nullptr, e); }

void raceChanSendImpl(const void* chan) {
    RaceThread* t = self();
    {
        std::lock_guard<std::mutex> lk(g_chanM);
        ChanShadow& sh = g_chans[chan];
        sh.items.push_back(t->vc);
        join(sh.all, t->vc);
    }
    if (g_raceTrace) trace("release channel T%u@%u", t->tid, t->vc[t->tid]);
    t->vc[t->tid]++;
}
void raceChanTakeImpl(const void* chan, size_t left) {
    VC got;
    {
        std::lock_guard<std::mutex> lk(g_chanM);
        ChanShadow& sh = g_chans[chan];
        if (sh.items.size() == left + 1) { got = std::move(sh.items.front()); sh.items.pop_front(); }
        else {   // out of step: every send so far, and line the items up again
            got = sh.all;
            while (sh.items.size() > left) sh.items.pop_front();
        }
    }
    acquire(got, nullptr, RaceEdge::Channel);
}
void raceChanCloseImpl(const void* chan) {
    RaceThread* t = self();
    std::lock_guard<std::mutex> lk(g_chanM);
    release(t, g_chans[chan].closed, nullptr, RaceEdge::ChannelClose);
}
void raceChanSawCloseImpl(const void* chan) {
    VC got;
    { std::lock_guard<std::mutex> lk(g_chanM); got = g_chans[chan].closed; }
    acquire(got, nullptr, RaceEdge::ChannelClose);
}

// ---- sites ----
// Where an access happened, in one int: the line in the low 20 bits, and
// above them the file, an index into the files seen so far. Index 0 is the
// program itself, which the report leaves unnamed.
namespace {
std::mutex g_filesM;
auto& g_files = *new std::vector<std::string>{std::string()};   // how the report names each file
int fileIndex(const std::string& display, bool program) {
    if (program) return 0;
    std::lock_guard<std::mutex> lk(g_filesM);
    for (size_t i = 1; i < g_files.size(); i++) if (g_files[i] == display) return (int)i;
    if (g_files.size() >= 2047) return 2047;   // (past that, "a module")
    g_files.push_back(display);
    return (int)g_files.size() - 1;
}
std::string siteWords(int site) {
    const int fi = site >> 20;
    std::string s = "line " + std::to_string(site & 0xFFFFF);
    if (fi > 0) {
        std::lock_guard<std::mutex> lk(g_filesM);
        s += " of " + (fi < (int)g_files.size() ? g_files[(size_t)fi] : std::string("a module"));
    }
    return s;
}
int siteNow() {
    Interpreter* I = Interpreter::liveTarget();
    return I ? I->raceSite() : currentStmtLine() & 0xFFFFF;
}
} // namespace

int Interpreter::raceSite() const {
    const int line = currentStmtLine() & 0xFFFFF;
    const auto& fr = tctx().callFrames;
    const Callable* c = !fr.empty() && fr.back().code ? fr.back().code->code() : nullptr;
    const std::string& f = c ? c->ci().declFile.str() : curDeclFileRef();
    RaceThread* t = self();
    if (t->fileIdx < 0 || f != t->fileText) {   // (a loop stays in one file: no lock, no path work)
        t->fileText = f;
        t->fileIdx = fileIndex(btDisplayPath(f, srcFileAbs_, srcFile_), f.empty() || f == srcFileAbs_ || f == srcFile_);
    }
    return (t->fileIdx << 20) | line;
}

// The named routines the current access is in, innermost first, each with
// the site that called it: "in method bump, called at line 9".
std::string Interpreter::raceBacktrace() const {
    const auto& fr = tctx().callFrames;
    std::string out;
    int shown = 0;
    for (size_t i = fr.size(); i-- > 0 && shown < 5;) {
        const Callable* c = fr[i].code ? fr[i].code->code() : nullptr;
        if (!c || c->ci().name.empty()) continue;   // (a block, a pointy: its line already says where)
        const Callable* caller = i > 0 && fr[i - 1].code ? fr[i - 1].code->code() : nullptr;
        const std::string& cf = caller ? caller->ci().declFile.str() : curDeclFileRef();
        const int fi = fileIndex(btDisplayPath(cf, srcFileAbs_, srcFile_), cf.empty() || cf == srcFileAbs_ || cf == srcFile_);
        out += std::string("    in ") + (c->ci().isMethod ? "method " : "sub ") + c->ci().name +
               ", called at " + siteWords((fi << 20) | (fr[i].line & 0xFFFFF)) + "\n";
        shown++;
    }
    return out;
}

RaceThread* raceFork() {
    RaceThread* p = self();
    const int line = siteNow();
    RaceThread* c = newThread(p->forkTag, line);
    c->vc = p->vc;
    if (c->vc.size() <= c->tid) c->vc.resize(c->tid + 1);
    c->vc[c->tid] = 1;
    if (g_raceTrace) trace("fork %s T%u -> T%u (line %d)", edgeName(p->forkTag), p->tid, c->tid, line & 0xFFFFF);
    g_forked.store(true, std::memory_order_relaxed);
    p->vc[p->tid]++;
    return c;
}
void raceBegin(RaceThread* t) { t_hot.race = t; }
// The thread's body is over: nothing will read its clock again (what it
// released lives on in the sync objects), so the memory goes now, not with
// the record — a program that spawns thousands keeps only the live ones'.
void raceEnd() { if (RaceThread* t = t_hot.race) VC().swap(t->vc); }
RaceEdge raceSetForkTag(RaceEdge e) {
    RaceThread* t = self();
    RaceEdge old = t->forkTag;
    t->forkTag = e;
    return old;
}

// ---- P2: locations ----
//
// One shadow cell per location, in one ordered map keyed by address, so that
// a dying container's cells go in one range erase. One mutex guards the map
// and the race records: the detector serializes its own bookkeeping, which
// orders nothing in the program's vector clocks.

namespace {
using EpochVec = std::vector<std::pair<uint32_t, int>>;   // (clock, line) per thread
struct RaceCell {
    uint32_t wTid = 0, wClk = 0;          // the last plain write (wClk 0: none yet)
    uint32_t rTid = 0, rClk = 0;          // the last plain read, while the reads are ordered
    int wLine = 0, rLine = 0;
    std::unique_ptr<EpochVec> reads;      // …each thread's last read, once two were concurrent
    std::unique_ptr<EpochVec> atomics;    // each thread's last atomic access
};
std::mutex g_shadowM;
auto& g_shadow = *new std::map<uintptr_t, RaceCell>;
// P2b: a container's locations, keyed (container, sub): an element's index, a
// hash entry's key hash, an attribute's slot address, or kWhole for the
// array's length or the hash's keys. Ordered by container, so that a dying
// one's cells go in the same range erase as a scalar's.
constexpr uint64_t kWhole = uint64_t(1) << 63;
using LocKey = std::pair<uintptr_t, uint64_t>;
auto& g_locs = *new std::map<LocKey, RaceCell>;
std::atomic<size_t> g_shadowSize{0};      // (lets a dying container skip the lock when nothing is kept)

enum : int { kRead, kWrite, kAtomic };
const char* const kAccName[] = {"read", "write", "atomic access"};
// a race: the two sites and their kinds, the same record whichever came
// second (sorted), → how often, and the first occurrence as it was seen
struct RaceSeen {
    uint64_t count = 0;
    int cur = 0, prev = 0, curAcc = 0, prevAcc = 0;   // sites and kinds, in the order first seen
    uint32_t curTid = 0, prevTid = 0;
    std::string name, bt;   // bt: the routines the later access was in
    std::string also;       // the name the other thread knows it by, if another (`is rw`, `:=`)
    bool otherSide = false; // …once the other thread has found it too
    int what = 0;
};
auto& g_races = *new std::map<std::tuple<int, int, int, int>, RaceSeen>;

void eraseRange(uintptr_t b, uintptr_t e) {   // (g_shadowM held)
    size_t n = 0;
    auto lo = g_shadow.lower_bound(b), hi = g_shadow.lower_bound(e);
    if (lo != hi) { n += (size_t)std::distance(lo, hi); g_shadow.erase(lo, hi); }
    auto lo2 = g_locs.lower_bound(LocKey{b, 0}), hi2 = g_locs.lower_bound(LocKey{e, 0});
    if (lo2 != hi2) { n += (size_t)std::distance(lo2, hi2); g_locs.erase(lo2, hi2); }
    if (n) g_shadowSize.fetch_sub(n, std::memory_order_relaxed);
}

// The check both kinds of access share, on a location's cell (g_shadowM
// held). `atomic`: never against another atomic access, always against plain
// ones; a plain access, the reverse.
bool check(RaceThread* t, RaceCell& c, int acc, int line) {
    const uint32_t me = t->tid, now = t->vc[me];
    auto known = [&](uint32_t tid, uint32_t clk) {
        return tid == me || clk <= (tid < t->vc.size() ? t->vc[tid] : 0);
    };
    bool fresh = false;
    auto found = [&](int prev, int prevLine, uint32_t prevTid) {
        auto a = std::make_pair(line, acc), b = std::make_pair(prevLine, prev);
        if (b < a) std::swap(a, b);   // (two threads take turns: one race, not one per order)
        auto k = std::make_tuple(a.first, b.first, a.second, b.second);
        RaceSeen& s = g_races[k];
        if (s.count++ == 0) {
            s.cur = line; s.prev = prevLine; s.curAcc = acc; s.prevAcc = prev; s.curTid = me; s.prevTid = prevTid;
            t->lastSites.push_back(k);
            fresh = true;
        }
        else if (!s.otherSide && me != s.curTid) {   // the other thread may know it by another name
            s.otherSide = true;
            t->lastSites.push_back(k);
            fresh = true;
        }
    };
    auto against = [&](const EpochVec& v, int kind) {
        for (uint32_t u = 0; u < v.size(); u++)
            if (v[u].first && !known(u, v[u].first)) found(kind, v[u].second, u);
    };
    // the same access again in the same epoch has nothing new to say
    if (acc == kWrite && c.wClk == now && c.wTid == me) return false;
    if (acc == kRead && (c.reads ? me < c.reads->size() && (*c.reads)[me].first == now
                                 : c.rClk == now && c.rTid == me)) return false;
    if (acc != kAtomic && c.atomics) against(*c.atomics, kAtomic);
    if (c.wClk && !known(c.wTid, c.wClk)) found(kWrite, c.wLine, c.wTid);
    if (acc != kRead) {   // a write, or an atomic access: against the plain reads too
        if (c.reads) against(*c.reads, kRead);
        else if (c.rClk && !known(c.rTid, c.rClk)) found(kRead, c.rLine, c.rTid);
    }
    if (acc == kWrite) { c.wTid = me; c.wClk = now; c.wLine = line; c.rClk = 0; c.reads.reset(); }
    else if (acc == kAtomic) {
        if (!c.atomics) c.atomics = std::make_unique<EpochVec>();
        if (c.atomics->size() <= me) c.atomics->resize(me + 1);
        (*c.atomics)[me] = {now, line};
    }
    else if (c.reads) {
        if (c.reads->size() <= me) c.reads->resize(me + 1);
        (*c.reads)[me] = {now, line};
    }
    else if (!c.rClk || known(c.rTid, c.rClk)) { c.rTid = me; c.rClk = now; c.rLine = line; }
    else {   // two threads' reads with nothing between them: keep both
        c.reads = std::make_unique<EpochVec>(std::max(me, c.rTid) + 1);
        (*c.reads)[c.rTid] = {c.rClk, c.rLine};
        (*c.reads)[me] = {now, line};
        c.rClk = 0;
    }
    return fresh;
}
// …the cell found (or made) in either map
template <class M, class K> bool checkAt(M& map, const K& key, int acc, int line) {
    RaceThread* t = self();
    t->lastSites.clear();
    std::lock_guard<std::mutex> lk(g_shadowM);
    auto ins = map.try_emplace(key);
    if (ins.second) g_shadowSize.fetch_add(1, std::memory_order_relaxed);
    return check(t, ins.first->second, acc, line);
}

std::string threadWords(uint32_t tid) {
    RaceThread* t;
    { std::lock_guard<std::mutex> lk(g_threadsM); t = tid < g_threads.size() ? g_threads[tid].get() : nullptr; }
    std::string s = "thread T" + std::to_string(tid);
    if (t && (t->line & 0xFFFFF)) s += std::string(", a ") + edgeName(t->kind) + " from " + siteWords(t->line);
    else if (t && tid != 0) s += ", started by the engine";   // (forked where no statement runs: a supply's own thread)
    else if (tid == 0) s += ", the main thread";
    return s;
}
std::string withCommas(uint64_t n) {
    std::string s = std::to_string(n);
    for (int i = (int)s.size() - 3; i > 0; i -= 3) s.insert((size_t)i, ",");
    return s;
}
} // namespace

bool raceAccess(const void* key, RaceAcc acc, int line) {
    return checkAt(g_shadow, (uintptr_t)key, acc == RaceAcc::Write ? kWrite : kRead, line);
}
bool raceAtomicAccess(const void* key, int line) { return checkAt(g_shadow, (uintptr_t)key, kAtomic, line); }
void raceNameLast(const std::string& name, int what) {
    RaceThread* t = self();
    Interpreter* I = Interpreter::liveTarget();
    const std::string bt = I ? I->raceBacktrace() : std::string();
    std::lock_guard<std::mutex> lk(g_shadowM);
    for (auto& k : t->lastSites) {   // (one access can race with a write and with reads)
        auto it = g_races.find(k);
        if (it == g_races.end()) continue;
        RaceSeen& s = it->second;
        if (s.name.empty()) { s.name = name; s.what = what; s.bt = bt; }
        else if (name != s.name && s.also.find(name) == std::string::npos) s.also += (s.also.empty() ? "" : ", ") + name;
    }
}

void raceForgetRange(const void* begin, size_t bytes) {
    if (!g_shadowSize.load(std::memory_order_relaxed)) return;
    std::lock_guard<std::mutex> lk(g_shadowM);
    eraseRange((uintptr_t)begin, (uintptr_t)begin + bytes);
}
void raceForgetEnv(const Env* e) {
    if (!g_shadowSize.load(std::memory_order_relaxed)) return;
    std::lock_guard<std::mutex> lk(g_shadowM);
    if (!e->pad.empty()) eraseRange((uintptr_t)&e->pad[0], (uintptr_t)(&e->pad[0] + e->pad.size()));
    for (auto& kv : e->vars) eraseRange((uintptr_t)&kv.second, (uintptr_t)(&kv.second + 1));
}

int raceFinish(int code) {
    if (!g_race) return code;
    std::lock_guard<std::mutex> lk(g_shadowM);
    if (g_races.empty()) return code;
    uint64_t total = 0;
    std::string out;
    for (auto& [k, s] : g_races) {
        const int acc = s.curAcc, prev = s.prevAcc;
        total += s.count;
        out += "rakupp: data race on " + (s.name.empty() ? std::string("a scalar") : s.name) +
               (s.also.empty() ? std::string() : " (also reached as " + s.also + ")") + "\n";
        out += std::string("  ") + kAccName[acc] + " at " + siteWords(s.cur) + " (" + threadWords(s.curTid) + ")\n";
        out += s.bt;
        out += std::string("  previous ") + kAccName[prev] + " at " + siteWords(s.prev) +
               " (" + threadWords(s.prevTid) + ")\n";
        out += acc == kAtomic || prev == kAtomic
             ? "  one of them is atomic and the other plain: \xe2\x9a\x9b orders atomic accesses only with each other\n"
             : "  nothing orders them: no Lock, await, Channel, Supply or \xe2\x9a\x9b between the two threads\n";
        if (s.what == 1)
            out += "  (a push, pop, shift, unshift, splice, list assignment or store past the end changes\n"
                   "  an array's length, and every element access reads it)\n";
        else if (s.what == 2)
            out += "  (inserting a key, deleting one or assigning the whole hash changes its keys,\n"
                   "  and every lookup reads them)\n";
    }
    out += "rakupp: " + std::to_string(g_races.size()) + (g_races.size() == 1 ? " data race" : " data races") +
           " (" + withCommas(total) + (total == 1 ? " occurrence" : " occurrences") + ")\n";
    std::fflush(stdout);
    std::fputs(out.c_str(), stderr);
    std::fflush(stderr);
    return code == 0 ? 66 : code;
}

void raceReadAt(const void* p) { if (Interpreter* I = Interpreter::liveTarget()) I->raceSlot(p, RaceAcc::Read); }

void raceQuiet(int delta) { self()->quiet += delta; }

// ---- P2b: container locations ----

namespace {
enum : uint8_t { kElem, kLen, kEntry, kKeys, kAttr };
const char* const kLocName[] = {"element", "length", "entry", "keys", "attribute"};
uint64_t keyHash(const std::string& k) { return std::hash<std::string>{}(k) & ~kWhole; }

// One access to a container's location. A race found for the first time is
// named then, and only then: the walk for the variable's name is not cheap.
void locAccess(uint8_t kind, const void* c, uint64_t sub, RaceAcc acc, const std::string* key) {
    if (!c || acc == RaceAcc::None || !g_forked.load(std::memory_order_relaxed)) return;
    RaceThread* t = self();
    if (acc != RaceAcc::Atomic && t->quiet) return;
    const int line = siteNow();
    const int a = acc == RaceAcc::Write ? kWrite : acc == RaceAcc::Atomic ? kAtomic : kRead;
    if (g_raceTraceAcc) trace("%s %s of %p at line %d by T%u", kAccName[a], kLocName[kind], c, line & 0xFFFFF, t->tid);
    if (!checkAt(g_locs, LocKey{(uintptr_t)c, sub}, a, line)) return;
    Interpreter* I = Interpreter::liveTarget();
    raceNameLast(I ? I->raceLocName(kind, c, sub, key) : std::string("a container"),
                 kind == kLen ? 1 : kind == kKeys ? 2 : 0);
}

void note(const void* addr, uint8_t kind, const void* c, uint64_t sub, const std::string* key) {
    RaceThread* t = self();
    if (g_raceTraceAcc) trace("note: the %s of %p is at %p (T%u)", kLocName[kind], c, addr, t->tid);
    RaceThread::Note& n = t->notes[t->noteAt++ % 4];
    n.addr = addr; n.kind = kind; n.c = c; n.sub = sub;
    if (key) n.key = *key;
    else n.key.clear();
}
} // namespace

void raceElemImpl(const void* arr, size_t i, RaceAcc acc) {
    locAccess(kLen, arr, kWhole, RaceAcc::Read, nullptr);
    locAccess(kElem, arr, i, acc, nullptr);
}
void raceLenImpl(const void* arr, RaceAcc acc) { locAccess(kLen, arr, kWhole, acc, nullptr); }
void raceEntryImpl(const void* h, const std::string& key, RaceAcc acc) {
    locAccess(kKeys, h, kWhole, RaceAcc::Read, nullptr);
    locAccess(kEntry, h, keyHash(key), acc, &key);
}
void raceKeysImpl(const void* h, RaceAcc acc) { locAccess(kKeys, h, kWhole, acc, nullptr); }
void raceKeysLvImpl(const void* h, const std::string& key) {
    if (!h || !g_forked.load(std::memory_order_relaxed)) return;
    locAccess(kKeys, h, kWhole, static_cast<const ValueMap*>(h)->count(key) ? RaceAcc::Read : RaceAcc::Write, nullptr);
}
void raceAttrImpl(const void* obj, const void* slot, RaceAcc acc) { locAccess(kAttr, obj, (uintptr_t)slot, acc, nullptr); }
void raceOnImpl(const Value* v, const std::string& op) {
    if (!v) return;
    if (v->pk_ == PK::List) locAccess(kLen, v->pv<ValueList>(), kWhole, raceResizes(op) ? RaceAcc::Write : RaceAcc::Read, nullptr);
    else if (v->pk_ == PK::Hash) locAccess(kKeys, v->pv<ValueMap>(), kWhole, RaceAcc::Read, nullptr);
}
bool raceResizes(const std::string& m) {
    return m == "push" || m == "append" || m == "pop" || m == "shift" || m == "unshift" ||
           m == "prepend" || m == "splice" || m == "grab";
}

const void* raceNoteElemImpl(const void* addr, const void* arr, size_t i) {
    if (!g_forked.load(std::memory_order_relaxed)) return addr;
    locAccess(kLen, arr, kWhole, RaceAcc::Read, nullptr);   // finding the element read the length
    note(addr, kElem, arr, i, nullptr);
    return addr;
}
const void* raceNoteEntryImpl(const void* addr, const void* h, const std::string& key) {
    if (g_forked.load(std::memory_order_relaxed)) note(addr, kEntry, h, keyHash(key), &key);
    return addr;
}
const void* raceNoteAttrImpl(const void* addr, const void* obj) {
    if (g_forked.load(std::memory_order_relaxed)) note(addr, kAttr, obj, (uintptr_t)addr, nullptr);
    return addr;
}
bool raceNotedImpl(const void* addr, RaceAcc acc) {
    RaceThread* t = self();
    for (auto& n : t->notes)
        if (n.addr == addr) {
            n.addr = nullptr;   // one store per note: a stale one must not claim a later address
            locAccess(n.kind, n.c, n.sub, acc, n.kind == kEntry ? &n.key : nullptr);
            return true;
        }
    return false;
}

namespace {
// the container a variable holds, if it holds one (a cell: what is inside)
const void* heldContainer(const Value& v0) {
    const Value& v = v0.isCell() ? *v0.deref() : v0;
    if (v.pk_ == PK::List) return v.pv<ValueList>();
    if (v.pk_ == PK::Hash) return v.pv<ValueMap>();
    return nullptr;
}
std::string keyWords(const std::string& k) {
    bool word = !k.empty();
    for (unsigned char ch : k) if (!(std::isalnum(ch) || ch == '_' || ch == '-' || ch >= 0x80)) { word = false; break; }
    if (word) return "<" + k + ">";
    std::string q = "{\"";
    for (char ch : k) { if (ch == '"' || ch == '\\') q += '\\'; q += ch; }
    return q + "\"}";
}
} // namespace

// The variable holding the container, from the accessing thread's scope chain
// (or an attribute of its `self`), then the location within it.
std::string Interpreter::raceLocName(int kind, const void* c, uint64_t sub, const std::string* key) const {
    if (kind == kAttr) {
        const auto* od = static_cast<const ObjectData*>(c);
        std::string an;
        for (auto& kv : od->attrs) if ((uintptr_t)&kv.second == sub) { an = kv.first; break; }
        if (auto cut = an.find('\x01'); cut != std::string::npos) an = an.substr(cut + 1);   // a role-qualified slot
        const std::string cls = od->cls ? " of " + od->cls->name : std::string();
        if (an.empty()) return "an attribute" + cls;
        return (std::strchr("@%&", an[0]) ? std::string(1, an[0]) + "!" + an.substr(1) : "$!" + an) + cls;
    }
    const bool arr = kind == kElem || kind == kLen;
    std::string var;
    for (const Env* e = tctx().cur.get(); e && var.empty(); e = e->parent.get()) {
        if (e->layout)
            for (size_t i = 0; i < e->pad.size() && var.empty(); i++)
                if (heldContainer(e->pad[i]) == c && i < e->layout->names.size()) var = e->layout->names[i];
        for (auto& kv : e->vars)
            if (var.empty() && heldContainer(kv.second) == c) var = kv.first;
    }
    if (var.empty())
        if (const Value* self = tctx().cur ? tctx().cur->findSelf() : nullptr; self && self->obj())
            for (auto& kv : self->obj()->attrs)
                if (!kv.first.empty() && heldContainer(kv.second) == c) {
                    const std::string& k = kv.first;
                    var = std::strchr("@%", k[0]) ? std::string(1, k[0]) + "!" + k.substr(1) : (arr ? "@!" : "%!") + k;
                    break;
                }
    const std::string ks = key ? keyWords(*key) : std::string();
    if (var.empty())
        return kind == kElem ? "element " + std::to_string(sub) + " of an array"
             : kind == kLen  ? std::string("the length of an array")
             : kind == kEntry ? "key " + ks + " of a hash" : std::string("the keys of a hash");
    return kind == kElem ? var + "[" + std::to_string(sub) + "]"
         : kind == kLen  ? "the length of " + var
         : kind == kEntry ? var + ks : "the keys of " + var;
}

void Interpreter::raceSlot(const void* p, RaceAcc acc) const {
    if (acc == RaceAcc::None || !everSpawned_.load(std::memory_order_relaxed)) return;
    if (acc != RaceAcc::Atomic && self()->quiet) return;
    const Value* key = nullptr;
    std::string name;
    for (const Env* e = tctx().cur.get(); e && !key; e = e->parent.get()) {
        if (e->layout && !e->pad.empty()) {
            const Value* b = &e->pad[0];
            const size_t n = e->pad.size();
            auto nameOf = [&](size_t i) { return i < e->layout->names.size() ? e->layout->names[i] : std::string(); };
            if (p >= (const void*)b && p < (const void*)(b + n)) {
                const int ps = (int)((const Value*)p - b);
                if (slotPrivate(e, ps)) {   // only its own thread can reach it
                    if (g_raceTraceAcc) trace("access %d skipped: %s is a private slot", (int)acc, nameOf((size_t)ps).c_str());
                    return;
                }
                key = b[ps].isCell() ? b[ps].deref() : (const Value*)p;
                name = nameOf((size_t)ps);
                break;
            }
            for (size_t i = 0; i < n && !key; i++)
                if (b[i].isCell() && (const void*)b[i].deref() == p) { key = (const Value*)p; name = nameOf(i); }
            if (key) break;
        }
        for (auto& kv : e->vars) {
            if ((const void*)&kv.second == p) { key = kv.second.isCell() ? kv.second.deref() : &kv.second; }
            else if (kv.second.isCell() && (const void*)kv.second.deref() == p) key = (const Value*)p;
            if (key) { name = kv.first; break; }
        }
    }
    const int line = raceSite();
    if (!key) {   // not a scalar: a container location lvalue() noted, or a temporary
        if (raceNotedImpl(p, acc)) return;
        if (g_raceTraceAcc) trace("access %d skipped at line %d: %p is not a known location", (int)acc, line & 0xFFFFF, p);
        return;
    }
    if (g_raceTraceAcc) trace("%s %s at line %d by T%u", acc == RaceAcc::Write ? "write" : acc == RaceAcc::Read ? "read" : "atomic",
                              name.c_str(), line & 0xFFFFF, self()->tid);
    if (acc == RaceAcc::Atomic ? raceAtomicAccess(key, line) : raceAccess(key, acc, line))
        raceNameLast(name.empty() ? std::string("a scalar") : name);
}

} // namespace rakupp
