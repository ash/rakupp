# Raku++ vs Rakudo vs mutsu — speed

How fast Raku++ runs a set of small programs that every engine here runs
identically — as a tree-walking interpreter and as a native compiler — against
Rakudo, the reference implementation, and
[mutsu](https://github.com/tokuhirom/mutsu), the other from-scratch one.

**Measured 2026-09-30 at `v5.1.0`**, on the machine of record (Apple M3, macOS
Darwin 27.0.0), in one sitting: three interleaved passes of the kernel harness
and one of the `-O` harness, against **Rakudo 2026.09**. Earlier sittings, with
the notes that explained each movement, are in
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
- **perl** — Perl 5, for the two kernels that ship a `.pl` twin

`--bundle` and `--aot` tree-walk the program, so they run at `interp` speed and
are not shown separately. [Raku.js](../../rakujs), the interpreter compiled to
WebAssembly, is measured in
[rakujs/INTERNALS.md](../../rakujs/INTERNALS.md#performance-vs-native-and-node-vs-bun-vs-browser).

## The short version

- **Startup:** 2.9 ms for a native binary and 3.6 ms interpreting; mutsu 5.2
  ms, Rakudo 71.0 ms.
- **`--exe` beats Rakudo on sixteen of seventeen kernels**, from 1.2× on
  `objects` and 2.1× on `rats` to 14.0× on `loopsum`, 25.6× on `strcat` and
  31.4× on `bigint`. The seventeenth, `multiwhere`, is 3.1× behind, and its
  binary is not compiled code: codegen declines a `where` on a multi candidate
  and bundles the interpreter.
- **The interpreter beats Rakudo on thirteen of seventeen.** It loses `fib`
  (1.3×), `streq` (1.4×), `objects` (1.8×) and `multiwhere` (3.2×).
- **Against mutsu, `--exe` wins all seventeen and the interpreter fourteen.**
  The interpreter loses `fib` (3.0×), `loopsum` (1.3×) and `arraypush` (1.3×).
- **v5.1.0 interprets faster than v5.0.0** on sixteen of eighteen kernels:
  `arrayops` −29%, `loopsum` −23%, `streq` −19%, `hash` −18%, `sortby` and
  `mainwhen` −16%, `strcat` −14%, `hashfill` and `rats` −11%. `fib` is −2%,
  `objects` +1% and `multiwhere` +2%. Compiled, `arrayops` −29%, `sortby`
  −20%, `rats` −16% and `fib` −7%; `mainwhen` is 8% slower. The table is
  [below](#v500-to-v510).
- **Rakudo 2026.09 is faster than 2026.08** on most of these kernels, so a
  ratio against Rakudo can fall where Raku++ got faster: `loopsum` 218 → 173 ms,
  `streq` 282 → 183, `sortnums` 221 → 164, `fib` 348 → 300.
- **Against Perl 5**, `--exe` is 1.6× faster on `hashfill` and perl 2.4× faster
  on `textsplit`.

## Results

Best of 6 timed runs per engine (7 spawned, the first discarded), minimum
across three passes, process startup included; lower is better. Rows are
ordered by the ratio against Rakudo, most favourable to Raku++ first. **Bold**
marks a Raku++ lead; otherwise the faster engine is named. All four engines
produced byte-identical output on every kernel in every pass.

### Interpreter vs Rakudo and mutsu

| Benchmark | Raku++ (interp) | mutsu | Rakudo | vs Rakudo | vs mutsu |
|---|---:|---:|---:|---|---|
| bigint | 6.5 ms | 8.6 ms | 160.0 ms | **24.6×** | **1.3×** |
| strcat | 8.3 ms | 8.4 ms | 89.7 ms | **10.8×** | **1.0×** |
| sortnums | 25.7 ms | 36.1 ms | 164.1 ms | **6.4×** | **1.4×** |
| hash | 19.9 ms | 44.8 ms | 125.9 ms | **6.3×** | **2.3×** |
| sortby | 25.1 ms | 34.7 ms | 143.9 ms | **5.7×** | **1.4×** |
| regex | 35.3 ms | 91.2 ms | 181.8 ms | **5.2×** | **2.6×** |
| arrayops | 37.4 ms | 85.9 ms | 174.2 ms | **4.7×** | **2.3×** |
| mainwhen | 77.9 ms | 83.7 ms | 281.7 ms | **3.6×** | **1.1×** |
| textsplit | 60.6 ms | 97.1 ms | 187.4 ms | **3.1×** | **1.6×** |
| hashfill | 106.2 ms | 200.4 ms | 272.2 ms | **2.6×** | **1.9×** |
| loopsum | 79.9 ms | 59.4 ms | 173.2 ms | **2.2×** | mutsu 1.3× |
| arraypush | 153.3 ms | 120.7 ms | 260.2 ms | **1.7×** | mutsu 1.3× |
| rats | 222.8 ms | 362.6 ms | 227.1 ms | **1.0×** | **1.6×** |
| fib | 379.2 ms | 127.9 ms | 300.1 ms | Rakudo 1.3× | mutsu 3.0× |
| streq | 258.3 ms | 438.9 ms | 183.1 ms | Rakudo 1.4× | **1.7×** |
| objects | 389.9 ms | 727.5 ms | 212.7 ms | Rakudo 1.8× | **1.9×** |
| multiwhere | 707.3 ms | 6182.7 ms | 223.2 ms | Rakudo 3.2× | **8.7×** |

`objects` — 200k `.new`, 300k method calls, 500k attribute reads — is the one
kernel that measures `class`/`has`/method dispatch, and Rakudo's lead there is
spesh: type-specialised dispatch and inlining, which neither from-scratch
engine has. mutsu's `fib` lead is its JIT doing what a JIT is for, tiny-body
recursion entered 1.6 million times; `--exe` compiles the same program to
49.8 ms, 2.6× faster than that JIT. `multiwhere` is 400k calls into a
`where`-constrained multi candidate.

### Native (`--exe`) vs Rakudo and mutsu

The last column is the speed-up over interpreting the same program.

| Benchmark | Raku++ (`--exe`) | mutsu | Rakudo | vs Rakudo | vs mutsu | vs interp |
|---|---:|---:|---:|---|---|---:|
| bigint | 5.1 ms | 8.6 ms | 160.0 ms | **31.4×** | **1.7×** | 1.3× |
| strcat | 3.5 ms | 8.4 ms | 89.7 ms | **25.6×** | **2.4×** | 2.4× |
| hash | 7.8 ms | 44.8 ms | 125.9 ms | **16.1×** | **5.7×** | 2.6× |
| loopsum | 12.4 ms | 59.4 ms | 173.2 ms | **14.0×** | **4.8×** | 6.4× |
| mainwhen | 23.0 ms | 83.7 ms | 281.7 ms | **12.2×** | **3.6×** | 3.4× |
| sortnums | 13.7 ms | 36.1 ms | 164.1 ms | **12.0×** | **2.6×** | 1.9× |
| streq | 20.0 ms | 438.9 ms | 183.1 ms | **9.2×** | **21.9×** | 12.9× |
| sortby | 16.0 ms | 34.7 ms | 143.9 ms | **9.0×** | **2.2×** | 1.6× |
| regex | 21.8 ms | 91.2 ms | 181.8 ms | **8.3×** | **4.2×** | 1.6× |
| hashfill | 34.1 ms | 200.4 ms | 272.2 ms | **8.0×** | **5.9×** | 3.1× |
| fib | 49.8 ms | 127.9 ms | 300.1 ms | **6.0×** | **2.6×** | 7.6× |
| textsplit | 32.4 ms | 97.1 ms | 187.4 ms | **5.8×** | **3.0×** | 1.9× |
| arrayops | 35.6 ms | 85.9 ms | 174.2 ms | **4.9×** | **2.4×** | 1.1× |
| arraypush | 58.3 ms | 120.7 ms | 260.2 ms | **4.5×** | **2.1×** | 2.6× |
| rats | 108.5 ms | 362.6 ms | 227.1 ms | **2.1×** | **3.3×** | 2.1× |
| objects | 182.8 ms | 727.5 ms | 212.7 ms | **1.2×** | **4.0×** | 2.1× |
| multiwhere | 702.9 ms | 6182.7 ms | 223.2 ms | Rakudo 3.1× | **8.8×** | 1.0× |

Compiling gains most where a tree-walker re-dispatches a tiny body many times —
`streq` 12.9×, `fib` 7.6×, `loopsum` 6.4× — and almost nothing where the time
is inside a runtime method both modes share: `arrayops` (`.grep`/`.map`/`.sum`),
`bigint` (the `BigInt` multiply) and `multiwhere` (the bundled interpreter).

### v5.0.0 to v5.1.0

Both local builds, `build-arm64/rakupp` at `5.0.0-g312cde8e` (the 2026-09-29
sitting) and at `5.1.0-gabc6652c` (this one). mutsu is the same binary in both
sittings and is the control: it read within 3% on seventeen of its eighteen
kernels, and so did perl on its two. (mutsu's `multiwhere`, 22.4 s then and
6.2 s now, is the exception and is not understood.) The Rakudo lane cannot be
the control this time, because it moved from 2026.08 to 2026.09. Milliseconds,
minimum of three passes each.

| kernel | interp v5.0.0 | interp v5.1.0 | | `--exe` v5.0.0 | `--exe` v5.1.0 | |
|---|---:|---:|---:|---:|---:|---:|
| arrayops | 52.4 | 37.4 | −29% | 50.0 | 35.6 | −29% |
| loopsum | 104.0 | 79.9 | −23% | 12.6 | 12.4 | −2% |
| streq | 320.3 | 258.3 | −19% | 19.6 | 20.0 | +2% |
| hash | 24.2 | 19.9 | −18% | 7.7 | 7.8 | +1% |
| sortby | 30.0 | 25.1 | −16% | 19.9 | 16.0 | −20% |
| mainwhen | 92.8 | 77.9 | −16% | 21.2 | 23.0 | +8% |
| strcat | 9.6 | 8.3 | −14% | 3.4 | 3.5 | +3% |
| hashfill | 119.9 | 106.2 | −11% | 33.8 | 34.1 | +1% |
| rats | 250.2 | 222.8 | −11% | 129.3 | 108.5 | −16% |
| arraypush | 168.3 | 153.3 | −9% | 56.6 | 58.3 | +3% |
| regex | 38.5 | 35.3 | −8% | 21.4 | 21.8 | +2% |
| startup | 3.7 | 3.6 | −3% | 3.0 | 2.9 | −3% |
| sortnums | 26.2 | 25.7 | −2% | 13.4 | 13.7 | +2% |
| fib | 386.3 | 379.2 | −2% | 53.4 | 49.8 | −7% |
| bigint | 6.6 | 6.5 | −2% | 5.1 | 5.1 | 0% |
| textsplit | 61.4 | 60.6 | −1% | 31.5 | 32.4 | +3% |
| objects | 387.0 | 389.9 | +1% | 183.3 | 182.8 | 0% |
| multiwhere | 694.8 | 707.3 | +2% | 694.8 | 702.9 | +1% |

The interpreter's gains are the per-node work taken off the hot path — a block
that does its entry and exit work only when it has some, and operators that
find their own code without comparing their spelling against every other
([CHANGELOG](../../CHANGELOG.md)). `fib` and `objects`, which spend their time
in the call path, did not move; v5.0.0's cost there, measured against v4.0.1 in
the [history](../dev/findings/BENCHMARKS-HISTORY.md#v401-to-v500), is still
owed. `mainwhen`'s compiled binary read 23.0-23.4 ms in all three passes, where
the v5.0.1 release artifact read 22.2-22.5 in the same sitting.

### Startup

[`startup.raku`](../../tools/bench/startup.raku) is `say "Hello, World!"`, so
this row is process startup and almost nothing else.

| mode | startup | vs Rakudo |
|---|---:|---:|
| Raku++ native `--exe` | 2.9 ms | **24.5×** |
| Raku++ interp | 3.6 ms | **19.7×** |
| mutsu | 5.2 ms | **13.7×** |
| Rakudo | 71.0 ms | — |

A native Raku++ binary has no VM to bring up and no precompiled setting to
load; Rakudo's ~70 ms is a fixed cost inside every row on this page. mutsu is
in the same order of magnitude as Raku++ for the same reason.

### vs Perl 5

[`hashfill`](../../tools/bench/hashfill.raku) fills a 200k-key hash through
interpolated keys, sweeps `%h.values`, and builds a string with 50k `~=`
appends; [`textsplit`](../../tools/bench/textsplit.raku) splits 20k lines into
fields, reorders and rejoins them. Each has a line-for-line
[`.pl`](../../tools/bench/hashfill.pl) twin, timed by the same harness with the
same output check. perl is v5.44.0; the two other perls on the machine (5.34.3
and 5.34.1) time within 4% of it on both.

| engine | hashfill | vs perl | textsplit | vs perl |
|---|---:|---:|---:|---:|
| Raku++ `--exe` | 34.1 ms | **1.6× faster** | 32.4 ms | 2.4× slower |
| Perl 5 | 55.3 ms | — | 13.3 ms | — |
| Raku++ interp | 106.2 ms | 1.9× slower | 60.6 ms | 4.6× slower |
| mutsu | 200.4 ms | 3.6× slower | 97.1 ms | 7.3× slower |
| Rakudo | 272.2 ms | 4.9× slower | 187.4 ms | 14.1× slower |

`--exe` wins `hashfill` because the hash payload is an insertion-ordered open
hash in the perl mold
([PERL5-TECHNIQUES.md](../dev/findings/engines/PERL5-TECHNIQUES.md)); text
munging is where perl still leads.

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
| intsum      | 106.7 ms | **4.1 ms** | **26.0×** | 597.3 ms | 5M int accumulation — `+=` lane, zero boxing |
| powmod      | 468.6 ms | **19.9 ms** | **23.6×** | 454.6 ms | 1M `** 3` then `% 1000` — inline pow + mod lane |
| sieve       | 461.4 ms | **21.4 ms** | **21.6×** | 1403.7 ms | primes < 200k by trial division — `* <= %%` all laned |
| fibcalls    | 198.0 ms | **61.7 ms** | **3.2×** | 973.7 ms | fib(32) — direct-arity calls + int-lane condition |
| arrayidx    | 97.5 ms | **46.9 ms** | **2.1×** | 635.0 ms | 2M `@a[$i]` read-modify-write — no element lane yet |
| nummath     | 193.0 ms | 168.5 ms | 1.1× | 442.0 ms | Mandelbrot escape count — `Num` math, and the F64 lane does not reach this shape |
| methodcalls | 141.6 ms | 125.3 ms | 1.1× | 277.3 ms | 1M monomorphic method calls — not devirtualized yet |
| stringbuild | 6.3 ms | 6.2 ms | 1.0× | 123.0 ms | 400k `~=` appends — in-place O(n) string build |
| bigmul      | 12.6 ms | 12.6 ms | 1.0× | 810.6 ms | 10000! by `*=` — the bignum compound-assign lane, no `-O` route |

Plain `--exe` on `sieve` is 461 ms where v5.0.0's table read 977; `-O` is
unchanged at ~21 ms.

The bottom four name what `-O` does not reach: no element lane for indexed
array access, no lane for this shape of `Num` math (the floating-point lane in
[UNBOX-PLAN.md](../dev/plans/UNBOX-PLAN.md) fires elsewhere, not here), no
devirtualized method call, and nothing to add where the time is already in-place
append or the bignum multiply. `-O` is off by default and produces identical
output.

### Real-world: grammar parsing (YAMLish)

A whole module doing real work: YAMLish 0.1.3 (zef:leont, unmodified) parsing
the Raku course's table of contents (`_data/toc/en.yaml`, 2,653 lines) with
`load-yamls`. The grammar exercises parameterised tokens, `|` alternations,
lookbehinds, `:my` state, aliased captures and action methods. Both engines
produce the same data, compared as sorted-key JSON. Best of 5, wall-clock, the
two engines interleaved; measured 2026-09-29 at `v5.0.1` against Rakudo
v2026.08, and not re-run for v5.1.0. It needs `v5.0.1` or later: `v5.0.0` does
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
  interpreter, because codegen declines a `where` on a multi candidate).
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
  two are within 6% on fourteen of these seventeen kernels; RakuAST is faster on
  `streq` (0.81×), `startup` (0.88×) and `loopsum` (0.91×).
- **mutsu:** upstream `f74838a` (2026-09-29, still versioned 0.23.0),
  `cargo build --release` with default features (Cranelift JIT on). Native
  arm64: check `file` on the binary, since an x86_64 build would run under
  Rosetta and the harness would not notice.
- **perl:** v5.44.0 (`/opt/homebrew/bin/perl`), for the two kernels with a
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
- **This sitting:** re-measured 2026-09-30 at `v5.1.0` — the local build is
  `5.1.0-gabc6652c`, the version commit, which carries all of v5.1.0's code.
  The 1-minute load stayed between 2.4 and 3.0. The three passes agree within
  4% on every cell but `objects --exe` (5.4%) and the 3 ms `startup` (6.9%).

## Reproducing

```sh
RAKUDO=/opt/homebrew/bin/rakudo ./build-arm64/rakupp tools/run-bench.raku
./build-arm64/rakupp tools/run-bench.raku --only=fib,objects --tsv=out.tsv
```

`--tsv=` writes the minimum and median per lane with the CPU and toolchain the
sitting ran on; `--rusage` adds CPU time and peak RSS. The mutsu lane is
optional and found as `$MUTSU`, then `mutsu` on `PATH`, then
`$HOME/mutsu/target/release/mutsu`; without it the column reads `—`. The harness
checks the architecture of `$RAKUPP` only, so check the references yourself:

```sh
file $(which rakudo)               # want: Mach-O 64-bit executable arm64
rakudo -e 'say $*KERNEL.hardware'  # want: arm64 — reads x86_64 when translated
```

Every released build can be re-measured the same way with
[`tools/rakupp-bench-sweep.sh`](../../tools/rakupp-bench-sweep.sh), which
fetches each release's binary and emits one TSV.
