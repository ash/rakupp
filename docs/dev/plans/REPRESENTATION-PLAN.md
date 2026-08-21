# Plan: swap the JSON ratio — the result tree, not the tree-walker

*Written 2026-08-09, before any code. Goal set by the user: JSON::Fast's
`from-json` is ~13× faster under Rakudo than under rakupp, and the ratio should
end up the other way round.*

**The number a stranger can re-measure:**

```bash
L=/Users/ash/raku-module-battery/dists/JSON--Fast-0.19/lib
tools/bench/diagnose/json-parse.raku --reps=5 d800.json     # under both engines
```

rakupp parses `d800.json` (278 KB) **faster than Rakudo**, having started
13.1× slower. Target ≥ 10× faster; ≥ 3× is the point at which the campaign is
worth having shipped.

Measurements below: 2026-08-09, `build-arm64/rakupp` at v3.0.1 + the two
uncommitted probes, Apple clang 17, macOS 15 (Darwin 24.6.0), arm64, quiet
machine. Probes: [`tools/json-native-probe.cpp`](../../../tools/json-native-probe.cpp),
[`tools/value-build-probe.cpp`](../../../tools/value-build-probe.cpp).

---

## Where we are — measured

| doc | chars | rakupp | Rakudo | ratio |
|---|---:|---:|---:|---:|
| d200 | 68,990 | 117 ms | 8 ms | 14.6× |
| d400 | 138,495 | 234 ms | 17 ms | 13.8× |
| d800 | 277,872 | 470 ms | 36 ms | 13.1× |
| d1600 | 558,322 | 945 ms | 71 ms | 13.3× |

Both engines are ×2 per doubling. The quadratics
[STRING-SCAN-QUADRATICS.md](../findings/STRING-SCAN-QUADRATICS.md) closed are
still closed; what remains is a flat constant factor, which is what §6 of that
file predicted.

## The finding that reframes the campaign

The obvious plan — make the tree-walker faster — was tested first, and it does
not reach the goal. The obvious *second* plan, a native `from-json` in C++, was
then priced directly rather than assumed, and it does not reach the goal either
for a reason nobody had written down:

`tools/json-native-probe.cpp` parses `d800.json` with a hand-written C++ parser
building the same `Value` tree the interpreter would return:

| | ms |
|---|---:|
| scan the document, build nothing | **0.57** |
| scan **and build the result** | **12.14** |
| ↳ the structure build alone | 11.58 (95%) |

**95% of a native JSON parser's time is constructing the result**, at 600 ns per
node over 19,201 nodes. 12.14 ms against Rakudo's 36 ms is only 3.0× — a native
parser written today would not swap the ratio, and the parser is not why.

So the bottleneck is neither the tokenizer nor the AST walk. It is **how rakupp
represents a Raku value**. `tools/value-build-probe.cpp` splits the 600 ns over
12,000 key/value pairs:

| shape | ns/entry |
|---|---:|
| A. `std::map<std::string, Value>` — today's Hash | 302 |
| B. `unordered_map<std::string, Value>` | 148 |
| C. `std::map<std::string, 24-byte value>` | 165 |
| D. both changes | **59** |
| E. `vector<Value>` — today's Array | 35 |
| F. `vector<24-byte value>` | **2** |

- the container alone (`std::map` → hashed): **2.04×**
- `sizeof(Value)` alone (392 → 24 bytes): **1.83×**
- both: **5.09×** — and **17×** on the Array path

`sizeof(Value)` is 392 bytes: 4 `std::string`s and 11 `shared_ptr`s, all
constructed, copied and destroyed for a value that is an `Int`. `Hash` is a
red-black tree with a malloc per entry.

### What that predicts

Native parse with both representation changes: 0.57 + 11.58/5.09 ≈ **2.8 ms**,
against Rakudo's 36 ms — **~12.7× faster**, from 13.1× slower. That is the
ratio swapped, and every step of the arithmetic above is measured rather than
assumed.

*Re-measured after phase 1 batch 1, which banked part of the size win: the
structure build is 265 ns/entry and the two remaining changes are worth 4.34×
together, so the projection is 0.69 + 11.26/4.34 ≈ **3.3 ms**, ~11× faster than
Rakudo. The prediction survives the first batch.*

### The same two causes dominate the interpreted path

A `RelWithDebInfo` `sample` of the d800 parse, self time by source line:

| | % |
|---|---:|
| `Value.h:260` — `Value`'s implicit ctor/dtor/copy | 12.8 |
| malloc/free family | ~12 |
| string compare (`memcmp` + `strlen` + `string == char const*`) | ~10 |
| `vector.h:1151` — `ValueList` | 5.2 |
| thread-local access (`_tlv_get_addr`, `__tls_init`, `tctx_`) | ~3.4 |

Slimming `Value` attacks the first, second and fourth directly. This is the
same conclusion [PERF-CAMPAIGN.md](../experiments/PERF-CAMPAIGN.md) reached
from the profiling end and ranked #2 ("the 31%"), never actioned.

## What this is NOT

[IR-EXPERIMENT.md](../experiments/IR-EXPERIMENT.md) measured a bytecode/register
IR and rejected it: opcode dispatch is worth 0.28 ns/node while the escape-hatch
store costs 11.2 ns/node, so ~42% of nodes must be lowered before it breaks
even. **Nothing in this plan revives that**, and the measurements here reinforce
it — at 600 ns to build one result node, flattening the tree that produced it is
not where the time is.

That file names one thing that *would* revive an IR: unboxed typed registers.
That is phase 4 here, last and optional, and only because JSON::Fast's inner
loops are `int`-only. It is not on the path to the goal.

## The phases

Ordered so that each is independently shippable and each earns its keep on
workloads other than JSON. Phases 1 and 2 are the campaign; 3–5 are what turn
a large win into a swapped ratio.

### Phase 1 — slim `Value` (392 → ≤ 64 bytes)

#### Batch 1 — the interned tag fields (landed 2026-08-09)

`Value` carried four `std::string` members that are empty on almost every
value. `hashKind`, `enumName` and `enumType` are drawn from a **closed
vocabulary** — container kinds and type names — so they became `IStr`
([src/IStr.h](../../../src/IStr.h)): an 8-byte interned handle, trivially
copyable and trivially destructible, with the same operator surface `MName`
already uses for method names.

That surface is the point. ~1,300 call sites read `.hashKind == "Buf"` or
`.ofType.empty()`, and **eight** needed editing: seven ternaries mixing an
`IStr` with a string literal, where C++ has no common type to pick, and one
`CowStr == IStr` ambiguity. The compiler found every one; there is no silent
failure mode in this change.

A/B against a `git worktree` build of HEAD, both binaries on the same idle
machine in the same session — the only comparison this file's own rules allow:

| | base | now | |
|---|---:|---:|---:|
| `sizeof(Value)` | 392 | **344** | |
| non-trivial members | 15 | **12** | |
| `json-parse` d200 | 115 ms | **110 ms** | −4.3% |
| `json-parse` d400 | 232 ms | **221 ms** | −4.7% |
| `json-parse` d800 | 468 ms | **441 ms** | −5.8% |
| `json-parse` d1600 | 934 ms | **888 ms** | −4.9% |
| Hash build (12k entries) | 302 ns | **265 ns** | −12% |

Still ×2 per doubling, so nothing about the scaling shape changed. Against
Rakudo the d800 ratio goes 13.1× → 12.2×.

Gates: local suite 398/398; `perf-guard --check` green on all seven kernels
(fib −14.1%, asg −12.9%, hash −9.5%, strscan −8.2% against the v1.5.1
baseline); Roast **197,025 / 593 files** against the baseline build's
**197,069 / 593**, with all seven per-file differences being timeout flips on
concurrency and I/O files (+1 subst, +20 cas, +4 scalar-assign, −28
atomic-ops, −29 move, −5 channel, −7 permutations = −44).

A first run of this batch reported −591, which was **one spurious timeout**:
`S03-operators/set_addition.t` is a 613-assertion file that passes in under a
second on the same binary. Worth recording, because a single flaky timeout in a
large file moves the headline number by ten times the real noise band and looks
exactly like a regression. Read the per-file diff, not the total.

The constructors are deliberately `explicit`: implicit ones make `k == "Buf"`
ambiguous, because the literal converts to `IStr` as readily as `IStr` converts
to `std::string`.

**`ofType` is deliberately NOT interned, and must not be** until one site
moves. An `IO::Path` stores its `:CWD` there, because a path value has no other
use for the field (Builtins.cpp:4242) — so `ofType` can hold a runtime
*directory name*, not a type name. The intern table is append-only by design, so
a program walking many directories would add an entry per directory and never
release it. Moving `:CWD` somewhere else unblocks the last 16 bytes of this
batch.

**Open, to measure before it matters:** interning takes a `shared_mutex` on
assignment from text (never on copy or comparison). Under parallel-by-default,
a hot `hashKind = "Buf"` on many threads would contend on the reader count.
Single-threaded kernels all improved, so this is a question for
`tools/bench/parallel`, not a known problem.

The 11 `shared_ptr`s and 4 `std::string`s are empty for almost every value. Move
the rare ones behind a single `shared_ptr<ValueExt>`, keeping tag + inline
scalar + `CowStr` + one pointer. Every field stays *reachable*; only the
storage moves.

- Worth **1.83×** on structure building, and it is the top line of the
  interpreted profile.
- Blast radius is wide but mechanical: field access becomes an accessor. The
  risk is that a hot path acquires an indirection it did not have — so the
  gate is `perf-guard --check` on every batch, not at the end.
- `natBits`/`natSigned`/`natFloat`/`readonly`/`itemized`/`isList` are hot flags
  and stay inline; `enumName`/`enumType`/`ofType`/`hashKind`/`shape`/`ratN`/
  `ratD`/`big`/`ext`/`pairKey`/`pairVal` are the candidates to move.

#### Batch 2 — the pointer union, MEASURED AND ABANDONED AS DESIGNED (2026-08-09)

The plan for batch 2 was: `Value`'s eleven `shared_ptr`s are mutually exclusive
by type tag — a value cannot be both an Array and a Hash — so they collapse into
two tag-dispatched slots, worth 144 bytes and nine non-trivial members.

**That premise is false, and a census says so.** A `-DRAKUPP_PTR_CENSUS` build
(the destructor records which pointers are live; compiled out of every normal
build) over the local suite and a feature-targeted Roast slice — 30M `Value`
destructions:

| live pointers | count |
|---|---:|
| (none) | 25,583,692 |
| `arr` | 4,268,636 |
| `big` | 539,483 |
| `ratN ratD` | 466,950 |
| `obj` | 181,374 |
| `code` | 67,662 |
| `hash` | 45,444 |
| `pairVal` | 40,354 |
| `ext` | 30,924 |
| **`arr hash`** | 16,803 |
| **`arr hash pairVal`** | 7,090 |
| **`arr hash ext`** | 2,373 |
| `pairVal pairKey` | 1,496 |
| `hash ext` | 522 |
| `pairKey` | 289 |
| `arr ext` | 117 |
| `arr shape` | 24 |
| **`arr hash pairVal ext`** | 4 |

**Four pointers are live at once**, not two: `arr`, `hash`, `pairVal` and `ext`
co-occur, because a Capture and a Match legitimately carry positionals in `arr`
and nameds in `hash` at the same time. Two tagged slots cannot express that, and
a build that assumed they could would have corrupted Captures in ways the type
tag would never reveal.

What the census DOES license, because these combinations never occur:

- `big` is always alone;
- `ratN`/`ratD` occur only with each other;
- `obj` and `code` are always alone;
- `shape` only with `arr`; `pairKey` only with `pairVal`.

So the revised batch 2 is not a union but a **lazily-allocated cold block**:
move `big`, `ratN`, `ratD`, `fatRat`, `shape`, `pairKey`, `ext`, `im`, the four
range fields and `ofType` into one `shared_ptr<ValueExt>`, keeping `arr`,
`hash`, `code`, `obj`, `pairVal` and the `CowStr` inline. ~148 bytes leave and
16 come back: **`sizeof(Value)` 344 → ~204, and 12 non-trivial members → 6.**

Two things make that better than it first looks. The `BigInt`s go in **by
value**, so a `Rat` costs *one* allocation instead of today's two — 466,950 of
those in the census. And `ofType` rides along, which is where the `:CWD` problem
from batch 1 goes away without interning it.

It is also ~800 call sites and NOT source-compatible: `v.big` becomes an
accessor with a read path that must not allocate and a write path that must.
That is a batch of its own.

**Sequencing note.** Phase 2 below is worth *more* than this (1.99× against
1.64×), is ~1,550 sites, and IS source-compatible if the replacement exposes
`std::map`'s API. On the measurements, phase 2 should come first.

**LANDED (2026-08-22), as the revised cold-block design.** `ValueExt` holds
the census-licensed fields — `big`, `ratN`, `ratD`, `fatRat`, `shape`,
`pairKey`, `ext`, `im`, the five range fields and `ofType` — behind one
`shared_ptr<ValueExt> x_` that stays null on almost every Value.
**`sizeof(Value)` 344 → 200** (`ValueExt` itself is 152, allocated only when
one of its fields is set). The block is copy-on-write: a Value copy shares it
(one shared_ptr where ~148 inline bytes used to copy), and every write goes
through `xw()`, which clones a block another Value can still see —
`use_count` is an atomic read, and the write-after-copy clone is the rare
case by the census. Reads (`v.big()`) go through `xr()` and never allocate;
writes are the `M`-suffixed accessors (`v.bigM() = …`). The one deviation
from the sketch above: the BigInts stayed `shared_ptr` INSIDE the block
rather than by-value — it keeps the 128 `if (v.big())` truthiness sites'
semantics, and the one-allocation-Rat can still be taken later inside the
block without touching call sites again.

The conversion was compiler-driven, as batch 1's surface trick promised:
every site was mechanically rewritten to the read accessor, then const-ness
errors enumerated the ~156 write sites and "no call operator" errors
enumerated the AST-node fields (`IntLit::big`, `NameTerm::ofType`) that
share the names and had to stay fields. One genuinely silent surface
existed — Codegen EMITS `.rFrom`/`.ofType` inside string literals for the
`--exe` range-loop lowering, which no compiler pass of rakupp itself checks —
caught by the t/ suite's native-binary goldens.

Measured, same idle machine (Darwin 25.5), A/B against the same-day
pre-change build, release binaries, best-of-6:

| | pre | post | |
|---|---:|---:|---:|
| perf-guard, all 7 kernels | — | — | −6% … −13% |
| bench `sortnums` | 65.3 ms | 48.5 ms | −26% |
| bench `arrayops` | 123.6 ms | 98.9 ms | −20% |
| bench `hashfill` | 241.6 ms | 204.3 ms | −15% |
| bench `hash` | 43.0 ms | 37.6 ms | −13% |
| bench `streq` | 626.7 ms | 546.9 ms | −13% |
| remaining 5 kernels | — | — | −3% … −11% |
| JSON::Fast `from-json` (347 KB, interpreted) | 700 ms | 643 ms | −8% |
| grammar capturing parse (51 KB) | 66.7 ms/parse | 56.6 ms | −15% (nocap flat) |
| `hashfill` peak RSS | 244.9 MB | 148.8 MB | −39% |

Gates: t/run.raku 499/499 including every `--exe`/`--slim` golden; full Roast
per-file PASS diff vs a same-day pristine-build baseline **empty in both
directions** (634 files); parmap correct under GIL and `RAKUPP_PARALLEL=1`
with no ThreadSanitizer reports through the new CoW accessors; the
`-DRAKUPP_PTR_CENSUS` build still compiles (ptrMask reads through the block).

#### Batch 3 — grammar-parse result trees: FlatMap children + memo reaper (2026-08-13)

Sibling of this campaign on the GRAMMAR path, driven by the
`tools/bench/diagnose/grammar-split.raku` decomposition (51 KB JSON, LTM=0):
a capturing grammar parse spent ~47% matching, **~28% in `~GrammarMatcher`
destroying the packrat memo**, ~17% converting ParseNode→Match, ~8% in
`setMatchVar` destroying the previous `$/`. Two changes:

- **`FlatMap`** (Regex.h): the parse tree's child/named maps
  (`ChildMap`, `GrammarHooks::NamedMap`, `MState.children/named`,
  `MemoEntry.named`, `RxMatch.children/named`) moved from `std::map` to a
  sorted flat vector exposing the same API subset. Iteration order is
  unchanged (key-sorted). One signature edit in Regex.cpp; everything else
  recompiled untouched. Profiling then showed teardown was NOT map-node
  frees but the destructor WALK itself (alternating shared_ptr release →
  `~ParseNode` recursion with 1-2-sample free leaves), so this alone did
  not move the teardown share — kept for the allocation-count reduction on
  the build/snapshot side and as the campaign's container direction.
- **The memo reaper** (`GrammarMatcher::reapMemo`, Regex.cpp): the memo's
  frozen subtrees are refcounted and self-contained (the winning parse
  holds its own refs; shared_ptr counts are atomic), so a large memo
  (≥512 entries) is moved into a box and destroyed on a detached thread —
  off the parse's critical path. Small memos, or ≥4 reapers already in
  flight, destroy inline: graceful degradation to the old behavior, never
  unbounded RAM.

Measured effect. Sample-profile shape (load-independent): main-thread
share went 47/28/17/8 (match/teardown/convert/setMatchVar) →
**76/~0/15.5/8** — the teardown walk runs on reaper threads now. Wall
clock, quiet-machine interleaved A/B vs the same-day pre-batch binary
(RAKUPP_LTM=0 so both parse identically), four alternating rounds:
pre-batch 53.6-57.2 ms/parse, this build 50.3-52.8 — **−6%, the new
build faster in every pair**. The gap between −6% and the removed 28%
is the reaper's frees contending with the next parse's allocations in
the same malloc zone (plus page fault-back after madvise) — the arena
REUSE design (keep pages, skip madvise) is the follow-up that would
close it. `setMatchVar`'s 8% was deliberately NOT deferred: interpreter
`Value` destructors are not provably side-effect-free (handles), unlike
ParseNode trees.

Adjacent observation while measuring, no action needed: the default NFA
ranker vs RAKUPP_LTM=0 on this bench is +18%/+12%/−4% at 100/400/1600
records — both scale linearly and the sign FLIPS on large inputs (the
NFA's per-alternation scan amortizes; the probe's double-descent grows),
so the mid-size gap is workload balance, not a scaling bug.

Gates: suite 442/442, LTM regression file 18/18 both engines, oracle grid
11×3 Rakudo-identical, **perf-guard --check GREEN** on the quiet machine
(worst kernel subcall +3.6%, rest ≤+0.1%), full Roast **196,950/217,940
declared, 15 TIME** vs the same-day pre-batch run's 196,803/16 TIME —
per-file diff shows only documented flappers (lines.t 6↔4,
advent2012-day13 23↔22, three load-band TIME flips), zero new failures,
S05 untouched. Remaining follow-up: one TSan stress leg over the reaper
(it shares no mutable state — a boxed map + one atomic — but the suite
should say so; the linux-tsan CI job will on next push).

### Phase 2 — MEASURED AND REVERTED, and what it found instead (2026-08-09)

Phase 2 was written and it works: `RakuHash` — `unordered_map` storage with a
lazily-built sorted view, so `gist()`'s key order survives — landed behind the
same source-compatible surface trick as batch 1. One compile error across ~1,550
sites. It is **not** shipped, because the number it was adopted on was measured
on the wrong shape.

**The probe built ONE map of 12,000 entries. A document is ~800 hashes of 15.**

| shape | `std::map` | `unordered_map` | |
|---|---:|---:|---:|
| one hash of 12,000 | 215 ns/entry | 133 ns/entry | **1.81×** |
| 800 hashes of 15 | **76 ns/entry** | 98 ns/entry | **0.78×** |

A hash table amortises its bucket array over many entries; at 15 entries there
is nothing to amortise, while a tree's per-node malloc is the same either way.
`RakuHash` measured 0.80× on the real shape and 1.62× on the big one — a trade,
not a win, and a regression on the workload this campaign exists for. Reverted.

A small-hash design (a sorted vector of nodes) would beat both, but the nodes
must stay individually heap-allocated: `lvalue()` hands out `Value*` into a hash
and the interpreter writes through it, so anything that relocates elements on
growth turns a held pointer into a dangling write. That constraint is why
`unordered_map` was chosen over open addressing in the first place, and it caps
what a small-hash rewrite can win.

**What the shape error uncovered.** Once the container was ruled out, the
remaining structure-build time had to be somewhere, so it was decomposed:

| | before | after |
|---|---:|---:|
| `BigInt::gcd` (values fitting in 64 bits) | 15,761 ns | **183 ns** |
| `BigInt::divmod` (same) | 2,071 ns | **76 ns** |
| `Value::rat(1/7)` | 9,239 ns | **484 ns** |

`divmod` did a **binary search over [0, 10⁹) per limb**, each step a full BigInt
multiplication — about thirty of them to divide a one-limb number. `gcd` is
Euclid over `divmod`, and `Value::rat()` calls `gcd` plus two `divmod`s on
**every Rat it constructs** — which is every decimal literal and every `p/q` in
Raku. Both now take a 64-bit fast path, which is the case almost all real
arithmetic is in.

That, and not the container or `sizeof(Value)`, was the JSON structure build:

| | before | after |
|---|---:|---:|
| native parse of d800 | 11.95 ms | **2.91 ms** |
| ↳ structure build | 11.34 ms | 2.31 ms |
| throughput | 23.3 MB/s | **95.4 MB/s** |
| Rat-heavy loop, sum | 717 ms | **72 ms** |
| Rat-heavy loop, construct | 669 ms | **82 ms** |

**2.91 ms against Rakudo's 36 ms is 12.4× — the ratio, swapped**, on the native
path. The plan predicted 2.8 ms and got 2.91; it had the number about right and
the *cause* entirely wrong, which is worth more than being right for the wrong
reason would have been.

### Phase 2 (original text) — `Hash` stops being a red-black tree

Replace `std::map<std::string, Value>` with an open-addressing hash map exposing
the same API (`operator[]`, `find`, `begin`/`end`, `erase`, `count`, `size`), so
the ~1,550 `->hash` sites compile unchanged.

- Worth **2.04×** on structure building.
- **The risk that decides the design:** `std::map` iterates in *sorted key
  order*, and rakupp's `.gist`/`.raku` output, the spec-site oracle and an
  unknown number of Roast expectations may depend on that determinism. Rakudo's
  order is pseudo-random, so matching Rakudo is not the constraint —
  *not changing our own output* is. Mitigation: keep iteration sorted by sorting
  at iteration time (O(k log k) when iterated, which this workload does rarely,
  against O(1) inserts which it does constantly). Measure both; do not assume
  the sort is free for hash-iteration-heavy programs.

### Phase 3 — the per-call allocations and the TLS lookups

Already attributed by IR-EXPERIMENT §5 (`make_shared<Env>` 103 ns/call,
`ValueList` 51 ns/call) and visible in this profile. Plus two this workload
surfaced:

- **`is rw` still costs ~430 ns/call**, measured by `call-cost.raku`, despite
  STRING-SCAN-QUADRATICS §6 fixing the binder's fast-path exclusion. The
  remaining cost is `setupRwLinks`/`copyOutRw`: an `EnvExtras` malloc plus two
  `std::map` node allocations with string keys per rw parameter, then a
  `valueEqv` and teardown per call. JSON::Fast makes ~30,600 such calls per
  `d200` parse — ~13 ms of 117 ms. This is a bug-shaped cost, not architecture.
- **~3.4% in thread-local access.** `tctx_` is a `thread_local` reached through
  `_tlv_get_addr` with an `__tls_init` guard on every access on macOS/arm64.
  Hoisting it into a local across hot regions is a small, local change.

### Phase 4 — unboxed typed lowering (optional, measure first)

The one thing IR-EXPERIMENT names as reviving an IR. JSON::Fast's inner loops
are `int` locals and `nqp::*_i` ops end to end, so a routine whose locals are
provably native could run without materialising a `Value` at all. **Do not start
this before phases 1–3 are measured**, because slimming `Value` changes the
premise it rests on.

### Phase 5 — native `from-json`/`to-json`

rakupp already answers `use Test` and `use NativeCall` natively
(`isPragmaName`, Interpreter.cpp:3487). Extending that to `JSON::Fast` makes
`from-json` the 2.8 ms C++ parser above instead of interpreted Raku.

**State plainly what this does and does not prove.** It swaps the ratio on this
benchmark by not running the benchmark's code. It is legitimate — rakupp
implements the whole Raku setting in C++, and JSON is a data format rather than
Raku semantics — but it is a different achievement from making the interpreter
faster, and the docs must not blur them. The interpreted path must stay correct
and keep improving on phases 1–3; `RAKUPP_NATIVE_MODULES=0` must run the real
Raku source, and the gate below depends on it.

## Gates

Standing gates on every batch, per [RELEASING.md](../RELEASING.md): zero Roast
regressions, the local suite, `perf-guard --check`. Plus, for this campaign:

- **JSON::Fast's own test suite passes**, under native and interpreted paths
  both — the v1.5.2 standard, and the only thing that makes phase 5 safe.
- The module battery does not regress. Phases 1 and 2 change representation
  under every module; §4a of STRING-SCAN-QUADRATICS is the standing warning
  that a performance change is a concurrency change and the battery catches
  what unit suites do not.
- `json-parse.raku` on the **full size ladder** each batch, both engines,
  reading the scaling column and not the improvement factor.
- Phase 2 additionally: a byte-identical `.gist`/`.raku` diff over the examples
  and the spec-site corpus, because sorted iteration is an output contract.

## What would falsify this plan

- If phase 1 lands and structure building does not move ~1.8×, the probe's
  24-byte stand-in was not representative of an achievable `Value` and the
  remaining phases need re-pricing.
- If sorted-on-iteration costs more than hashing saves on a
  hash-iteration-heavy program, phase 2 needs an insertion-ordered design
  instead, at more memory per entry.
- If phases 1–3 leave the interpreted parse above ~150 ms, phase 5 is carrying
  the whole result and that should be said in the release notes rather than
  implied away.
