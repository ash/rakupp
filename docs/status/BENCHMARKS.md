# Raku++ vs Rakudo vs mutsu — speed

How fast Raku++ runs a set of small programs that every engine here runs
identically — as a tree-walking interpreter and as a native compiler — against
Rakudo, the reference implementation, and
[mutsu](https://github.com/tokuhirom/mutsu), the other from-scratch one.

**Measured 2026-10-02 at `v5.2.0`**, on the machine of record (Apple M3, macOS
Darwin 27.0.0), in one sitting: three interleaved passes of the kernel harness,
one of the `-O` harness, and two of the v5.1.0 release binary for the
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
- **perl** — Perl 5, for the two kernels that ship a `.pl` twin

`--bundle` and `--aot` tree-walk the program, so they run at `interp` speed and
are not shown separately. [Raku.js](../../rakujs), the interpreter compiled to
WebAssembly, is measured in
[rakujs/INTERNALS.md](../../rakujs/INTERNALS.md#performance-vs-native-and-node-vs-bun-vs-browser).

## The short version

- **Startup:** 2.9 ms for a native binary and 3.8 ms interpreting; mutsu 4.8
  ms, Rakudo 72.3 ms.
- **`--exe` beats Rakudo on sixteen of seventeen kernels**, from 1.1× on
  `objects` and 2.5× on `rats` to 52× on `fib` and `loopsum` and 91× on
  `mainwhen`. The seventeenth, `multiwhere`, is 1.5× behind, and its binary is
  not compiled code: codegen declines a `where` on a multi candidate and
  bundles the interpreter.
- **The interpreter beats Rakudo on fifteen of seventeen.** It loses `objects`
  (1.5×) and `multiwhere` (1.6×).
- **Against mutsu, `--exe` wins all seventeen and the interpreter sixteen**;
  `arraypush` is level (mutsu 2% ahead).
- **v5.2.0 interprets faster than v5.1.0 on every kernel**: `fib` −97%,
  `loopsum` and `streq` −93%, `mainwhen` −92%, `strcat` −51%, `multiwhere`
  −49%, the rest −9% to −33%. The first four are the new integer and loop
  kernels, which run a sub or a loop on machine integers and strings when
  everything in it is one of the shapes they cover. Compiled, `fib` −88%,
  `mainwhen` −86%, `loopsum` −73%, `streq` −64%; `objects` is 9% slower. The
  table is [below](#v510-to-v520).
- **Against Perl 5**, `--exe` is 2.1× faster on `hashfill` and perl 2.4× faster
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
| mainwhen | 6.3 ms | 85.7 ms | 290.7 ms | **46.1×** | **13.6×** |
| loopsum | 5.3 ms | 54.6 ms | 176.8 ms | **33.4×** | **10.3×** |
| bigint | 6.4 ms | 8.5 ms | 164.4 ms | **25.7×** | **1.3×** |
| fib | 13.7 ms | 132.8 ms | 305.1 ms | **22.3×** | **9.7×** |
| strcat | 4.3 ms | 8.0 ms | 90.9 ms | **21.1×** | **1.9×** |
| streq | 17.7 ms | 466.9 ms | 185.9 ms | **10.5×** | **26.4×** |
| hash | 13.9 ms | 27.2 ms | 128.8 ms | **9.3×** | **2.0×** |
| sortnums | 18.2 ms | 26.5 ms | 167.0 ms | **9.2×** | **1.5×** |
| sortby | 20.8 ms | 32.8 ms | 146.8 ms | **7.1×** | **1.6×** |
| regex | 32.0 ms | 81.8 ms | 185.7 ms | **5.8×** | **2.6×** |
| arrayops | 32.6 ms | 55.7 ms | 177.1 ms | **5.4×** | **1.7×** |
| textsplit | 56.5 ms | 97.7 ms | 193.3 ms | **3.4×** | **1.7×** |
| hashfill | 90.0 ms | 180.2 ms | 277.9 ms | **3.1×** | **2.0×** |
| arraypush | 126.8 ms | 124.2 ms | 262.1 ms | **2.1×** | mutsu 1.0× |
| rats | 175.4 ms | 376.2 ms | 230.7 ms | **1.3×** | **2.1×** |
| objects | 324.5 ms | 656.0 ms | 218.3 ms | Rakudo 1.5× | **2.0×** |
| multiwhere | 360.5 ms | 6281.3 ms | 230.7 ms | Rakudo 1.6× | **17.4×** |

The top of this table is the integer and loop kernels at work: `fib`,
`loopsum`, `mainwhen` and `streq` are each one sub or one loop that uses only
plain Int and Str variables, so the whole of it runs on machine integers and
strings rather than through the tree-walker. A loop that touches an array, a
hash, a Num or Rat, or a method other than `.chars` runs the ordinary way, and
so does the rest of the table. `objects` — 200k `.new`, 300k method calls,
500k attribute reads — is the one kernel that measures `class`/`has`/method
dispatch, and Rakudo's lead there is spesh: type-specialised dispatch and
inlining, which neither from-scratch engine has. `multiwhere` is 400k calls
into a `where`-constrained multi candidate.

### Native (`--exe`) vs Rakudo and mutsu

The last column is the speed-up over interpreting the same program.

| Benchmark | Raku++ (`--exe`) | mutsu | Rakudo | vs Rakudo | vs mutsu | vs interp |
|---|---:|---:|---:|---|---|---:|
| mainwhen | 3.2 ms | 85.7 ms | 290.7 ms | **90.8×** | **26.8×** | 2.0× |
| fib | 5.8 ms | 132.8 ms | 305.1 ms | **52.6×** | **22.9×** | 2.4× |
| loopsum | 3.4 ms | 54.6 ms | 176.8 ms | **52.0×** | **16.1×** | 1.6× |
| bigint | 5.3 ms | 8.5 ms | 164.4 ms | **31.0×** | **1.6×** | 1.2× |
| strcat | 3.4 ms | 8.0 ms | 90.9 ms | **26.7×** | **2.4×** | 1.3× |
| streq | 7.2 ms | 466.9 ms | 185.9 ms | **25.8×** | **64.8×** | 2.5× |
| hash | 7.9 ms | 27.2 ms | 128.8 ms | **16.3×** | **3.4×** | 1.8× |
| sortnums | 11.7 ms | 26.5 ms | 167.0 ms | **14.3×** | **2.3×** | 1.6× |
| hashfill | 27.5 ms | 180.2 ms | 277.9 ms | **10.1×** | **6.6×** | 3.3× |
| sortby | 15.0 ms | 32.8 ms | 146.8 ms | **9.8×** | **2.2×** | 1.4× |
| regex | 22.6 ms | 81.8 ms | 185.7 ms | **8.2×** | **3.6×** | 1.4× |
| textsplit | 31.9 ms | 97.7 ms | 193.3 ms | **6.1×** | **3.1×** | 1.8× |
| arrayops | 31.9 ms | 55.7 ms | 177.1 ms | **5.6×** | **1.7×** | 1.0× |
| arraypush | 54.4 ms | 124.2 ms | 262.1 ms | **4.8×** | **2.3×** | 2.3× |
| rats | 90.6 ms | 376.2 ms | 230.7 ms | **2.5×** | **4.2×** | 1.9× |
| objects | 202.7 ms | 656.0 ms | 218.3 ms | **1.1×** | **3.2×** | 1.6× |
| multiwhere | 357.2 ms | 6281.3 ms | 230.7 ms | Rakudo 1.5× | **17.6×** | 1.0× |

Compiling gains least now where the interpreter already runs a kernel — `fib`,
`loopsum`, `mainwhen`, `streq` are within 1.6-2.5× of their binaries, and
much of what is left on those rows is process startup — and nothing where the
time is inside a runtime method both modes share: `arrayops`
(`.grep`/`.map`/`.sum`), `bigint` (the `BigInt` multiply) and `multiwhere` (the
bundled interpreter). `--exe` gives an integer sub an int64 twin in the
emitted C++ and runs its loop lanes without `-O`, which is where its own
`fib` −88% and `loopsum` −73% come from.

### v5.1.0 to v5.2.0

v5.1.0 is its release binary (`rakupp-macos-universal.tar.gz`, sha256 checked,
the arm64 slice), timed in this sitting in two passes. It reads 0-8% slower
than the v5.1.0 sitting's own local build (17% on the 3 ms `startup`), so on
the shortest kernels the gains below are a few points larger than against that
build. v5.2.0 is the local build at `5.2.0-g5b1c0362`, three passes. The
Rakudo lane, timed beside both, held within 1.1% between the two halves of the
sitting. Milliseconds, minimum across the passes.

| kernel | interp v5.1.0 | interp v5.2.0 | | `--exe` v5.1.0 | `--exe` v5.2.0 | |
|---|---:|---:|---:|---:|---:|---:|
| fib | 392.2 | 13.7 | −97% | 50.3 | 5.8 | −88% |
| loopsum | 80.9 | 5.3 | −93% | 12.7 | 3.4 | −73% |
| streq | 261.4 | 17.7 | −93% | 20.0 | 7.2 | −64% |
| mainwhen | 81.0 | 6.3 | −92% | 22.6 | 3.2 | −86% |
| strcat | 8.8 | 4.3 | −51% | 3.6 | 3.4 | −6% |
| multiwhere | 710.1 | 360.5 | −49% | 707.6 | 357.2 | −50% |
| sortnums | 27.3 | 18.2 | −33% | 14.1 | 11.7 | −17% |
| hash | 20.6 | 13.9 | −33% | 8.1 | 7.9 | −2% |
| rats | 227.4 | 175.4 | −23% | 108.4 | 90.6 | −16% |
| sortby | 26.7 | 20.8 | −22% | 16.7 | 15.0 | −10% |
| objects | 397.6 | 324.5 | −18% | 185.3 | 202.7 | +9% |
| hashfill | 109.7 | 90.0 | −18% | 35.9 | 27.5 | −23% |
| arraypush | 154.5 | 126.8 | −18% | 58.7 | 54.4 | −7% |
| arrayops | 38.8 | 32.6 | −16% | 36.8 | 31.9 | −13% |
| regex | 35.7 | 32.0 | −10% | 21.8 | 22.6 | +4% |
| textsplit | 63.0 | 56.5 | −10% | 33.4 | 31.9 | −4% |
| startup | 4.2 | 3.8 | −10% | 3.0 | 2.9 | −3% |
| bigint | 7.0 | 6.4 | −9% | 5.2 | 5.3 | +2% |

The kernels account for the top four rows and for `strcat`. The rest of the interpreter's gains
are the tree-walker's hot path — plain blocks and subs that skip the entry and
exit work they do not have, Int assignment and `++` written into the slot in
place, pads for methods and inline blocks, no scope allocated per iteration —
and multi dispatch that allocates nothing and caches its winner, which is
`multiwhere`'s −49% in both modes ([CHANGELOG](../../CHANGELOG.md)).

**`objects --exe` is slower**, 185.3 → 202.7 ms (v5.1.0's local build read
182.8 in its own sitting), and an interleaved re-run of the two compiled
binaries agreed (best of 15: 186.6 against 203.1 ms, the same output). The interpreter's `objects` is 18% faster over the same span,
and perf-guard, which times the interpreter only, cannot see it. Not yet
bisected.

### Startup

[`startup.raku`](../../tools/bench/startup.raku) is `say "Hello, World!"`, so
this row is process startup and almost nothing else.

| mode | startup | vs Rakudo |
|---|---:|---:|
| Raku++ native `--exe` | 2.9 ms | **24.9×** |
| Raku++ interp | 3.8 ms | **19.0×** |
| mutsu | 4.8 ms | **15.1×** |
| Rakudo | 72.3 ms | — |

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
| Raku++ `--exe` | 27.5 ms | **2.1× faster** | 31.9 ms | 2.4× slower |
| Perl 5 | 57.1 ms | — | 13.4 ms | — |
| Raku++ interp | 90.0 ms | 1.6× slower | 56.5 ms | 4.2× slower |
| mutsu | 180.2 ms | 3.2× slower | 97.7 ms | 7.3× slower |
| Rakudo | 277.9 ms | 4.9× slower | 193.3 ms | 14.4× slower |

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
| sieve       | 455.9 ms | **22.2 ms** | **20.6×** | 1408.0 ms | primes < 200k by trial division — inline `* <= %%` |
| powmod      | 180.7 ms | **15.0 ms** | **12.1×** | 461.1 ms | 1M `** 3` then `% 1000` — inline pow + mod |
| arrayidx    | 89.8 ms | **49.0 ms** | **1.8×** | 639.7 ms | 2M `@a[$i]` read-modify-write — no element lane yet |
| nummath     | 193.8 ms | 168.7 ms | 1.1× | 448.1 ms | Mandelbrot escape count — `Num` math, no lane yet |
| methodcalls | 142.3 ms | 123.8 ms | 1.1× | 279.2 ms | 1M monomorphic method calls — not devirtualized yet |
| stringbuild | 5.4 ms | 5.2 ms | 1.0× | 125.5 ms | 400k `~=` appends — in-place O(n) string build |
| intsum      | 4.1 ms | 4.2 ms | 1.0× | 613.1 ms | 5M int accumulation — inline `+ - *` |
| fibcalls    | 13.6 ms | 13.1 ms | 1.0× | 996.7 ms | fib(32) — direct-arity calls + inline `< + -` |
| bigmul      | 12.1 ms | 11.9 ms | 1.0× | 812.1 ms | 10000! by `*=` — the bignum compound-assign lane, no `-O` route |

Plain `--exe` now does what `-O` did for two of these: its loop lanes run
without the flag and an integer sub gets an int64 twin, so `intsum` went
106.7 → 4.1 ms and `fibcalls` 198.0 → 13.6 ms with no `-O`, and `powmod`
468.6 → 180.7. `-O` still decides `sieve` and `powmod`.

The middle rows name what `-O` does not reach: no element lane for indexed
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
- **mutsu:** 0.24.0, upstream `26cbbe30c` (2026-10-03 JST), `cargo build
  --release` with default features (Cranelift JIT on). That commit declares
  Rust 1.99.0 and Homebrew's arm64 Rust is 1.98.0, so it was built with
  `--ignore-rust-version`. Native arm64: check `file` on the binary, since an
  x86_64 build would run under Rosetta and the harness would not notice.
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
- **This sitting:** re-measured 2026-10-02 at `v5.2.0` — the local build is
  `5.2.0-g5b1c0362`, the version commit, which carries all of v5.2.0's code.
  The 1-minute load stayed between 2.4 and 4.6: WindowServer held about a third
  of a core throughout, `mediaanalysisd` a whole one during the first v5.1.0
  pass, `ecosystemd` up to a third at times. The three passes agree within 4% on
  every cell but the 3 ms `startup` (6.9% native, 8.3% mutsu), and the Rakudo
  lane within 1.1% between the main and the v5.1.0 passes.

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
