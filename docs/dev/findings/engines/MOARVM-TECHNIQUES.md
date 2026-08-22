# What MoarVM teaches Raku++

Fifth in the series ([PERL5-](PERL5-TECHNIQUES.md), [PHP7-](PHP7-TECHNIQUES.md),
[PYTHON3-](PYTHON3-TECHNIQUES.md), [RUBY-TECHNIQUES.md](RUBY-TECHNIQUES.md)).
Sources, read 2026-08-22: the MoarVM repository docs — `arg-passing.md`,
`interpreter.md`, `strings.asciidoc` — plus the spesh and new-disp designs
from Jonathan Worthington's public talks and 6guts posts, cited from memory
(the blog was not fetchable at time of writing; treat those two sections as
faithful summaries, not quotations).

This study is unlike the others: MoarVM runs the **same language**. Its
value is not a bag of borrowable mechanisms — it is the map of where Raku
semantics actually cost, drawn by the team that paid every tax in full, plus
direct A/Bs where both engines solved the same problem. The honest frame
cuts the other way for once: MoarVM implements *all* of Raku with total
fidelity, bootstrapped through NQP, and carries costs (startup, precomp,
meta-circularity) that buying full generality forced. Raku++'s entire
strategy is choosing which of those taxes to pay ahead of time in C++
instead — so every MoarVM mechanism below reads as "the dynamic answer to a
question we answer statically", and the interesting cases are where their
answer says our static one is aimed at the wrong target. (The compiler half
of the reference — Rakudo proper, its optimizer and dispatchers — has its
own doc: [RAKUDO-TECHNIQUES.md](RAKUDO-TECHNIQUES.md).)

## 1. Interned callsites: the argument-passing shape is static — treat it so

From `arg-passing.md`: an `MVMCallsite` is a static descriptor — argument
count, per-argument flags, the named-argument names — interned so identical
shapes are one object process-wide. Frames **preallocate** argument space
sized for their largest callsite; arguments land in a contiguous register
group; `checkarity` and the typed `param_*` ops are validated against the
callsite at bytecode load, not per call. The multi cache (pre-new-disp) was
likewise keyed on callsite + argument types.

Us: every call builds a `ValueList`, and the binder rediscovers the call's
shape *each time* — Interpreter.cpp:1467 counts positionals by scanning the
whole argument vector for `namedArg`-flagged Pairs, per call; and we
confirmed today there is **no multi-dispatch cache** — `candidates` are
re-filtered per call. The lever, which subsumes two earlier items (PHP7
item 6's args-into-pad lane, and the arity machinery TARG already added):
intern the callsite shape at parse time — positional count, named names in
order, flattening flag — annotate the call AST node with it, and let the
binder precompute a **binding plan** per (callsite, callable) pair: which
arg slot feeds which pad slot, which named is present, whether the fast lane
applies. First call computes the plan; subsequent calls replay it. The
namedArg scan, the arity precheck, and the multi candidate filter all become
plan lookups. This is the single most actionable item MoarVM contributes.

## 2. Spesh as the tax map (from talks; not a fetched source)

Spesh logs what the interpreter sees — callsite shapes, operand types,
concreteness, container contents — into per-thread buffers consumed by a
specializer thread, which plans specializations by statistics: insert
guards (type, concreteness, "this Scalar holds an Int"), then optimize
under them — devirtualize, inline, and above all **delete container
derefs**; failed guards deoptimize back to the general path, with on-stack
replacement both ways.

Read as a map, the guard census is a ranked list of what Raku semantics
cost at runtime: container indirection first, then type/definedness checks,
then dispatch. That ordering independently confirms this project's roadmap
— the container/binding refactor is aimed at spesh's #1, and our static
machinery (`PadLayout.simple`, the TARG simple-assign lane, "cells only
where bound" from PHP7 item 4) is the AOT rendition of exactly the
deletions spesh performs dynamically. The philosophical alignment is also
worth recording: spesh optimizes only what statistics make near-certain and
keeps deopt cheap — the same speculate-only-at-p≈1 economics JSC quantifies
([JSC-TECHNIQUES.md](JSC-TECHNIQUES.md) item 1) and our
`DecidedOnce`-family flags practice.

## 3. new-disp: all dispatch is one problem (from talks; not a fetched source)

MoarVM's 2021 dispatch rewrite replaced the method cache, the multi cache,
spesh plugins and the invocation protocol with **one** mechanism: a
dispatcher (written in NQP) runs once at a callsite, records the checks it
performed as a *dispatch program* — a straight-line guard list (argument
type, literal value, attribute read) ending in an action — and the VM
replays that program on subsequent calls, feeding it to spesh for further
specialization. Method dispatch, multiple dispatch, `callsame`/`nextsame`
resumption, container invocation — all the same machinery; megamorphic
sites fall back to hash-table lookups.

For us this is a design instruction for the per-callsite caches (PHP7
item 1): do not grow a method cache *and* a multi cache *and* an accessor
fast path as separate ad-hoc structures. A cached entry should be a tiny
guard list — "invocant ClassInfo is X (serial S), arity shape is Y →
target Z" — so the same cache slot naturally covers method calls, multi
candidates (guards on argument types), and accessor dispatch. We have no
multi cache today (item 1); designing the one cache to be guard-shaped
means multis get it for free the day it lands.

## 4. NFG strings and strands: the same two problems, opposite answers

From `strings.asciidoc`: MoarVM strings are **grapheme** sequences,
normalized on input (NFG = NFC plus synthetics), stored in one of three
widths (ASCII-ish 8-bit, 8-bit, 32-bit) — synthetic graphemes are negative
indices into a trie-backed table (1024-combiner cap) — and concatenation or
substring can produce a **strand**: a rope of references into source
strings, flattened by heuristics rather than eagerly.

Us: UTF-8 bytes in `CowStr`, grapheme semantics recovered lazily — cached
`allAscii`/`crFree`/`nGraphemes` and byte-offset index tables built on
first positional use (the STRING-SCAN-QUADRATICS work). The trade is clean:
MoarVM pays normalization and synthetic bookkeeping on *every input string*
to make grapheme indexing O(1) forever; we pay nothing up front and an
O(n) table build on first positional op, which ASCII-dominant CLI workloads
never trigger. For Roast's deep NFG corners (synthetics inside regex
classes, `\r\n` as one grapheme through every operation) their model is the
reference to test against; nothing in the numbers says to adopt it
wholesale. Strands: two mature engines rope their strings (JSC too — see
the JSC doc item 4), and both grew flattening heuristics because reads got
branchy; our in-place `~=` append on uniquely-owned strings already made
building O(n) without read-path cost. Verdict stands: no ropes until a
profile shows concat-heavy *shared* strings.

## 5. The anti-model half: startup and precompilation

Rakudo-on-MoarVM startup is on the order of 100+ ms, CORE.setting is
precompiled at build time and modules through a precomp store whose
invalidation has its own bug lineage. None of that is carelessness — it is
the structural price of bootstrapping the full language in itself over a
bytecode VM. It is also the single clearest justification of this project's
existence, and the warning that matters as ambitions grow: every piece of
the prelude that moves from C++ into Raku buys elegance with boot-time
parsing, and the mitigation toolkit is already indexed —
[V8-LAZY-PARSING.md](V8-LAZY-PARSING.md) (skip bodies until called; embed
the serialized prelude), PYTHON3 item 6 (freeze what boot always runs).
Decide per addition, with the 2–3 ms budget in view.

## 6. The non-nested runloop rule

From `interpreter.md`: MoarVM never re-enters the interpreter from C — a
callback that needs interpretation is encoded as a state machine and
resumed by the single runloop, which is what keeps continuations,
`gather`/`take` resumption and coroutine-like control transferable. Lua
arrived at the identical rule independently
([LUA-TECHNIQUES.md](LUA-TECHNIQUES.md) item 6: the interpreter must not
use the C stack for language-level calls). Us: the tree-walk recurses the
C stack per Raku call, and laziness works through buffer-fill hooks
(`g_forceLazy`) rather than true suspension. That is fine for what the
engine promises today; the note for the file is that *if* first-class
resumable `gather` at arbitrary depth (or real coroutines) ever becomes a
Roast-blocking requirement, the two-VM consensus says the fix is the
flat/threaded execution loop (PERL5 item 3) — not green threads bolted
onto the walker.

## What deliberately does not transfer

- **6model's full meta-object generality** (custom HOWs, representation
  polymorphism as user surface) — we hard-code the metamodel in C++ and
  answer `.HOW` queries from it; adopting the machinery would be adopting
  the startup bill.
- **The GC** — precise moving generational collector; we are refcounted by
  design (the PEP 703 discussion in PYTHON3 item 5 owns that thread).
- **The JIT** (template-based over spesh graphs) — sits on infrastructure
  we don't have; the series' JIT thinking lives in the Ruby doc (BBV).

## The standing relationship

One note to keep the file honest (and consistent with
[BENCHMARKS.md](../../../status/BENCHMARKS.md)'s framing): Rakudo/MoarVM is
the mature, complete reference; every mechanism above exists because the
full language demanded it. This doc mines their answers so that when a
Roast area forces a mechanism — resumable dispatch, NFG corners, real
continuations — the production-tested design is already indexed, and so
that our static shortcuts are chosen *knowing* what the dynamic engine
found load-bearing.

## Suggested order

| # | change | payoff | cost | depends on |
|---|---|---|---|---|
| 1 | interned callsite shapes + per-(callsite,callable) binding plans | high — every call, and multis get their first cache | medium | pads + arityShape (landed); design with PHP7 item 6 |
| 3 | guard-list shape for the unified callsite cache | design input | — | fold into PHP7 item 1 before it lands |
| 2 | container refactor targets spesh's guard census (containers > types > dispatch) | confirmation | — | already the plan |
| 4 | NFG conformance testing against their model; no ropes for now | correctness | low | Roast NFG sections |
| 5, 6 | markers: prelude growth budget; runloop rule if resumability lands | future | — | V8/Lua docs carry the mechanisms |
