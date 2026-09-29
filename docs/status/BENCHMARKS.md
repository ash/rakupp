# Raku++ vs Rakudo vs mutsu — speed

How fast Raku++ runs a set of small programs that every engine here runs
identically — as a tree-walking interpreter and as a native compiler — against
Rakudo, the reference implementation, and
[mutsu](https://github.com/tokuhirom/mutsu), the other from-scratch one.

**Measured 2026-09-29 at `v5.0.0`**, on the machine of record (Apple M3, macOS
Darwin 27.0.0), in one sitting: three interleaved passes of the kernel harness,
one of the `-O` harness, and two of the v5.0.0 release artifact as a check on
the local build. Earlier sittings, with the notes that explained each movement,
are in [findings/BENCHMARKS-HISTORY.md](../dev/findings/BENCHMARKS-HISTORY.md);
[raku.online/spec/dashboard](https://raku.online/spec/dashboard/) charts every
release.

The lanes:

- **interp** — Raku++ interpreting the source (tree-walk)
- **native** — Raku++ `--exe`: the program transpiled to C++ and compiled to a
  native binary
- **mutsu** — mutsu interpreting the source (Rust bytecode VM with a Cranelift
  JIT, on by default as it ships)
- **rakudo** — Rakudo interpreting the source (MoarVM)
- **perl** — Perl 5, for the two kernels that ship a `.pl` twin

`--bundle` and `--aot` tree-walk the program, so they run at `interp` speed and
are not shown separately. [Raku.js](../../rakujs), the interpreter compiled to
WebAssembly, is measured in
[rakujs/INTERNALS.md](../../rakujs/INTERNALS.md#performance-vs-native-and-node-vs-bun-vs-browser).

## The short version

- **Startup:** 3.0 ms for a native binary and 3.7 ms interpreting; mutsu 5.3
  ms, Rakudo 81.6 ms.
- **`--exe` beats Rakudo on sixteen of seventeen kernels**, from 1.3× on
  `objects` and 2.1× on `rats` to 17.3× on `loopsum`, 27.7× on `strcat` and
  32.3× on `bigint`. The seventeenth, `multiwhere`, is 2.8× behind, and its
  binary is not compiled code: codegen declines a `where` on a multi candidate
  and bundles the interpreter.
- **The interpreter beats Rakudo on thirteen of seventeen.** It loses `fib`
  and `streq` (1.1× each), `objects` (1.6×) and `multiwhere` (2.8×).
- **Against mutsu, `--exe` wins all seventeen and the interpreter twelve.** The
  interpreter loses `fib` (3.0×), `loopsum` (1.8×), `arraypush` (1.4×), `strcat`
  (1.2×) and `mainwhen` (1.1×). mutsu at `f74838a` is much faster than the
  2026-08-31 build this page measured before — `fib` 245 → 128 ms, `loopsum`
  120 → 59, `strcat` 132 → 8.3, `objects` 1,247 → 718.
- **v5.0.0 is slower than v4.0.1 when interpreting.** Seventeen of eighteen
  kernels, from +1% (`bigint`) to +30% (`fib`) and +41% (`streq`); compiled, −3%
  to +14%. `mainwhen` is the exception, 52% faster interpreted and 83% compiled.
  The per-kernel table is [below](#v401-to-v500), and the
  [CHANGELOG](../../CHANGELOG.md) says where the cost came from.
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
| bigint | 6.6 ms | 8.6 ms | 164.9 ms | **25.0×** | **1.3×** |
| strcat | 9.6 ms | 8.3 ms | 94.2 ms | **9.8×** | mutsu 1.2× |
| sortnums | 26.2 ms | 36.4 ms | 221.2 ms | **8.4×** | **1.4×** |
| sortby | 30.0 ms | 34.0 ms | 184.3 ms | **6.1×** | **1.1×** |
| hash | 24.2 ms | 44.3 ms | 131.0 ms | **5.4×** | **1.8×** |
| regex | 38.5 ms | 91.4 ms | 202.8 ms | **5.3×** | **2.4×** |
| arrayops | 52.4 ms | 84.8 ms | 209.9 ms | **4.0×** | **1.6×** |
| textsplit | 61.4 ms | 97.9 ms | 208.5 ms | **3.4×** | **1.6×** |
| mainwhen | 92.8 ms | 86.1 ms | 285.4 ms | **3.1×** | mutsu 1.1× |
| hashfill | 119.9 ms | 200.0 ms | 293.3 ms | **2.4×** | **1.7×** |
| loopsum | 104.0 ms | 59.2 ms | 218.4 ms | **2.1×** | mutsu 1.8× |
| arraypush | 168.3 ms | 120.0 ms | 297.1 ms | **1.8×** | mutsu 1.4× |
| rats | 250.2 ms | 361.5 ms | 267.5 ms | **1.1×** | **1.4×** |
| fib | 386.3 ms | 127.6 ms | 348.2 ms | Rakudo 1.1× | mutsu 3.0× |
| streq | 320.3 ms | 448.8 ms | 281.6 ms | Rakudo 1.1× | **1.4×** |
| objects | 387.0 ms | 717.6 ms | 235.3 ms | Rakudo 1.6× | **1.9×** |
| multiwhere | 694.8 ms | 22427.3 ms | 249.9 ms | Rakudo 2.8× | **32.3×** |

`objects` — 200k `.new`, 300k method calls, 500k attribute reads — is the one
kernel that measures `class`/`has`/method dispatch, and Rakudo's lead there is
spesh: type-specialised dispatch and inlining, which neither from-scratch
engine has. mutsu's `fib` lead is its JIT doing what a JIT is for, tiny-body
recursion entered 1.6 million times; `--exe` compiles the same program to
53.4 ms, 2.4× faster than that JIT. `multiwhere` is 400k calls into a
`where`-constrained multi candidate, where mutsu takes 22 seconds.

### Native (`--exe`) vs Rakudo and mutsu

The last column is the speed-up over interpreting the same program.

| Benchmark | Raku++ (`--exe`) | mutsu | Rakudo | vs Rakudo | vs mutsu | vs interp |
|---|---:|---:|---:|---|---|---:|
| bigint | 5.1 ms | 8.6 ms | 164.9 ms | **32.3×** | **1.7×** | 1.3× |
| strcat | 3.4 ms | 8.3 ms | 94.2 ms | **27.7×** | **2.4×** | 2.8× |
| loopsum | 12.6 ms | 59.2 ms | 218.4 ms | **17.3×** | **4.7×** | 8.3× |
| hash | 7.7 ms | 44.3 ms | 131.0 ms | **17.0×** | **5.8×** | 3.1× |
| sortnums | 13.4 ms | 36.4 ms | 221.2 ms | **16.5×** | **2.7×** | 2.0× |
| streq | 19.6 ms | 448.8 ms | 281.6 ms | **14.4×** | **22.9×** | 16.3× |
| mainwhen | 21.2 ms | 86.1 ms | 285.4 ms | **13.5×** | **4.1×** | 4.4× |
| regex | 21.4 ms | 91.4 ms | 202.8 ms | **9.5×** | **4.3×** | 1.8× |
| sortby | 19.9 ms | 34.0 ms | 184.3 ms | **9.3×** | **1.7×** | 1.5× |
| hashfill | 33.8 ms | 200.0 ms | 293.3 ms | **8.7×** | **5.9×** | 3.5× |
| textsplit | 31.5 ms | 97.9 ms | 208.5 ms | **6.6×** | **3.1×** | 1.9× |
| fib | 53.4 ms | 127.6 ms | 348.2 ms | **6.5×** | **2.4×** | 7.2× |
| arraypush | 56.6 ms | 120.0 ms | 297.1 ms | **5.2×** | **2.1×** | 3.0× |
| arrayops | 50.0 ms | 84.8 ms | 209.9 ms | **4.2×** | **1.7×** | 1.0× |
| rats | 129.3 ms | 361.5 ms | 267.5 ms | **2.1×** | **2.8×** | 1.9× |
| objects | 183.3 ms | 717.6 ms | 235.3 ms | **1.3×** | **3.9×** | 2.1× |
| multiwhere | 694.8 ms | 22427.3 ms | 249.9 ms | Rakudo 2.8× | **32.3×** | 1.0× |

Compiling gains most where a tree-walker re-dispatches a tiny body many times —
`streq` 16.3×, `loopsum` 8.3×, `fib` 7.2× — and almost nothing where the time is
inside a runtime method both modes share: `arrayops` (`.grep`/`.map`/`.sum`),
`bigint` (the `BigInt` multiply) and `multiwhere` (the bundled interpreter).

### v4.0.1 to v5.0.0

Both release artifacts, `rakupp-macos-universal.tar.gz` run natively on this
machine: v4.0.1 on 2026-09-20 and v5.0.0 in this sitting. The Rakudo lane read
within 1.5% across the two days on sixteen kernels and 3.0% on `bigint`, so the
difference is the code. Milliseconds, best of the passes each release got.

| kernel | interp v4.0.1 | interp v5.0.0 | | `--exe` v4.0.1 | `--exe` v5.0.0 | |
|---|---:|---:|---:|---:|---:|---:|
| streq | 237.3 | 335.2 | +41% | 20.0 | 19.7 | −2% |
| fib | 308.6 | 400.6 | +30% | 50.2 | 53.7 | +7% |
| arraypush | 139.6 | 171.2 | +23% | 53.6 | 56.7 | +6% |
| rats | 208.2 | 248.1 | +19% | 118.4 | 127.4 | +8% |
| arrayops | 45.1 | 53.5 | +19% | 45.5 | 51.3 | +13% |
| textsplit | 51.7 | 61.2 | +18% | 29.7 | 31.4 | +6% |
| objects | 333.5 | 391.9 | +18% | 165.4 | 188.2 | +14% |
| regex | 34.1 | 39.9 | +17% | 21.2 | 21.1 | 0% |
| hash | 21.8 | 24.9 | +14% | 7.6 | 7.8 | +3% |
| multiwhere | 606.9 | 689.7 | +14% | 607.8 | 681.6 | +12% |
| loopsum | 91.0 | 102.7 | +13% | 13.1 | 12.7 | −3% |
| sortby | 27.2 | 30.6 | +13% | 18.2 | 20.5 | +13% |
| sortnums | 24.1 | 27.0 | +12% | 12.8 | 13.5 | +5% |
| hashfill | 108.0 | 119.5 | +11% | 33.5 | 34.5 | +3% |
| strcat | 9.1 | 9.8 | +8% | 3.3 | 3.5 | +6% |
| startup | 4.0 | 4.2 | +5% | 2.9 | 3.0 | +3% |
| bigint | 6.7 | 6.8 | +1% | 4.9 | 5.1 | +4% |
| mainwhen | 190.9 | 91.5 | −52% | 125.3 | 21.5 | −83% |

The interpreter paid for the v5 Roast work in steps of a few percent each; the
[CHANGELOG](../../CHANGELOG.md) lists the ones bisected and the two fixed
before the tag, and v6 starts from this table. `mainwhen` fell before the Roast
campaign began: the 2026-09-20 sitting at `v4.0.1-84` already read 80.5 ms
interpreted and 17.3 compiled.

### Startup

[`startup.raku`](../../tools/bench/startup.raku) is `say "Hello, World!"`, so
this row is process startup and almost nothing else.

| mode | startup | vs Rakudo |
|---|---:|---:|
| Raku++ native `--exe` | 3.0 ms | **27.2×** |
| Raku++ interp | 3.7 ms | **22.1×** |
| mutsu | 5.3 ms | **15.4×** |
| Rakudo | 81.6 ms | — |

A native Raku++ binary has no VM to bring up and no precompiled setting to
load; Rakudo's ~80 ms is a fixed cost inside every row on this page. mutsu is
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
| Raku++ `--exe` | 33.8 ms | **1.6× faster** | 31.5 ms | 2.4× slower |
| Perl 5 | 54.3 ms | — | 13.3 ms | — |
| Raku++ interp | 119.9 ms | 2.2× slower | 61.4 ms | 4.6× slower |
| mutsu | 200.0 ms | 3.7× slower | 97.9 ms | 7.4× slower |
| Rakudo | 293.3 ms | 5.4× slower | 208.5 ms | 15.7× slower |

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
| sieve       | 977.4 ms | **20.6 ms** | **47.4×** | 1622.1 ms | primes < 200k by trial division — `* <= %%` all laned |
| intsum      | 106.0 ms | **3.9 ms** | **26.8×** | 803.8 ms | 5M int accumulation — `+=` lane, zero boxing |
| powmod      | 527.8 ms | **19.7 ms** | **26.8×** | 534.2 ms | 1M `** 3` then `% 1000` — inline pow + mod lane |
| fibcalls    | 216.5 ms | **62.0 ms** | **3.5×** | 1155.3 ms | fib(32) — direct-arity calls + int-lane condition |
| arrayidx    | 95.7 ms | **47.1 ms** | **2.0×** | 901.4 ms | 2M `@a[$i]` read-modify-write — no element lane yet |
| nummath     | 207.7 ms | 167.1 ms | 1.2× | 660.7 ms | Mandelbrot escape count — `Num` math, and the F64 lane does not reach this shape |
| methodcalls | 143.2 ms | 125.0 ms | 1.1× | 347.4 ms | 1M monomorphic method calls — not devirtualized yet |
| stringbuild | 6.1 ms | 6.0 ms | 1.0× | 147.3 ms | 400k `~=` appends — in-place O(n) string build |
| bigmul      | 12.5 ms | 12.5 ms | 1.0× | 792.4 ms | 10000! by `*=` — the bignum compound-assign lane, no `-O` route |

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
two engines interleaved; measured 2026-09-29 against Rakudo v2026.08 with a
build newer than `v5.0.0`, which does not finish this parse in 60 s.

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
- **Rakudo:** v2026.08, the homebrew/core arm64 bottle at
  `/opt/homebrew/bin/rakudo`, passed to the harness **by path**. On this machine
  bare `raku` is Raku++, and `/usr/local/bin` precedes `/opt/homebrew/bin`.
  MoarVM has no JIT backend on arm64, so this Rakudo runs spesh without machine
  code; an x86_64 Rakudo column from another machine is not comparable.
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
- **This sitting:** re-measured 2026-09-29 at `v5.0.0` — the local build is
  `5.0.0-g312cde8e`, the last code commit before the tag. The 1-minute load
  stayed between 2.3 and 2.8. The three passes agree within 6% on every cell
  but Rakudo's `multiwhere`, which is bimodal on this machine (~250 or ~342 ms;
  the table keeps the minimum). The CI-built release artifact, timed in the same
  sitting, reads within 5% of the local build on every kernel, 13% on the 3 ms
  `startup`.

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
