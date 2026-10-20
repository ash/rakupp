# BinaryHeap — one module, five private features, and the release that retired it

Issue #47 ("Rakupp ≈40% slower on macOS over Math::NumberTheory and Graph")
sent us into `BinaryHeap`, and what we found there is the same story
[RAKUDO-INTERNALS.md](RAKUDO-INTERNALS.md) tells about zef, in a different
key: a published module standing on features that have no documentation and
no stability promise, because the documented language could not do what its
author needed. This page is the census, what rakupp answers, what it cost
us in speed, and the fact that changes the priority — the module's only
consumer replaced it before the issue was filed.

## The chain, and how short it is

From the zef ecosystem index (360.zef.pm, read 2026-08-30), counting every
dist in the ecosystem that declares each as a dependency:

| dist | dependents | who |
|---|---|---|
| **BinaryHeap** (zef:dumarchie) | **1** | Graph 0.0.11 … 0.1.2 — 32 releases |
| **Parameterizable** (zef:dumarchie) | **1** | BinaryHeap 0.0.2 … 0.0.7 |
| **LeftistHeap** (zef:antononcube) | **1** | Graph **0.1.3** |

So the whole thing is a single chain — `Graph → BinaryHeap →
Parameterizable` — with exactly one dependent at each link, and it
terminates at Graph 0.1.2. Nothing else in the ecosystem touches either
module. That matters for how much engine work either one deserves.

Inside Graph, `BinaryHeap` is the priority queue for three methods, of
which the benchmark hits one:

| file | line | method |
|---|---|---|
| `Graph::DistanceFindish` | 13 | `!dijkstra-shortest-path-distances` ← what `.diameter` runs |
| `Graph` | 659 | `!dijkstra-shortest-path` |
| `Graph` | 705 | `!a-star-shortest-path` |

`Graph.diameter` calls the first once per vertex: 144 Dijkstra runs for a
12×12 grid, 38,160 pushes and pops, each pop a `replace` into `!sift-down`.

## The private surface it stands on

A binary heap is an ordinary array-backed data structure; nothing about the
*algorithm* is exotic. What is exotic is the Raku it is written in.

| feature | where | what it is |
|---|---|---|
| `method ^parameterize(Mu:U \obj, **@pos is raw)` | Parameterizable | declaring a **caret meta-method on a class**, so `Foo[…]` dispatches to it. Undocumented Rakudo |
| `.^mixin(ROLE)` / `.^set_name(…)` | Parameterizable | MOP calls with no documented contract |
| `multi method push(::?CLASS:U $_ is rw: **@values is raw)` | BinaryHeap | an **`is rw` invocant bound to `$_`**, so the method can autovivify the container it was called on |
| `@!path.BIND-POS($i, $node)` | BinaryHeap `!sift-down` | the Positional bind primitive; not user-facing API |
| `self.CREATE!SET-SELF(@copy)` | BinaryHeap `clone`/`heapify` | `Mu.CREATE` (raw allocation) plus a private-method call on the result |
| `role BinaryHeap[&infix:<precedes> = * cmp * == Less]` | BinaryHeap | a role parameterized by an **operator**, defaulting to a WhateverCode curry |
| `@!array[$!elems - 1]:delete` | BinaryHeap `!extract` | `:delete` on an array element |

`Parameterizable`'s own POD is candid about the first two rows:

> Class `Parameterizable` uses an undocumented Rakudo feature that allows
> classes to be parameterized.

### Why the author did it

The same shape as the zef story: the documented language does not offer the
thing. A parameterized heap wants autovivification — `my
BinaryHeap::MinHeap $h; $h.push(42)` should fill in the container — and that
needs a method that assigns to its own invocant. Rakudo's automatic role
punning does not support that
([rakudo/rakudo#4916](https://github.com/rakudo/rakudo/issues/4916)), so the
type has to be a **class**; but classes are not parameterizable in
documented Raku. `Parameterizable` exists to bridge exactly that gap, and it
can only do so through the MOP. The blame lands on the missing language
feature, not on the author — who, notably, named the repo `raku-` and later
said he should perhaps have named it `rakudo-`.

## What rakupp answers today

All of it, and — the part worth recording — **generically**, not as a
special case named after either module:

- **A user caret meta-method wins over the built-in one**
  ([Builtins.cpp:4478](../../../src/Builtins.cpp)). Any class declaring
  `method ^foo` owns `.^foo`; `^parameterize` is just the instance of that
  rule this chain needs.
- **`T[…]` routes through a user `method ^parameterize`**
  ([Interpreter.cpp:26264](../../../src/Interpreter.cpp)), checked ahead of
  the generic path that would otherwise build `T[Int]` quietly.
- The rest arrived as ordinary conformance work: `BIND-POS` aliasing an
  array slot, a lexical `&infix:<op>` shadowing the built-in it spells, and
  a curry that remembers its comparator (38a0d90, 945285c), the `:D`/`:U`
  smiley enforced on ordinary binds — which is how Parameterizable picks a
  parameterization by which `MIXIN` signature accepts the arguments — and
  `.^set_name` renaming for real (see MODULE-FINDINGS.md).

**This adds no documented-language surface.** A program that never declares
`method ^parameterize` cannot observe any of it; we are matching Rakudo's
meta-object behaviour, not inventing syntax. That is the right shape and
there is nothing here to undo.

There is also nothing here to *win*. In the 12×12 diameter profile,
`^parameterize` is 144 calls and 4.3 ms of 1922 ms — **0.2%** — and `MIXIN`
another 1.5 ms. Class parameterization is not a performance problem.

## What it did cost, and where

Two real engine faults surfaced through this module. Both are worth fixing
on their own merits; neither is "BinaryHeap support".

**1. A Bloom-filter collision taxes an unrelated built-in operator.**
(Fixed 2026-10-20; see [BinaryHeap 2.0](#binaryheap-20-october-2026) below.
The section is kept as the August record.)
`Env::define` arms `g_lexShadowMask` when an anonymous `&infix:<op>` binding
enters a scope, hashed as `(p[0]*31 + p[n-1]*7 + n) & 63`
([Interpreter.h](../../../src/Interpreter.h), added ece4e87). That hash puts
`"precedes"` and `"*"` in the same slot 61, and `"cmp"` and `"=="` in the
same slot 16. BinaryHeap's role parameter is literally `&infix:<precedes>`
and its `MIXIN` takes `&infix:<cmp>`, so loading and using it arms both.

A false positive is not free: at
[Interpreter.cpp:22595](../../../src/Interpreter.cpp) the armed branch
diverts around `evalBinary`'s `fastShape` specialisation (which reads
operands by pointer instead of copying a Value), copies both operands,
builds `"&infix:<*>"` in a string, walks the scope chain to the root and
fails, then takes generic `applyBinOp` instead of `applyArith`.

Measured, 500k iterations of `$x OP $y`, rakupp at HEAD, best of three:

| op | slot | alone | `use BinaryHeap` | |
|---|---|---|---|---|
| `*` | **61** | 0.1109 | **0.1846** | **+66%** |
| `+` | 35 | 0.0918 | 0.0954 | +4% |
| `-` | 47 | 0.0935 | 0.1053 | +13% |
| `<` | 41 | 0.0951 | 0.0945 | -1% |
| `%` | 63 | 0.0928 | 0.1107 | +19% |
| `==` | 16 | 0.1032 | 0.1025 | 0% |
| `/` | 59 | 0.5273 | 0.5018 | -5% |

Slot 61 is the outlier. The `-` and `%` rows are machine noise — this box
was carrying other work and single runs drift ±15%; an earlier quieter pass
put every non-`*` row inside ±5%, with `*` at +82%. Do not read those two
rows as signal. What does not depend on the timing at all is the arithmetic
(`lexShadowSlot("precedes") == lexShadowSlot("*") == 61`) and the code path
at Interpreter.cpp:22595, and both say the same thing.

(`==` does move as well, ~29%, but only once a heap is actually
instantiated, since `cmp` is armed by `MIXIN` rather than at load — this
table only does `use BinaryHeap`.) Other colliding pairs the same hash
produces: `eqv`/`gcd` (56), `>=`/`-` (47), `x`/`===` (17). **This is a
general fault** — any program binding an anonymous `&infix:<…>` silently
slows an unrelated operator across its whole run — and it should be fixed
whatever happens to Graph.

**2. Element binding is our most expensive primitive.** `!sift-down` is
968 ms exclusive of the 1922 ms run, 37,872 calls, and it is built out of
`my $node := @!array[$pos]`, `@!path.BIND-POS(…)` and `my \left =
@!array[$pos]`. Against Rakudo, 1M iterations minus the empty loop:
`elem_bind` 7.6x, `bindpos` 9.6x, `sigilless_bind` 8.9x, attribute read
4.1x. `makeArraySlotProxy`
([Interpreter.cpp:15933](../../../src/Interpreter.cpp)) allocates a
`ValueHash` and four string-keyed entries per binding; because `ValueHash`
stores entries in a `std::deque<Entry>` with `sizeof(Entry) == 168`, the
first insert mallocs a 4032-byte block to hold four entries. See
DISPATCH-PERF-PLAN.md phase 6 — noting that its "a Hash plus two Callables
plus two `std::function`s, about seven allocations" text is **stale**: the
FETCH/STORE pair are function-local statics now, so it is four allocations,
one of them oversized.

## The twist: upstream retired it before the issue was filed

| when | what |
|---|---|
| 2026-08-27 17:30 | antononcube opens [dumarchie/raku-binaryheap#1](https://github.com/dumarchie/raku-binaryheap/issues/1) — a PR removing `Parameterizable` |
| 2026-08-28 16:53 | first commit of `antononcube/Raku-LeftistHeap` |
| 2026-08-29 20:39 | PR closed: *"I no longer need the merging of this PR. ('Graph' was refactored.)"* |
| 2026-08-30 13:41 | rakupp issue #47 filed, measuring **Graph 0.1.2** |

His reason for the PR, in his own words:

> With the new, faster implementations of Raku on Raku subsets of
> functionalities (Rakupp, raptor) "BinaryHeap" does not compile (Rakupp) or
> it is "out of scope" (raptor). Hence, I am interested in simpler
> implementations that use more standard or common features, which are very
> well documented and easy to implement.

Graph **0.1.3** is published on zef and depends on `LeftistHeap:ver<0.0.2+>`
instead. `LeftistHeap` is plain documented Raku — an immutable `HeapNode`
class, `submethod TWEAK`, a `Callable $.comparator` invoked as
`$!comparator($a, $b)`. **No `Parameterizable`, no `^parameterize`, no
`BIND-POS`, and no anonymous `&infix:<…>` binding**, so neither of the two
faults above arms at all.

`Graph::Grid.new(12,12).diameter`, interleaved, best of three, rakupp at
HEAD (2026-08-31):

| Graph | rakupp | Rakudo | ratio |
|---|---|---|---|
| 0.1.2 (BinaryHeap) | 1.978 | 0.626 | 3.16x |
| 0.1.3 (LeftistHeap) | 4.488 | 2.842 | **1.58x** |

The rakupp column is reproducible to ~2%; the Rakudo column drifted ~8%
between passes on a busy box, so read the ratios as "about 3x" and "about
1.6x", not to three digits. (Two earlier readings were discarded: one on a
box at load 10, which reported the two engines as equal and was simply
wrong, and one taken against a binary carrying uncommitted experimental
patches. Re-measured from a clean build of HEAD; rakupp's own times moved
under 2.5%, so the patches were not what those numbers were showing.)

Two things at once, and both belong in any reply to #47. Our **ratio**
improves, 3.03x to 1.71x, because the two faults above stop arming. The
**absolute** time gets worse for everyone — 2.3x for rakupp, 4.1x for
Rakudo — because a persistent leftist heap allocating a `HeapNode` per
insert is simply a costlier structure here than an array-backed binary heap.
That second half is the module's own trade and not ours to fix, but it does
mean "upgrade Graph" is not a speed fix we can offer a user.

The practical consequence: a fresh `rakupp install Graph` today gets 0.1.3.
Issue #47's Graph figure measures a code path its own author has retired,
so the number to re-measure and quote is the 0.1.3 one.

## BinaryHeap 2.0 (October 2026)

The module came back. Between 2026-10-07 and 2026-10-19 its author rewrote
it (0.0.8 → 2.0.3): `Parameterizable` is gone, the heap is a role
`GenHeap[&infix:<tops>]` with `MaxHeap`/`MinHeap` composing it over
`* cmp * == More` / `* cmp * == Less`, and 2.0.3's changelog says "Make
non-author tests pass on Raku++". On 2026-10-20 the author wrote in the
closed PR ([dumarchie/raku-binaryheap#1](https://github.com/dumarchie/raku-binaryheap/pull/1#issuecomment-6285157354))
that 2.0.3 runs on Raku++ 5.4.1 and that its benchmarks are 30–40 times
slower there than on Rakudo Star 2026.09.

Measured here at HEAD (5.4.1-147) against Rakudo 2026.09, the module's own
`benchmark/` scripts, median of five interleaved runs, ms:

| benchmark | rakupp before | rakupp after | Rakudo | after / Rakudo |
|---|---|---|---|---|
| binaryheap-extract | 5449.8 | 5337.4 | 516.3 | 10.3x |
| binaryheap-pop | 5595.9 | 5136.7 | 442.2 | 11.6x |
| binaryheap-push | 6042.5 | 5529.1 | 490.4 | 11.3x |
| heapsort | 5995.2 | 5663.9 | 494.6 | 11.5x |
| heapify | 4701.1 | 4404.7 | 471.9 | 9.3x |

"After" is faster in 23 of the 25 pairs. The 30–40x was measured on the
5.4.1 release on the author's machine; this box has no 5.4.1 binary, so that
figure is not reproduced or refuted here, only the ~10x at HEAD.

### Fault 1, fixed: the filter, again

`tops` lands in `>`'s slot (53) the way `precedes` landed in `*`'s, and a
second fault sat beside it: evalBinary's Unicode-twin test (`×` reaches the
evaluator as `*`) admitted `*` `-` `/` `>=` `<=` `!=` whenever **any** bit
was armed, not the twin's own. So one `&infix:<tops>` binding anywhere sent
every `>`, `*` and `-` in the program around the fast shape. A plain integer
loop ran 23.8 ms alone and 93–96 ms with `role R[&infix:<tops>] { }`
declared next to it.

Fix: `lexShadowArm` records each armed spelling (64 of them, then every hit
counts as real again), a filter hit is confirmed against that list, and the
twin is tested under its own spelling (`lexShadowPossibleOrTwin`). Armed and
unarmed loops now time the same. The collision also produced a wrong
answer: with `*`'s slot armed, `lexInfixLookup` looked for `&infix:<*>`,
found nothing, and never tried the twin, so a `my sub infix:<×>` in scope
answered the built-in product. The twin is now tried whenever the ASCII
spelling finds nothing in scope.
[t/regression/lex-shadow-filter-exact.raku](../../../t/regression/lex-shadow-filter-exact.raku)
holds both, and it fails on the old binary in exactly those two checks (cost
ratio 4.14, `2 × 3` answering 6).

That is worth 2–8% here. The remaining ~10x is somewhere else.

### What the remaining ~10x is

200,000 iterations, ms, same binaries:

| operation | rakupp | Rakudo |
|---|---|---|
| `$x = @a[3]` (element read) | 21.7 | 66.3 |
| `my \l = @a[3]` | 124.9 | 10.0 |
| `my $n := @a[3]` | 130.1 | 9.6 |
| `@p.BIND-POS(3, @a[5])` | 126.1 | 18.8 |
| `my \c = @a[3]; $node = c; $node := c` | 265.2 | 12.7 |
| `$x++ if (@a[3] cmp @a[5]) == More` | 96.9 | 30.1 |
| `tops(@a[3], @a[5])`, `&tops = * cmp * == More` | 114.1 | 33.6 |

Reading an element is fast. Binding one is 10–20x Rakudo's cost, and
`!sift-down` and `!insert` bind three or four times per heap level. That is
fault 2 above, unchanged: every bind builds a `Proxy` hash (an array slot
proxy has six string-keyed entries, so its ChunkList grows past the first
chunk and rehashes), the cycle collector walks it when it drops, and each
read through it does four hash lookups. Sampling `my \l = @a[3]` puts ~40%
of the bind in that hash's construction, lookups and free. A cut to three
entries is possible inside InterpreterCalls.cpp, but it would hide the
array edge from the cycle collector the way the shared-cell proxy already
hides its cell. A real fix is an array element that holds a cell (as an
Env slot already can, `Value::cellHolder`) instead of a hash.

The `Order` path is the second cost, smaller here. `cmp` builds an `Order`,
`== Less` takes applyArithGeneral's string-compared operator chain, and the
bare term `Less` re-runs `coreEnumValue` and a scope walk on every
evaluation. Swapping BinaryHeap's comparator for `* < *` moved its time by
only ~2%, so for this module it is secondary, but any `sort { $^a cmp $^b
== … }`-shaped code pays it.

Under both, `_tlv_get_addr` was ~13% of every profile taken: `tctx_` and the
other `thread_local`s go through Darwin's TLV thunk on each access. That is
general, not BinaryHeap's. *Since 2026-10-20* the per-thread context is
reached through a constant-initialized pointer, and the cost was mostly the
init guard each mention of the old `tctx_` paid beside the variable, and the
wrapper call around both from other files
([INTERP-SPEED-PLAN.md](../plans/INTERP-SPEED-PLAN.md), task 4). On a heap
pop the share is 10.1% → 4.8% of busy samples, and the five benchmarks
retire 3.1–4.3% fewer instructions (5.1% fewer cycles, geomean).

For scale, the same heap written with plain element assignment (no binds,
`(@!a[$c] cmp $v) == Less`, 2^16 pops) is 2232.8 ms here and 684.9 ms on
Rakudo: still 3.3x. With `<` in place of `cmp … == Less` it is 1678 ms
against 650, so the `Order` path is about a quarter of rakupp's time there,
and the 2.6x left is ordinary method-call and attribute-array cost on a
shape Rakudo's specializer handles well.

## Policy

- **Keep the meta-method support.** It is generic, it is correct, it costs
  0.2%, and removing it would break two published dists for nothing.
- **Fix the two engine faults on their own merits.** The Bloom collision is
  a correctness-of-optimisation bug with an ecosystem-wide blast radius; the
  element-binding proxy is the plan's phase 6. Neither is contingent on
  BinaryHeap.
- **Do not tune for BinaryHeap specifically.** One dependent, and that
  dependent has moved on. Measure Graph 0.1.3 — and note that the 1.71x
  left there is a different problem from the 3.03x: no `BIND-POS`, no
  shadow filter, just object allocation and `Callable` invocation, which
  points at the same call-and-allocate costs DISPATCH-PERF-PLAN.md phases
  1–2 are about.
- **Do not report any of this upstream** — the same rule as
  MODULE-FINDINGS.md. Anton reached his own conclusion independently and
  acted on it; we do not need to relitigate a closed PR in someone else's
  repository. (2026-10-20: the module's author raised rakupp's speed in that
  PR directly, so a factual reply there is no longer unprompted. Whether to
  write one is the maintainer's call, not this page's.)

## See also

- [RAKUDO-INTERNALS.md](RAKUDO-INTERNALS.md) — the same pattern at
  ecosystem scale: nine top-200 dists, zef among them, on
  `Rakudo::Internals`. BinaryHeap is the *narrow* case of the same problem —
  a missing documented feature, routed around through the MOP.
- [MODULE-FINDINGS.md](MODULE-FINDINGS.md) — the `^parameterize` entry and
  the smiley-enforcement finding behind it.
- `docs/dev/plans/DISPATCH-PERF-PLAN.md` — phases 4 and 6, and the
  measurement frame this page's numbers come from.
