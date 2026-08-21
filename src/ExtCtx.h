#pragma once
// ExtCtx — the value context behind BOTH halves of the C ABI.
//
// rakupp_ext.h's `RkCtx` is an opaque handle to one of these. ExtApi.cpp gives
// each extension call a stack-allocated one whose arena dies with the call;
// EmbedApi.cpp gives each embedded interpreter a long-lived one it clears
// between evaluations. Same struct, so a host reaches every accessor an
// extension has — which is the whole reason there is one value vocabulary and
// not two (docs/dev/plans/ABI-PLAN.md).
//
// Internal. Not part of the ABI, and nothing outside src/ should include it:
// an extension that saw this file would see `Value`, which is the one thing the
// boundary exists to hide.

#include "Interpreter.h"
#include "Value.h"
#include "rakupp_ext.h"

#include <deque>
#include <map>
#include <string>

namespace rakupp {

struct ExtCtx {
    // A deque rather than a vector because an extension may create thousands of
    // handles, and a vector would reallocate and invalidate every one it had
    // already handed out.
    std::deque<Value> arena;
    ValueList* args = nullptr;
    Interpreter* interp = nullptr;   // ABI 2: rk_call re-enters through this
    std::string error;
    bool failed = false;
    // A Raku exception caught at the rk_call boundary. Kept whole rather than
    // flattened to a message so that returning NULL re-raises the ORIGINAL
    // exception, type included — a native fast path must not turn every
    // X::Whatever into an X::AdHoc on its way through C.
    bool     hasPending = false;
    RakuError pending{Value::any(), ""};

    // rk_key_at/rk_val_at walk an ordered map, and walking it from begin()
    // every time makes iterating a hash quadratic — which is why the first
    // native module left its serializer in Raku. One remembered position turns
    // the sequential case (every serializer, every iteration) into O(1) per
    // step. Keyed by the hash actually being walked, so alternating between two
    // hashes degrades to the old behaviour instead of returning wrong keys.
    const ValueMap* memoHash = nullptr;
    ValueMap::const_iterator memoIt;
    size_t memoIdx = 0;

    RkValue make(Value v) {
        arena.push_back(std::move(v));
        return reinterpret_cast<RkValue>(&arena.back());
    }

    // Release everything handed out so far. The embedding side calls this at
    // the top of each evaluation — "a value is valid until the next rk_eval" is
    // the host-shaped spelling of the extension rule that a handle dies with
    // its call. rk_root is the way out of both.
    void clear() {
        arena.clear();
        memoHash = nullptr;
        memoIdx = 0;
    }

    // Positioned at `i` in `h`, reusing the remembered iterator when it is at
    // or before `i` in the same hash. Returns end() when `i` is out of range.
    ValueMap::const_iterator at(const ValueMap* h,
                                                    size_t i) {
        if (i >= h->size()) return h->end();
        if (memoHash == h && memoIdx <= i) {
            std::advance(memoIt, (long)(i - memoIdx));
        }
        else {
            memoIt = h->begin();
            std::advance(memoIt, (long)i);
            memoHash = h;
        }
        memoIdx = i;
        return memoIt;
    }
};

inline ExtCtx* C(RkCtx c)   { return reinterpret_cast<ExtCtx*>(c); }
inline Value*  V(RkValue v) { return reinterpret_cast<Value*>(v); }

} // namespace rakupp
