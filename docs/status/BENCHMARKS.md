# Raku++ vs Rakudo vs mutsu — speed

How fast Raku++ runs a set of small programs that every engine here runs
identically — as a tree-walking interpreter and as a native compiler — against
Rakudo, the reference implementation, and
[mutsu](https://github.com/tokuhirom/mutsu), the other from-scratch one.

**Measured 2026-10-08 at `v5.3.0`**, on the machine of record (Apple M3, macOS
Darwin 27.0.0), in one sitting: three interleaved passes of the kernel harness,
one of the `-O` harness, and two of the v5.2.1 release binary for the
comparison below, against **Rakudo 2026.09**. Earlier sittings, with the notes
that explained each movement, are in
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

- **Startup:** 2.9 ms for a native binary and 3.9 ms interpreting; mutsu 5.1
  ms, Rakudo 72.7 ms.
- **Against Rakudo 2026.09, `--exe` takes less time on seventeen of eighteen
  kernels**, from 1.1× on `objects` and 2.5× on `rats` to 86× on `mainwhen`
  and 102× on `intcat`. On `multiwhere` Rakudo is 4% ahead, and that binary is
  not compiled code: codegen declines a `where` on a multi candidate and
  bundles the interpreter.
- **The interpreter takes less time than Rakudo on seventeen of eighteen**,
  from 1.05× on `objects` to 45× on `mainwhen` and 121× on `intcat`;
  `multiwhere` is level (Rakudo 2% ahead).
- **Against mutsu, both modes take less time on every kernel mutsu was timed
  on** (seventeen; `intcat` takes mutsu about 160 s a run and was left out).
- **v5.3.0 against the v5.2.1 release binary, interpreted**: `arraypush` −86%,
  `rats` −85%, `hashfill` −52%, `hash` −48%, `objects` −40%, `multiwhere`
  −36%, `regex` −12%. Compiled, `multiwhere` −35%, `regex` −19%, `objects`
  −13%. `intcat`, 2M Int appends, takes 49.7 ms; v5.2.1 did not finish a run
  in a minute. The other rows moved by less than the 9% an artifact and a
  local build of the same code differ by. The table is
  [below](#v521-to-v530).
- **The loop kernels now reach arrays, hashes, Nums and Rats**, so
  `arraypush` (18.2 ms) and `rats` (26.0 ms) run faster interpreted than
  their `--exe` binaries (56.8 and 91.0 ms), whose code does not have those
  lanes.
- **Against Perl 5**, the interpreter is 1.2× faster on `hashfill` and 1.5×
  on `intcat`, `--exe` 1.8× and 1.3×; perl is 2.4× faster than `--exe` on
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
| intcat | 49.7 ms | — | 6006.6 ms | **120.9×** | — |
| mainwhen | 6.4 ms | 88.1 ms | 285.2 ms | **44.6×** | **13.8×** |
| loopsum | 5.2 ms | 54.7 ms | 175.2 ms | **33.7×** | **10.5×** |
| bigint | 6.4 ms | 8.5 ms | 162.3 ms | **25.4×** | **1.3×** |
| fib | 13.9 ms | 143.0 ms | 302.9 ms | **21.8×** | **10.3×** |
| strcat | 4.3 ms | 7.6 ms | 91.3 ms | **21.2×** | **1.8×** |
| hash | 7.5 ms | 26.6 ms | 125.9 ms | **16.8×** | **3.5×** |
| arraypush | 18.2 ms | 121.3 ms | 258.7 ms | **14.2×** | **6.7×** |
| streq | 17.6 ms | 488.2 ms | 186.1 ms | **10.6×** | **27.7×** |
| sortnums | 18.1 ms | 25.1 ms | 164.7 ms | **9.1×** | **1.4×** |
| rats | 26.0 ms | 285.5 ms | 225.6 ms | **8.7×** | **11.0×** |
| sortby | 21.0 ms | 30.7 ms | 143.7 ms | **6.8×** | **1.5×** |
| regex | 27.8 ms | 84.8 ms | 182.9 ms | **6.6×** | **3.1×** |
| hashfill | 42.0 ms | 180.3 ms | 271.5 ms | **6.5×** | **4.3×** |
| arrayops | 34.8 ms | 95.6 ms | 177.1 ms | **5.1×** | **2.7×** |
| textsplit | 59.4 ms | 90.9 ms | 191.7 ms | **3.2×** | **1.5×** |
| objects | 203.3 ms | 568.8 ms | 213.4 ms | **1.05×** | **2.8×** |
| multiwhere | 228.8 ms | 6331.0 ms | 224.1 ms | Rakudo 1.02× | **27.7×** |

The integer and loop kernels run a sub or a loop on machine values when
everything in it is one of the shapes they cover. In v5.2.0 that meant plain
Int and Str variables (`fib`, `loopsum`, `mainwhen`, `streq`); since v5.3.0 it
includes array and hash elements read, written and pushed in place, Nums, Rats
and `**`, which is where `arraypush`, `hash` and `rats` moved from. `intcat`
is issue #130's shape — a string built by appending Ints — and is linear now
in every mode. `objects` — 200k `.new`, 300k method calls, 500k attribute
reads — is the one kernel that measures `class`/`has`/method dispatch, and
`multiwhere` is 400k calls into a `where`-constrained multi candidate; both
dropped about 40% this release, from a dispatch cache that covers `where`
candidates and a direct `.new` for plain classes.

### Native (`--exe`) vs Rakudo and mutsu

The last column is the speed-up over interpreting the same program.

| Benchmark | Raku++ (`--exe`) | mutsu | Rakudo | vs Rakudo | vs mutsu | vs interp |
|---|---:|---:|---:|---|---|---:|
| intcat | 59.1 ms | — | 6006.6 ms | **101.6×** | — | 0.8× |
| mainwhen | 3.3 ms | 88.1 ms | 285.2 ms | **86.4×** | **26.7×** | 1.9× |
| loopsum | 3.2 ms | 54.7 ms | 175.2 ms | **54.8×** | **17.1×** | 1.6× |
| fib | 5.8 ms | 143.0 ms | 302.9 ms | **52.2×** | **24.7×** | 2.4× |
| bigint | 5.3 ms | 8.5 ms | 162.3 ms | **30.6×** | **1.6×** | 1.2× |
| strcat | 3.4 ms | 7.6 ms | 91.3 ms | **26.9×** | **2.2×** | 1.3× |
| streq | 7.3 ms | 488.2 ms | 186.1 ms | **25.5×** | **66.9×** | 2.4× |
| hash | 7.3 ms | 26.6 ms | 125.9 ms | **17.2×** | **3.6×** | 1.0× |
| sortnums | 12.0 ms | 25.1 ms | 164.7 ms | **13.7×** | **2.1×** | 1.5× |
| regex | 18.0 ms | 84.8 ms | 182.9 ms | **10.2×** | **4.7×** | 1.5× |
| sortby | 15.0 ms | 30.7 ms | 143.7 ms | **9.6×** | **2.0×** | 1.4× |
| hashfill | 28.8 ms | 180.3 ms | 271.5 ms | **9.4×** | **6.3×** | 1.5× |
| textsplit | 32.9 ms | 90.9 ms | 191.7 ms | **5.8×** | **2.8×** | 1.8× |
| arrayops | 33.8 ms | 95.6 ms | 177.1 ms | **5.2×** | **2.8×** | 1.0× |
| arraypush | 56.8 ms | 121.3 ms | 258.7 ms | **4.6×** | **2.1×** | 0.3× |
| rats | 91.0 ms | 285.5 ms | 225.6 ms | **2.5×** | **3.1×** | 0.3× |
| objects | 188.2 ms | 568.8 ms | 213.4 ms | **1.1×** | **3.0×** | 1.1× |
| multiwhere | 232.8 ms | 6331.0 ms | 224.1 ms | Rakudo 1.04× | **27.2×** | 1.0× |

Three rows are slower compiled than interpreted. In each, every loop runs as
an interpreter kernel (`RAKUPP_KERNEL_TRACE=1` names them), which since this
release takes array elements, pushes, Nums and Rats, while the `--exe`
binaries of `arraypush` and `rats` stand where v5.2.1's did (53.9 → 56.8 and
91.1 → 91.0 ms). Elsewhere compiling gains least where the interpreter already runs a kernel —
`fib`, `loopsum`, `mainwhen`, `streq` are within 1.6-2.4× of their binaries,
and much of what is left on those rows is process startup — and nothing where
the time is inside a runtime method both modes share: `arrayops`
(`.grep`/`.map`/`.sum`), `hash`, `bigint` (the `BigInt` multiply) and
`multiwhere` (the bundled interpreter).

### v5.2.1 to v5.3.0

v5.2.1 is its release binary (`rakupp-macos-universal.tar.gz`, sha256 checked,
the arm64 slice), timed in this sitting in two passes. v5.3.0 is the local
build at `5.3.0-g042a74ad`, three passes. At the last sitting the v5.2.0
artifact read −6% to +9% against a local build of the same code, so a row that
moved by less than that is not a change in either direction. The Rakudo lane,
timed beside both, held within 3% between the two halves of the sitting.
Milliseconds, minimum across the passes.

| kernel | interp v5.2.1 | interp v5.3.0 | | `--exe` v5.2.1 | `--exe` v5.3.0 | |
|---|---:|---:|---:|---:|---:|---:|
| arraypush | 126.6 | 18.2 | −86% | 53.9 | 56.8 | +5% |
| rats | 172.9 | 26.0 | −85% | 91.1 | 91.0 | 0% |
| hashfill | 87.3 | 42.0 | −52% | 29.1 | 28.8 | −1% |
| hash | 14.5 | 7.5 | −48% | 7.5 | 7.3 | −3% |
| objects | 338.2 | 203.3 | −40% | 215.5 | 188.2 | −13% |
| multiwhere | 357.4 | 228.8 | −36% | 355.6 | 232.8 | −35% |
| regex | 31.7 | 27.8 | −12% | 22.2 | 18.0 | −19% |
| startup | 4.3 | 3.9 | −9% | 3.1 | 2.9 | −6% |
| loopsum | 5.7 | 5.2 | −9% | 3.3 | 3.2 | −3% |
| sortnums | 19.7 | 18.1 | −8% | 11.6 | 12.0 | +3% |
| strcat | 4.6 | 4.3 | −7% | 3.3 | 3.4 | +3% |
| bigint | 6.8 | 6.4 | −6% | 5.3 | 5.3 | 0% |
| mainwhen | 6.8 | 6.4 | −6% | 3.3 | 3.3 | 0% |
| sortby | 22.1 | 21.0 | −5% | 15.4 | 15.0 | −3% |
| streq | 18.0 | 17.6 | −2% | 7.3 | 7.3 | 0% |
| fib | 13.8 | 13.9 | +1% | 5.5 | 5.8 | +5% |
| arrayops | 34.0 | 34.8 | +2% | 33.3 | 33.8 | +2% |
| textsplit | 57.5 | 59.4 | +3% | 32.5 | 32.9 | +1% |
| intcat | — | 49.7 | | — | 59.1 | |

The interpreted gains are the loop kernels reaching arrays, hashes, Nums and
Rats (`arraypush`, `rats`, `hash`, `hashfill`), the `where`-aware dispatch
cache and the direct `.new` (`multiwhere`, `objects`), and regex search that
skips start positions no match can begin at (`regex`)
([CHANGELOG](../../CHANGELOG.md)). `intcat` has no v5.2.1 figure: each `~=`
of an Int copied the whole string there.

**Method calls are slower.** These kernels make few calls each, so they do not
show it; perf-guard's call kernels do. Against a local build of v5.2.1 in two
interleaved rounds, `privmeth` is 9.5% slower, `strpass` 9.0%, `method` 8.6%,
`attrread` 5.8%, and `subcall`, `multimeth` and `junctionwide` about 4.5%;
retired instructions grew 6–7% on the first three. It built up over many
commits of the cycle rather than in one. The optimizer harness's
`methodcalls` (below) shows it under `--exe` as well.

### Startup

[`startup.raku`](../../tools/bench/startup.raku) is `say "Hello, World!"`, so
this row is process startup and almost nothing else.

| mode | startup | vs Rakudo |
|---|---:|---:|
| Raku++ native `--exe` | 2.9 ms | **25.1×** |
| Raku++ interp | 3.9 ms | **18.6×** |
| mutsu | 5.1 ms | **14.3×** |
| Rakudo | 72.7 ms | — |

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
| Raku++ `--exe` | 28.8 ms | **1.8× faster** | 32.9 ms | 2.4× slower | 59.1 ms | **1.3× faster** |
| Raku++ interp | 42.0 ms | **1.2× faster** | 59.4 ms | 4.4× slower | 49.7 ms | **1.5× faster** |
| Perl 5 | 51.7 ms | — | 13.5 ms | — | 74.5 ms | — |
| mutsu | 180.3 ms | 3.5× slower | 90.9 ms | 6.7× slower | — | — |
| Rakudo | 271.5 ms | 5.3× slower | 191.7 ms | 14.2× slower | 6006.6 ms | 80.6× slower |

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
best of 5.

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
  rebuilt for this sitting. Not timed on `intcat`. Native arm64: check `file` on the binary, since an
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
- **This sitting:** re-measured 2026-10-08 at `v5.3.0` — the local build is
  `5.3.0-g042a74ad`, the version commit. It carries all of v5.3.0's code but
  1305af21, a parser fix for a statement prefix with two modifiers, which no
  kernel here goes through.
  `intcat` ran in three passes of its own after the other five, without the
  mutsu lane. The 1-minute load stayed between about 3 and 4.5: WindowServer
  held about a third of a core throughout, and `ecosystemd`, `biomesyncd` and an
  iOS Simulator took up to a whole one at times. 59 of the 78 cells agree within
  4% across the three passes; the worst are `hashfill` under perl (13.2%) and
  mutsu (10.1%), `intcat` under Rakudo (7.5%) and the 3 ms native `startup`
  (6.9%). The Rakudo lane held within 3% between the v5.3.0 and the v5.2.1
  passes.

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
