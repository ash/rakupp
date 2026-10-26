#pragma once
// The happens-before race detector (docs/dev/plans/RACE-DETECTOR-PLAN.md).
//
// P1: thread records, vector clocks and the sync edges of the plan's table,
// with no locations yet. Every edge is release on one side and acquire on the
// other; RAKUPP_RACE_TRACE=edges prints each as it fires. The detector is on
// when RAKUPP_RACE=1 or RAKUPP_RACE_TRACE is set; off, every hook here is one
// branch on a global const bool (the g_noKernels pattern), and the state a
// sync object carries for it is one null pointer.
//
// Engine-internal synchronization is never an edge: the GIL, ParStripe and
// SlotStripe, sharedMut_, refcounts, liveWorkers_, sleep. The hooks sit where
// the memory model's primitives (guide/ASYNC.md) hand data across threads.
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>

namespace rakupp {

extern const bool g_race;
// the guard every hook takes: off is the case to lay the code out for
#if defined(__GNUC__) || defined(__clang__)
#define RAKUPP_RACE_ON() __builtin_expect(::rakupp::g_race, 0)
#else
#define RAKUPP_RACE_ON() (::rakupp::g_race)
#endif

// What made an edge. The trace names it; the corpus checks it.
enum class RaceEdge : uint8_t {
    Start, Then, Hyper, Timer, Worker,  // forks: what spawned the thread
    Promise, Lock, LockAsync, Semaphore, Channel, ChannelClose, Supply, React, Atomic,
};

struct RaceSync;     // a sync object's vector clock (RaceDetector.cpp)
struct RaceThread;   // a thread's record: its id, its clock, where it was spawned
struct RaceSnapData; // a clock taken at a release, carried by a queued item
using RaceSnap = std::shared_ptr<RaceSnapData>;

// The clock a sync object carries, made at its first edge.
struct RaceSlot {
    std::atomic<RaceSync*> p{nullptr};
    RaceSlot() = default;
    RaceSlot(const RaceSlot&) = delete;
    RaceSlot& operator=(const RaceSlot&) = delete;
    ~RaceSlot();
};

// The out-of-line halves; call the guarded inline forms below.
void raceReleaseSlot(RaceSlot&, RaceEdge);
void raceAcquireSlot(RaceSlot&, RaceEdge);
// An object with no state of its own to carry a clock (a Lock under the GIL,
// a Lock::Async, a Semaphore, an atomic's container) keeps one in a side
// table, keyed by its identity and the edge kind. Not reclaimed yet: a freed
// object's address that comes back inherits its clock, which can only add an
// edge (a missed report, never a false one). P2's destructor hook ends that.
void raceReleaseKey(const void*, RaceEdge);
void raceAcquireKey(const void*, RaceEdge);
RaceSnap raceSnapshot(RaceEdge);                 // a release into a fresh clock
void raceAcquireSnapImpl(const RaceSnap&, RaceEdge);
inline void raceAcquireSnap(const RaceSnap& s, RaceEdge e) { if (s) raceAcquireSnapImpl(s, e); }   // (null when off)
// A Channel: every item carries its sender's clock, and the pull that takes it
// acquires that clock. `left` is the queue's length after the pull. A pull the
// detector did not see the send of (or the reverse) acquires every send so
// far instead: coarser, so it can miss a report but never make one up.
void raceChanSendImpl(const void* chan);
void raceChanTakeImpl(const void* chan, size_t left);
void raceChanCloseImpl(const void* chan);
void raceChanSawCloseImpl(const void* chan);
inline void raceChanSend(const void* c) { if (RAKUPP_RACE_ON()) raceChanSendImpl(c); }
inline void raceChanTake(const void* c, size_t left) { if (RAKUPP_RACE_ON()) raceChanTakeImpl(c, left); }
inline void raceChanClose(const void* c) { if (RAKUPP_RACE_ON()) raceChanCloseImpl(c); }        // .close / .fail
inline void raceChanSawClose(const void* c) { if (RAKUPP_RACE_ON()) raceChanSawCloseImpl(c); }  // a pull that found it closed

// ---- P2: locations ----
// One access to a location, checked against its shadow (FastTrack): the last
// write's epoch, and the last read's (a vector of them once reads by two
// threads were concurrent). `key` is the location's container: a scalar's is
// its address (a pad slot, a cell's inside, a frame variable). Recording
// starts at the first spawn: until then one thread cannot race itself.
enum class RaceAcc : uint8_t { None, Read, Write, Atomic };
struct Env;
// Returns true when this access found a site pair not reported before (so
// that the caller, which knows the location, can name it: raceNameLast).
bool raceAccess(const void* key, RaceAcc acc, int line);
void raceNameLast(const std::string& name, int what = 0);   // what: 1 an array's length, 2 a hash's keys
// An atomic access (⚛, cas, atomic-*): never races another atomic access,
// but a plain access mixed with it does (the plan's "plain mixed with ⚛").
bool raceAtomicAccess(const void* key, int line);
// P2b: a container's locations, keyed by identity so that a growing array's
// elements keep theirs. An element is (array, index), and an access to it also
// reads the array's length; the length is (array), written by a push, pop,
// shift, unshift, splice, a store past the end or a list assignment. A hash
// entry is (hash, key) and reads the hash's keys; the keys are (hash), written
// by an insert, a delete or a list assignment. An attribute is (object, its
// slot). The container is the payload's address (ValueList*, ValueMap*,
// ObjectData*), which raceForgetRange drops with its body; null skips.
//
// The hooks are macros, not inline functions, so that their arguments (often
// a call, `v.arr()`) are evaluated only when the detector is on. An inline
// function evaluates its arguments before its own test, and the element-read
// fast path paid for that: +1.7% on a loop of element reads, off.
#if defined(__GNUC__) || defined(__clang__)
#define RAKUPP_RACE_COLD [[gnu::cold, gnu::noinline]]
#else
#define RAKUPP_RACE_COLD
#endif
RAKUPP_RACE_COLD void raceElemImpl(const void* arr, size_t i, RaceAcc acc);
RAKUPP_RACE_COLD void raceLenImpl(const void* arr, RaceAcc acc);
RAKUPP_RACE_COLD void raceEntryImpl(const void* h, const std::string& key, RaceAcc acc);
RAKUPP_RACE_COLD void raceKeysImpl(const void* h, RaceAcc acc);
RAKUPP_RACE_COLD void raceKeysLvImpl(const void* h, const std::string& key);   // a find-or-insert: writes the keys if `key` is new
RAKUPP_RACE_COLD void raceAttrImpl(const void* obj, const void* slot, RaceAcc acc);
bool raceResizes(const std::string& method);   // an Array method that changes the length (push, splice, …)
struct Value;
// An operation on a whole Array or Hash: a method on it, a built-in sub given
// it first (`push @a, …`, `elems %h`), `+@a`. It reads the length or the keys,
// or for an Array resizer (raceResizes) writes the length. Null skips.
RAKUPP_RACE_COLD void raceOnImpl(const Value* v, const std::string& op);
#define raceOn(v, op)            (RAKUPP_RACE_ON() ? ::rakupp::raceOnImpl(v, op) : (void)0)
#define raceElem(a, i, acc)      (RAKUPP_RACE_ON() ? ::rakupp::raceElemImpl(a, i, acc) : (void)0)
#define raceLen(a, acc)          (RAKUPP_RACE_ON() ? ::rakupp::raceLenImpl(a, acc) : (void)0)
#define raceEntry(h, k, acc)     (RAKUPP_RACE_ON() ? ::rakupp::raceEntryImpl(h, k, acc) : (void)0)
#define raceKeys(h, acc)         (RAKUPP_RACE_ON() ? ::rakupp::raceKeysImpl(h, acc) : (void)0)
#define raceKeysLv(h, k)         (RAKUPP_RACE_ON() ? ::rakupp::raceKeysLvImpl(h, k) : (void)0)
#define raceAttr(o, s, acc)      (RAKUPP_RACE_ON() ? ::rakupp::raceAttrImpl(o, s, acc) : (void)0)
// A store's location, noted where lvalue() resolves it. The store hooks
// (SlotStripe, raceW) see only the address they write, after the right-hand
// side ran, so raceSlot matches that address against the thread's last few
// notes. Each answers its `addr` (a Value*), so that `return &slot;` becomes
// `return raceNoteElem(&slot, …);`. The element's note also reads the length.
RAKUPP_RACE_COLD const void* raceNoteElemImpl(const void* addr, const void* arr, size_t i);
RAKUPP_RACE_COLD const void* raceNoteEntryImpl(const void* addr, const void* h, const std::string& key);
RAKUPP_RACE_COLD const void* raceNoteAttrImpl(const void* addr, const void* obj);
bool raceNotedImpl(const void* addr, RaceAcc acc);   // raceSlot's fallback: checks a noted location
inline Value* raceNoted(const void* p) { return const_cast<Value*>(static_cast<const Value*>(p)); }
#define raceNoteElem(addr, a, i)  (RAKUPP_RACE_ON() ? ::rakupp::raceNoted(::rakupp::raceNoteElemImpl(addr, a, i)) : (addr))
#define raceNoteEntry(addr, h, k) (RAKUPP_RACE_ON() ? ::rakupp::raceNoted(::rakupp::raceNoteEntryImpl(addr, h, k)) : (addr))
#define raceNoteAttr(addr, o)     (RAKUPP_RACE_ON() ? ::rakupp::raceNoted(::rakupp::raceNoteAttrImpl(addr, o)) : (addr))
// A container died, or a frame is about to be reused: its locations' shadows
// go with it, so whatever is made at the same address starts clean.
void raceForgetRange(const void* begin, size_t bytes);
void raceForgetEnv(const Env* e);
// At the program's end: the report, on stderr. A program that ran with a
// race and would have exited 0 exits 66 (Go's -race code); a failing one
// keeps its own status.
int raceFinish(int code);
// An engine read that is not the program's: an atomic op's container, which
// the generic argument pass evaluates before the op takes it by reference
// (`cas` and the `atomic-*` family take their target `is rw`). The location
// hooks look away while one lives; a nested one counts.
void raceQuiet(int delta);
struct RaceQuiet {
    bool on;
    explicit RaceQuiet(bool b) : on(b && g_race) { if (on) raceQuiet(+1); }
    ~RaceQuiet() { if (on) raceQuiet(-1); }
    RaceQuiet(const RaceQuiet&) = delete;
};
// A read on a fast path with no Interpreter at hand (a static helper that
// reads a variable straight from its pad): the attached interpreter's
// raceSlot. `p = raceRd(p)` in place, null passes through.
#if defined(__GNUC__) || defined(__clang__)
[[gnu::cold]]
#endif
void raceReadAt(const void* p);
template <class P> inline P raceRd(P p) { if (RAKUPP_RACE_ON() && p) raceReadAt(p); return p; }

inline void raceRelease(RaceSlot& s, RaceEdge e) { if (RAKUPP_RACE_ON()) raceReleaseSlot(s, e); }
inline void raceAcquire(RaceSlot& s, RaceEdge e) { if (RAKUPP_RACE_ON()) raceAcquireSlot(s, e); }
inline void raceRelease(const void* k, RaceEdge e) { if (RAKUPP_RACE_ON()) raceReleaseKey(k, e); }
inline void raceAcquire(const void* k, RaceEdge e) { if (RAKUPP_RACE_ON()) raceAcquireKey(k, e); }

// Threads. A thread is made on its spawner (BigStackThread's constructor, the
// one entry every worker has): the child starts with the spawner's clock and
// the spawner ticks. The child adopts its record before running anything.
RaceThread* raceFork();
void raceBegin(RaceThread*);
void raceEnd();
// What the forks made in this scope are, for the trace (default: Start).
RaceEdge raceSetForkTag(RaceEdge);
struct RaceForkTag {
    RaceEdge saved{};
    explicit RaceForkTag(RaceEdge e) { if (RAKUPP_RACE_ON()) saved = raceSetForkTag(e); }
    ~RaceForkTag() { if (RAKUPP_RACE_ON()) raceSetForkTag(saved); }
    RaceForkTag(const RaceForkTag&) = delete;
};

// A critical section on an object with no mutex of its own to hook: acquire
// on entry, release on leaving, however it is left.
struct RaceHold {
    const void* k; RaceEdge e;
    RaceHold(const void* key, RaceEdge edge) : k(key), e(edge) { raceAcquire(k, e); }
    ~RaceHold() { raceRelease(k, e); }
    RaceHold(const RaceHold&) = delete;
};

// PromiseState::done. A settle is a release, and a read that sees the promise
// settled is an acquire: await, .result, .status, Thread.finish, a combinator
// folding its members, a `.then` registered after the fact. Every keep, break
// and wait in the engine reads or writes this flag, so one type covers them.
struct RaceDone {
    bool v = false;
    mutable RaceSlot race;
    RaceDone& operator=(bool b) { if (b) raceRelease(race, RaceEdge::Promise); v = b; return *this; }
    operator bool() const { if (v) raceAcquire(race, RaceEdge::Promise); return v; }
};

// A Supplier's mutex: emits, done, quit and tap registration serialize on it,
// so a tap's runs are ordered with each other and after the tap was made.
class RaceMutex {
    std::recursive_mutex m_;
    RaceSlot race_;
public:
    void lock() { m_.lock(); raceAcquire(race_, RaceEdge::Supply); }
    bool try_lock() { if (!m_.try_lock()) return false; raceAcquire(race_, RaceEdge::Supply); return true; }
    void unlock() { raceRelease(race_, RaceEdge::Supply); m_.unlock(); }
};

// A react's wake-up. Whatever ends one of its sources, or the react itself
// (`done`, `last`, a source's done or quit), notifies this, so a notify is a
// release; the react acquires once its loop has finished.
struct RaceCv : std::condition_variable {
    RaceSlot race;
    void notify_all() noexcept { raceRelease(race, RaceEdge::React); std::condition_variable::notify_all(); }
    void notify_one() noexcept { raceRelease(race, RaceEdge::React); std::condition_variable::notify_one(); }
};

} // namespace rakupp
