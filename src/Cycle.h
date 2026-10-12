// The backup cycle collector (docs/dev/plans/CYCLE-GC-PLAN.md, issue #151).
//
// Reference counting frees everything here except a group of containers that
// refer to each other: when the program lets go of the group, each member's
// count stays at the references the others hold. This collector finds such
// groups and breaks them, and reference counting then frees them as usual.
//
// CANDIDATES. A container whose count stays above zero after a release may
// have just become garbage held up by its own group. An object's body is
// TRACKED: every such release of it, wherever it happens, makes it a
// candidate (Ref.h's kRefTracked; the inline release test is unchanged). An
// array, a hash, a Pair or a container cell is noted where a scope exit drops
// it (cycleNote) — and one the drop frees instead takes its contents' handles
// with it, so those are looked at the same way, a bounded number of levels
// down. The buffer holds NO count: a body that dies while buffered takes
// itself out (cycleForget, from the release path in Ref.h), so being a
// candidate never keeps anything alive or delays a free.
//
// COLLECTION (Cycle.cpp) runs when the buffer passes a threshold, at a scope
// exit on the main thread with no worker threads live: a walk from the
// candidates over the references container bodies hold, then local trial
// deletion — a node whose count exceeds the references from inside the walk
// is held from outside, and so is everything it reaches; the rest is garbage.
// Garbage objects get their DESTROY, the set is checked again (a DESTROY may
// store `self`), and what is still garbage has its contents cleared.
//
// RAKUPP_CYCLE=0 turns it off, RAKUPP_CYCLE=dry finds garbage without freeing
// it, RAKUPP_CYCLE_STATS=1 reports what it did at exit,
// RAKUPP_CYCLE_TRIGGER=N collects whenever N candidates wait (1: at nearly
// every scope exit — the stress setting for the gates), and
// RAKUPP_CYCLE_POISON=1 swaps a collected object's class for a sentinel that
// dies on any method call instead of clearing it — the check that the walk
// never counted a reference that does not exist.
#pragma once

#include <atomic>
#include <cstddef>

#include "Value.h"

namespace rakupp {

class Interpreter;

// 0 off, 1 collect, 2 dry run (find, report, free nothing)
extern int g_cycleMode;
// Candidates in the buffer, and the size that triggers a collection.
extern std::atomic<size_t> g_cycleBufN;
extern std::atomic<size_t> g_cycleTrigger;

// The payload kinds that can hold references to other payloads: what a walk
// visits. A routine (Code) holds its scope, and the scope its variables — the
// walk follows both (a scope is a node of its own, Cycle.cpp).
constexpr unsigned kCyclePkMask = (1u << (unsigned)PK::List) | (1u << (unsigned)PK::Hash) |
                                  (1u << (unsigned)PK::PairV) | (1u << (unsigned)PK::Obj) |
                                  (1u << (unsigned)PK::Match) | (1u << (unsigned)PK::Cell) |
                                  (1u << (unsigned)PK::Code);

// …and the kinds a scope exit notes. Not an object: an object's body is
// TRACKED (Ref.h, kRefTracked), so every release that leaves it alive, a
// scope exit's included, already made it a candidate. Not a routine: a
// scope exit drops one in nearly every frame, and what a routine holds is
// reached through the scopes it captures (cycleNoteKept).
constexpr unsigned kCycleNoteMask = kCyclePkMask & ~(1u << (unsigned)PK::Obj) & ~(1u << (unsigned)PK::Code);

void cycleNoteSlow(const Value& v) noexcept;
// `v` is about to be dropped by a scope that is going away.
inline void cycleNote(const Value& v) noexcept {
    if ((kCycleNoteMask >> (unsigned)v.pk_) & 1u) cycleNoteSlow(v);
}

void cycleNoteKeptSlow(const Value& v) noexcept;
// `v` sits in a scope that is being let go of but stays alive, because a
// routine made in it captured it. A cycle through that routine and the scope
// passes through one of the scope's variables: each becomes a candidate.
inline void cycleNoteKept(const Value& v) noexcept {
    if ((kCyclePkMask >> (unsigned)v.pk_) & 1u) cycleNoteKeptSlow(v);
}

void cycleCollectSlow() noexcept;
// A safe point for a collection: the end of a scope exit.
inline void cycleMaybeCollect() noexcept {
    if (g_cycleBufN.load(std::memory_order_relaxed) >= g_cycleTrigger.load(std::memory_order_relaxed))
        cycleCollectSlow();
}

// An object of a class that cannot be on a cycle has its tracking taken off
// (called from the first release that leaves it alive).
void cycleClassify(Payload* body, ObjectData* od) noexcept;

// The interpreter collections run DESTROY through, and whose worker count
// says when the process is quiet. Set once, by the main interpreter.
void cycleAttach(Interpreter* in) noexcept;
// …and lets go of it (its destructor): the candidates go with it.
void cycleDetach(Interpreter* in) noexcept;
// Collect now, whatever the buffer holds (`$*VM.request-garbage-collection`,
// program end). Returns the number of containers freed.
size_t cycleCollectNow() noexcept;
// Turn cycle collection off for the rest of the process.
void cycleDisable() noexcept;

} // namespace rakupp
