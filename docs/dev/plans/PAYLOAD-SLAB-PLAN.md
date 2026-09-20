# Plan: the allocator line, not the copy line

*Written 2026-09-20, before any engine change. Priced with
[tools/payload-slab-probe.cpp](../../../tools/payload-slab-probe.cpp).*

[VALUE32-PLAN.md](VALUE32-PLAN.md) profiled an array/hash-heavy 1.8 s workload
and the top line was not `Value`:

    malloc family 21.5%   std::vector<Value> members 7.6%
    Value copy/move 3.7%  thread-local access 6.1%

The representation campaign is about the 3.7%. This file prices the 21.5%.
Every call into the allocator comes from a **payload** — `make_shared<ValueList>`
(24 sites), `<ObjectData>` (47), `<ValueExt>`, `<MatchData>`, `<ValueHash>`,
`<StrBody>`. All fixed-size, all high-churn: the textbook slab shape.

**The number a stranger can re-measure:**

```bash
cmake -S . -B build-probe -DCMAKE_BUILD_TYPE=Release
cmake --build build-probe --target rakupp_rt rakupp_parse rakupp_ucd_names \
      rakupp_ucd_coll rakupp_ucd_props rakupp_stubs -j 8
c++ -std=c++20 -O2 -DNDEBUG -Isrc -Iinclude tools/payload-slab-probe.cpp \
    build-probe/librakupp_{rt,parse,ucd_names,ucd_coll,ucd_props,stubs}.a \
    -o /tmp/payload-slab-probe && /tmp/payload-slab-probe
```

Measurements below: 2026-09-20, Darwin 25.5 / arm64, Apple clang, `Value` at
128 bytes. Two consecutive runs agree to within 0.1x on every ratio except
section E's malloc column, noted there. These are C++ **probe** timings that
price allocation directly; none is an engine kernel number and none belongs in
BENCHMARKS.md.

---

## What one value of each shape costs the allocator

A global `operator new` records every block. RVec's capacity-1..4 free lists are
**warm**, as they are in a running program:

| shape | blocks | bytes | sizes |
|---|---:|---:|---|
| Int | 0 | 0 | — |
| Str (short) | 0 | 0 | — |
| Str (promoted) | 2 | 168 | 80, 88 |
| Array (empty) | 1 | 48 | 48 |
| Array (2 elems) | 1 | 48 | 48 |
| Hash (empty) | 1 | 136 | 136 |
| Hash (3 keys) | 4 | 4208 | 8, 32, 136, 513 |
| Match | 4 | 464 | 48, 88, 136, 192 |
| ValueExt only | 1 | 192 | 192 |
| Object | 1 | 304 | 304 |

Three things fall out of the table.

**The hot case is already free.** An `Int` and a short `Str` allocate nothing.
Batches 2 and 4 of the representation campaign did that, and nothing here
touches it.

**`RVec`'s pool works, and it covers exactly one of the two blocks.** "Array
(2 elems)" costs the same single block as an empty one: the capacity-1 and
capacity-2 *data* blocks come off the thread-local free list in
[src/ValueVec.h](../../../src/ValueVec.h). What is left is the 48-byte
`shared_ptr` **control block** — the refcounts plus the 24-byte `RVec` header —
and it goes to malloc every time. That block is the unfinished half of a pool
the codebase already has. (Run the probe with the warm-up loop removed and the
same line reads 3 blocks / 432 bytes: that is the cold-pool number, and quoting
it for a running interpreter would overstate the shape by two blocks of three.)

**A `Match` is four allocations.** `ValueExt` (192), `MatchData` (88),
`ValueList` (48), `ValueMap` (136) — 464 bytes across four malloc calls for one
regex match, on the engine's hottest parsing path.

## The drop-in: `allocate_shared` with a thread-local slab

Same ownership, same refcount, same call sites — only the allocator changes.
3M construct+destroy pairs:

| payload | make_shared | slab | ratio |
|---|---:|---:|---:|
| ValueList | 90.24 ms | 30.01 | **3.01x** |
| ValueExt | 92.02 | 30.18 | 3.05x |
| MatchData | 87.18 | 29.95 | 2.91x |
| ValueHash | 92.42 | 33.93 | 2.72x |
| ObjectData | 116.36 | 42.00 | 2.77x |

And on whole `Value`s, built and destroyed, 2M each:

| shape | today | slab | ratio |
|---|---:|---:|---:|
| Array (empty) | 67.82 ms | 23.49 | 2.89x |
| arg list (2 elems) | 136.35 | 82.23 | 1.66x |
| Match | 270.91 | 96.71 | 2.80x |
| Object | 78.82 | 27.82 | 2.83x |

The argument list is the honest one: 1.66x, not 3x, because the two `Value`
constructions and the pooled data block dilute the one control block the slab
actually improves. It is also the shape that runs on every interpreted call.

## Churn order does not hurt it

A slab's advertised case is LIFO, which is also a tree-walker's normal case. The
worry is a long-lived structure that frees in arbitrary order. Measured — a live
set of 200k `ValueList`s, 3M replacements at pseudo-random indices:

    make_shared 215.94 ms    slab 35.74    6.04x

**Better than LIFO, not worse.** malloc degrades as the live set fragments; an
intrusive free list does not care what order blocks come back in. This is the
one result that contradicted the prediction going in.

## Threads

1.5M construct+destroy per thread, wall clock:

| threads | make_shared | slab | ratio |
|---:|---:|---:|---:|
| 1 | 40.39 ms | 15.12 | 2.67x |
| 2 | 233.32 | 15.71 | 14.85x |
| 4 | 51.92 | 18.07 | 2.87x |
| 8 | 78.72 | 22.97 | 3.43x |

**The load-bearing claim is the slab column, which is flat** (15.1 → 23.0 across
an 8x thread count). The malloc column is not monotonic and the 2-thread figure
is a reproducible pathology — it survives best-of-7 and reappears across runs
(233 ms, then 197 ms) — but we did not chase it to a cause, so **the 14.85x
should not be quoted as a result.** It is a reason to look at what libmalloc
does at two threads, separately from this plan.

## The ceiling, and what it would cost

3M `ValueList` construct+destroy:

| | | |
|---|---:|---|
| `make_shared` | 90.33 ms | 1.00x |
| `allocate_shared` + slab | 30.38 | **2.97x** — drop-in, refcount kept |
| slab + placement new, no `shared_ptr` | 8.84 | 10.22x — no control block, no atomic |
| bump arena, never frees | 15.65 | 5.77x |

Two things worth saying about that table.

**The bump arena is slower than the free list.** 15.65 vs 8.84. A freed slot
comes back off the LIFO list still in L1; a bump pointer walks forward through
fresh 1 MB chunks and misses. So "never free" is not the floor — reuse beats it.

**The 10.22x is the same ~190 ownership sites `VALUE32-PLAN` already costed.**
That file measured an intrusive refcount at **1.08x on copying** and concluded it
earns its place only as part of a size target. This is a different lever on the
same sites: on *allocation* the control block is worth 3.4x beyond what the
allocator swap gets. Neither number alone justifies the move; together they are
a better case than either made separately.

## Where the acceleration actually is — measured on real runs

The probe prices an allocator *operation*. It cannot say how much of a *program*
is allocator operations, so [tools/malloc-census.c](../../../tools/malloc-census.c)
— a DYLD interposer that counts every malloc by exact size — answers that on
real kernels. The payload signature sizes from section A (48 `ValueList`, 88
`MatchData`, 136 `ValueHash`, 192 `ValueExt`, 304 `ObjectData`) attribute the
traffic without guessing.

**`regex.raku`** — 50k matches, 40 ms wall, 303,247 mallocs:

| size | count | share | source |
|---:|---:|---:|---|
| 48 | 100,151 | 33.0% | ValueList control ×2 |
| 88 | 50,010 | 16.5% | MatchData control |
| 136 | 50,002 | 16.5% | ValueMap control |
| 192 | 50,168 | 16.5% | ValueExt control |
| 40 | 50,134 | 16.5% | (unattributed) |

**82.5% of every allocation this kernel makes is a payload control block** the
swap covers. At the 20 ns/op the probe measures (30.1 → 10.1 ns per
construct+destroy pair), that is 250k × 20 ns = **5 ms on 40 ms, ~12%**.

**`objects.raku`** — 200k objects, 450 ms wall, 2,503,494 mallocs:

| size | count | share | source |
|---:|---:|---:|---|
| 168 | 600,009 | 24.0% | 3 per object (unattributed) |
| 32 | 400,508 | 16.0% | 2 per object |
| 152 | 400,006 | 16.0% | 2 per object |
| 136 | 200,002 | 8.0% | **ValueHash control** |
| 304 | 200,006 | 8.0% | **ObjectData control** |
| 4032 | 200,006 | 8.0% | ValueHash's deque chunk |
| 64 | 200,049 | 8.0% | 1 per object |
| 8 | 200,063 | 8.0% | 1 per object |
| 48 | 100,162 | 4.0% | **ValueList control** |

Only **20%** are payload control blocks. 500k × 20 ns = **10 ms on 450 ms,
~2%**.

So the honest answer to "where is the acceleration": **it is on the Match path,
not the object path.** The swap is worth roughly 12% on regex-shaped code and
roughly 2% on object-shaped code, and anyone expecting one number for both will
be disappointed by whichever kernel they run second.

### The bigger lead this turned up

**One `Point` object costs about eleven allocations.** ObjectData (304) +
ValueHash control (136) + a **4032-byte deque chunk** + three at 168 + two at
152 + two at 32 + one 64 + one 8. The deque chunk alone is 806 MB of the
kernel's 1088 MB of allocation traffic — a 4 KB block to hold two attributes,
which is the price of `ValueHash`'s reference-stability contract
(`std::deque`, so `Value&` survives autovivification) applied to a container
that never grows past `has` count.

That is a much larger number than the slab is playing for, it is on the shape
"most real Raku code is written in" (the kernel's own words), and it is a
different change: a small-size representation for `ValueHash` that skips the
deque entirely below a handful of entries. It should be its own plan, and it
should probably be written before this one is implemented.

## What this does NOT claim

The two percentages above are **estimates**, not measurements: they multiply a
real allocation count by a probe-measured per-op saving. The probe's malloc runs
in a tight loop where its fast path stays warm, which a real interleaved program
may not; the error could go either way. `/usr/bin/time` also resolves to 10 ms,
so the 40 ms denominator behind the 12% is ±1 band. Only wiring the swap in and
re-running the kernels settles it. Nothing here should be quoted as "rakupp got
N% faster".

**The slab in the probe never returns memory.** That is deliberate for a probe
and wrong for the engine: `RVec::dealloc` caps each class at `kPoolMax = 64`
blocks for a reason the codebase already learned the hard way (a million
one-element arrays holding four-element blocks: 424 MB → 664 MB, 22% of that
program's cycles). A real implementation needs that cap, and the cap will shave
some of the 3x on spiky workloads. The probe does not price that.

## The order to do it in

1. Generalise `RVec`'s `BlockPool` into a size-classed thread-local slab with
   `kPoolMax` retention, in its own header. No call-site changes.
2. Swap `make_shared<T>` → `allocate_shared<T>` at the payload sites, heaviest
   first: `ValueList` (24), `ObjectData` (47), then `ValueExt` / `MatchData` /
   `ValueHash`. Mechanical, reversible per type.
3. Re-run BENCHMARKS.md kernels. If the JSON and array kernels do not move, stop
   — the 21.5% was not where this plan assumed.
4. Consider a small-size `ValueHash` that skips the deque — the eleven-allocation
   object above is a bigger number than this whole plan.
5. Only then consider the control block (the 10.22x), and only jointly with
   `VALUE32-PLAN`'s `Ref`, since it is the same ~190 sites.

## Gates

Nothing below has been run — no engine change exists yet. When one does:
`t/run.raku` twice, full Roast per-file diff against a same-day pre-change run
(the zero-regression file list is the gate), `-DRAKUPP_PTR_CENSUS` still
compiles, and — because the slab is thread-local and blocks freed on one thread
join that thread's list — **ThreadSanitizer with zero reports**, plus parmap and
`t/stress/parallel-map` under `RAKUPP_PARALLEL=1`. The threading discipline is
the risk in this change, not the arithmetic.

## What would falsify this plan

- If the benchmark kernels do not move after step 2, the poolable share of the
  21.5% is small and the remaining allocator traffic is strings and AST nodes —
  a different plan.
- If the `kPoolMax` cap costs most of the 3x, the win was retention rather than
  pooling, and the honest version is a bigger `RVec` pool, not a new allocator.
- If TSan reports anything the existing `BlockPool` does not, the generalisation
  broke an invariant that the capacity-keyed design was holding by accident.

---

# IMPLEMENTED AND MEASURED — 2026-09-20

Steps 1 and 2 are done: [src/SlabPool.h](../../../src/SlabPool.h) plus 88
mechanical call-site swaps (`std::make_shared<T>` → `makePayload<T>`) across 7
files, +89/-88 lines. Output is byte-identical on every kernel checked.

`SlabPool` follows **RVec's retention discipline, not the probe's**: blocks come
from `::operator new` one at a time and are merely retained on free, up to 64 per
size class, released at thread exit. The probe's 64 KB chunk carving measures
faster and cannot give individual blocks back, which is the trade
[src/ValueVec.h](../../../src/ValueVec.h) already refused once.

## What it actually bought

**The machine is noisier than the effect.** A before-vs-before control run shows
±3% on sequential A/B, so the first table this section was going to contain was
thrown away: it "measured" -5.1% on `arraypush`, a kernel whose census shows 155
payload allocations in 3,414 mallocs, which the change cannot move. That delta
was code layout, not the slab. **Any single-digit sequential A/B number in this
repo needs a null control before it is believed.**

Re-measured with the two binaries **interleaved pair by pair**, 11 pairs, so
drift hits both sides equally. The discriminator is the SIGN DISTRIBUTION, not
the median:

| kernel | pairs negative | median | null control |
|---|---|---:|---|
| `regex` | **10 of 11** (1 zero, 0 positive) | **-4.27%** | 6 pos / 5 neg, median +1.56% |
| `objects` | 8 of 11 | -3.72% | — |

`regex` is a **real win of roughly 4%**: eleven pairs with no positive result
against a null that splits evenly is a sign test at p ≈ 0.012. `objects` at 8 of
11 is suggestive and **not established** (p ≈ 0.11).

Peak RSS, which the control showed at **0.0% on every kernel** and is therefore
the trustworthy column:

| kernel | before | after | delta |
|---|---:|---:|---:|
| `regex` | 6.3 MB | 6.2 | **-1.6%** |
| `objects` | 7.0 | 6.9 | -1.4% |
| `sortby` | 25.8 | 25.8 | 0.0% |
| `arrayops` | 79.5 | 79.5 | 0.0% |
| `arraypush` | 63.3 | 63.2 | -0.2% |

**The retention cost this plan warned about did not appear.** Memory is flat to
slightly down, so the `kPoolMax` cap is doing its job and the bounded free lists
cost less than the malloc metadata they displace.

## The estimate was 3x too optimistic, and why

This plan predicted **~12%** for `regex` from 250k payload allocations × 20 ns.
Measured: **~4%**. The error is the per-op saving, not the allocation count —
the count came from a real run and is right.

20 ns was measured in a tight loop where the free-list head and the block coming
off it stay in L1 across millions of iterations. A real interpreter interleaves
tree-walking between allocations and evicts both. The plan's own caveat said the
probe's warm fast path "may not" hold in a real program and "the error could go
either way"; it went the way that hurts, by about a factor of three.

**The general lesson for the rest of this campaign: an allocator microbenchmark
overstates its own effect by roughly 3x on this engine.** `VALUE32-PLAN`'s
`Slim32` ratios (4.3x on copy, 5.0x on array build) were measured the same way
and should be discounted the same way before anyone budgets against them.

## What is still open

- The **eleven allocations per object** finding is untouched and remains the
  larger number: a 4032-byte deque chunk per `ValueHash` is 806 MB of
  `objects.raku`'s 1088 MB of traffic. That is the next plan, not this one.
- No Roast run yet. The gates below are unrun, and this must not merge without
  them — in particular TSan, since `SlabPool` adds a second thread-local free
  list beside `RVec`'s.
- `SlabPool::pool()` is a function-local `static thread_local` with a
  non-trivial destructor, so every payload allocation pays an init guard. This
  repo has been bitten by exactly that before (`emptyValueExt`'s comment: a
  function-local-static guard "was most of a 28% regex regression"). Worth
  testing a `constinit` namespace-scope pool against it.

## The init guard, removed — and what it was actually worth

The open item at the end of the previous section said `SlabPool::pool()` was a
function-local `static thread_local` whose type has a non-trivial destructor, so
every payload allocation paid an ABI initialisation guard — the pattern
`emptyValueExt` blames for "most of a 28% regex regression". That is now fixed:
the hot state is constant-initialised `thread_local` arrays reached with no
guard, and the thread-exit drain RVec's pool performs is kept by paying the
guard **once per thread**, on the first allocation, behind a `tlArmed` flag the
hot path only reads.

(`constinit` is C++20 and this project is C++17, so the keyword sits behind a
`__cpp_constinit` test. It only asserts what the initialisers already are. The
standalone unit test had been compiling at `-std=c++20` and passed the keyword
happily — a reminder to compile probes with the project's own flags.)

**v2 (guard-free) against the original baseline**, 11 interleaved pairs:

| kernel | pairs negative | median | spread |
|---|---|---:|---|
| `regex` | **11 of 11** | **-6.62%** | -6.00 / -7.44 |
| `streq` | 8 of 11 | -6.67% | -14.86 / +9.27 |
| `loopsum` | 9 of 11 (2 zero, 0 positive) | -0.71% | -1.33 / +0.00 |

**v1 against v2 — the guard priced on its own:**

| kernel | pairs negative | median | verdict |
|---|---|---:|---|
| `loopsum` | 10 of 11 | -1.33% | the guard caused the regression |
| `regex` | 4 of 11 (3 positive, 4 zero) | +0.00% | **no signal** |

Two conclusions, one of them a correction.

**The guard was real, and it was the `loopsum` regression.** A kernel that barely
allocates was paying it anyway, and removing it turned 0-of-11-negative
(consistently slower) into 9-of-11-negative. That is the whole of what the fix
bought.

**It was NOT worth 2.3 points on `regex`, which is what the two measurements
looked like side by side.** `regex` read -4.27% before the fix and -6.62% after,
but priced directly against each other the two binaries are indistinguishable.
The difference was the machine: the -4.27% run happened while another session's
test suite was running, the -6.62% run did not. **The tight one is the true
figure** — eleven pairs inside a 1.4-point band, against a spread of ±10 points
on the noisy run. Anything measured on this box while it is busy is worth about
half a significant figure.

So the headline for the whole change stands at roughly **-6.6% on `regex`**,
similar on `streq`, neutral elsewhere, memory flat to -1.6%.

## Gates — run

**`t/run.raku` under the guard-free build: in progress at 949 ok / 0 failures**
(the earlier guarded build finished 1078/1078, 0 failures).

**ThreadSanitizer** (`-fsanitize=thread -g -O1`, Debug), `RAKUPP_PARALLEL=1`
over `t/stress`:

| test | warnings |
|---|---:|
| parallel-map, atomic-counter, lock-counter | 0 |
| channel-pipeline, hash-guarded, supply-fanin | 0 |
| **promise-chain** | **1** |

The one warning is a 1-byte race on a Promise state flag, written by the promise
worker inside `spawnPromise`'s `$_0` and read by the main thread in
`methodCallPart2`. **It is pre-existing, and that is measured, not assumed**: a
TSan build of the baseline commit (`ab31e04`, no slab, no call-site swaps)
reports the same warning on 3 runs of 3, with the same
`spawnPromise` / `BigStackThread::__invoke` stack. Neither binary's report
mentions `SlabPool`, `SlabAlloc` or `makePayload` anywhere — grep count zero.

It is the race already written up in
[EVALCALL-RACE-2026-09-17.md](../findings/EVALCALL-RACE-2026-09-17.md), found by
the v4.0.0 release gate, recorded rather than fixed because "a data-race fix is
a project rather than a release task". Unchanged by this work, and still open.

**The pooling false-positive that did NOT appear, and why.** A free list is a
classic source of spurious TSan reports: a block freed on one thread and
reissued on another makes two logically distinct objects share an address, and
TSan flags the reuse as a race. `SlabPool` is immune by construction — every
list is thread-private, so a block can only be reissued on the thread that freed
it. That is RVec's property too, and it is the reason this design copied RVec's
rather than inventing one.

**Roast — the zero-regression per-file gate: GREEN.** Two full runs of the same
1,464-file corpus (`roast b2cbe8a42`), one under the changed binary and one
under a Release build of the baseline commit, `--cpu=5`:

| | baseline | change |
|---|---:|---:|
| files reporting a status | 1,363 | 1,363 |
| fully passing | **698** | **698** |
| per-file status differences | — | **0** |

Not 698 against `COUNTING.md`'s 676 — that figure predates many commits on
`main`, so it is not the gate. The gate is a same-day diff of the two file
lists, and it is empty: no file moved between PASS, part, TIME or no-TAP in
either direction.
