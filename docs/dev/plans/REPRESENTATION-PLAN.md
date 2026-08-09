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

### Phase 2 — `Hash` stops being a red-black tree

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
