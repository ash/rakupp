# Raku++ vs Rakudo vs mutsu — speed

How fast Raku++ runs a set of small programs that every engine here runs
identically — as a tree-walking interpreter and as a native compiler — against
Rakudo, the reference implementation, and
[mutsu](https://github.com/tokuhirom/mutsu), the other from-scratch one.

**Measured 2026-10-13 at `v5.4.0`**, on the machine of record (Apple M3, macOS
Darwin 27.0.0), in one sitting: three interleaved passes of the kernel harness
and two of a local v5.3.0 build for the comparison below, against **Rakudo
2026.09**. The `-O` harness and the YAMLish parse were not re-run; their
sections say when they were measured. Earlier sittings, with the notes that
explained each movement, are in
[findings/BENCHMARKS-HISTORY.md](../dev/findings/BENCHMARKS-HISTORY.md);
[raku.online/spec/dashboard](https://raku.online/spec/dashboard/) charts every
release.

The lanes:

- **interp** — Raku++ interpreting the source (tree-walk)
- **native** — Raku++ `--exe`: the program transpiled to C++ and compiled to a
  native binary
- **mutsu** — mutsu interpreting the source (Rust bytecode VM with a Cranelift
  JIT, on by default as it ships)
- **rakudo** — Rakudo interpreting the source (MoarVM), with 2026.09's default
  frontend, RakuAST
- **perl** — Perl 5, for the three kernels that ship a `.pl` twin

`--bundle` and `--aot` tree-walk the program, so they run at `interp` speed and
are not shown separately. [Raku.js](../../rakujs), the interpreter compiled to
WebAssembly, is measured in
[rakujs/INTERNALS.md](../../rakujs/INTERNALS.md#performance-vs-native-and-node-vs-bun-vs-browser).

## The short version

- **Startup:** 3.0 ms for a native binary and 4.4 ms interpreting; mutsu 4.8
  ms, Rakudo 70.3 ms.
- **Against Rakudo 2026.09, `--exe` takes less time on seventeen of eighteen
  kernels**, from 1.10× on `objects` and 2.5× on `rats` to 84× on `intcat`
  and `mainwhen`. On `multiwhere` Rakudo is 7% ahead, and that binary is not
  compiled code: codegen declines a `where` on a multi candidate and bundles
  the interpreter.
- **The interpreter takes less time than Rakudo on seventeen of eighteen**,
  from 1.02× on `objects` to 40× on `mainwhen` and 105× on `intcat`; on
  `multiwhere` Rakudo is 5% ahead.
- **Against mutsu, both modes take less time on every kernel mutsu was timed
  on** (seventeen; `intcat` takes mutsu about 160 s a run and was left out).
- **v5.4.0 against v5.3.0**, both built here and timed in this sitting:
  `textsplit` −13% interpreted and −24% compiled, `arraypush` −66% compiled
  (56.7 → 19.4 ms). Interpreted, `mainwhen` is 11% slower and `sortby` 8%;
  both are work added, not noise (retired instructions +13% and +8%). The
  table is [below](#v530-to-v540).
- **Against Perl 5**, the interpreter is 1.2× faster on `hashfill` and 1.5×
  on `intcat`, `--exe` 1.8× and 1.2×; perl is 1.8× faster than `--exe` on
  `textsplit`.

## Results

Best of 6 timed runs per engine (7 spawned, the first discarded), minimum
across three passes, process startup included; lower is better. Rows are
ordered by the ratio against Rakudo, most favourable to Raku++ first. **Bold**
marks a Raku++ lead; otherwise the faster engine is named. All four engines
produced byte-identical output on every kernel in every pass. mutsu was not
timed on `intcat` (160.5 s a run when the kernel was added, 2026-10-06).

### Interpreter vs Rakudo and mutsu

| Benchmark | Raku++ (interp) | mutsu | Rakudo | vs Rakudo | vs mutsu |
|---|---:|---:|---:|---|---|
| intcat | 49.2 ms | — | 5163.4 ms | **104.9×** | — |
| mainwhen | 6.9 ms | 87.6 ms | 278.7 ms | **40.4×** | **12.7×** |
| loopsum | 5.4 ms | 54.3 ms | 167.1 ms | **30.9×** | **10.1×** |
| bigint | 6.4 ms | 8.3 ms | 160.1 ms | **25.0×** | **1.3×** |
| fib | 14.0 ms | 137.8 ms | 302.9 ms | **21.6×** | **9.8×** |
| strcat | 4.4 ms | 7.4 ms | 88.1 ms | **20.0×** | **1.7×** |
| hash | 7.5 ms | 26.5 ms | 123.9 ms | **16.5×** | **3.5×** |
| arraypush | 18.1 ms | 120.5 ms | 254.4 ms | **14.1×** | **6.7×** |
| streq | 17.8 ms | 474.2 ms | 181.3 ms | **10.2×** | **26.6×** |
| sortnums | 17.7 ms | 24.9 ms | 163.0 ms | **9.2×** | **1.4×** |
| rats | 26.5 ms | 275.1 ms | 223.8 ms | **8.4×** | **10.4×** |
| regex | 26.7 ms | 82.4 ms | 180.6 ms | **6.8×** | **3.1×** |
| hashfill | 40.7 ms | 178.9 ms | 266.6 ms | **6.6×** | **4.4×** |
| sortby | 22.1 ms | 29.9 ms | 141.6 ms | **6.4×** | **1.4×** |
| arrayops | 33.5 ms | 94.9 ms | 171.5 ms | **5.1×** | **2.8×** |
| textsplit | 51.6 ms | 88.8 ms | 186.3 ms | **3.6×** | **1.7×** |
| objects | 207.0 ms | 568.2 ms | 210.2 ms | **1.02×** | **2.7×** |
| multiwhere | 231.4 ms | 6248.2 ms | 220.2 ms | Rakudo 1.05× | **27.0×** |

The integer and loop kernels run a sub or a loop on machine values when
everything in it is one of the shapes they cover: plain Int and Str variables
(`fib`, `loopsum`, `mainwhen`, `streq`), array and hash elements read, written
and pushed in place, Nums, Rats and `**` (`arraypush`, `hash`, `rats`).
`intcat` is issue #130's shape — a string built by appending Ints — and is
linear in every mode. `objects` — 200k `.new`, 300k method calls, 500k
attribute reads — is the one kernel that measures `class`/`has`/method
dispatch, and `multiwhere` is 400k calls into a `where`-constrained multi
candidate.

### Native (`--exe`) vs Rakudo and mutsu

The last column is the speed-up over interpreting the same program.

| Benchmark | Raku++ (`--exe`) | mutsu | Rakudo | vs Rakudo | vs mutsu | vs interp |
|---|---:|---:|---:|---|---|---:|
| mainwhen | 3.3 ms | 87.6 ms | 278.7 ms | **84.5×** | **26.5×** | 2.1× |
| intcat | 61.4 ms | — | 5163.4 ms | **84.1×** | — | 0.8× |
| fib | 5.6 ms | 137.8 ms | 302.9 ms | **54.1×** | **24.6×** | 2.5× |
| loopsum | 3.3 ms | 54.3 ms | 167.1 ms | **50.6×** | **16.5×** | 1.6× |
| bigint | 5.3 ms | 8.3 ms | 160.1 ms | **30.2×** | **1.6×** | 1.2× |
| strcat | 3.3 ms | 7.4 ms | 88.1 ms | **26.7×** | **2.2×** | 1.3× |
| streq | 7.2 ms | 474.2 ms | 181.3 ms | **25.2×** | **65.9×** | 2.5× |
| hash | 7.2 ms | 26.5 ms | 123.9 ms | **17.2×** | **3.7×** | 1.0× |
| sortnums | 11.6 ms | 24.9 ms | 163.0 ms | **14.1×** | **2.1×** | 1.5× |
| arraypush | 19.4 ms | 120.5 ms | 254.4 ms | **13.1×** | **6.2×** | 0.9× |
| regex | 17.8 ms | 82.4 ms | 180.6 ms | **10.1×** | **4.6×** | 1.5× |
| hashfill | 27.1 ms | 178.9 ms | 266.6 ms | **9.8×** | **6.6×** | 1.5× |
| sortby | 15.7 ms | 29.9 ms | 141.6 ms | **9.0×** | **1.9×** | 1.4× |
| textsplit | 24.5 ms | 88.8 ms | 186.3 ms | **7.6×** | **3.6×** | 2.1× |
| arrayops | 32.0 ms | 94.9 ms | 171.5 ms | **5.4×** | **3.0×** | 1.0× |
| rats | 91.3 ms | 275.1 ms | 223.8 ms | **2.5×** | **3.0×** | 0.3× |
| objects | 191.2 ms | 568.2 ms | 210.2 ms | **1.10×** | **3.0×** | 1.1× |
| multiwhere | 235.6 ms | 6248.2 ms | 220.2 ms | Rakudo 1.07× | **26.5×** | 1.0× |

Three rows are slower compiled than interpreted. In each, every loop runs as
an interpreter kernel (`RAKUPP_KERNEL_TRACE=1` names them), which takes array
elements, pushes, Nums and Rats, while the `--exe` code does not have those
lanes. `arraypush` is close to level now (19.4 against 18.1 ms), down from
56.7 ms compiled at v5.3.0. Elsewhere compiling gains least where the
interpreter already runs a kernel — `fib`, `loopsum`, `mainwhen`, `streq` are
within 1.6-2.5× of their binaries, and much of what is left on those rows is
process startup — and nothing where the time is inside a runtime method both
modes share: `arrayops` (`.grep`/`.map`/`.sum`), `hash`, `bigint` (the
`BigInt` multiply) and `multiwhere` (the bundled interpreter).

### v5.3.0 to v5.4.0

Both columns are local builds with the same compiler, timed in this sitting:
v5.3.0 is the tag built in a clean worktree, two passes, and v5.4.0 is
`5.4.0-g36b420ad`, three passes. At earlier sittings a release artifact read
−6% to +16% against a local build of the same code, so this table compares
two local builds instead. The Rakudo lane, timed beside both, held within 1.6%
between the two halves of the sitting. Milliseconds, minimum across the
passes.

| kernel | interp v5.3.0 | interp v5.4.0 | | `--exe` v5.3.0 | `--exe` v5.4.0 | |
|---|---:|---:|---:|---:|---:|---:|
| textsplit | 59.5 | 51.6 | −13% | 32.1 | 24.5 | −24% |
| regex | 27.4 | 26.7 | −3% | 17.7 | 17.8 | +1% |
| arrayops | 34.3 | 33.5 | −2% | 34.0 | 32.0 | −6% |
| bigint | 6.4 | 6.4 | 0% | 5.2 | 5.3 | +2% |
| hashfill | 40.6 | 40.7 | 0% | 27.3 | 27.1 | −1% |
| multiwhere | 229.3 | 231.4 | +1% | 228.7 | 235.6 | +3% |
| sortnums | 17.5 | 17.7 | +1% | 11.7 | 11.6 | −1% |
| intcat | 48.6 | 49.2 | +1% | 58.7 | 61.4 | +5% |
| hash | 7.4 | 7.5 | +1% | 7.2 | 7.2 | 0% |
| loopsum | 5.3 | 5.4 | +2% | 3.2 | 3.3 | +3% |
| strcat | 4.3 | 4.4 | +2% | 3.2 | 3.3 | +3% |
| streq | 17.3 | 17.8 | +3% | 7.1 | 7.2 | +1% |
| objects | 200.4 | 207.0 | +3% | 190.8 | 191.2 | 0% |
| rats | 25.6 | 26.5 | +4% | 91.0 | 91.3 | 0% |
| fib | 13.4 | 14.0 | +4% | 5.6 | 5.6 | 0% |
| arraypush | 17.2 | 18.1 | +5% | 56.7 | 19.4 | −66% |
| sortby | 20.5 | 22.1 | +8% | 14.8 | 15.7 | +6% |
| mainwhen | 6.2 | 6.9 | +11% | 3.3 | 3.3 | 0% |
| startup | 3.8 | 4.4 | +16% | 3.0 | 3.0 | 0% |

**Two interpreted rows are slower, and both are work added.** `mainwhen`
retires 13% more instructions at 200k iterations; the extra is a fixed ~10M
instructions (about 0.5 ms) that does not grow with the loop — at 2M
iterations it is the same 10M, 2% of the run, which is why perf-guard's
6M-iteration `mainwhen` does not see it. `sortby` retires 8% more and the
extra grows with the input (+6% at 120k words), so it is a per-element cost
in the `.map`/`.sort(*.chars)` path. `startup` reads +16% here but is within
2% in thirty interleaved runs of each binary, with identical instruction
counts; `fib`, `rats` and interpreted `arraypush` (+4-5%) sit at the edge of
what two sittings of one build differ by. Neither cause is found yet
([TODO.md](../dev/plans/TODO.md)).

perf-guard's call kernels stand where v5.3.0 left them: against a local
v5.3.0 build in two interleaved rounds every kernel is within 3% but
`strscan` and `substrloop`, both 6% slower in wall time with 1% more retired
instructions. Against the recorded v5.2.0 baseline, the v5.3.0 call-path cost
is still there (`strpass`, `method`, `privmeth`, `subcall` +5-10%).

### Startup

[`startup.raku`](../../tools/bench/startup.raku) is `say "Hello, World!"`, so
this row is process startup and almost nothing else.

| mode | startup | vs Rakudo |
|---|---:|---:|
| Raku++ native `--exe` | 3.0 ms | **23.4×** |
| Raku++ interp | 4.4 ms | **16.0×** |
| mutsu | 4.8 ms | **14.6×** |
| Rakudo | 70.3 ms | — |

A native Raku++ binary has no VM to bring up and no precompiled setting to
load; Rakudo's ~70 ms is a fixed cost inside every row on this page. mutsu is
in the same order of magnitude as Raku++ for the same reason.

### vs Perl 5

[`hashfill`](../../tools/bench/hashfill.raku) fills a 200k-key hash through
interpolated keys, sweeps `%h.values`, and builds a string with 50k `~=`
appends; [`textsplit`](../../tools/bench/textsplit.raku) splits 20k lines into
fields, reorders and rejoins them; [`intcat`](../../tools/bench/intcat.raku)
appends 2M Ints to two strings, one with a separator. Each has a line-for-line
`.pl` twin ([hashfill.pl](../../tools/bench/hashfill.pl)), timed by the same
harness with the same output check. perl is v5.44.0; the two other perls on
the machine (5.34.3 and 5.34.1) time within 4% of it on `hashfill` and
`textsplit`.

| engine | hashfill | vs perl | textsplit | vs perl | intcat | vs perl |
|---|---:|---:|---:|---:|---:|---:|
| Raku++ `--exe` | 27.1 ms | **1.8× faster** | 24.5 ms | 1.8× slower | 61.4 ms | **1.2× faster** |
| Raku++ interp | 40.7 ms | **1.2× faster** | 51.6 ms | 3.9× slower | 49.2 ms | **1.5× faster** |
| Perl 5 | 49.9 ms | — | 13.3 ms | — | 73.7 ms | — |
| mutsu | 178.9 ms | 3.6× slower | 88.8 ms | 6.7× slower | — | — |
| Rakudo | 266.6 ms | 5.3× slower | 186.3 ms | 14.0× slower | 5163.4 ms | 70.1× slower |

The hash payload is an insertion-ordered open hash in the perl mold
([PERL5-TECHNIQUES.md](../dev/findings/engines/PERL5-TECHNIQUES.md)), and
since v5.3.0 the interpreter's loop kernels write hash elements in place
(`hashfill` 87.3 → 42.0 ms interpreted); `intcat` is an in-place append in
both modes. Text munging is where perl still leads.

### `-O` (the optimizer flag)

`--exe -O` adds three speculative codegen passes: direct-arity calls for
fixed-arity subs, inline arithmetic and string comparison, and guarded
native-int lanes for int assignments and conditions
([OPTIMIZATION.md](../internals/OPTIMIZATION.md)). Measured by
[`tools/run-optbench.raku`](../../tools/run-optbench.raku), which first checks
each program's output four ways (interp, `--exe`, `--exe -O`, Rakudo); one pass,
best of 5. Measured 2026-10-08 at `v5.3.0`, and not re-run for v5.4.0.

| Benchmark | `--exe` | `--exe -O` | `-O` vs `--exe` | Rakudo | showcases |
|---|---:|---:|---:|---:|---|
| sieve       | 450.5 ms | **22.1 ms** | **20.4×** | 1411.7 ms | primes < 200k by trial division — inline `* <= %%` |
| arrayidx    | 94.9 ms | **53.7 ms** | **1.8×** | 639.1 ms | 2M `@a[$i]` read-modify-write — no element lane yet |
| methodcalls | 151.0 ms | 123.9 ms | 1.2× | 278.7 ms | 1M monomorphic method calls — not devirtualized yet |
| nummath     | 217.2 ms | 194.0 ms | 1.1× | 444.1 ms | Mandelbrot escape count — `Num` math, no lane yet |
| fibcalls    | 13.7 ms | 13.1 ms | 1.0× | 970.4 ms | fib(32) — direct-arity calls + inline `< + -` |
| bigmul      | 12.2 ms | 12.1 ms | 1.0× | 812.1 ms | 10000! by `*=` — the bignum compound-assign lane, no `-O` route |
| stringbuild | 4.9 ms | 4.9 ms | 1.0× | 126.5 ms | 400k `~=` appends — in-place O(n) string build |
| intsum      | 4.4 ms | 4.4 ms | 1.0× | 595.9 ms | 5M int accumulation — inline `+ - *` |
| powmod      | 4.0 ms | 4.0 ms | 1.0× | 459.2 ms | 1M `** 3` then `% 1000` — inline pow + mod |

Plain `--exe` now does what `-O` did for three of these: its loop lanes run
without the flag, an integer sub gets an int64 twin, and since v5.3.0 the
lanes take `**` and `mod`, so `powmod` went 180.7 → 4.0 ms with no `-O`.
`-O` still decides `sieve`, and helps `arrayidx`.

The middle rows name what `-O` does not reach: no element lane for indexed
array access, no lane for this shape of `Num` math (the floating-point lane in
[UNBOX-PLAN.md](../dev/plans/UNBOX-PLAN.md) fires elsewhere, not here), no
devirtualized method call, and nothing to add where the time is already in-place
append or the bignum multiply. `methodcalls` and `nummath` are 6% and 12%
slower without `-O` than at v5.2.0 (142.3 and 193.8 ms); the first is the
call-path cost above. `-O` is off by default and produces identical output.

### Real-world: grammar parsing (YAMLish)

A whole module doing real work: YAMLish 0.1.3 (zef:leont, unmodified) parsing
the Raku course's table of contents (`_data/toc/en.yaml`, 2,653 lines) with
`load-yamls`. The grammar exercises parameterised tokens, `|` alternations,
lookbehinds, `:my` state, aliased captures and action methods. Both engines
produce the same data, compared as sorted-key JSON. Best of 5, wall-clock, the
two engines interleaved; measured 2026-09-29 at `v5.0.1` against Rakudo
v2026.08, and not re-run since. It needs `v5.0.1` or later: `v5.0.0` does
not finish this parse in 60 s.

| Workload | Raku++ (interp) | Rakudo | Faster |
|---|---:|---:|---|
| load-yamls, one parse per process | 0.46 s | 0.86 s | **Raku++ 1.9×** |
| load-yamls, 10 parses in-process | 4.51 s | 7.16 s | **Raku++ 1.6×** |

The one-parse row includes each engine's startup and module load; the
ten-parse row is closer to parsing alone. Earlier measurements, with an
earlier YAMLish, are in the [history](../dev/findings/BENCHMARKS-HISTORY.md).

## Parallel scaling

`tools/bench/parmap.raku` sums squares over a fixed total range split across N
`start` blocks, no sharing. **Measured 2026-08-09 at v3.0.0**, when parallel
threads became the default, on this machine (4 performance + 4 efficiency
cores):

| workers | wall | speed-up |
|---:|---:|---:|
| 1 | 996 ms | 1.00× |
| 2 | 494 ms | 2.02× |
| 4 | 258 ms | 3.86× |
| 8 | 192 ms | 5.19× |

Past four workers the extra threads land on the efficiency cores, so the gain
is sub-linear, as [ASYNC.md](../guide/ASYNC.md) advises when sizing a fan-out.
A single-threaded program runs at the same speed as under `RAKUPP_GIL=1`.
Fan-out measured on 5.2.1 is in
[PARALLEL-SPEEDUP.md](../guide/PARALLEL-SPEEDUP.md); v5.3.0's work on the lexical
stripes took a CPU-bound fan-out on 8 workers from 3.16× to 5.32×
([PARALLEL-SCALING-PLAN.md](../dev/plans/PARALLEL-SCALING-PLAN.md)).

## How to read this

- **Every row includes process startup.** Each run is a fresh spawn, so an
  engine's startup is part of its time. Rakudo's ~80 ms start dominates its
  shortest rows, so the large multipliers there are not an execution-speed
  comparison; the `-O` kernels and the in-process YAMLish parse are closer to
  one.
- **`--exe` helps where a tree-walker hurts.** It removes the per-node dispatch
  of interpreting, so it gains most on `streq`, `loopsum` and `fib` and almost
  nothing on `arrayops`, `bigint` or `multiwhere`, whose time is inside runtime
  methods both modes share (and `multiwhere`'s `--exe` binary bundles the
  interpreter, because codegen declines a `where` on a multi candidate). It
  is behind the interpreter where the interpreter's loop kernels reach
  further than the code generator does: `arraypush`, `rats` and `intcat`.
- **These kernels are the overlap.** Every program here runs identically on all
  engines, which the harness checks before timing. Speed on this set says
  nothing about coverage; for that see [ROAST.md](ROAST.md).

## Methodology

- **Machine:** Apple M3 (4 performance + 4 efficiency cores), macOS Darwin
  27.0.0 — the machine of record for every timing in this repository.
- **Raku++:** `build-arm64/rakupp`, CMake Release (`-O3 -DNDEBUG`), Apple clang 21.
- **Rakudo:** v2026.09, the homebrew/core arm64 bottle at
  `/opt/homebrew/bin/rakudo`, passed to the harness **by path**, with its default
  frontend (RakuAST; `RAKUDO_RAKUAST=0` selects the old one, and
  `BEGIN $*LANG.^name` says which is running: `Raku::Grammar` or
  `Perl6::Grammar`). On this machine bare `raku` is Raku++, and `/usr/local/bin`
  precedes `/opt/homebrew/bin`. MoarVM has no JIT backend on arm64, so this
  Rakudo runs spesh without machine code; an x86_64 Rakudo column from another
  machine is not comparable. Timed on both frontends in the same sitting, the
  two were within 6% on fourteen of seventeen kernels at the 2026-10-02
  sitting; RakuAST was faster on `streq` (0.81×), `startup` (0.88×) and
  `loopsum` (0.91×). Not re-timed on the old frontend for this sitting.
- **mutsu:** 0.25.0, upstream `49442b379` (2026-10-04 JST), `cargo build
  --release` with default features (Cranelift JIT on), built 2026-10-04 and not
  rebuilt for this sitting or the one before. Not timed on `intcat`. Native arm64: check `file` on the binary, since an
  x86_64 build would run under Rosetta and the harness would not notice.
- **perl:** v5.44.0 (`/opt/homebrew/bin/perl`), for the three kernels with a
  `.pl` twin.
- **Harness:** [`tools/run-bench.raku`](../../tools/run-bench.raku). It runs every
  program under every engine and compares stdout before timing anything; a
  disagreement in a Raku++ lane fails the run, a mutsu disagreement is only
  reported. Each engine is spawned 7 times per kernel, the first run
  discarded, and the minimum of the other 6 kept; the engines are interleaved
  round by round, so a load spike lands on every lane. For `native`, each
  program is compiled with `--exe` once and the binary is timed; the compile is
  not counted. The harness refuses a Raku++ binary built for another
  architecture.
- **This sitting:** re-measured 2026-10-13 at `v5.4.0` — the local build is
  `5.4.0-g36b420ad`, the version commit. `intcat` ran in three passes of its
  own after the other three, without the mutsu lane; the v5.3.0 comparison
  build ran two passes after that, without mutsu. The 1-minute load read
  between 2.2 and 2.7 at the end of each pass. 90 of the 97 cells (the
  `--cnp` lane included) agree within 4% across the three passes; the worst
  are native `loopsum` (9.1%) and the 3 ms native `startup` (6.7%), both a
  few tenths of a millisecond. The Rakudo lane held within 1.6% between the v5.4.0 and the
  v5.3.0 passes.

## Reproducing

```sh
RAKUDO=/opt/homebrew/bin/rakudo ./build-arm64/rakupp tools/run-bench.raku
./build-arm64/rakupp tools/run-bench.raku --only=fib,objects --tsv=out.tsv
```

`--tsv=` writes the minimum and median per lane with the CPU and toolchain the
sitting ran on; `--rusage` adds CPU time and peak RSS. The mutsu lane is
optional and found as `$MUTSU`, then `mutsu` on `PATH`, then
`$HOME/mutsu/target/release/mutsu`; without it the column reads `—`.

`--suite=mutsu` times mutsu's own benchmark files (every `benchmarks/*.raku`
of `$MUTSU_ROOT`, default `~/mutsu`) instead of the kernels above; it is
opt-in, and a plain run is always this table's kernels. These are the files
mutsu's CI times Raku++ on. A file that prints `bench-section-seconds:` gets a
second `name@section` row with the in-process time of the operation it
measures. `--no-native` skips the `--exe` lane. The harness
checks the architecture of `$RAKUPP` only, so check the references yourself:

```sh
file $(which rakudo)               # want: Mach-O 64-bit executable arm64
rakudo -e 'say $*KERNEL.hardware'  # want: arm64 — reads x86_64 when translated
```

Every released build can be re-measured the same way with
[`tools/rakupp-bench-sweep.sh`](../../tools/rakupp-bench-sweep.sh), which
fetches each release's binary and emits one TSV.
