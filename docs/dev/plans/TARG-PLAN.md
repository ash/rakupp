# Plan: TARG — the cost of a result, not the cost of an operation

*Written 2026-08-22, before any code, straight after the pads batch
([PADS-PLAN.md](PADS-PLAN.md)). PERL5-TECHNIQUES item 4. Book notes continue
in [../../book/NOTES-pads.md](../../book/NOTES-pads.md) — this campaign is
the same chapter's second half.*

## What perl's TARG is, and what it becomes in a tree-walker

Perl gives every value-producing op a pad slot at compile time (`op_targ`);
at runtime the op writes its result into that same SV and pushes the
POINTER. Zero allocations, zero copies per operation — because perl's
execution protocol is a stack of pointers.

Raku++'s tree-walk returns `Value` BY VALUE from every `eval()`. A result
slot alone cannot remove that move — the protocol would still copy out of
the slot. So the literal mechanism translates poorly, but the COST it
removes translates exactly: everything a hot expression pays that is not
the arithmetic. Measure that first.

## The decomposition (this box, pads binary, best-of-3, 2M iterations)

| body | ns/iter | increment |
|---|---:|---|
| `for ^N { }` | 65 | the loop floor (runLoopBody, execBlock, safePoint, topic) |
| `{ $x }` | 102 | +37 — one pad READ, incl. exec dispatch and the sink copy |
| `{ $x = 1 }` | 173 | **+108 — the assignment CEREMONY for a constant store** |
| `{ $x = $x + 1 }` | 201 | +28 — the specialized binary itself (ch. 19 already took the operand copies) |
| `{ $x += 1 }` | 127 | +62 — the compound path, proof a leaner lane exists |
| `while $n < N { $p = $p + 4; $n = $n + 1 }` | 389 | cond eval + two full assigns |

The arithmetic is 28 ns. The act of STORING its result is 108. And the
subcall sample puts the third residue in the call machinery: bindParams,
typeCheckBind and typeMatchesArg frames on every call of a typed signature
(`str $t, int $p is rw` — string-compared type names, per call, 200k
times). perf-guard's own comment predicted this: "per-call work that
belongs on the AST".

## The levers, in order

**A. The simple-assign lane** — `$padvar = EXPR` (and unify `op=`). A
DecidedOnce shape verdict on the Assign node: target is a pad-annotated
VarExpr, no coercion/type/default/native machinery applies to the slot
(checked once against the layout name, cached; a container that later
grows machinery invalidates by the same liveness rules pads use).
On the verdict: evaluate RHS, move straight into the slot, skip the
ceremony — the keepType/keepDefault reads, nilElemDefault, the
container-identity refill checks, the rw-sync probe — all of which exist
for shapes this one provably is not. Target: a constant store at ≤ the
`+=` lane's 62 ns; `$x = $x + 1` toward ~90 ns/iter.

**B. Conditions without a Value** — a `while`/`if` condition of a
specialized comparison shape (`$n < LIT`, `$a < $b`) evaluates to C++ bool
directly (an `evalCondBool` entry that reuses ch. 19's shape verdicts),
skipping one Value construct+destroy per iteration of every hot loop.

**C. The binder's per-call AST work** — per-Param caches: the resolved
type-accept verdict (so `str`/`int`/`Str`/`Int` params stop
string-comparing type names per call — a DecidedOnce enum on Param), and
pad-direct binding for plain positional scalars (the param's slot index is
its position in the layout; write `pad[i]` and its live bit directly,
skipping define()'s hash probe). rw/named/slurpy/sub-signature params keep
the full path untouched.

**What this plan deliberately does NOT build**: literal per-Binary result
slots. With operands already pointer-read (ch. 19) and the result move
being one 128-byte headerful, the slot would save less than lever A does,
at the price of a new pad region and lifetime rules. Result slots become
the right shape at item 3 (the threaded loop), where the protocol stops
returning by value — the plan there should revisit them.

## Predictions and falsifiers

- asg (407 ms after pads) is 65+108+28: lever A alone should put it near
  **310–330 ms**. If it lands above 370, the verdict check itself is
  costing what it saves — put the check on the NODE, not in the loop.
- subcall (~258 ms) is call-dominated: lever C should take **−15–25%**.
  If typeMatchesArg still shows in a sample afterwards, the cache key is
  wrong (multi/where params fall back — confirm they are excluded, not
  mis-cached).
- loopsum/hash should move again with A (their stores are `+=`-shaped but
  the topic-read and cond paths get B).
- strscan moves little with any of this (substr/ord dispatch-bound — the
  interning story, not this plan). If it MOVES a lot, something else was
  wrong.

## Landed so far (2026-08-22): lever A, plus a lever the plan had not numbered

**A shipped as designed** — `Assign` carries a decided-once shape verdict;
the per-activation checks (the layout's per-slot `simple` bit for
untyped-ness, since typedness lives in `varDefault` and is invisible on the
slot Value; `x_`/`natBits`/`readonly`/Proxy on the slot) all run BEFORE the
RHS evaluates, so the full path never re-runs a side-effecting expression.
The lane replicates exactly: Nil→Any reset, `$`-itemization of Array/Hash,
readonly stripping, the striped store, the rw write-through hook.

**The unnumbered lever**: ch. 19's fast shapes still fetched their operands
with `Env::find` — pads had not reached them. A shared `padPtr(ve)` helper
(the derive-and-compare rule in one place) now feeds the eval and lvalue
fast paths, the simple-assign lane, `evalBinary`'s `scal()` and
`evalIndex`'s base/index fetches. That is why strscan moved despite the
prediction that it would not: its condition and accumulator fetches sit in
specialized binaries.

Measured (interleaved best-of-2 vs the same-day pre-TARG build): asg
405→243 ms (**−40%**, ~121 ns/iter — past the predicted 310–330), strpass
−13%, strscan −11%, fib −7%; loopsum/hash flat (their stores are `op=`
compound — extending the lane to `op=` is follow-up), subcall flat as
predicted (call-bound — lever C).

## Landed, part two (2026-08-22): the four remaining items

All four shipped in one batch:

- **`op=` extension** — the lane verdict became a small op class (`=`, `+=`,
  `-=`, `*=`, `~=`); compounds mirror the full tail exactly: the neutral
  autoviv (`*=` from 1, `~=` from '', `+=`/`-=` from 0), the in-place ASCII
  `~=` append with the NFC fallback, applyArith for the rest, and the rare
  Object-rhs case runs the infix-overload/strOf path INLINE (a bail there
  would re-evaluate a side-effecting rhs). Short-circuit ops (`||=` family)
  and everything off the whitelist keep the full path.
- **Native-int lane** — the layout's `simple` bit now covers lowercase
  (native) declared types, mirroring the full path's own predicate (only
  uppercase types and atomicint register a varDefault check); the lane
  captures the width flags before the store and applies `wrapNative` after,
  exactly as the full path does. `my int8 $b = 100; $b += 100` wraps to −56
  through the lane.
- **Cond-to-bool (lever B)** — `tryCondBool` answers the six Int comparisons
  for chapter-19 shapes as a C++ bool, wired into bare `while`/`until` and
  C-style `loop` conditions (a cond that binds `-> $x` keeps the Value
  path). First iteration decides the shape via the normal path; the rest
  skip the Value round trip. The C-style loop also gained the flat-body
  scope reuse the for-loops already had.
- **Lever C (binder)** — `typeCheckBind` gets a per-Param accept-class
  (decided once from the type NAME) with a per-call shadow guard: two hash
  counts confirm no user subset/class has stolen the core name — even
  mid-run — before the fast-accept fires, so a late `subset Int` still gets
  the full matcher and its exact error message. Fast-ACCEPT only; every
  rejection goes through the full path. paramNatSpec was already cached.

Measured (interleaved best-of-2, loaded box, vs the same-day pre-batch
build): **subcall −19.5%** — inside the predicted −15–25% band — with the
falsifier passing: a post-change sample of subcall shows ZERO frames in
typeMatchesArg/typeCheckBind. strpass **−19.5%**, strscan **−12%**,
loopsum **−8%** (the op= lane), fib/asg/hash flat as expected (untyped
1-param binder, already-laned, index-bound respectively). The targeted
`my int` while-shape (two native assigns + cond) measured **−45%** in
isolation.

**C2 landed too (2026-08-23)**: resolvePads annotates each slottable Param
with (padSlot, padOwner) — the same identity-compare pair VarExpr carries —
and the binder's fast path writes the slot directly with define()'s exact
publication order (value, release-bit, twin erase) whenever the frame
carries the layout the annotation was made against; primed copies and
layout-less frames keep the define() path. Interleaved A/B vs the
pre-C2 build: fib and subcall both **−4.1%**, strpass flat. With that, the
plan's whole list is done; what remains of the per-iteration floor
(runLoopBody's std::function argument, execBlock's statement dispatch,
eval's return-by-value protocol) is item 3 — the threaded loop — where
the result-slot idea this plan set aside becomes the right shape.

## Gates

The standing set, unchanged from the pads batch: t/run.raku 499/499 with
`--exe` goldens, full Roast per-file diff vs a same-day baseline with
serial re-verification of flaps, parmap + stress under RAKUPP_PARALLEL=1,
TSan zero-report leg, census build compiles, interleaved best-of-N A/B
against a stashed pre-batch binary for every number quoted.

**Lever A's run (2026-08-22)**: t/ 499/499; Roast **634**/1464 fully-pass
(the day's best — up from the pads run's 632, timeouts 17→13), per-file
diff vs the pads baseline all TIME-recoveries and documented flappers
(lines.t 6<->7, pick.t RNG), zero regressions; edge semantics verified
one-liner by one-liner (readonly param throw, typed reject-then-accept,
`is default` Nil reset, native-int wrap via the full path); parallel
stress PASS and ThreadSanitizer zero reports through the lane's striped
store and padPtr.
