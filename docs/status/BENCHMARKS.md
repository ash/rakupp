# Raku++ vs Rakudo vs mutsu — speed

A small, honest performance comparison on the subset of Raku that **every**
engine here runs identically. This is not a claim that Raku++ is "better" —
Rakudo is the mature, complete, production reference implementation, and Raku++
is [far behind it on Roast coverage](ROAST.md), and behind
[mutsu](https://github.com/tokuhirom/mutsu) too. The point is only to give a
fair picture of where Raku++ — as both a tree-walking interpreter and a native
compiler — lands against an optimizing VM and against the other from-scratch
implementation.

Raku++ can run a program two ways that differ in speed, and this compares both
against two other engines:

- **interp** — Raku++ interpreting the source (tree-walk)
- **native** — Raku++ `--exe`: the program transpiled to C++ and compiled to a
  native binary (no interpreter inside)
- **mutsu** — mutsu interpreting the source (Rust bytecode VM + Cranelift JIT,
  the JIT on by default as it ships)
- **rakudo** — Rakudo interpreting the source (MoarVM + JIT)

**mutsu is new to this file as of the 2026-08-31 sitting**, which is also the
first sitting in which every engine on this page is a native arm64 binary. The
Rakudo column was re-measured that day against from-source builds of 2026.06,
2026.07 and 2026.08; earlier revisions of this file measured it with an x86_64
Rakudo under Rosetta 2, so **the Rakudo figures here are not comparable with
those in any earlier revision** — the Raku++ columns are. The methodology says
what changed and what it cost.

Raku++ has two other standalone-binary modes — `--bundle` and `--aot` — but both
*tree-walk* the program, so they run at **`interp` speed** and aren't shown
separately (`--aot` fib runs at interp's ~770 ms). `--exe` is the only
mode that changes runtime performance, so it's the one the `native` column
tracks.

(A fourth environment — **[Raku.js](../../rakujs)**, the interpreter compiled to
WebAssembly — is measured against `interp` on these same kernels under Node,
Bun, and the browser in
[rakujs/INTERNALS.md](../../rakujs/INTERNALS.md#performance-vs-native-and-node-vs-bun-vs-browser):
1.3–6.8× slower than native on a clean host, dominated by the `-fexceptions`
call trampolines. That comparison is still experimental — see the status note
there.)

## The short version

- **Startup:** ~2–3 ms on this machine (3.0 ms interpreting, 2.5 ms native) —
  a tiny native binary with no VM to spin up. For one-liners, CLI glue, and
  small programs it is instant. mutsu starts in 4.4 ms; Rakudo in 75.6. The
  two newer engines are on one side of that gap and the reference on the other.
- **Native (`--exe`) beats Rakudo on fourteen of the fifteen kernels** — from
  3.2× on `arrayops` to 16.7× on `loopsum`, 17.0× on `hash`, and 29.1× on
  `strcat`. On the fifteenth it falls 1.6× short. Compiling removes interpreter
  overhead.
- **The interpreter beats Rakudo on fourteen of the fifteen too**, `fib` and
  `streq` included since 2026-08-22 — both had been Rakudo's for the whole life
  of this file, and both are thin (1.3×, 1.2×), so read them as "level, our
  side" rather than as leads.
- **Against mutsu, the interpreter wins twelve of fifteen and `--exe` wins
  fourteen.** The three the tree-walker loses are `bigint` (3.5×), `fib` (1.4×)
  and `sortnums` (1.1×); compiled, only `bigint` still goes their way. See
  "What mutsu is faster at".
- **`bigint` is the one clear loss, and it was predicted.** mutsu links
  `num-bigint`; we hand-rolled ours because we take no dependencies, and it is
  3.5× slower interpreted and 3.3× compiled. `--exe` does not help — the time
  is inside the runtime's multiply, not the loop around it.
- **The fifteenth is `objects`, and Rakudo leads it by 1.7×** — but it leads
  mutsu on that kernel by **5.3×**. It is the only kernel that measures
  `class`/`has`/method dispatch, the shape most real Raku code is written in,
  and neither from-scratch engine is close to `spesh`. Adding the kernel is
  what found it; nothing is profiled yet.
- Compiling still widens `fib` and `streq`: `--exe` puts them 5.5× and 13.9×
  ahead of Rakudo (string `eq`/`lt` compile to inline byte-compares — see
  [internals/DISPATCH.md](../internals/DISPATCH.md) for the dispatch story),
  and 2.9× and 29.4× ahead of mutsu.
- The `loopsum` loop kernel gained most when lexical pads landed; against a
  native reference it reads **2.7×**. `hashfill`, one of the two kernels with a Perl 5 twin, reads 2.6×
  this sitting — the kernel is the noisiest in the set (allocation-bound), so
  the ladder in "vs Perl 5" below is the row to read, not this one.
- **String building (`~=`) appends in place** in every mode, so `strcat` is
  O(n) rather than O(n²) — 10.2× ahead of Rakudo and 12.4× ahead of mutsu even
  interpreted.

## Methodology

- **Machine:** macOS (Darwin 24.6, Apple Silicon M3), re-measured 2026-08-31 at
  `v3.23.0-45-gb6905bf` — the SAME machine as every earlier revision
  of this file, so the rows are comparable with the 2026-08-24, the 2026-08-22
  and the 2026-08-21 ones. The reference lanes are the proof of that: against
  the 2026-08-24 sitting Rakudo moved within ±4.3% on every shared kernel and
  `perl` by 7.8%, while the Raku++ lanes moved -11% to +3% — a spread that is
  code (45 commits of it), not machine. The box was **not** idle by the
  1-min < 2.5 rule during this sitting (load hovered ~4, `mediaanalysisd`
  pegging a core); that rule was overridden on evidence rather than waived — a
  repeatability probe read max/min = 1.028 on `hash` over nine consecutive
  runs, and `hash` interp itself landed at 18.8 ms against the previous
  sitting's 18.9 ms. The load was background-QoS work, which Apple Silicon
  schedules on the efficiency cores and which therefore does not contend with a
  benchmark on the performance cores. (An earlier
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
  the previous one: on the ten kernels both sittings share, Rakudo lands
  within ±1.6% of its previous row and `perl` within 2%, so the Raku++ movement
  below is the code, not the machine. The rev named above is the last **code** commit measured, not
  the commit that records the tables — a write-up commit gets a new SHA every
  time the branch is rebased, and one sitting was already left naming a SHA
  that no longer existed. The harness REFUSES
  a rakupp binary built for another architecture, the same guard perf-guard
  carries: a stale x86_64 `build/` beside a `build-arm64/` measures under
  Rosetta at a uniform 1.7–2× penalty, and exactly that nearly shipped in
  this file at v3.14.0. (Rows are not comparable across doc revisions — absolute times
  shift a few percent with machine state; the Rakudo column, measured every
  time, is the fixed yardstick. A per-iteration `std::function` allocation that
  had crept into the loop path over the v0.7.1→v0.9.0 cycle was found by bisect
  and removed, restoring the tight-loop kernels to their v0.7.1 speed.)
- **Raku++:** built `-O3 -DNDEBUG` (CMake Release).
- **mutsu:** 0.23.0, `cargo build --release` at upstream `a093272`, default
  features (Cranelift JIT on, as it ships). Native **arm64** — see "The mutsu
  lane" above for why that needed a second Rust toolchain, and why an x86_64
  build would have silently flattered every Raku++ row here.
- **Rakudo:** `raku` v2026.08 (MoarVM backend), the oracle era this release
  verifies against. The previous revision of this file measured v2026.07; the
  reference column moved a few percent in both directions across the upgrade
  (`strcat` 179.9 → 166.3 ms, `loopsum` 261.7 → 276.4 ms), which is within
  ordinary sitting-to-sitting spread, so read the change as noise rather than
  as Rakudo getting faster or slower.
- **The Rakudo column is measured against a native arm64 build**, as of the
  2026-08-31 re-measurement. Every revision of this file *before* that one
  measured Rakudo with the Intel Homebrew build (`/usr/local`), an **x86_64**
  binary translated on an arm64 host, because it was the only Rakudo installed
  here. The reference is now built from source at each era tag — 2026.06,
  2026.07 and 2026.08, one toolchain for all three — so a step at an era
  boundary cannot be a packaging artefact.

  **Which arm64 Rakudo it is matters, not just which version.** A from-source
  build and homebrew/core's `arm64_sequoia` bottle of the *same* v2026.08
  differ by up to 12% per kernel, in different directions — `fib` 11% faster
  from source, `strcat` 9% faster, `loopsum` 6% slower — reproduced to within
  1% across three separate runs. On `fib` that is the difference between
  Raku++ level and Raku++ 1.1× behind, so the reference build is named here,
  not just its version. The bottle lives at `/opt/homebrew/bin/raku` and has
  to be asked for by path, since `/usr/local/bin` precedes `/opt/homebrew/bin`
  on this machine's `$PATH` and bare `raku` still resolves to the x86_64 build.

  **MoarVM has no JIT backend on arm64.** Its JIT sources are x64-only: the
  generated Makefile consumes `$(JIT_STUB)` (`src/jit/stub.o`) and never
  `$(JIT_ARCH_X64)`, and the arm64 `libmoar` exports 18 JIT symbols against the
  x86_64 build's 148, with none of the emit/tile/DynASM machinery. Both the
  Homebrew bottle and the from-source builds agree. So the reference here runs
  spesh but no machine code, while the old translated reference ran the full
  x64 JIT under Rosetta. The two are not the same engine minus a penalty, and
  an x86_64 `rakudo` column from another machine is not comparable with this
  one.

  **What translation cost, for the record** — measured on both builds of
  v2026.08, interleaved, best of 6, on 2026-08-31, and *net* of the missing
  JIT rather than a pure translation cost:

  | penalty | kernels |
  |---|---|
  | 1.7–1.9× | `startup` 1.84×, `strcat` 1.76× |
  | 1.3–1.7× | `hash` 1.67×, `sortby` 1.55×, `bigint` 1.48×, `textsplit` 1.44×, `regex` 1.42×, `hashfill` 1.37×, `arrayops` 1.35×, `fib` 1.33× |
  | 1.0–1.3× | `loopsum` 1.26×, `rats` 1.23×, `objects` 1.23×, `arraypush` 1.21×, `sortnums` 1.04×, `streq` 1.00× |

  Mean 1.38×, and per-kernel: `streq` (1.00×) and `sortnums` (1.04×) were free,
  which is what "net of the missing JIT" looks like — those are kernels where
  the x64 JIT paid enough to cancel Rosetta's cost. Nothing in the harness
  catches a mis-built *reference*, because the arch guard inspects `$RAKUPP`
  only; `tools/run-bench.raku` now records the Rakudo binary it used.
- **Harness:** [`tools/run-bench.raku`](../../tools/run-bench.raku) — itself a Raku
  program, run *by Raku++* (it also runs under Rakudo). It only spawns each
  engine as a fresh subprocess and times it with `now`, so the language the
  harness is written in does not bias either contestant.
- **Timing:** 7 runs per benchmark, first discarded as warm-up, **minimum of the
  remaining 6** reported (least noisy; the mean was within a few percent). For
  the `native` column each program is compiled with `--exe` once, then the
  resulting binary is timed — the compile step is not counted.
- **Fairness:** the harness runs each program under every engine and compares
  their stdout **before** timing anything; if `interp`, `native`, or `rakudo`
  don't emit byte-identical output it flags the row and exits non-zero, so a
  divergent kernel can't be silently benchmarked. (Rakudo is the reference;
  every benchmark program is deterministic, so identical output is expected.)
  A **mutsu** disagreement is reported beside the row but deliberately does not
  fail the run — this harness gates *our* lanes, and a third-party engine's
  result is a reference point, not a defect in our suite. In this sitting
  nothing was flagged: **all four engines produced byte-identical output on all
  sixteen kernels**, so every row below is a like-for-like comparison.
- **Harness overhead:** spawning + capturing a subprocess adds a small fixed
  cost per run. On top of that each engine pays its *own* process startup —
  negligible for Raku++'s native binary, but Rakudo loads a full precompiled
  runtime, a fixed cost included in every row below. See "How to read this".
- **Perl 5:** a bench may ship a `.pl` twin (`hashfill` and `textsplit` do); the harness then
  also times `perl` on it — same spawn-and-capture, same byte-identical-output
  gate — and prints a `perl` column (`—` for rows without a twin). Override the
  binary with `PERL=`.

### The mutsu lane

`tools/run-bench.raku` can also time **[mutsu](https://github.com/tokuhirom/mutsu)**,
an independent Raku implementation in Rust — a bytecode VM with a Cranelift JIT
(on by default). It is a genuinely different set of engineering trade-offs from
either engine already in this table, which is what makes it worth measuring
against: Rakudo tells us where a mature optimising VM lands, and mutsu tells us
where a second from-scratch implementation lands.

The lane is **optional and auto-discovered** — `$MUTSU`, then `mutsu` on `PATH`,
then `$HOME/mutsu/target/release/mutsu`. On a machine with no mutsu the column
reads `—` and nothing else changes. Rakudo remains the correctness oracle for
every lane, and a mutsu disagreement is reported beside the row but never turns
the harness's own gate red: this harness gates *our* lanes, and another
implementation's result is a reference point, not a defect in our suite.

**The tables below carry the mutsu column as of the 2026-08-31 sitting** — the
bench-machine sitting the lane was waiting for. mutsu 0.23.0, built from source
at `a093272` with the release profile and its default features, so the Cranelift
JIT is compiled in and live: `MUTSU_VM_STATS=1` on the `fib` kernel reports
`compiles=1 entries=1663980` with `MUTSU_JIT` unset, and zero with
`MUTSU_JIT=off`. The lane therefore measures mutsu **as it ships**, not an
interpreter-only configuration of it.

**Building it native took a toolchain that was not on this machine.** The only
Rust here was the x86_64 Homebrew one (`/usr/local`, host
`x86_64-apple-darwin`), which has no aarch64 std at all — it cannot produce an
arm64 binary, and a first build attempt duly failed at the link step *for
x86_64*. An arm64 Rust plus arm64 `pcre2` and `libffi` (`/opt/homebrew`) were
installed for this sitting; `file` on the result reads `Mach-O 64-bit
executable arm64`. This matters more than it sounds: an x86_64 mutsu would have
run under Rosetta and flattered every row, with nothing in the harness to catch
it. How much by is not predictable from the Raku++ figure — the penalty turns
out to be engine- and kernel-dependent (1.0–1.8× on Rakudo, measured; see the
methodology), so the only safe move is not to translate the binary at all.
**Check `file` on the mutsu binary before believing any row below**, exactly as
the harness already does for `$RAKUPP`.

## Pending re-measurement: four changes landed after this sitting (2026-08-31)

**Every table below predates these, and none of them is folded in.** The rows in
this file are all measured on the machine named under Methodology, and the work
described here was done on a different one (Apple M1, Darwin 25.5 — the box this
file already warns runs a uniformly slower Raku++ binary). Splicing numbers from
it into these tables would break the one property that makes revisions
comparable, so the tables stand as measured and the next bench-machine sitting
picks the changes up. What follows is the M1 before/after for each, from the
same harness kernels, best of 9:

| kernel | lane | before | after | mutsu (same box) |
|---|---|---:|---:|---:|
| bigint   | `--exe` | 45.2 ms | **11.4 ms** | 11.5 ms |
| bigint   | interp  | 60.4 ms | **12.6 ms** | 11.5 ms |
| sortnums | `--exe` | 26.4 ms | 22.6 ms | 40.6 ms |
| loopsum  | `--exe` | 16.5 ms | 15.1 ms | 158.4 ms |
| fib      | interp  | 447.5 ms | 424.4 ms | 308.7 ms |

Four changes, all described in
[internals/OPTIMIZATION.md](../internals/OPTIMIZATION.md) under its list of
non-`-O` defaults:

- **A one-limb `BigInt` multiply** whose carry stays in a register, with the limb
  product split off the carry chain. `bigint` is 90% inside that loop by profile;
  it is the whole 4× above, and it lands in every mode because it is a runtime
  change rather than a codegen one. It closes the one kernel where mutsu led
  `--exe`, which is what prompted the work.
- **`.sort` on an all-native-Int list** orders a flat `(key, index)` array instead
  of calling `valueCmp` through two random probes into an array of `Value`s.
- **`applyArith` takes the operator as a `const char*`**, so compiled code stops
  building a `std::string` per operator to have its first byte read.
- **The `&name` lexical key of a `Call` is built once** and published on the node,
  rather than concatenated on each of `fib`'s 1.6 M calls.

Two things measured on that box are worth recording even though they are not
changes to the engine:

- **`--exe -O` is a large, unmeasured lane.** This file's `native` column is
  plain `--exe`; the harness never passes `-O`. On the same M1, `-O` takes `fib`
  from 92.2 to **21.2 ms**, `loopsum` from 15.1 to 8.1, `arrayops` from 84.5 to
  58.4, and `streq` from 25.6 to 18.1. `-O` is documented as
  semantics-preserving and opt-in; whether the published column should measure
  it, both, or stay as it is, is a decision this file has not taken.
- **Caching `tctx_` in the big dispatch functions is a LOSS.** `_tlv_get_addr` is
  the top leaf of an interpreted `fib` profile at ~18%, and `execBlock` and
  `callCallableRaw` already take one resolution per call for exactly that reason.
  Extending the same cut to `exec`, `eval`, `evalBinary` and `evalUnary` made
  `fib` *worse* — 425 → 478 ms — because those are dispatch switches whose
  branches mostly touch `tctx_` zero or one times, so binding the reference at
  entry pays the thread-local call on every path and holds a register across
  2 500 lines. Removing the thread-local qualifier outright (unsafe; measured
  only as a ceiling) buys 12% on `fib` and 21% on `loopsum`, so the cost is real
  — but it is not recoverable this way.

## Results

Best of 6 timed runs per engine (7 spawned, the first discarded as warm-up),
taken across three full harness passes and minimised across them; includes
process startup; lower is better. Rows are ordered most-Raku++-favourable
first. `startup` has its own section below rather than a row here — it is
process startup, not a workload.

### Interpreter vs Rakudo and mutsu

The tree-walker wins on **eleven of these fifteen kernels** against Rakudo, is
level on two (`streq`, `rats`), and loses two. The larger loss is `objects`,
and it is the point of the row: it is the only kernel that measures
`class`/`has`/method dispatch — the shape most real Raku code is written in —
and Rakudo leads it by **2.4×**. That is not a machine artefact; see below.
The other loss is `fib`, by 1.1×.

These are measured against a **native arm64 Rakudo**, which is what changed the
count from the fourteen-of-fifteen this file reported while the reference ran
under Rosetta 2 — see the methodology. The Rakudo column is measured **once
per Rakudo release**, not once per Raku++ release: Rakudo ships monthly, so it
is a constant between its own releases and any variation in it was noise. Each
value is the minimum of 80 runs across four interleaved passes.

Against **mutsu** the interpreter wins twelve of fifteen, is level on one
(`sortby`), and loses three: `bigint` by 3.5×, `fib` by 1.4×, `sortnums` by
1.1×. Those three are the informative ones and each has a different cause —
see "What mutsu is faster at" below. Bold marks a Raku++ lead; otherwise the
winning engine is named.

The two rows Rakudo had held for the whole life of this file were reported as
crossing on 2026-08-22 — `fib` (tiny-body recursion) with the `Value` shrink
and lexical pads, `streq` (1M string comparisons) with the TARG assignment
slice. Against a native reference neither crossing holds: `streq` is level and
`fib` is a 1.1× Rakudo lead. Both are tiny-body kernels where a JIT should be
at its best, and it is worth naming what the reference does here — MoarVM's JIT
backend is x64-only, so on arm64 it compiles `src/jit/stub.o` and runs with
spesh but no machine-code JIT.

Variable handling is what moved all of it. Resolving a `my` to a frame slot
instead of a hash lookup halved `loopsum` (1.5× → 2.4× against Rakudo) and took
about a quarter off `hash` and `strcat`; deciding a plain `$padvar = EXPR` once
and skipping the assignment ceremony took a fifth off `streq`. What did *not*
move, in either change, is `arrayops`, `sortnums` and `bigint` — whose time is
inside a runtime method rather than in touching variables — and that is the
clearest sign the wins are where they claim to be.

| Benchmark | Raku++ (interp) | mutsu | Rakudo | vs Rakudo | vs mutsu |
|---|---:|---:|---:|---|---|
| strcat    |   8.6 ms |  106.9 ms |  87.4 ms | **10.2×** | **12.4×** |
| hash      |  18.8 ms |   42.0 ms | 123.8 ms | **6.6×** | **2.2×** |
| sortnums  |  33.7 ms |   30.4 ms | 189.4 ms | **5.6×** | mutsu 1.1× |
| bigint    |  31.4 ms |    9.1 ms | 160.9 ms | **5.1×** | mutsu 3.5× |
| sortby    |  32.2 ms |   32.8 ms | 163.4 ms | **5.1×** | level |
| regex     |  41.1 ms |  238.5 ms | 182.2 ms | **4.4×** | **5.8×** |
| arrayops  |  59.9 ms |   97.7 ms | 189.0 ms | **3.2×** | **1.6×** |
| textsplit |  64.7 ms |  246.3 ms | 184.5 ms | **2.9×** | **3.8×** |
| loopsum   |  86.2 ms |  123.6 ms | 230.4 ms | **2.7×** | **1.4×** |
| hashfill  | 109.5 ms |  392.9 ms | 284.1 ms | **2.6×** | **3.6×** |
| arraypush | 129.4 ms |  383.3 ms | 262.5 ms | **2.0×** | **3.0×** |
| streq     | 232.2 ms |  609.3 ms | 231.6 ms | level | **2.6×** |
| rats      | 243.0 ms |  367.6 ms | 236.6 ms | level | **1.5×** |
| fib       | 352.7 ms |  245.6 ms | 311.4 ms | Rakudo 1.1× | mutsu 1.4× |
| objects   | 498.0 ms | 1585.1 ms | 207.2 ms | Rakudo 2.4× | **3.2×** |

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
undoing the regression. **The tables above carry the fix**: interpreted `regex` reads
41.1 ms against v3.6.0's 113.4 and v3.14.0's 88.5, and the native row 27.4 ms
against 79.8 — so the kernel ends up roughly half of what it cost before the
regression was ever introduced, and `regex` moves from the second-worst
interpreter row to the fifth-best.

### Native (`--exe`) vs Rakudo and mutsu

Compiling removes interpreter overhead on top of that — pushing every row
ahead of Rakudo except one: `objects` compiled is 336.7 ms against Rakudo's
**interpreter** at 207.2, so Rakudo still leads that row by 1.6×. Against mutsu
the compiled binary wins **fourteen of fifteen**, `bigint` being the sole
exception and by the same 3.3× it costs the interpreter. The last column is the
speed-up over interpreting the same program.

| Benchmark | Raku++ (`--exe`) | mutsu | Rakudo | vs Rakudo | vs mutsu | vs interp |
|---|---:|---:|---:|---|---|---:|
| strcat    |   3.0 ms |  106.9 ms |  87.4 ms | **29.1×** | **35.6×** | 2.9× |
| hash      |   7.3 ms |   42.0 ms | 123.8 ms | **17.0×** | **5.8×** | 2.6× |
| loopsum   |  13.8 ms |  123.6 ms | 230.4 ms | **16.7×** | **9.0×** | 6.2× |
| streq     |  20.7 ms |  609.3 ms | 231.6 ms | **11.2×** | **29.4×** | 11.2× |
| sortnums  |  19.1 ms |   30.4 ms | 189.4 ms | **9.9×** | **1.6×** | 1.8× |
| sortby    |  21.3 ms |   32.8 ms | 163.4 ms | **7.7×** | **1.5×** | 1.5× |
| hashfill  |  38.6 ms |  392.9 ms | 284.1 ms | **7.4×** | **10.2×** | 2.8× |
| regex     |  27.4 ms |  238.5 ms | 182.2 ms | **6.6×** | **8.7×** | 1.5× |
| bigint    |  30.2 ms |    9.1 ms | 160.9 ms | **5.3×**  | mutsu 3.3× | 1.0× |
| textsplit |  38.7 ms |  246.3 ms | 184.5 ms | **4.8×**  | **6.4×** | 1.7× |
| arraypush |  55.3 ms |  383.3 ms | 262.5 ms | **4.7×**  | **6.9×** | 2.3× |
| fib       |  85.4 ms |  245.6 ms | 311.4 ms | **3.6×**  | **2.9×** | 4.1× |
| arrayops  |  58.7 ms |   97.7 ms | 189.0 ms | **3.2×**  | **1.7×** | 1.0× |
| rats      | 148.8 ms |  367.6 ms | 236.6 ms | **1.6×**  | **2.5×** | 1.6× |
| objects   | 336.7 ms | 1585.1 ms | 207.2 ms | Rakudo 1.6× | **4.7×** | 1.5× |

### What mutsu is faster at

Three rows go the other way, and they are worth more than the twelve that do
not, because each one names something specific.

**`bigint` — 9.1 ms against our 31.4 interpreted and 30.2 compiled, so 3.5× and
3.3×.** This is the cleanest result in the sitting and the least surprising:
mutsu links [`num-bigint`](https://crates.io/crates/num-bigint), a mature,
widely-used, well-tuned arbitrary-precision library, and Raku++ uses the BigInt
it hand-rolled in-tree because it takes no third-party dependencies. The
[implementations FAQ](../guide/faq/implementations.md) predicted exactly this
trade before it was measured — "`bigint` is a kernel where a well-tuned
external library is simply faster than what we hand-rolled" — and here is the
number. Note the shape of it: `--exe` does not help (1.0× over interp), because
the time is inside the runtime's own multiply, not in interpreting the loop
around it. That is the same reason `arrayops` is flat under compilation, and it
is the honest limit of the `--exe` answer to performance.

**`fib` — 245.6 ms against our 352.7 interpreted, so 1.4×.** This is the
Cranelift JIT doing the thing a JIT is for: tiny-body recursion, the same
function entered 1.66 million times, and by the end it is running compiled
machine code where we are still walking a tree. It is also the row where our
two answers to performance separate most clearly — `--exe` compiles the same
program to 85.4 ms, which is 2.9× *faster than mutsu's JIT*. Ahead-of-time
beats just-in-time here because the C++ compiler has unlimited time to optimise
and the program is small enough to hand it whole.

**`sortnums` — 30.4 ms against our 33.7 interpreted, so 1.1×.** Level, in
practice: both are calling a library sort on 50,000 integers, and the row is
mostly a measure of the two sorts. `--exe` takes it back at 19.1 ms.

The pattern in the other direction is just as consistent. mutsu's weakest rows
here are `objects` (1585.1 ms, 3.2× behind our interpreter and 5.3× behind
Rakudo), `streq` (609.3 ms, 2.6× behind), `hashfill` and `arraypush` (~3×), and
`strcat` (106.9 ms against our 8.6 — 12.4×, the widest gap in the table, and
the row where our in-place `~=` append turns an O(n²) into an O(n)). A bytecode
VM removes dispatch overhead; it does not by itself make the string, hash and
object *runtimes* underneath fast, and that is where these five kernels spend
their time.

**Reading the `vs interp` column:** compiling helps most where a tree-walker
hurts — `streq` 12.6× (per-node walking around what is, after the fast path, a
trivial byte-compare — see [internals/DISPATCH.md](../internals/DISPATCH.md)), `loopsum` 7.1×,
`fib` 4.4× (both re-dispatch a tiny body a huge number of times). Every one of
those margins has been NARROWING since lexical pads and the TARG slice landed —
`streq` was 18.1× three sittings ago and `fib` 5.4×. The compiler already kept
variables in C++ locals, so this is the gap closing from the interpreter's
side, not codegen slowing down.
It's a near no-op (1.0–1.8×) for the workloads whose time is spent *inside* runtime
methods — `arrayops`/`sortnums` (`.grep`/`.map`/`.sort`) and especially
`bigint`, which lives almost entirely in `BigInt` multiply. There the driving
loop is trivial, so removing interpreter overhead changes little. `objects` at
1.7× is the interesting one in this column: compiling a method call gains about
as much as compiling a `Rat` loop, which is to say the cost is in the
dispatcher both modes share, not in the tree-walk around it.

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

### `objects`: the one kernel Rakudo leads — and it leads both of us

Five kernels landed on 2026-08-22 to close value classes the set had never
measured: `rats` (Rational arithmetic), `objects` (`class`/`has`/method
dispatch), `arraypush` (eager array mutation), `sortby` (`.sort` with a 1-ary
key extractor) and `textsplit` (split into fields, reorder, rejoin — the second
kernel with a `.pl` twin). Their first sitting was taken on an M1/Darwin 25.5
box; the rows in the tables above are the re-measure on the M3/Darwin 24.6
benchmarks machine, so they are now directly comparable with every other row.

Four of the five are wins. `objects` is not, and it was the reason to add them:

| | interp | `--exe` | mutsu | Rakudo | |
|---|---:|---:|---:|---:|---|
| M1 / Darwin 25.5, first sitting | 568.5 ms | 314.4 ms | — | 254.4 ms | Rakudo 2.2× |
| M3 / Darwin 24.6, 2026-08-24    | 518.8 ms | 285.1 ms | — | 285.6 ms | Rakudo 1.8× |
| M3 / Darwin 24.6, these tables  | 498.0 ms | 336.7 ms | 1585.1 ms | 207.2 ms | Rakudo 2.4× |

The first two Rakudo cells were measured under Rosetta 2 and the third against
a native arm64 build, so only the Raku++ columns are comparable down the table;
the Rakudo column changes scale at the last row.

**The machine was not the explanation.** The first sitting's note estimated
that correcting for the box would narrow the gap to "roughly 1.5×"; measured
against a native arm64 Rakudo it is **2.4×**, so the loss is real and larger
than the correction predicted — and larger than this file reported while the
reference was translated. Compiling does not rescue it either: `--exe` at
336.7 ms does not reach Rakudo's *interpreter* at 207.2, the only row in these
tables where that is true. The compiled side agrees independently — `methodcalls`
under `-O` gains 1.0×, i.e. the optimizer has nothing to give a monomorphic
method call yet, because it is not devirtualized.

**mutsu makes the shape of this much clearer, and it is less a Raku++ defect
than a Rakudo achievement.** The second from-scratch engine does not merely
lose this kernel — it loses it by **5.3×** against Rakudo and by 3.2× against
our tree-walker, its single worst row in the sitting, with the Cranelift JIT
switched on. So the ranking on the one kernel that measures `class`/`has`/
method dispatch is Rakudo, then Raku++, then mutsu: both engines built from
scratch in the last year are a long way behind the mature one. That is a good
argument that what Rakudo has here is not a small implementation advantage but
`spesh` — type-specialising dispatch on observed types and inlining through it,
precisely the optimisation neither newer engine has. It also means our 1.7× is
the *closest* any from-scratch implementation currently gets, which is worth
knowing before reading the row as a straightforward deficiency.

It is still unexplained on our side. Nothing here has been profiled, and the
honest statement remains the one the kernel was added to make: the one hot path
never covered by a kernel is the one that turns out to be slow. `objects` is
200k `.new` plus 300k method calls plus 500k attribute reads, and Raku++ is
behind Rakudo on all of that.

**`textsplit` is where `perl` still wins**, though by less than the first
sitting suggested: interpreted it is 1.9× behind `perl` here (the M1 sitting
read 3.7× raw and estimated 2.3× corrected), and compiled it is within 14% of
it. Set against `hashfill`, whose interpreted row is also within 10% of `perl`,
text munging rather than hashing is where the perl comparison is closest.

**`rats` answers the question the value census left open.** Moving the `Rat`
numerator/denominator pair behind the lazily-allocated cold block could have
taxed a program that mass-creates short-lived `Rat`s and reads each once. That
shape now has a kernel, in `tools/bench` and in the release gate both, and it
runs 1.4× ahead of Rakudo and 1.5× ahead of mutsu. The exposure was real and is small.

`sortby` and `arraypush` are wins (9.2× and 2.8× over Rakudo interpreted).
`sortby` exists to keep a win won: `.sort` with a **1-ary key extractor** is
contractually one key call per element, and calling it inside the comparator
instead is O(n log n) — an 18.09 s → 0.68 s fix
([internals/OPTIMIZATION.md](../internals/OPTIMIZATION.md)) that until now had
no kernel standing behind it, so a regression would have been invisible.

### Startup

The harness times sixteen programs in
[`tools/bench/`](../../tools/bench), and one of them is not a workload at all:
[`startup.raku`](../../tools/bench/startup.raku) is `say "Hello, World!"` and
nothing else, so the row is process startup and almost nothing but. It has
always been timed; it gets its own table here because it does not belong in
the ratio-ordered tables above.

| mode | startup | vs Rakudo |
|---|---:|---:|
| Raku++ native `--exe` | 2.5 ms | **30.2×** |
| Raku++ interp | 3.0 ms | **25.2×** |
| mutsu | 4.4 ms | **17.2×** |
| Rakudo | 75.6 ms | — |

A native Raku++ binary has no VM to bring up and no precompiled runtime to
load; Rakudo's 76 ms is a fixed cost paid by every row in every table on this
page, its own included. (It was 152.7 ms while the reference ran under Rosetta
2 — startup was the kernel that paid the largest translation penalty, 1.84×.)
**mutsu is in the same order of magnitude as us and not Rakudo** — 4.4 ms, so
1.8× our compiled binary and 1.5× our interpreter, but 17.2× faster to start
than the reference. That is the clearest
architectural agreement between the two newer implementations: neither has a
`CORE.setting` to load, and mutsu chose Cranelift over LLVM specifically to
keep it that way. The gap that separates the three engines on this row is a
design decision, not an efficiency difference. For one-liners and CLI glue that difference is the
whole story — but note it cuts the other way as a *measurement* caveat: on the
shortest kernels here (`strcat` at 3 ms compiled) startup is most of what is
being timed, which is why small percentage moves on those rows are usually
startup and not codegen.

Both Raku++ rows grew ~0.3–0.6 ms when lexical pads landed on 2026-08-22 —
laying out a pad is a fixed per-process cost — and that is recorded in the
2026-08-22 re-snapshot notes rather than smoothed away.

### vs Perl 5: the `hashfill` kernel

Perl 5 is the family yardstick for CLI-scale scripting, so one kernel ships as
a twin pair — [`tools/bench/hashfill.raku`](../../tools/bench/hashfill.raku)
and [`tools/bench/hashfill.pl`](../../tools/bench/hashfill.pl), the same
program line for line: fill a 200k-key hash through interpolated string keys
(`%h{"key$i"} = $i * 2`), sweep `%h.values` into an accumulator, then build a
string with 50k `~=` appends.

Measured 2026-08-31, all five engines in the same harness run (best of 6,
startup-inclusive; the `perl` on this machine's PATH is v5.44.0), after the
`ValueHash` payload, the `Value` shrink and lexical pads landed (see below):

| engine | hashfill | vs perl |
|---|---:|---:|
| Raku++ `--exe` | 38.6 ms | **2.7× faster** |
| Perl 5 | 103.2 ms | — |
| Raku++ interp | 109.5 ms | 1.1× slower |
| mutsu | 392.9 ms | 3.8× slower |
| Rakudo | 284.1 ms | 2.8× slower |

**mutsu lands with Rakudo on this one, not with us**, and it is the kernel
where that is most striking: the two engines with a real garbage collector sit
at ~280–395 ms and the two Raku++ modes at 39–110 ms. `hashfill` allocates
200,000 interpolated string keys and sweeps them, which is exactly the shape
that makes a collector earn its keep — and exactly the shape refcounting
without a collector does cheaply, as long as the program makes no cycles. This
one row is the clearest price/benefit picture of the memory-management choice
described in the [implementations FAQ](../guide/faq/implementations.md): we are
much faster here and we leak cycles; they collect cycles and pay for it here.

`textsplit` is the second kernel with a `.pl` twin, and it is the one perl
still wins: 34.1 ms against Raku++'s 64.7 interpreted (1.9× behind) and 38.7
compiled (1.1× behind). mutsu runs it in 246.3 ms — 7.2× behind perl, and the
only engine here that Rakudo (184.5 ms, 5.4× behind) is within hailing distance
of. Two twins now disagree about where the perl comparison stands, which is
more informative than one agreeing with itself — hashing is level, text
munging is not.

The interpreted row is the one to watch: it was 1.6× slower than perl on
2026-08-21, within 10% on 2026-08-24, and 6% off it now, on a kernel built out
of exactly the things a scripting language is asked to do — interpolated hash
keys, a values sweep, and 50k string appends.

Replacing the hash payload's `std::map` with `ValueHash` — an insertion-ordered
open hash with the key's hash stored, in the perl mold
([PERL5-TECHNIQUES.md](../dev/findings/engines/PERL5-TECHNIQUES.md)) — is what put the
compiled row ahead of perl. The `hash` kernel moved the same way; the non-hash
kernels are unchanged within noise.

The harness's `native` column compiles with plain `--exe`; the `-O` codegen
passes widen the margin further. The full mode ladder below is from the
2026-08-21 sitting, taken before the `Value` shrink — its absolute rows are
superseded by the table above (plain `--exe` is 38.0 ms now, not 51.5), so
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
output all four ways — interp, `--exe`, `--exe -O` and Rakudo as the oracle —
before timing). Re-measured 2026-08-24 at `v3.6.0-85-g6095c4f` for v3.7.0,
one harness pass, best of 5 within it; Rakudo v2026.08 shown for reference:

| Benchmark | `--exe` | `--exe -O` | `-O` vs `--exe` | Rakudo | showcases |
|---|---:|---:|---:|---:|---|
| sieve       | 1007.4 ms | **19.5 ms**  | **51.7×** | 1007.8 ms | primes < 200k by trial division — `* <= %%` all laned |
| powmod      | 537.0 ms  | **21.3 ms**  | **25.2×** | 744.0 ms  | 1M `** 3` then `% 1000` — inline pow + mod lane |
| intsum      | 128.2 ms  | **16.6 ms**  | **7.7×**  | 734.3 ms  | 5M int accumulation — `+=` lane, zero boxing |
| fibcalls    | 352.8 ms  | **62.5 ms**  | **5.6×**  | 1409.5 ms | fib(32) — direct-arity calls + int-lane condition |
| arrayidx    | 93.0 ms   | **48.0 ms**  | **1.9×**  | 582.1 ms  | 2M `@a[$i]` read-modify-write — no element lane yet |
| nummath     | 121.5 ms  | 92.5 ms      | 1.3×      | 441.1 ms  | Mandelbrot escape count — `Num` math, no lane yet |
| methodcalls | 297.0 ms  | 282.8 ms     | 1.0×      | 317.2 ms  | 1M monomorphic method calls — not devirtualized yet |
| stringbuild | 5.7 ms    | 5.7 ms       | 1.0×      | 218.1 ms  | 400k `~=` appends — in-place O(n) string build |

**The bottom three rows are the honest end of the table.** `arrayidx`,
`nummath` and `methodcalls` were added on 2026-08-22 to name what `-O` does
*not* do yet: there is no element lane for indexed array access, no lane for
`Num` math, and no devirtualization of a monomorphic method call. `methodcalls`
gaining 1.0× is the compiled-side counterpart of the `objects` loss in the
kernel tables — the optimizer has nothing to give a method call, which is
exactly why that kernel is slow.

_Against the previous sitting of this table (Rakudo v2026.06), plain `--exe`
improved on every row and by a lot on three — `stringbuild` 24.0 → 5.5 ms,
`intsum` 298.7 → 127.0, `fibcalls` 670.6 → 347.5 — which is the general
compiled-path work of the last weeks arriving here. `-O` improved further on
top, so the `-O` gain column moved both ways: `powmod` went 10.7× → 24.0× (its
`-O` row more than halved) while `intsum` fell 9.4× → 7.9× and `fibcalls`
4.0× → 5.7×. A gain column shrinking is not a regression when both of its
columns got faster — read the milliseconds first._

The lanes (pass 3) dominate this table: `sieve`'s inner loop — `while $d * $d
<= $n`, `if $n %% $d`, `$d++` — runs as raw `int64`, taking it from a tie with
Rakudo at plain `--exe` (1007.4 against 1007.8 ms) to **52×** ahead, and
`intsum` shed its four per-iteration `Value` constructions. The figures for
`-O` on the main kernels above — fib 27.3 ms, loopsum 7.1 ms, streq 14.5 ms
(the `$c++`/`$c--` counters lane on top of the inline `eq`/`lt`) — are from an
earlier sitting and have not been re-measured with this table.
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
./build-arm64/rakupp tools/run-bench.raku    # checks every engine agrees, then times them
```

The harness compiles each program with `--exe` for the native column; the
benchmark programs are plain, readable Raku in `tools/bench/*.raku` (edit or add
freely).

The **mutsu** lane is optional and auto-discovered — `$MUTSU`, then `mutsu` on
`PATH`, then `$HOME/mutsu/target/release/mutsu`. Without one the column reads
`—` and no other lane changes, so this command reproduces the Raku++ and Rakudo
rows anywhere. To reproduce the mutsu rows, build it from source and check the
architecture matches your host before believing the numbers:

```sh
cargo build --release && file target/release/mutsu
```

The same check applies to **Rakudo**, which the harness does not verify — its
arch guard inspects `$RAKUPP` only. On Apple Silicon an Intel Homebrew Rakudo
runs under Rosetta 2 at a per-kernel cost of 1.0–1.8× (figures in the
methodology), and `raku --version` does not report the architecture:

```sh
file $(which rakudo)               # want: Mach-O 64-bit executable arm64
raku -e 'say $*KERNEL.hardware'    # want: arm64 — reads x86_64 when translated
```

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

_**2026-08-22 re-snapshot, fourth sitting of the day (`v3.6.0-19-gbbd1249`)** —
the tables above are this run: three harness passes on the benchmarks machine
(Darwin 24.6, Apple M3, Apple clang 17.0.0), minimum across all three, engines
interleaved within each round, all byte-identical before timing. The commit is
`bbd1249`, the TARG plan's first slice: a plain `$padvar = EXPR` carries a
decided-once verdict and skips the whole of `evalAssign`'s shape ceremony, and
the fast arithmetic shapes read the pad directly. Against the sitting above:_

| kernel | interp pads → TARG | | `--exe` pads → TARG | |
|---|---:|---:|---:|---:|
| streq    | 312.2 → 248.4 ms | **−20%** | 20.3 → 20.4 ms | +1% |
| fib      | 420.9 → 394.1 ms | **−6%**  | 85.8 → 85.8 ms | ±0% |
| arrayops |  65.4 → 64.9 ms  | −1%      | 64.0 → 65.0 ms | +2% |
| regex    |  39.8 → 39.7 ms  | −0%      | 24.9 → 25.1 ms | +1% |
| bigint   |  31.6 → 31.5 ms  | −0%      | 30.7 → 31.6 ms | +3% |
| hash     |  18.1 → 18.2 ms  | +1%      | 7.3 → 7.2 ms   | −1% |
| hashfill | 111.9 → 112.9 ms | +1%      | 37.2 → 37.4 ms | +1% |
| sortnums |  33.8 → 34.1 ms  | +1%      | 18.9 → 18.8 ms | −1% |
| strcat   |   9.6 → 9.7 ms   | +1%      | 2.9 → 3.1 ms   | +7% |
| loopsum  | 105.2 → 107.6 ms | **+2%**  | 13.9 → 13.9 ms | ±0% |

_**The kernel this commit targets is not in this table.** `asg` — a tight loop
of scalar assignments — lives in `tools/perf-baseline.raku`, the release gate,
and it is where the slice lands: 294.3 → 171.4 ms, **−42%**, matching the
commit's own claim. The gate's string kernels move with it (`strscan` 155.5 →
136.6, −12%; `strpass` 113.1 → 97.5, −14%), which is what put `streq` −20% in
the table above. `perf-guard --check` is green with 19–62% of headroom on all
seven kernels: `asg` −61.7% against the v3.14.0 baseline, `hash` −50.8%,
`loopsum` −45.7%, `fib` −38.1%._

_**`streq` crossed.** Interpreted, it is 248.4 ms against Rakudo's 284.3 — the
last of the ten kernels Rakudo held, and the second to fall in one day after
`fib`. The tree-walker now leads on all ten. Both new margins are thin (1.1×,
1.2×) and the tables say so rather than calling them wins._

_**`loopsum` is the one row that moved the wrong way**, +2% interpreted (105.2
→ 107.6 ms), and it is recorded rather than rounded away: the three passes here
read 107.6/107.6/109.0 against 105.2/105.3/105.4 in the previous sitting, so
the two do not overlap, and `perf-guard` sees the same +1.4% on its own copy of
the kernel. It is small next to `asg` −42% and within the gate's tolerance, but
it is a real cost of the new lane and should be looked at if it grows._

_Reference engines: Rakudo moved by at most 1.6% on any kernel and `perl` by
2%, so nothing here is a machine effect. The `--exe` column is flat, as it has
been for both interpreter changes — codegen already kept variables in C++
locals. The `+7%` on native `strcat` is 0.2 ms on a 3 ms kernel that is mostly
process startup._

_**2026-08-22, fifth sitting (`v3.6.0-36-g9dfc982`) — the five new kernels fold
in.** Three harness passes on the benchmarks machine (Darwin 24.6, Apple M3,
Apple clang 17.0.0), minimum across all three, engines interleaved, all
byte-identical before timing; three passes of `run-optbench.raku` likewise.
This is the sitting the previous revision's checklist asked for, and it
replaces the separately-tabled M1/Darwin 25.5 rows: `rats`, `objects`,
`arraypush`, `sortby` and `textsplit` are now in the two main tables, and
`arrayidx`, `nummath` and `methodcalls` in the `-O` table._

_**The `objects` finding survived the re-measure, which was the point of it.**
The first sitting read Rakudo 2.2× ahead and estimated ~1.5× once the box was
corrected for; the direct M3 measurement reads **1.8×**, between the two and
closer to the raw figure. Compiled `--exe` (291.3 ms) is level with Rakudo's
interpreter (287.5). `methodcalls` gaining 1.0× under `-O` says the same thing
from the compiled side._

_The ten established kernels barely moved against the `bbd1249` sitting — all
within ±2% except `loopsum` (107.6 → 97.2 ms, −10%), `strcat` (−6%) and `fib`
(−5%), which is the dozen commits since. **`streq` went the other way, +2.9%**
(248.4 → 255.5 ms) with Rakudo flat at 283.4, so the crossing there is thinner
than it was: 1.1× and worth watching rather than banking._

_The gate: `perf-guard --check` green before anything was re-recorded, every
one of the seven recorded kernels 33–62% faster than the v3.14.0 baseline
(`asg` −61.9%, `loopsum` −49.4%, `hash` −48.9%), and `rats` correctly reported
as "not gated" rather than dividing by a missing baseline._

_**2026-08-24 re-snapshot (v3.7.0, `v3.6.0-85-g6095c4f`)** — the tables above
are this run, on the same Darwin 24.6 / Apple M3 box as every revision since
the machine note was added: one harness pass, engines interleaved, all
byte-identical before timing; `run-optbench.raku` likewise, so **the `-O` table
is re-measured this round** rather than carried forward. The reason for the
whole sitting is the oracle: Rakudo moved **v2026.07 → v2026.08** with this
release, and the reference column is measured every time, so it had to be
taken again._

_**Rakudo moved a few percent in both directions and none of it is a trend.**
`strcat`'s reference lane 179.9 → 166.3 ms, `hash` 221.4 → 215.7, against
`loopsum` 261.7 → 276.4 and `fib` 462.2 → 465.0. `hashfill` is the one wide
mover — 453.7 → 397.4 ms — and it is the noisiest kernel in the set on both
sides, which is why its ratio reads 3.2× here against 4.1× last sitting while
the `--exe` lane barely moved (38.0 → 36.2 ms). Read the perl ladder in "vs
Perl 5" for that kernel, not the main-table ratio._

_**Raku++'s own lanes are flat**, as they should be: the only engine change
since the previous sitting is a `$*VM.config` accessor, which no kernel
touches. Every interp and `--exe` cell is within ±3% of the `9dfc982` sitting
except `hashfill` interp (110.8 → 122.7 ms) and `objects` interp (507.7 →
518.8), both inside that kernel's own spread. `perf-guard --check` was green
before this run, worst kernel +2.8% against a 5% tolerance._

_**A caveat that had never been written down**, now in the methodology above:
the reference engine is an **x86_64 Rakudo running under Rosetta 2** — the only
one on this machine — so it pays the same 1.7–2× translation penalty this file
warns about for a mis-built Raku++ binary. The harness's arch guard inspects
`$RAKUPP` only and cannot see it. Every earlier revision measured Rakudo the
same way, so the series is self-consistent and the trends hold; the absolute
multiples are an upper bound, not a like-for-like result._

_**2026-08-31 re-snapshot at `v3.23.0-45-gb6905bf` — mutsu joins the tables.**
The tables above are this run: three full harness passes, minimum across all
three, on the Darwin 24.6 / M3 benchmarks machine, and the first sitting to
carry a fourth engine. All sixteen kernels produced byte-identical output on
all four engines, so nothing here is a flagged row._

_**The comparability check passes.** Against the 2026-08-24 sitting the two
reference lanes barely moved — Rakudo within ±4.3% on every shared kernel,
`perl` +7.8% on `hashfill` — while the Raku++ lanes moved −11% to +3%. A
same-direction shift on the reference lanes would have meant the machine; a
shift confined to ours across 45 commits is code. The widest movers are
`loopsum` interp (97.3 → 86.2 ms), `hashfill` interp (122.7 → 109.5) and
`arrayops` interp (64.5 → 59.9), all in our favour. `objects` `--exe` went the
other way (285.1 → 336.7 ms) and is the one row worth watching next sitting._

_**The box was not idle, and the rule was overridden on evidence rather than
waived.** `mediaanalysisd` held a core for the whole sitting and the 1-minute
load sat near 4, against the project's own strict-idle bar of < 2.5. Two
controls said measure anyway: a repeatability probe read max/min = 1.028 over
nine consecutive `hash` runs, and `hash` interp came out at 18.8 ms against
18.9 the previous sitting. Background-QoS work is scheduled on the efficiency
cores on Apple Silicon, so it inflates load average without contending for the
performance cores a benchmark runs on. Load average is the wrong instrument
here; a repeatability probe is the right one, and it is cheap._

_**Building mutsu native needed a toolchain this machine did not have.** The
only Rust present was the x86_64 Homebrew build with no aarch64 std, so the
first build failed at the link step for x86_64; arm64 Rust, `pcre2` and
`libffi` were installed for this sitting. Had that gone unnoticed, mutsu would
have run under Rosetta and every Raku++ row would have been flattered by the
same 1.7–2× this file warns about elsewhere — and the harness's arch guard,
which inspects `$RAKUPP` only, would not have caught it. `file` on the mutsu
binary is now part of the runbook._

_**mutsu is the first engine in this file that is a like-for-like architecture
match.** Both it and Raku++ are native arm64 here; Rakudo is not. So the
Raku++-vs-mutsu columns are the only untranslated comparison the file has ever
carried, and where the two disagree with the Rakudo column about a kernel, the
mutsu column is the better evidence._

_**2026-08-31, later the same day: the caveat above is fixable after all, and
the penalty it assumed was too large.** homebrew/core ships an `arm64_sequoia`
bottle for rakudo/moarvm/nqp at v2026.08 — the exact version this sitting
measures — so "the ARM prefix has no rakudo formula" was stale, not a
constraint. It is installed at `/opt/homebrew/bin/raku`. Measuring the two
builds of v2026.08 against each other gives a translation penalty of **1.00× to
1.84×, mean 1.38×**, not the uniform 1.7–2× this file had assumed by carrying
the figure over from mis-built Raku++ binaries. Only `startup` (1.84×) and
`strcat` (1.76×) reach that band; `streq` (1.00×) and `sortnums` (1.04×) are
free, confirmed on a 12-round re-check against a `strcat` control that held at
1.76× on both minimum and median. The x86_64 control reproduced this sitting's
published Rakudo column to within ~1% on every kernel re-timed, which
established that the two references could be measured against each other. The
tables were then **re-measured rather than corrected arithmetically** — see the
next note._

_**2026-08-31: every released build re-measured, each against its own era's
Rakudo.** All 28 tagged releases ship a `rakupp-macos-universal.tar.gz`, and a
bare invocation runs its arm64 slice — checked, not assumed: `loopsum` reads
198 ms bare and under `arch -arm64`, against 337 ms under `arch -x86_64`. Each
release was run through `tools/run-bench.raku` against a from-source Rakudo
matching its release date (2026.06 through 2026-07-24, 2026.07 through
2026-08-21, 2026.08 after): 16 kernels, 453 rows, and every engine agreed on
output in every row. The interp column reproduced the published one to within
2.1% worst case, so only the reference side moved.

**The Rakudo column is measured once per RAKUDO release, not once per Raku++
release.** Measuring it per tag was wrong and it showed: Rakudo cannot change
between two Raku++ tags cut on the same day, yet the column moved by up to 19%
on `sortby` — whose lane is bimodal on this machine at ~168/198 ms — and 2–10%
elsewhere, drawing run-to-run noise as if it were signal. It is now three
measurements, one per Rakudo release, assigned by date, so the column is a step
function that changes only on 2026-07-25 and 2026-08-22. Each value is the
minimum of 80 runs: 20 timed runs after a discarded warm-up, all three Rakudos
interleaved inside every round, four such passes. Convergence was checked
rather than assumed — `min(pass A,B)` against an independent `min(pass C,D)`
moved by at most 1.8%, and by more than 2% on none of the 48 values. Two passes
alone had disagreed by up to 10%, because they ran as sequential blocks and
tracked the machine's drift; the minimum over four interleaved passes removes
it. This also settles `sortby`, which the per-tag measurement had left at
"5.2–6.1×": the converged floor is the fast mode, and the row reads 5.1×.

The era-to-era *changes* are the sturdiest thing in this file — they reproduced
to 1–2% across passes even while drift moved absolute levels by 10%. Rakudo
2026.08 is genuinely ~13% faster than 2026.07 on `hashfill`, ~7% faster on
`strcat`, and ~6% slower on `loopsum`.

Three things the release series shows that no single sitting could, because
each happened between sittings: `sortby` interpreted fell from 3.9 s to 60 ms
at **v1.2.5**; `arraypush` was quadratic until **v1.5.2** and cannot be
measured before it at all — a single run exceeds 10 s, so those eleven rows are
recorded as skipped in the sweep data rather than estimated; and **v3.7.0**
halved `loopsum` and took 43% off compiled `fib`. Two releases also ship
binaries whose `--version` reports the previous release (v3.20.0 says 3.7.0,
v3.0.0 says 2.0.0) — distinct builds by hash and size, only the embedded string
stale, so the sweep keys rows by tag and never by `--version`.

The per-release numbers live in `sites/spec/src/data/bench-backfill.tsv` in the
raku.online repo, where they replaced a 2026-08-21 sitting taken on the Darwin
25.5 box (one kernel, 22 tags). `gen-dashboard.raku` merges them as an override
per engine rather than as gap-fill, because a tag's own committed tables were
measured on whatever machine that release had. [`tools/rakupp-bench-sweep.sh`](../../tools/rakupp-bench-sweep.sh) reproduces
the whole thing on another machine (see
[dev/BENCH-SWEEP.md](../dev/BENCH-SWEEP.md), and
`rakupp-bench-sweep.ps1` for Windows); note that on any x86_64 host MoarVM *has* its JIT, so a Rakudo column
measured there is not comparable with the one above._
