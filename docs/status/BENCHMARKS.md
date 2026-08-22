# Raku++ vs Rakudo — speed

A small, honest performance comparison on the subset of Raku that **both**
engines run identically. This is not a claim that Raku++ is "better" — Rakudo
is the mature, complete, production reference implementation, and Raku++ is
[far behind it on Roast coverage](ROAST.md). The point is only to give a fair
picture of where Raku++ — as both a tree-walking interpreter and a native
compiler — lands relative to an optimizing VM.

Raku++ can run a program three ways, and this compares all of them against
Rakudo:

- **interp** — Raku++ interpreting the source (tree-walk)
- **native** — Raku++ `--exe`: the program transpiled to C++ and compiled to a
  native binary (no interpreter inside)
- **rakudo** — Rakudo interpreting the source (MoarVM + JIT)

Raku++ has two other standalone-binary modes — `--bundle` and `--aot` — but both
*tree-walk* the program, so they run at **`interp` speed** and aren't shown
separately (`--aot` fib runs at interp's ~770 ms). `--exe` is the only
mode that changes runtime performance, so it's the one the `native` column
tracks.

(A fourth environment — **[Raku.js](../../rakujs)**, the interpreter compiled to
WebAssembly — is measured against `interp` on these same kernels under Node,
Bun, and the browser in
[rakujs/README.md](../../rakujs/README.md#performance-vs-native-and-node-vs-bun-vs-browser):
1.3–6.8× slower than native on a clean host, dominated by the `-fexceptions`
call trampolines. That comparison is still experimental — see the status note
there.)

## The short version

- **Startup:** ~2–3 ms on this machine (2.8 ms interpreting, 2.4 ms native) —
  a tiny native binary with no VM to spin up. For one-liners, CLI glue, and
  small programs it is instant. (Both rows grew ~0.3–0.6 ms when lexical pads
  landed on 2026-08-22: laying out a pad is a fixed per-process cost, paid once
  and repaid many times over by every kernel that then runs.)
- **Native (`--exe`) beats Rakudo on every benchmark here** — from 4.3× on
  `arrayops` to 18.6× on `loopsum`, 30.5× on `hash`, and 62.0× on `strcat`.
  Compiling removes interpreter overhead.
- **`streq` is the one row Rakudo still leads, and only by 1.1×** interpreted.
  `fib` — Rakudo's for the whole life of this file, level as of the `Value`
  shrink — is now a Raku++ interpreter win at 1.1×. Compiling settles both:
  `--exe` puts `fib` 5.3× ahead and `streq` 13.9× ahead (string `eq`/`lt`
  compile to inline byte-compares — see
  [internals/DISPATCH.md](../internals/DISPATCH.md) for the dispatch story).
- The **interpreter** now beats Rakudo on 9 of 10 — everything except `streq` —
  including the `loopsum` loop kernel, which went from 1.5× to **2.5×** when
  lexical pads landed, and `hashfill`, the one kernel with a Perl 5 twin (see
  "vs Perl 5" below).
- **String building (`~=`) appends in place** in every mode, so `strcat` is
  O(n) rather than O(n²) — 18.7× ahead of Rakudo even interpreted.

## Methodology

- **Machine:** macOS (Darwin 24.6, Apple Silicon M3), re-measured 2026-08-22 at
  `v3.6.0-19-g4c0e80b` — the SAME machine as every earlier revision of this
  file, so the rows are comparable with the earlier 2026-08-22 and the
  2026-08-21 ones. (An earlier
  2026-08-21 revision carried a sitting taken on a Darwin 25.5 machine whose
  Raku++ binary was uniformly 1.3–1.5× slower while `perl` and Rakudo were as
  fast or faster there — not a machine-speed difference, so those rows were
  replaced rather than kept. See the re-snapshot log at the end.) This sitting
  is three full harness passes, minimum across all three; the three passes
  agreed to within 1–2% on every cell. The harness now **interleaves the
  engines** — each measured round times every engine once, back to back — so a
  load spike lands on all lanes instead of one column, and `--tsv=` writes the
  median alongside the minimum plus the CPU and C++ toolchain the sitting ran
  on. The reference engines are the check that this sitting is comparable with
  the previous one: Rakudo lands within ±1.2% of its previous row on all ten
  kernels and `perl` is 4.5% faster, so the Raku++ movement below is the code,
  not the machine. The harness REFUSES
  a rakupp binary built for another architecture, the same guard perf-guard
  carries: a stale x86_64 `build/` beside a `build-arm64/` measures under
  Rosetta at a uniform 1.7–2× penalty, and exactly that nearly shipped in
  this file at v3.14.0. (Rows are not comparable across doc revisions — absolute times
  shift a few percent with machine state; the Rakudo column, measured every
  time, is the fixed yardstick. A per-iteration `std::function` allocation that
  had crept into the loop path over the v0.7.1→v0.9.0 cycle was found by bisect
  and removed, restoring the tight-loop kernels to their v0.7.1 speed.)
- **Raku++:** built `-O3 -DNDEBUG` (CMake Release).
- **Rakudo:** `raku` v2026.07 (MoarVM backend).
- **Harness:** [`tools/run-bench.raku`](../../tools/run-bench.raku) — itself a Raku
  program, run *by Raku++* (it also runs under Rakudo). It only spawns each
  engine as a fresh subprocess and times it with `now`, so the language the
  harness is written in does not bias either contestant.
- **Timing:** 7 runs per benchmark, first discarded as warm-up, **minimum of the
  remaining 6** reported (least noisy; the mean was within a few percent). For
  the `native` column each program is compiled with `--exe` once, then the
  resulting binary is timed — the compile step is not counted.
- **Fairness:** the harness runs each program under all three engines and
  compares their stdout **before** timing anything; if `interp`, `native`, and
  `rakudo` don't emit byte-identical output it flags the row and exits non-zero,
  so a divergent kernel can't be silently benchmarked. (Rakudo is the reference;
  every benchmark program is deterministic, so identical output is expected.)
- **Harness overhead:** spawning + capturing a subprocess adds a small fixed
  cost per run. On top of that each engine pays its *own* process startup —
  negligible for Raku++'s native binary, but Rakudo loads a full precompiled
  runtime, a fixed cost included in every row below. See "How to read this".
- **Perl 5:** a bench may ship a `.pl` twin (`hashfill` does); the harness then
  also times `perl` on it — same spawn-and-capture, same byte-identical-output
  gate — and prints a `perl` column (`—` for rows without a twin). Override the
  binary with `PERL=`.

## Results

Best of 6 timed runs per engine (7 spawned, the first discarded as warm-up),
taken across two full harness passes; includes process startup; lower is
better. Rows are ordered most-Raku++-favourable first.

### Interpreter vs Rakudo

Even without compiling, the tree-walker wins on all of these except one, and
that one is now close: Rakudo's VM leads `streq` by 1.1× (string comparisons
walk the interpreter's operator-dispatch chain). `fib` — tiny-body recursion, a
JIT's best case, and Rakudo's for the whole life of this file — went level with
the 2026-08-22 `Value` shrink and ahead, 1.1×, when lexical pads landed the
same day. Variable access is what moved: with a `my` resolved to a frame slot
instead of a hash lookup, `loopsum` halved (1.5× → 2.5× against Rakudo) and
`hash` and `strcat` fell by a bit over a quarter each, while `arrayops`,
`sortnums` and `bigint` — whose time is inside a runtime method, not in
touching variables — did not move at all.

| Benchmark | Raku++ (interp) | Rakudo | Faster |
|---|---:|---:|---|
| strcat   |   9.6 ms | 179.9 ms | **Raku++ 18.7×** |
| hash     |  18.1 ms | 222.3 ms | **Raku++ 12.3×** |
| bigint   |  31.6 ms | 250.5 ms | **Raku++ 7.9×** |
| sortnums |  33.8 ms | 249.3 ms | **Raku++ 7.4×** |
| regex    |  39.8 ms | 275.8 ms | **Raku++ 6.9×** |
| arrayops |  65.4 ms | 276.2 ms | **Raku++ 4.2×** |
| hashfill | 111.9 ms | 450.2 ms | **Raku++ 4.0×** |
| loopsum  | 105.2 ms | 258.1 ms | **Raku++ 2.5×** |
| fib      | 420.9 ms | 453.8 ms | **Raku++ 1.1×** |
| streq    | 312.2 ms | 282.0 ms | Rakudo 1.1× |

**`regex` regressed at v3.6.0 — bisected and fixed after the tag.** On this
machine the interpreted row was 88.5 ms at v3.14.0 (2026-08-11) and 113.4 ms
at v3.6.0, +28%, with
[`tools/bench/regex.raku`](../../tools/bench/regex.raku) unchanged since the
initial commit; the native row moved the same way (70.8 → 79.8 ms). Every
kernel `tools/perf-baseline.raku` does gate got 24–36% *faster* over the same
span, which is exactly why this slipped through: `regex` is not one of the
seven gated kernels. A build-at-commit bisect landed on `364c26b` ("the
POSIX-named character classes are Unicode, not <ctype.h>"): a regex literal
in a loop is recompiled per iteration, each compile lazily rebuilds its class
node's 256-entry byteset, and that rebuild now called the per-codepoint class
predicate (guarded static + slot lookup) 256× where inlined `<ctype.h>` used
to answer. Two fixes landed: the byteset build folds each class flag to a
precomputed 128-bit mask (one AND per byte), and the interpreter caches
compiled `Regex` objects per (post-interpolation pattern, flags) per thread —
match, substitution and `my regex` subrule bodies all stop re-parsing per
iteration. Same-machine A/B (Darwin 25.5 box, not the machine of the tables
above): the kernel went 120.4 → 89.7 ms with the mask fix alone and → 55.8 ms
with the cache — v3.14.0 measured 108.0 there, so this is well past merely
undoing the regression. **The tables above are now the 2026-08-22 re-measure
on the benchmarks machine and carry the fix**: interpreted `regex` reads
44.8 ms against v3.6.0's 113.4 and v3.14.0's 88.5, and the native row 23.8 ms
against 79.8 — so the kernel ends up roughly half of what it cost before the
regression was ever introduced, and `regex` moves from the second-worst
interpreter row to the fifth-best.

### Native (`--exe`) vs Rakudo

Compiling removes interpreter overhead on top of that — pushing **every row
clearly ahead of Rakudo**, `fib` and `streq` included. The last column is the
speed-up over interpreting the same program.

| Benchmark | Raku++ (`--exe`) | Rakudo | Faster | vs interp |
|---|---:|---:|---|---:|
| strcat   | 2.9 ms   | 179.9 ms | **Raku++ 62.0×** | 3.3× |
| hash     | 7.3 ms   | 222.3 ms | **Raku++ 30.5×** | 2.5× |
| loopsum  | 13.9 ms  | 258.1 ms | **Raku++ 18.6×** | 7.6× |
| streq    | 20.3 ms  | 282.0 ms | **Raku++ 13.9×** | 15.4× |
| sortnums | 18.9 ms  | 249.3 ms | **Raku++ 13.2×** | 1.8× |
| hashfill | 37.2 ms  | 450.2 ms | **Raku++ 12.1×** | 3.0× |
| regex    | 24.9 ms  | 275.8 ms | **Raku++ 11.1×** | 1.6× |
| bigint   | 30.7 ms  | 250.5 ms | **Raku++ 8.2×**  | 1.0× |
| fib      | 85.8 ms  | 453.8 ms | **Raku++ 5.3×**  | 4.9× |
| arrayops | 64.0 ms  | 276.2 ms | **Raku++ 4.3×**  | 1.0× |

**Reading the `vs interp` column:** compiling helps most where a tree-walker
hurts — `streq` 15.4× (per-node walking around what is, after the fast path, a
trivial byte-compare — see [internals/DISPATCH.md](../internals/DISPATCH.md)), `loopsum` 7.6×,
`fib` 4.9× (both re-dispatch a tiny body a huge number of times). Every one of
those margins NARROWED when lexical pads landed — the compiler already kept
variables in C++ locals, so the interpreter closing on slot-indexed access is
the gap shrinking from the interpreter's side, not codegen slowing down.
It's a near no-op (1.0–1.8×) for the workloads whose time is spent *inside* runtime
methods — `arrayops`/`sortnums` (`.grep`/`.map`/`.sort`) and especially
`bigint`, which lives almost entirely in `BigInt` multiply. There the driving
loop is trivial, so removing interpreter overhead changes little.

`fib` — a tiny function called 1.6M times, the case a JIT specializes best — used
to be the one place Rakudo led even the default `--exe`; hot-pathing integer
arithmetic in the runtime (`applyArith`) closed that gap and put native ~5.3×
ahead. `streq` got the same treatment on 2026-07-17: string comparisons used to
walk `applyArith`'s full dispatch chain (~118 ns per `eq`). Compiled code now
emits inline plain-`Str` byte-compares and calls builtins through pointers
resolved once at startup; the interpreter gained a matching char-dispatched
Str/Str fast path at the top of `applyArith` (909.7 → 562.5 ms — the remaining
gap to Rakudo is per-node tree-walk cost, not operator dispatch).
[internals/DISPATCH.md](../internals/DISPATCH.md) has the measurements.

### vs Perl 5: the `hashfill` kernel

Perl 5 is the family yardstick for CLI-scale scripting, so one kernel ships as
a twin pair — [`tools/bench/hashfill.raku`](../../tools/bench/hashfill.raku)
and [`tools/bench/hashfill.pl`](../../tools/bench/hashfill.pl), the same
program line for line: fill a 200k-key hash through interpolated string keys
(`%h{"key$i"} = $i * 2`), sweep `%h.values` into an accumulator, then build a
string with 50k `~=` appends.

Measured 2026-08-22, all four engines in the same harness run (best of 6,
startup-inclusive; the `perl` on this machine's PATH is v5.44.0), after the
`ValueHash` payload, the `Value` shrink and lexical pads landed (see below):

| engine | hashfill | vs perl |
|---|---:|---:|
| Raku++ `--exe` | 37.2 ms | **2.7× faster** |
| Perl 5 | 102.0 ms | — |
| Raku++ interp | 111.9 ms | 1.1× slower |
| Rakudo | 450.2 ms | 4.4× slower |

The interpreted row is the one to watch: it was 1.6× slower than perl on
2026-08-21 and is within 10% of it now, on a kernel built out of exactly the
things a scripting language is asked to do — interpolated hash keys, a values
sweep, and 50k string appends.

Replacing the hash payload's `std::map` with `ValueHash` — an insertion-ordered
open hash with the key's hash stored, in the perl mold
([PERL5-TECHNIQUES.md](../dev/findings/PERL5-TECHNIQUES.md)) — is what put the
compiled row ahead of perl. The `hash` kernel moved the same way; the non-hash
kernels are unchanged within noise.

The harness's `native` column compiles with plain `--exe`; the `-O` codegen
passes widen the margin further. The full mode ladder below is from the
2026-08-21 sitting, taken before the `Value` shrink — its absolute rows are
superseded by the table above (plain `--exe` is 37.2 ms now, not 51.5), so
read it for the ordering between the modes, not for the milliseconds (best of
5, spawn-inclusive, same machine state throughout, every mode byte-identical
with perl before timing):

| mode | hashfill | vs perl |
|---|---:|---:|
| `--exe -O3` | 47.0 ms | **2.1× faster** |
| `--exe -O` | 48.2 ms | **2.1× faster** |
| `--exe` | 51.5 ms | **2.0× faster** |
| Perl 5 | 100.5 ms | — |
| interp | 167.2 ms | 1.7× slower |

The row exists because this workload was the measured weak spot: before the
2026-08-21 change, the native binary spent twice perl's CPU time on it (0.15 s
vs 0.07 s, timing the program directly). Three fixes closed that: `%h.values`
(and `.keys`/`.kv`/`.pairs`/`.antipairs`) no longer snapshots the whole hash
into a Pair list it then discards; assigning a freshly built list into an
`@`-array steals the list's uniquely-owned buffer instead of copying it
element-wise; and compiled string interpolation emits literal parts as C
strings instead of constructing a `Value` per part per evaluation. After them
the native binary leads perl on both clocks on this machine — 0.06 s of CPU
against perl's 0.08 s, 0.07 s of wall clock against 0.09 s (timing the
programs directly, outside the harness).

### `-O` (the optimizer flag)

The `native` column above is the default `--exe`. Adding **`-O`** enables three
speculative codegen passes:

1. **direct-arity calls** — a fixed-arity positional sub gets direct `Value`
   parameters (plus a boxed adapter), skipping the per-call `ValueList` heap
   allocation;
2. **inline arithmetic & comparisons** — `+ - * ** % %% < <= > >= == !=` emit
   inline helpers that do the small-int case as native `int64` (overflow
   promotes to bignum), and `eq ne lt gt le ge` do the plain-`Str` case as a
   byte-compare, instead of the string-dispatched `applyArith`;
3. **guarded native-int expression lanes** — statement-position int assignments
   (`$x = …`, `$x += …`, `$x++`) and int conditions compute in raw `int64` with
   runtime tag guards and store into the target's existing box, constructing no
   `Value` at all; any guard/overflow failure re-runs the boxed form.

(In-place `~=` string building is *not* one of these — it is now the default in
both the interpreter and `--exe`.) Measured by
[`tools/run-optbench.raku`](../../tools/run-optbench.raku) on five showcase kernels
written to exercise the passes (each program is verified to produce identical
output all three ways before timing). Best of 5 runs; Rakudo (v2026.06) shown
for reference:

| Benchmark | `--exe` | `--exe -O` | `-O` vs `--exe` | Rakudo | showcases |
|---|---:|---:|---:|---:|---|
| sieve       | 1189.9 ms | **23.5 ms**  | **50.5×** | 991.9 ms  | primes < 200k by trial division — `* <= %%` all laned |
| powmod      | 571.0 ms  | **53.2 ms**  | **10.7×** | 748.8 ms  | 1M `** 3` then `% 1000` — inline pow + mod lane |
| intsum      | 298.7 ms  | **31.8 ms**  | **9.4×**  | 656.5 ms  | 5M int accumulation — `+=` lane, zero boxing |
| fibcalls    | 670.6 ms  | **169.6 ms** | **4.0×**  | 1417.4 ms | fib(32) — direct-arity calls + int-lane condition |
| stringbuild | 24.0 ms   | 25.6 ms      | 1.0×      | 219.5 ms  | 400k `~=` appends — in-place O(n) string build |

The lanes (pass 3) dominate this table: `sieve`'s inner loop — `while $d * $d
<= $n`, `if $n %% $d`, `$d++` — runs as raw `int64`, taking it from a tie with
Rakudo at plain `--exe` to 42× ahead, and `intsum` shed its four
per-iteration `Value` constructions. On the main kernels above, `-O` puts fib
at 27.3 ms (3.9× over `--exe`, 16.8× over Rakudo), loopsum at 7.1 ms, and streq
at 14.5 ms (the `$c++`/`$c--` counters lane on top of the inline `eq`/`lt`).
`stringbuild` gains nothing because in-place append is already the default
everywhere. It's opt-in, off by default, and produces identical output
(validated per-program before timing, plus every deterministic example against
its golden). See [OPTIMIZATION.md](../internals/OPTIMIZATION.md) for what each pass emits
and the C++ optimization-level forwarding (`-O3`/`-Os`/`-Ofast`).

### Real-world: grammar parsing (YAMLish)

The benchmarks above are small kernels. This one is a whole real module doing
real work: the zef-installed **YAMLish** grammar (unmodified) parsing the Raku
course's table-of-contents YAML (`_data/toc/en.yaml`, 2576 lines) with
`load-yamls`. It exercises the backtracking grammar engine — subrules, LTM `|`,
lookbehind assertions, `:my` side-effects, indentation-parameterised rules, and
action-method tree building — against the same source under both engines.

Best of 5 runs, wall-clock (`time`), lower is better.

| Workload | Raku++ (interp) | Rakudo | Faster |
|---|---:|---:|---|
| load-yamls, one parse per process | 0.54 s | 0.98 s | **Raku++ 1.8×** |
| load-yamls, 10 parses in-process   | 5.48 s | 6.43 s | **Raku++ 1.2×** |

The single-parse figure is the realistic one — `raku-pages.raku` (the course
generator) reads the TOC once per invocation, and Raku++ regenerates the entire
1,483-page course byte-for-byte identically to Rakudo. The gap narrows for
repeated in-process parses because each `load-yamls` rebuilds and recompiles the
grammar, so grammar-construction cost isn't amortised across calls; the
per-parse matching itself is where Raku++'s lead comes from. Getting here took
targeted work on the match engine — bounding the lookbehind scan window (the
grammar had an O(N²) start-position rescan), caching per-rule name resolution on
the compiled AST, resolving tail-position `return` without a C++ exception, and
non-owning match continuations. An earlier build of this same parse took ~195 s;
that was an unbounded lookbehind scan on a document this large.

## Parallel scaling (the default since v3.0.0)

`tools/bench/parmap.raku` is the scaling kernel the v3 parallelism campaign
gated on ([PARALLEL-PLAN.md](../dev/plans/PARALLEL-PLAN.md)): an
embarrassingly parallel map — each `start` block sums squares over its own
range, no sharing — with the **total work held constant** while the worker
count varies. Re-measured 2026-08-09 at the v3.0.0 flip (no env var — this
is the shipping default now) on the 8-core (4P+4E) reference machine:

| workers | wall | speed-up | at 2026-08-08 |
|---:|---:|---:|---:|
| 1 | 996 ms | 1.00× | 1.00× |
| 2 | 494 ms | 2.02× | 1.86× |
| 4 | 258 ms | 3.86× | 2.96× |
| 8 | 192 ms | 5.19× | 3.62× |

The right-hand column is the same kernel one day earlier: the campaign's
final lock work — the thread-safe worker registry, per-supplier mutexes off
the shared stripe pool, channel workers that no longer hold or spin on the
GIL, daemon teardown — bought the difference (3.62× → 5.19× at 8 workers)
with no change to the kernel. Two honest notes stand. The 8-worker row
spills onto the efficiency cores, so the marginal gain past 4 full-speed
cores stays sub-linear — exactly as [ASYNC.md](../guide/ASYNC.md) advises
when sizing a fan-out. And the single-thread row is the same speed as GIL
mode (996 vs 1005 ms measured at the flip): the safety machinery engages
only while workers are actually live, so a single-threaded program pays a
few predicted branches and nothing else.

## How to read this

- **The short kernels are startup-inclusive — read them that way.** Every row is
  a fresh spawn, so each engine's process startup is part of its time. Rakudo
  loads a full precompiled runtime once per run, a fixed cost that dominates its
  number on the fastest kernels — so those multipliers are *not* a pure
  execution-speed comparison. The compute-heavy `-O` kernels and the in-process
  YAMLish parse (10 parses in one process) are the execution-only picture.
  Raku++'s own ~2 ms start is a real convenience for scripts, editor tooling, and
  shelling out in a loop, but it's a convenience, not an execution-speed claim.
- **Interpreter throughput losses are real, and `--exe` addresses them.** A
  tree-walker re-dispatches every AST node on every execution; compiling to
  native code removes that, which is why `loopsum`/`fib` catch or pass Rakudo.
  What compiling *can't* speed up is time spent inside the runtime's own methods
  — matching Rakudo there would mean optimizing (or JIT-ing) those, which is
  further out on the [roadmap](ROADMAP.md).
- **This says nothing about coverage.** Rakudo runs essentially all of Roast;
  Raku++ runs a growing fraction. These benchmarks deliberately use only the
  overlap. Speed on a subset is not parity — it's just a fair snapshot of the
  engine's execution model.

## Reproducing

```sh
./build/rakupp tools/run-bench.raku          # checks all three engines agree, then times them
```

The harness compiles each program with `--exe` for the native column; the
benchmark programs are plain, readable Raku in `tools/bench/*.raku` (edit or add
freely).

_Snapshot taken 2026-07-22 with Raku++ 1.0.0 at 583 / 1,462 Roast files fully
passing, on Darwin 24.6 against Rakudo v2026.06 (kernels: best of 6 harness
runs; `-O` kernels: best of 5; YAMLish: best of 5, re-measured). The YAMLish
rows drifted ~8% slower than the 2026-07-20 snapshot (0.50→0.54 s single
parse) — snapshot-binary bisect shows the cost accreted gradually across the
90%-campaign legs (5.06 s → 5.21 s → 5.50 s on the 10-parse row, Jul 15 → 19 →
22), spread over the parse/regex hot paths rather than one change; profiling
shows no single new hotspot. Tracked as a post-1.0 item._

_**v1.1.0 (2026-07-24) re-run** (598 / 1,462 files fully passing): the
release-gate benchmark pass found **no regression**. All engines produced
identical output, and every engine-vs-engine ratio held within a hair of the
1.0.0 snapshot (strcat 14.1× vs 14.4×, hash 5.7× vs 6.0×, loopsum 1.3× vs 1.4×,
fib Rakudo-1.9× vs 1.8×). Absolute wall-clock ran ~5% higher uniformly, but the
measurement machine had heavy background load at the time (macOS `photoanalysisd`
pegged near 85% of a core, load-avg ~3), which inflates every row equally and
leaves the ratios intact — so the pristine 1.0.0 absolute numbers above are
retained pending a quiet-machine re-snapshot. The typed-blob / `.lines` /
`signal()` / module work in v1.1.0 does not touch these kernels' hot paths._

_**2026-07-29 re-snapshot** (625 / 1,462 files fully passing) — the quiet-machine
re-measure the v1.1.0 note was waiting for. The tables above are this run. It
also settles what that note left open: there **is** a real interpreter
regression since 1.0.0, and it is not measurement noise. Rakudo, measured the
same evening, is the yardstick and barely moved (fib 465.1 → 465.5 ms, hash
231.0 → 226.5), while the interpreter slowed:_

| perf-guard kernel | v1.0.0 (Jul 22) | 2026-07-29 | |
|---|---:|---:|---:|
| fib     | 816.3 ms | 911.0 ms | **+11.6%** |
| asg     | 502.0 ms | 527.7 ms | +5.1% |
| loopsum | 194.4 ms | 205.2 ms | +5.6% |
| hash    |  39.7 ms |  40.3 ms | +1.5% |

_Measured by running `tools/perf-guard.raku` against the installed 1.0.0 binary
and against HEAD on the same idle machine, minutes apart. The cost is **not** in
the v1.2.x conformance work of 2026-07-28/29: a binary kept from the start of
that session reads 907.8 ms on `fib`, within noise of HEAD's 911.0, so the
regression predates it and accreted somewhere across the v1.2.x cycle. Same
shape as the parse-time drift recorded above — gradual, spread across the
dispatch path, no single new hotspot. Not yet bisected; the snapshot binaries
that would localise it are the next step._

_**Post-campaign re-run.** The tables above are this run. Three interpreter
changes landed — see [dev/experiments/PERF-CAMPAIGN.md](../dev/experiments/PERF-CAMPAIGN.md): the method
invocant passes by const reference, `Env`'s rarely-used containers moved behind a
lazy pointer, and a call's argument vector is MOVED rather than copied._

| kernel | before campaign | after | |
|---|---:|---:|---:|
| fib | 903.3 ms | 744.1 ms | **−17.6%** |
| streq | 547.2 ms | 509.6 ms | −6.9% |
| strcat | 13.5 ms | 12.3 ms | −8.9% |
| hash | 41.0 ms | 38.0 ms | −7.3% |
| loopsum | 206.0 ms | 197.3 ms | −4.2% |

_`fib` is the call-heavy kernel and moves most — of that, roughly 9 points came
from the argument-vector move alone. `sortnums` (+2.5%) drifts the other way,
inside the ±3% the Rakudo column itself shows across these runs._

_The compiled (`--exe`) column is unaffected — `fib` 161.7 → 152.8 ms, `strcat`
4.4 → 3.9 — which fits a regression in interpreter dispatch rather than in the
runtime both modes share._

_**2026-07-31 re-snapshot** — the tables above are this run, all three engines
measured within the same hour. It follows the interpreter **node
specialization** described in [internals/NODE-SPECIALIZATION.md](../internals/NODE-SPECIALIZATION.md):
fast paths in `evalBinary`/`evalIndex` for `$a OP $b`, `$n OP literal` and
`@a[$i]`, which read variables by pointer instead of copying a 376-byte `Value`
and skip probes that cannot apply to plain scalars._

_Read the **ratios**, not the absolute times. This machine measured ~4–7% slower
across the board than the 2026-07-29 snapshot — Rakudo, which is unaffected by
anything we changed, moved with it (`fib` 456.9 → 482.4 ms, `hash` 226.3 →
235.0, `regex` 278.8 → 298.9) — so absolute rows shifted for reasons that have
nothing to do with the change. Against the previous binary, measured directly
and alternating on the same machine:_

| benchmark | before | after | |
|---|---:|---:|---:|
| streq | 538.2 ms | 455.6 ms | **−15.4%** |
| fib | 781.2 ms | 683.1 ms | **−12.5%** |
| arrayops | 124.1 ms | 121.8 ms | −1.8% |
| strcat | 16.6 ms | 16.5 ms | −0.5% |
| regex | 94.1 ms | 93.7 ms | −0.5% |
| loopsum | 211.7 ms | 210.9 ms | −0.4% |

_So the two rows Rakudo led both narrowed — `fib` 1.6× → 1.4×, `streq` 1.7× →
1.5× — and everything else is unchanged, which is expected: `strcat` and `regex`
spend their time inside runtime methods and string building, not in the operator
shapes the fast paths cover. The `--exe` columns are unchanged by this work
(codegen already kept variables in C++ locals); their movement here is the same
machine drift. The `-O` table was not re-measured this round._

_**2026-08-21 re-snapshot (v3.6.0)** — the tables above are this run: two
harness passes on the release binary, all engines byte-identical first,
Darwin 24.6 / Apple Silicon, the same machine as every earlier revision.
Rakudo is v2026.07 (was v2026.06). `hashfill` joins both tables as the tenth
kernel — the one with a Perl 5 twin (its own section above carries the perl
column and the `-O` mode ladder). Against 2026-08-11: every kernel faster
except `regex`, which regressed 28% interpreted and is flagged under the
interpreter table; `loopsum` stays a Raku++ interpreter win; compiled
`hashfill` now leads perl 5 rather than trailing it._

_**On the superseded 2026-08-21 sitting.** An earlier revision of this file
(commit `6239215`) carried a sitting taken on a second machine — Darwin 25.5
— which drew every dashboard series sharply upward. Those rows are not a
machine-speed difference and were replaced, not merged: in that sitting
`perl` (81.8 vs 90.1 ms) and Rakudo (`strcat` 123.8 vs 172.2 ms) were as fast
or **faster** than here, while every Raku++ row was 1.3–1.5× **slower** — a
handicap that lands on our binary alone. The arch guard in
[`tools/run-bench.raku`](../../tools/run-bench.raku) cannot catch this case:
it reads `file -b`, and a universal binary's description contains `arm64`
whichever slice actually executes. The `hashfill` backfill in the dashboard's
`bench-backfill.tsv` was measured in that same sitting off
`rakupp-macos-universal` artifacts and carries the same doubt._

_**2026-08-22 re-snapshot (`v3.6.0-8-g56de2be`)** — the tables above are this
run: three harness passes on the benchmarks machine (Darwin 24.6, Apple M3),
minimum across all three, all engines byte-identical before timing. Two
commits landed since the v3.6.0 sitting — the regex compile cache plus the
128-bit byteset mask (`1a14c23`), and `Value` shrinking from 344 to 200 bytes
by moving the cold fields behind a lazy block (`56de2be`). Against the
2026-08-21 rows:_

| kernel | interp 08-21 → 08-22 | | `--exe` 08-21 → 08-22 | |
|---|---:|---:|---:|---:|
| regex    | 113.4 → 44.8 ms | **−60%** | 79.8 → 23.8 ms  | **−70%** |
| sortnums |  53.2 → 38.8 ms | **−27%** | 35.0 → 23.4 ms  | **−33%** |
| arrayops | 102.4 → 78.4 ms | **−23%** | 102.6 → 78.5 ms | **−23%** |
| loopsum  | 199.6 → 192.9 ms | −3%     | 26.5 → 17.0 ms  | **−36%** |
| streq    | 462.5 → 416.1 ms | −10%    | 44.5 → 32.4 ms  | **−27%** |
| fib      | 532.6 → 506.0 ms | −5%     | 150.7 → 106.1 ms | **−30%** |
| hashfill | 178.0 → 161.4 ms | −9%     | 63.7 → 50.4 ms  | **−21%** |
| hash     |  30.1 → 28.3 ms | −6%      | 10.2 → 8.1 ms   | **−21%** |
| strcat   |  14.8 → 14.5 ms | −2%      | 2.9 → 2.7 ms    | −7% |
| bigint   |  30.7 → 32.6 ms | +6%      | 29.3 → 29.7 ms  | +1% |

_Read the deltas knowing the reference engines moved the other way in this
sitting: Rakudo is 4–7% **slower** here than on 2026-08-21 on every kernel
(`fib` 443.3 → 459.0, `hash` 213.5 → 222.4, `hashfill` 418.8 → 455.0) and
`perl` likewise (95.5 → 103.9). The desktop was not fully idle — the same
ambient load sits on the Raku++ rows too, so the drops above are, if anything,
understated. `regex` is the one row where a specific fix is being measured;
the broad `--exe` movement follows the `Value` shrink. `bigint`, whose time is
almost entirely inside `BigInt` multiply, is the one kernel that drifts the
wrong way, within the noise the Rakudo column itself shows._

_**2026-08-22 re-snapshot, second sitting of the day (`v3.6.0-12-g363c4b6`)** —
the tables above are this run: three harness passes on the benchmarks machine
(Darwin 24.6, Apple M3), minimum across all three, all engines byte-identical
before timing. One commit landed since the sitting above — `363c4b6`, `Value`
dropping from 200 to 128 bytes as five payload pointers collapse into one
tagged slot. Against the earlier 2026-08-22 rows:_

| kernel | interp 08-22a → 08-22b | | `--exe` 08-22a → 08-22b | |
|---|---:|---:|---:|---:|
| arrayops |  78.4 → 65.7 ms | **−16%** | 78.5 → 64.8 ms | **−17%** |
| sortnums |  38.8 → 33.6 ms | **−13%** | 23.4 → 18.5 ms | **−21%** |
| hashfill | 161.4 → 142.0 ms | **−12%** | 50.4 → 38.1 ms | **−24%** |
| streq    | 416.1 → 369.5 ms | **−11%** | 32.4 → 20.4 ms | **−37%** |
| hash     |  28.3 → 25.2 ms | **−11%** | 8.1 → 6.9 ms   | **−15%** |
| fib      | 506.0 → 456.7 ms | **−10%** | 106.1 → 85.1 ms | **−20%** |
| strcat   |  14.5 → 13.3 ms | −8%      | 2.7 → 2.6 ms   | −4% |
| loopsum  | 192.9 → 178.4 ms | −8%     | 17.0 → 13.3 ms | **−22%** |
| bigint   |  32.6 → 31.2 ms | −4%      | 29.7 → 30.2 ms | +2% |
| regex    |  44.8 → 43.4 ms | −3%      | 23.8 → 24.6 ms | +3% |

_The reference engines say this one is the code, not the machine: Rakudo moved
by at most 1.6% on any kernel between the two sittings (`fib` 459.0 → 459.2,
`hash` 222.4 → 222.9, `hashfill` 455.0 → 454.2) and `perl` by 2.8% (103.9 →
106.8, i.e. the *wrong* way), while all ten Raku++ interpreter rows dropped
(3–16%) and eight of the ten compiled rows dropped (4–37%). A smaller `Value` is a cache-footprint change, so
the kernels that move most are the ones that hold many live values at once —
`arrayops`, `sortnums`, `hashfill` — and `bigint`/`regex`, whose time sits
inside one runtime routine, sit still (their +2/+3% on the `--exe` side is
inside the spread the three passes themselves showed). `fib` interpreted
crossed Rakudo in this sitting: 456.7 vs 459.2 ms, which the tables call level
rather than a win. `tools/perf-guard.raku --check` is green against the
v3.14.0 baseline with 8–30% of headroom on all seven gated kernels._

_**2026-08-22 re-snapshot, third sitting of the day (`v3.6.0-19-g4c0e80b`)** —
the tables above are this run: three harness passes on the benchmarks machine
(Darwin 24.6, Apple M3, Apple clang 17.0.0), minimum across all three, engines
interleaved within each round, all byte-identical before timing. The commit
that matters is `d1e9082`, lexical pads: a `my` that a resolution pass can
prove is dominated by its declaration is read and written by frame slot — one
load, no hashing, no scope-chain walk — and a flat loop keeps one scope instead
of building a fresh one per iteration. Against the sitting above:_

| kernel | interp 08-22b → 08-22c | | `--exe` 08-22b → 08-22c | |
|---|---:|---:|---:|---:|
| loopsum  | 178.4 → 105.2 ms | **−41%** | 13.3 → 13.9 ms | +5% |
| hash     |  25.2 → 18.1 ms | **−28%**  | 6.9 → 7.3 ms   | +6% |
| strcat   |  13.3 → 9.6 ms  | **−28%**  | 2.6 → 2.9 ms   | +12% |
| hashfill | 142.0 → 111.9 ms | **−21%** | 38.1 → 37.2 ms | −2% |
| streq    | 369.5 → 312.2 ms | **−16%** | 20.4 → 20.3 ms | −1% |
| regex    |  43.4 → 39.8 ms | −8%       | 24.6 → 24.9 ms | +1% |
| fib      | 456.7 → 420.9 ms | −8%      | 85.1 → 85.8 ms | +1% |
| bigint   |  31.2 → 31.6 ms | +1%       | 30.2 → 30.7 ms | +2% |
| sortnums |  33.6 → 33.8 ms | +1%       | 18.5 → 18.9 ms | +2% |
| arrayops |  65.7 → 65.4 ms | −0%       | 64.8 → 64.0 ms | −1% |

_The shape is exactly what a variable-access change should draw, which is the
best evidence that it is one. Everything that spends its time **touching
variables** moves hard — `loopsum`, `hash`, `strcat`, `hashfill` — and
everything whose time sits **inside one runtime method** does not move at all:
`arrayops` (`.grep`/`.map`), `sortnums` (`.sort`) and `bigint` (`BigInt`
multiply) are flat to within 2%. The `--exe` column is flat by construction:
codegen already kept variables in C++ locals, so there was nothing there for
pads to win._

_The `+1` to `+12%` on the small `--exe` rows is **startup**, not codegen.
Interpreted startup went 2.2 → 2.8 ms and native 2.1 → 2.4 ms — laying out a
pad is a fixed per-process cost — and every native row here includes process
startup, so the shortest ones (`strcat` at 2.9 ms, `hash` at 7.3 ms) show that
0.3 ms as a percentage. It is a real cost and it is recorded rather than
absorbed; on any kernel that runs longer than a few milliseconds it is repaid
many times over._

_Reference engines: Rakudo moved by at most 1.2% on any kernel and `perl` is
4.5% **faster** here (106.8 → 102.0 ms), so the machine was, if anything,
slightly better this sitting — which understates nothing but does mean the
interpreter drops of 16–41% are not a fast-box artefact.
`tools/perf-guard.raku --check` is green against the v3.14.0 baseline with
17–51% of headroom: `hash` −50.8%, `loopsum` −46.5%, `fib` −35.4%, `asg`
−34.2%. Two rows crossed in this sitting — interpreted `fib` is a Raku++ win
at 1.1× rather than the tie it was that morning, and interpreted `hashfill` is
within 10% of `perl`._
