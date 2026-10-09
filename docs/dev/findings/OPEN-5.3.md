# What v5.3.0 ships broken or degraded

Collected at the v5.3.0 release sitting (2026-10-08/09), on the candidate
`7e8718c4`, from the gates in [RELEASING.md](../RELEASING.md) and the
[CHANGELOG](../../../CHANGELOG.md) entry they fed. Compared with v5.2.1
unless a line says otherwise. Each item says whether it is new in this cycle
or older, and how sure the figure is. Open work for each is in
[TODO.md](../plans/TODO.md).

## 1. Degraded in this cycle

### 1.1 Interpreted method and sub calls are slower — NEW, measured, shipping as is

Two interleaved perf-guard rounds against a local v5.2.1 build, same machine
and compiler (minimum of the rounds):

| kernel | v5.2.1 | v5.3.0 | change |
|---|---:|---:|---:|
| privmeth | 203.4 ms | 222.8 ms | +9.5% |
| strpass | 62.5 ms | 68.1 ms | +9.0% |
| method | 113.4 ms | 123.1 ms | +8.6% |
| attrread | 144.6 ms | 153.0 ms | +5.8% |
| subcall | 115.3 ms | 120.5 ms | +4.5% |
| junctionwide | 20.0 ms | 20.9 ms | +4.5% |
| multimeth | 154.4 ms | 161.2 ms | +4.4% |
| regexloop | 91.1 ms | 94.4 ms | +3.6% |
| strscan | 115.6 ms | 118.9 ms | +2.9% |

- Retired instructions: +6.3% method, +5.8% privmeth, +7.1% strpass, so this
  is added work, not layout.
- No single commit is the cause. It built up over about 120 commits.
  Bisect ratios: 1.004 at 1aa4992e, 1.031 at f3b1fe01, 1.051 at e209ae16,
  1.061 at 1fbac1ae, 1.093 at HEAD.
- The profile names three costs:
  - the `thread_local` `loopNest_`, touched twice per loop iteration in
    `runLoopBody` (f3b1fe01);
  - `setupRwLinks`, which now runs on calls with no rw parameter;
  - `SlotStripe`, which runs on single-threaded variable access (458886c2).
- Open: TODO.md §1, "Call-path cost shipped in v5.3.0".

### 1.2 `--exe` method calls and Num math are slower — NEW, one pass, cross-sitting

`run-optbench` without `-O` (one pass, best of 5), compared with the 2026-10-02
v5.2.0 sitting:
- `methodcalls`: 142.3 → 151.0 ms (+6%). This is probably the same call-path cost.
- `nummath`: 193.8 → 217.2 ms (+12%). Not investigated. The two sittings are
  different days, so confirm it with an interleaved A/B before trusting it.

### 1.3 The perf gate stays red — NEW (process)

- `perf-guard --check` fails against the v5.2.0 baseline on the call kernels.
- `--record --for=v5.3.0` refused three times on noise, so the baseline is
  still v5.2.0's.
- The next release's gate will be red on those kernels until the cost is
  removed or the baseline is re-recorded.

### 1.4 The `--exe --slim` hello binary is bigger — NEW, small

- 11,845,200 bytes (v5.2.1 release binary) → 12,266,440 bytes (v5.3.0 local
  build): +421 KB, +3.6%.
- Still within the 12.75 MB darwin budget.
- The figures come from different builds (CI artifact vs local).
- Against v5.0.0's 11.15 MB on the front page, the growth is +10%.
- The x86_64 slice (about 0.5 MB fatter) was not measured.

### 1.5 Within noise, not established

Against the v5.2.1 artifact, compiled `fib` (5.5 → 5.8 ms) and `arraypush`
(53.9 → 56.8 ms) are each +5%. An artifact and a local build differ by up to
9%, so this is not a finding. Re-check when the v5.3.0 artifact exists.

## 2. Deliberate changes that refuse code v5.2.1 ran — NEW, Rakudo-compatible

Each one matches Rakudo 2026.09. Code that relied on the old leniency now
fails:

- **A call that can never bind is a compile error** (d78d1cb0):
  `sub f(Str $x) { }; try f(42)` refuses to run. Breaks two Raku Koans solutions.
- **A label cannot shadow a pseudo-package** (3f57ed3c): under `OUTER: for`,
  `next OUTER` dies. Breaks one Raku Koans solution.
- `next(42)`/`last(99)` before 6.e and `redo(1)` anywhere are refused at
  compile time (b70bc60d).
- **Nominal type checks** follow Rakudo's graph (b2678f14): `A.new ~~ Cool`
  is False, a List no longer binds `Array`, and `my Any $x = Mu` dies.
- **A plain class has no Cool methods** (#135): `my A $a; $a.index` no longer
  answers.
- Consequence: the **Raku Koans gate is red**. The course pins v5.0.1 and
  cannot move to v5.3.0 until three koans change; the course's author has
  not been told yet.

## 3. Broken, older than this cycle, shipping again

### 3.1 Module battery: Color, Encode and Trap — broke between v5.0.1 and v5.2.1

The battery is 44/59 (48 at v5.0.1). A v5.2.1 build fails all three the same way:

- **Color** t/04-new-invalid.rakutest:
  `Color.new(rgb => [22, 42])` and `[22, 42, 45, 46]` (and the `rgbd` forms)
  no longer die.
- **Encode** t/01-basic.t: `X::Encode::Unknown` is undeclared where the test
  names it.
- **Trap** t/02-tee.rakutest: the tee writes its string twice.

YAMLish moved to ENV because Rakudo 2026.09 now fails its suite. Raku++
passes all five files, so YAMLish is not broken in Raku++.

Open: TODO.md §2, "Battery regressions since v5.0.1".

### 3.2 Raku.js: worker bodies run on the main thread's registers — OLDER, partly fixed

A WASM build has no threads, so a `start`/`react` worker body runs inline.
It replaces the main thread's interpreter registers for the rest of that
program.

- The crash this caused in the NEXT program on the same engine is fixed
  (19ed905f).
- The effect within one program remains, and nothing known fails from it.

### 3.3 The browser and embedders skip the CLI's compile-time checks — OLDER, decision

`rk_run` (Raku.js, the spreadsheet add-ins, the Python binding and any C
host) calls `rakuppRunOn` without `declCheck`. So these accept code the CLI
refuses:
- undeclared variables (#32);
- calls that can never bind (d78d1cb0, new this cycle, so the divergence grew).

Turning the checks on changes browser behaviour, including two koans, so it
waits on a decision (TODO.md §2).

### 3.4 Smaller, older

- `(1 if 1 if 1)` is accepted; Rakudo says "Missing semicolon". This is
  leniency in the plain-paren path, present in v5.2.1.
- `S17-promise/nonblocking-await.t` SIGSEGVs now and then (ROAST.md "the one
  file that flaps"). It did not occur in this cycle's four runs.
- `S17-supply/watch-path.t` timed out in the `--all` run. It is not in
  `spectest.data`, and it was seen once.
- 321 (adopter): 12 of 13 files are known-failing on code Rakudo refuses
  too. Unchanged.

## 4. Not checked this release, so unknown

- Documentation-example sweep and ecosystem sweep (not run for this release).
- Slim differential (`slim-diff`), `cpp-build-check`, the GCC
  second-toolchain build, and the `--exe` module battery (`aot-battery`).
- DBIish through an installed binary (`t/installed --dbiish`).
- The x86_64 slice of anything; the v5.3.0 release artifacts (not built yet).
- CI on the tagged commit (not pushed at the time of writing).
