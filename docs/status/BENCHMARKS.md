# Raku++ vs Rakudo vs mutsu — speed

How fast Raku++ runs a set of small programs that every engine here runs
identically — as a tree-walking interpreter and as a native compiler — against
Rakudo, the reference implementation, and
[mutsu](https://github.com/tokuhirom/mutsu), the other from-scratch one.

**Measured 2026-10-21 at `v5.5.0`**, on an Apple M4 standing in for the
machine of record (an Apple M3, out of reach for now), in one sitting: three
interleaved passes of the kernel harness, and three of a local v5.4.0 build for
the comparison below, against **Rakudo 2026.09**. The M4's milliseconds are
not the M3's: on builds both machines timed, it takes 0.75–1.03× of the M3's
time depending on the kernel ([two machines](#two-machines)), so compare this
page with earlier sittings by ratio, not by milliseconds. The `-O` harness and
the YAMLish parse were not re-run; their sections say when, and where, they
were measured. Earlier sittings, with the notes that explained each movement,
are in [findings/BENCHMARKS-HISTORY.md](../dev/findings/BENCHMARKS-HISTORY.md);
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

- **Startup:** 3.1 ms for a native binary and 4.4 ms interpreting; mutsu 4.2
  ms, Rakudo 62.5 ms.
- **Against Rakudo 2026.09, `--exe` takes less time on all eighteen kernels**,
  from 1.08× on `multiwhere` and 1.18× on `objects` to 77× on `mainwhen` and
  135× on `intcat`. On `multiwhere` that is the machine, not the release: on
  the M3, v5.4.0 trailed Rakudo there by 7%, and on this M4 v5.4.0 leads it
  too. Its binary is not compiled code either: codegen declines a `where` on a
  multi candidate and bundles the interpreter.
- **The interpreter takes less time than Rakudo on all eighteen**, from 1.09×
  on `multiwhere` and 1.2× on `objects` to 39× on `mainwhen` and 175× on
  `intcat`.
- **Against mutsu, both modes take less time on every kernel mutsu was timed
  on** (seventeen; `intcat` takes mutsu about 160 s a run and was left out).
- **v5.5.0 against v5.4.0**, both built here and timed in this sitting:
  `arrayops` −29% interpreted and −30% compiled (30.1 → 21.5 ms), `rats` −16%
  compiled, and `textsplit`, `sortby`, `sortnums` and `objects` 8–10% faster
  interpreted. `regex` is 6% slower interpreted and 7% compiled: about 4% a
  match, accumulated over the cycle in steps of about 1%, plus a little more
  startup. The table is [below](#v540-to-v550).
- **Against Perl 5**, the interpreter is 1.3× faster on `hashfill` and on
  `intcat`, `--exe` 1.7× on `hashfill` and level on `intcat`; perl is 1.8×
  faster than `--exe` on `textsplit`.

## Results

Best of 6 timed runs per engine (7 spawned, the first discarded), minimum
across three passes, process startup included; lower is better. Rows are
ordered by the ratio against Rakudo, most favourable to Raku++ first. **Bold**
marks a Raku++ lead; otherwise the faster engine is named. All four engines
produced byte-identical output on every kernel in every pass. mutsu was not
timed on `intcat` (160.5 s a run when the kernel was added, 2026-10-06).
Rakudo's `intcat` is the minimum over the sitting's twelve runs that carried
the kernel, which spread 7.1–8.7 s.

### Interpreter vs Rakudo and mutsu

| Benchmark | Raku++ (interp) | mutsu | Rakudo | vs Rakudo | vs mutsu |
|---|---:|---:|---:|---|---|
| intcat | 40.9 ms | — | 7139.1 ms | **174.6×** | — |
| mainwhen | 6.5 ms | 77.9 ms | 253.8 ms | **39.0×** | **12.0×** |
| loopsum | 5.2 ms | 49.2 ms | 150.9 ms | **29.0×** | **9.5×** |
| bigint | 5.9 ms | 7.2 ms | 145.6 ms | **24.7×** | **1.2×** |
| fib | 11.7 ms | 126.7 ms | 272.3 ms | **23.3×** | **10.8×** |
| strcat | 4.3 ms | 6.5 ms | 78.7 ms | **18.3×** | **1.5×** |
| hash | 7.0 ms | 24.6 ms | 111.9 ms | **16.0×** | **3.5×** |
| arraypush | 16.8 ms | 111.2 ms | 235.5 ms | **14.0×** | **6.6×** |
| streq | 15.0 ms | 409.0 ms | 160.4 ms | **10.7×** | **27.3×** |
| sortnums | 15.0 ms | 23.0 ms | 145.3 ms | **9.7×** | **1.5×** |
| rats | 24.5 ms | 244.6 ms | 201.3 ms | **8.2×** | **10.0×** |
| arrayops | 21.5 ms | 86.7 ms | 155.0 ms | **7.2×** | **4.0×** |
| sortby | 18.6 ms | 26.4 ms | 126.0 ms | **6.8×** | **1.4×** |
| hashfill | 37.2 ms | 160.5 ms | 250.3 ms | **6.7×** | **4.3×** |
| regex | 25.1 ms | 73.3 ms | 159.0 ms | **6.3×** | **2.9×** |
| textsplit | 41.2 ms | 81.9 ms | 173.1 ms | **4.2×** | **2.0×** |
| objects | 155.0 ms | 517.4 ms | 190.9 ms | **1.2×** | **3.3×** |
| multiwhere | 185.5 ms | 5891.6 ms | 201.6 ms | **1.09×** | **31.8×** |

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
| intcat | 52.8 ms | — | 7139.1 ms | **135.2×** | — | 0.8× |
| mainwhen | 3.3 ms | 77.9 ms | 253.8 ms | **76.9×** | **23.6×** | 2.0× |
| fib | 5.2 ms | 126.7 ms | 272.3 ms | **52.4×** | **24.4×** | 2.2× |
| loopsum | 3.4 ms | 49.2 ms | 150.9 ms | **44.4×** | **14.5×** | 1.5× |
| bigint | 4.7 ms | 7.2 ms | 145.6 ms | **31.0×** | **1.5×** | 1.3× |
| strcat | 3.3 ms | 6.5 ms | 78.7 ms | **23.8×** | **2.0×** | 1.3× |
| streq | 6.9 ms | 409.0 ms | 160.4 ms | **23.2×** | **59.3×** | 2.2× |
| hash | 6.7 ms | 24.6 ms | 111.9 ms | **16.7×** | **3.7×** | 1.0× |
| sortnums | 10.9 ms | 23.0 ms | 145.3 ms | **13.3×** | **2.1×** | 1.4× |
| arraypush | 17.8 ms | 111.2 ms | 235.5 ms | **13.2×** | **6.2×** | 0.9× |
| regex | 17.2 ms | 73.3 ms | 159.0 ms | **9.2×** | **4.3×** | 1.5× |
| hashfill | 27.5 ms | 160.5 ms | 250.3 ms | **9.1×** | **5.8×** | 1.4× |
| sortby | 14.1 ms | 26.4 ms | 126.0 ms | **8.9×** | **1.9×** | 1.3× |
| textsplit | 22.6 ms | 81.9 ms | 173.1 ms | **7.7×** | **3.6×** | 1.8× |
| arrayops | 20.5 ms | 86.7 ms | 155.0 ms | **7.6×** | **4.2×** | 1.0× |
| rats | 69.5 ms | 244.6 ms | 201.3 ms | **2.9×** | **3.5×** | 0.4× |
| objects | 161.5 ms | 517.4 ms | 190.9 ms | **1.18×** | **3.2×** | 1.0× |
| multiwhere | 187.4 ms | 5891.6 ms | 201.6 ms | **1.08×** | **31.4×** | 1.0× |

Three rows are slower compiled than interpreted. In each, every loop runs as
an interpreter kernel (`RAKUPP_KERNEL_TRACE=1` names them), which takes array
elements, pushes, Nums and Rats, while the `--exe` code does not have those
lanes; `rats` is the widest, though it gained 16% compiled this release.
Elsewhere compiling gains least where the interpreter already runs a kernel —
`fib`, `loopsum`, `mainwhen`, `streq` are within 1.5–2.2× of their binaries,
and much of what is left on those rows is process startup — and nothing where
the time is inside a runtime method both modes share: `arrayops`
(`.grep`/`.map`/`.sum`), `hash`, `objects` and `multiwhere` (the bundled
interpreter).

### v5.4.0 to v5.5.0

Both columns are local builds with the same compiler, timed in this sitting on
the M4, alternating pass by pass: v5.4.0 is `5.4.0-g36b420ad`, the very commit
the 2026-10-13 sitting timed on the M3, and v5.5.0 is `5.4.1-193-g403a7e70`,
three passes each. A release artifact has read −6% to +16% against a local
build of the same code, so this table compares two local builds. The Rakudo
lane, timed beside both, held within 0.4% at the median (3.1% at worst, on
`intcat`). Milliseconds, minimum across the passes.

| kernel | interp v5.4.0 | interp v5.5.0 | | `--exe` v5.4.0 | `--exe` v5.5.0 | |
|---|---:|---:|---:|---:|---:|---:|
| arrayops | 30.1 | 21.5 | −29% | 29.2 | 20.5 | −30% |
| textsplit | 45.6 | 41.2 | −10% | 22.5 | 22.6 | 0% |
| sortnums | 16.5 | 15.0 | −9% | 10.7 | 10.9 | +2% |
| sortby | 20.4 | 18.6 | −9% | 14.0 | 14.1 | +1% |
| objects | 167.8 | 155.0 | −8% | 161.6 | 161.5 | 0% |
| hashfill | 38.1 | 37.2 | −2% | 26.9 | 27.5 | +2% |
| fib | 11.9 | 11.7 | −2% | 5.1 | 5.2 | +2% |
| intcat | 41.3 | 40.9 | −1% | 52.6 | 52.8 | 0% |
| hash | 7.0 | 7.0 | 0% | 6.5 | 6.7 | +3% |
| rats | 24.4 | 24.5 | 0% | 83.1 | 69.5 | −16% |
| multiwhere | 183.8 | 185.5 | +1% | 183.1 | 187.4 | +2% |
| mainwhen | 6.4 | 6.5 | +2% | 3.2 | 3.3 | +3% |
| bigint | 5.8 | 5.9 | +2% | 4.6 | 4.7 | +2% |
| arraypush | 16.5 | 16.8 | +2% | 17.4 | 17.8 | +2% |
| strcat | 4.2 | 4.3 | +2% | 3.2 | 3.3 | +3% |
| streq | 14.5 | 15.0 | +3% | 6.8 | 6.9 | +1% |
| startup | 4.2 | 4.4 | +5% | 3.0 | 3.1 | +3% |
| regex | 23.7 | 25.1 | +6% | 16.1 | 17.2 | +7% |
| loopsum | 4.8 | 5.2 | +8% | 3.3 | 3.4 | +3% |

**`regex` is slower in both modes, and no single commit did it.** It held on
all three passes (23.7–23.9 ms against 25.1–25.3 interpreted). With the loop
raised tenfold, so that startup no longer counts, the interpreter goes
203.3 → 211.4 ms and the compiled binary 134.9 → 140.3 ms, +4.0% each,
accumulated in steps of about 1% along `main` (+1.0% by `36098d73`, +2.1% by
`cc12af7c`, +4.0% at `403a7e70`). Because `--exe` loses the same 4%, the cost
is in the regex runtime both modes share, not in the tree-walker; the rest of
the 6–7% is about 0.1 ms more process startup. It is in
[TODO.md](../dev/plans/TODO.md). `loopsum` (+8% interpreted) and `startup`
(+5%) are a few tenths of a millisecond: in one interleaved run of thirteen
builds across the cycle, `loopsum` read +1.5% and `startup` +2.2%.

### Startup

[`startup.raku`](../../tools/bench/startup.raku) is `say "Hello, World!"`, so
this row is process startup and almost nothing else.

| mode | startup | vs Rakudo |
|---|---:|---:|
| Raku++ native `--exe` | 3.1 ms | **20.2×** |
| Raku++ interp | 4.4 ms | **14.2×** |
| mutsu | 4.2 ms | **14.9×** |
| Rakudo | 62.5 ms | — |

A native Raku++ binary has no VM to bring up and no precompiled setting to
load; Rakudo's ~60 ms is a fixed cost inside every row on this page. mutsu is
in the same order of magnitude as Raku++ for the same reason, and on this
machine it starts 0.2 ms sooner than the interpreter.

### vs Perl 5

[`hashfill`](../../tools/bench/hashfill.raku) fills a 200k-key hash through
interpolated keys, sweeps `%h.values`, and builds a string with 50k `~=`
appends; [`textsplit`](../../tools/bench/textsplit.raku) splits 20k lines into
fields, reorders and rejoins them; [`intcat`](../../tools/bench/intcat.raku)
appends 2M Ints to two strings, one with a separator. Each has a line-for-line
`.pl` twin ([hashfill.pl](../../tools/bench/hashfill.pl)), timed by the same
harness with the same output check. perl here is the system's v5.34.1
(`/usr/bin/perl`); the M3's tables used v5.44.0, which timed within 4% of
5.34 there on `hashfill` and `textsplit`.

| engine | hashfill | vs perl | textsplit | vs perl | intcat | vs perl |
|---|---:|---:|---:|---:|---:|---:|
| Raku++ `--exe` | 27.5 ms | **1.7× faster** | 22.6 ms | 1.8× slower | 52.8 ms | level |
| Raku++ interp | 37.2 ms | **1.3× faster** | 41.2 ms | 3.3× slower | 40.9 ms | **1.3× faster** |
| Perl 5 | 47.3 ms | — | 12.5 ms | — | 52.9 ms | — |
| mutsu | 160.5 ms | 3.4× slower | 81.9 ms | 6.6× slower | — | — |
| Rakudo | 250.3 ms | 5.3× slower | 173.1 ms | 13.8× slower | 7139.1 ms | 135.0× slower |

The hash payload is an insertion-ordered open hash in the perl mold
([PERL5-TECHNIQUES.md](../dev/findings/engines/PERL5-TECHNIQUES.md)), and
since v5.3.0 the interpreter's loop kernels write hash elements in place;
`intcat` is an in-place append in both modes. Text munging is where perl still
leads.

### `-O` (the optimizer flag)

`--exe -O` adds three speculative codegen passes: direct-arity calls for
fixed-arity subs, inline arithmetic and string comparison, and guarded
native-int lanes for int assignments and conditions
([OPTIMIZATION.md](../internals/OPTIMIZATION.md)). Measured by
[`tools/run-optbench.raku`](../../tools/run-optbench.raku), which first checks
each program's output four ways (interp, `--exe`, `--exe -O`, Rakudo); one pass,
best of 5. Measured 2026-10-08 at `v5.3.0` on the M3, and not re-run for
v5.4.0 or v5.5.0.

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
append or the bignum multiply. `-O` is off by default and produces identical
output.

### Real-world: grammar parsing (YAMLish)

A whole module doing real work: YAMLish 0.1.3 (zef:leont, unmodified) parsing
the Raku course's table of contents (`_data/toc/en.yaml`, 2,653 lines) with
`load-yamls`. The grammar exercises parameterised tokens, `|` alternations,
lookbehinds, `:my` state, aliased captures and action methods. Both engines
produce the same data, compared as sorted-key JSON. Best of 5, wall-clock, the
two engines interleaved; measured 2026-09-29 at `v5.0.1` on the M3 against
Rakudo v2026.08, and not re-run since. It needs `v5.0.1` or later: `v5.0.0`
does not finish this parse in 60 s.

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
threads became the default, on the M3 (4 performance + 4 efficiency cores):

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
  engine's startup is part of its time. Rakudo's ~60 ms start dominates its
  shortest rows, so the large multipliers there are not an execution-speed
  comparison; the `-O` kernels and the in-process YAMLish parse are closer to
  one.
- **`--exe` helps where a tree-walker hurts.** It removes the per-node dispatch
  of interpreting, so it gains most on `streq`, `loopsum` and `fib` and almost
  nothing on `arrayops`, `hash` or `multiwhere`, whose time is inside runtime
  methods both modes share (and `multiwhere`'s `--exe` binary bundles the
  interpreter, because codegen declines a `where` on a multi candidate). It
  is behind the interpreter where the interpreter's loop kernels reach
  further than the code generator does: `arraypush`, `rats` and `intcat`.
- **These kernels are the overlap.** Every program here runs identically on all
  engines, which the harness checks before timing. Speed on this set says
  nothing about coverage; for that see [ROAST.md](ROAST.md).
- **Compare sittings by ratio.** This page and its history span two machines;
  a ratio against the Rakudo of the same sitting carries across, milliseconds
  do not.

## Two machines

Every sitting up to 2026-10-13 ran on one Apple M3. For this one it was out of
reach, and an Apple M4 stood in. Before measuring the release, the same
sitting re-timed two builds the M3 had timed too: the v5.3.0 release binary,
and the local build of `36b420ad` the M3 timed on 2026-10-13.

| M4 time ÷ M3 time | interp | `--exe` | mutsu | Rakudo 2026.09 |
|---|---:|---:|---:|---:|
| median, per build | 0.87–0.91× | 0.89–0.91× | 0.91× | 0.90× |
| range, by kernel | 0.75–0.96× | 0.78–1.03× | 0.87–0.95× | 0.87–0.94× |

The M4 takes about nine-tenths of the M3's time, but not uniformly — from
0.75× on interpreted `streq` to 1.03× on compiled `loopsum` — so no single
factor turns one machine's milliseconds into the other's. Ratios carry across:
on the `36b420ad` build, `arrayops` reads 5.3× Rakudo on both machines. The
exception is Rakudo's `intcat`, a 7-second run that builds very large strings:
7.1–8.7 s here against 5.2–6.0 s on the M3. The dashboard draws the M4's
readings as lines of their own beside the M3's, from those two shared points
on.

## Methodology

- **Machine:** Apple M4 (MacBook Air, 4 performance + 6 efficiency cores, 16
  GB, fanless), macOS 26.6.2 (Darwin 25.6.0), on AC power with Low Power Mode
  off. It stands in for the machine of record, an Apple M3 (4 performance + 4
  efficiency cores, macOS Darwin 27.0.0), which timed every earlier sitting.
- **Raku++:** `build-arm64/rakupp`, CMake Release (`-O3 -DNDEBUG`), Apple clang 21.
- **Rakudo:** v2026.09, the homebrew/core arm64 bottle (`arm64_tahoe`), the
  build the M3's 2026.09 figures used, at `/opt/homebrew/opt/rakudo/bin/rakudo`
  and passed to the harness **by path**, with its default frontend (RakuAST;
  `RAKUDO_RAKUAST=0` selects the old one, and `BEGIN $*LANG.^name` says which
  is running: `Raku::Grammar` or `Perl6::Grammar`). This machine also has an
  x86_64 Homebrew under `/usr/local`, first on `PATH`, whose Rakudo would run
  under Rosetta 2, so the bottle was installed through `/opt/homebrew/bin/brew`
  by path; the binary and `libmoar` are arm64-only and it reports `arm64` at
  run time. Bare `raku` is Raku++ here. MoarVM has no JIT backend on arm64, so
  this Rakudo runs spesh without machine code; an x86_64 Rakudo column from
  another machine is not comparable. A from-source 2026.09 timed on the same
  M4 reads 0.88–1.01× of the bottle (about 0.96× at the median).
- **mutsu:** 0.25.0, upstream `49442b379` (2026-10-04 JST), rebuilt on this
  machine with `cargo build --release` and default features (Cranelift JIT
  on). Not timed on `intcat`. Native arm64: check `file` on the binary, since
  an x86_64 build would run under Rosetta and the harness would not notice.
- **perl:** v5.34.1 (`/usr/bin/perl`), for the three kernels with a `.pl` twin.
- **Harness:** [`tools/run-bench.raku`](../../tools/run-bench.raku). It runs every
  program under every engine and compares stdout before timing anything; a
  disagreement in a Raku++ lane fails the run, a mutsu disagreement is only
  reported. Each engine is spawned 7 times per kernel, the first run
  discarded, and the minimum of the other 6 kept; the engines are interleaved
  round by round, so a load spike lands on every lane. For `native`, each
  program is compiled with `--exe` once and the binary is timed; the compile is
  not counted. The harness refuses a Raku++ binary built for another
  architecture.
- **This sitting:** re-measured 2026-10-21 at `v5.5.0` — the local build is
  `5.4.1-193-g403a7e70`, v5.5.0's code before the version commit, which
  changes only version strings. `intcat` ran in passes of its own without the
  mutsu lane; the v5.4.0 comparison build ran three passes, alternating with
  v5.5.0's. The 1-minute load read between 1.8 and 2.7. 86 of the 97 cells
  (the `--cnp` lane included) agree within 4% across the three passes; the
  worst are native `loopsum` (5.9%) and `--cnp` `loopsum` (6.8%), a few tenths
  of a millisecond, and the Rakudo lane's `intcat` (7.6%).

## Reproducing

```sh
RAKUDO=/opt/homebrew/opt/rakudo/bin/rakudo ./build-arm64/rakupp tools/run-bench.raku
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
