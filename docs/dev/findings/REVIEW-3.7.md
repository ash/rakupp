# Post-3.7 cooldown review — 2026-08-27

A 360° review of the tree at `6c25a94` (v3.7.0 + 37 commits), prompted by a red
`perf-guard --check`. Two halves: pivot-build forensics on the perf gate, and a
three-way sweep of `src/` — semantic duplication, the deferred items from the
2.0/3.5 reviews re-verified, and per-op costs on paths the guard's kernels do
not exercise. The perf half was measured on the Darwin 24.6 box, fresh Release
arm64 builds throughout, strict-idle sessions (1-min load < 2.5, 5-min < 5).

## Baselines (at 6c25a94)

- CI green as of `6c25a94` (Windows S_IS* shims, MSVC pollfd narrowing,
  OpenBSD `__st_birthtime`, slim budgets re-derived)
- `t/run.raku` 962/962 · SLIM suite green · Jupyter smoke green
- `perf-guard --check` RED against the 2026-08-24 v3.7.0 baseline:
  hash +11.3%, strscan +7.2%, rats +7.1% (idle-confirmed)

## The perf red was the baseline, not the code

Seven fresh builds spanning `12f1b02..HEAD` — the baseline commit itself,
`6879a71` (evalIndex suspect), `5faed01`, `7339c21` (xx laziness), `96f4335`
(DeclCheck), `ee35127` (dispatch band), and HEAD — time identically, within
the ±3% run-to-run noise of a strict-idle session:

| source point | fib | hash | strscan | rats |
|---|---|---|---|---|
| recorded baseline (08-24 sitting) | 369.0 | 17.7 | 118.8 | 245.3 |
| `12f1b02` fresh (the baseline source) | 382.1 | 21.5 | 124.2 | 256.8 |
| `6879a71` | 387.6 | 20.9 | 126.7 | 261.4 |
| `5faed01` | 387.5 | 20.3 | 126.1 | 261.3 |
| `7339c21` | 382.4 | 20.6 | 127.1 | 261.0 |
| `96f4335` | 383.2 | 20.4 | 126.2 | 263.8 |
| `ee35127` | 393.3 | 21.6 | 127.6 | 264.4 |
| HEAD `6c25a94` | 378.8 | 19.7–20.3 | 123.9–127.4 | 258.9–262.6 |

The recorded row is the outlier: a fresh build of the baseline's own source
reads hash +15–21% against it. The compiler did not change (Xcode 26.3
untouched since March, CLT since July — receipts plus `LC_BUILD_VERSION`
stamps in the 08-24 objects), flags identical (`-O3 -DNDEBUG`, arm64). The
08-24 release sitting had a quieter machine than this box reached at any point
during four idle-gated sessions (floor ≈ load 2–3 with the desktop apps
running). The 17.7 ms `hash` kernel amplifies that most: one 2–3 ms preemption
is +11–17%, and best-of-3 cannot dodge constant ambient load. The four clean
kernels (asg, loopsum, strpass, subcall, all ±1%) bound any fixed per-process
cost at ~0.5 ms and rule out uniform machine slowdown.

Method notes, learned again the hard way: the first sweep's opening pivot
timed on a still-settling box (5-min load 8.9) and read hash +25% — pure
noise that nearly sent the hunt to the wrong commit span. Same-session,
strict-idle, and when a gate goes red, **build the baseline commit fresh and
guard it first**: if the baseline source reproduces the regression, no commit
caused it and bisecting is wasted work.

**Resolution: re-record the baseline** (the guard's own escape hatch, reasons
in `tools/perf-baseline.raku` when done); `best` keeps 17.7 as the standing
record per the file's design. `build-arm64/` was scratch-rebuilt at `6c25a94`
so the recorded binary is single-vintage. Worth considering at the same time:
`hash` at 100_000 iterations is an 18 ms kernel — too short to hold a 5%
tolerance against ambient load; 500_000 (~90 ms) would make it as robust as
the others. As of this review the re-record is pending.

Incidental but load-bearing: `build/` on this box configures to **x86_64**
(empty `CMAKE_OSX_ARCHITECTURES`) and runs under Rosetta. Never take a size
or a timing from it as an arm64 number; the slim-budget comments that briefly
did have been corrected.

## Duplication with proven drift

The clone sweep found 15 semantic-duplicate clusters; these have verified
behavioural divergence today. (The NOTE at `Interpreter.cpp:16599` points at
`docs/dev/DUPLICATION-AUDIT`, which no longer exists — this file is its
replacement.)

1. **`[op]` reduce: compiled ≠ interpreted.** `applyReduce`
   (`Interpreter.cpp:23376`, ~180 loc: chained comparisons, scan forms,
   `[R-]`, `[\,]`) vs `rtReduce` (`:11032`, a 22-line left fold), and
   `Codegen.cpp:1026` emits `rtReduce` for **every** `[op]`. `[<] 3,1,2` is
   `False` interpreted, `True` compiled. The most serious finding of the
   round: same program, two answers under `--exe`.
2. **`open()` vs `.IO.open`** (`Builtins.cpp:8891` vs
   `MethodCallPart3.cpp:1576`): the method does not throw
   `X::IO::DoesNotExist` for read mode — `"/nope".IO.open` returns a live
   handle; it also drops `rejectNulPath` and ignores `:nl-in`.
3. **`slurp($p, :bin)` returns a CRLF-stripped Str, not a Blob**
   (`Builtins.cpp:8824` vs `MethodCallPart3.cpp:969`): the sub copy opens
   without `std::ios::binary` and has no `:bin` arm; its comment claiming the
   route to the method is false. Method `spurt` (`:993`) likewise drops
   `rejectNulPath` and `:createonly`/`:x`; `mkdir` is the same shape.
4. **`Rakudo::Internals::JSON.to-json` emits invalid JSON**: `jsonEncode`
   (`Builtins.cpp:3027`) concatenates hash keys with **no escaping** and
   escapes five characters in values; it also backs the dist-META writer.
   `jfEncode` (`:3077`, JSON::Fast) is correct. Three further string escapers
   — `JsonLite.h:69`, `Interpreter.cpp:10259`, `main.cpp:410` — are each
   valid but byte-different.
5. **Hyper compound assign `»+=«` fixed in one copy of two**: the
   parenthesised-list lvalue write-back lives in `Interpreter.cpp:20599`
   (evalBinary) only; via `applyBinOp` (`:20240`) — reached from reduce and
   the compiled path — the assignment still silently no-ops.
6. **Object construction: two stripped copies of the attribute-default
   walk** (`MethodCallPart2.cpp:2513`, `:2766` vs the canonical `:2555`):
   `class D is DateTime { has $.a = 1; has $.b = $!a * 2 }; D.new(a => 5)`
   gives `b == 2` through the copy, `b == 10` through the canonical walk;
   `self` in defaults is invisible to the copies. The BUILD/TWEAK epilogue is
   repeated five times (`:2470`, `:2488`, `:2506`, `:2539`, `:2750`).
7. **Negative subscript: Failure twice, throw once**
   (`Interpreter.cpp:23776`, `:24661` vs `MethodCallPart3.cpp:630`):
   `@a[-1]` is soft, `@a.AT-POS(-1)` fatal — issue #36's commit unified only
   the message text.
8. **Z/X: three implementations, two element models**
   (`Interpreter.cpp:16597` deep-flatten; `:20125` one-level + endless-range
   lazy; `:20635` one-level, no lazy arm): `(1,0) X (<a b>, <c d>)` is 4
   pairs on two paths, 8 on the third — and the third (`applyArith`) is what
   compiled code and `rtReduce` fold with.

Likely-drift ledger (verified duplicated, divergence latent): `.created` =
mtime on the method path vs birthtime in `nqp::stat` on macOS
(`MethodCallPart3.cpp:1546` vs `Builtins.cpp:11280`); the `.can`/`.^lookup`
unsafe-to-probe list duplicated verbatim and already unequal
(`Builtins.cpp:4277` vs `MethodCallPart2.cpp:3618`); the EVAL/EVALFILE
"defines symbols" predicate in four spellings with two gaps —
`$path.EVALFILE` escapes the flat-scan guard (`Interpreter.cpp:2322`),
`.EVAL`-as-method escapes Lint (`Lint.cpp:299`); DeclCheck's two parallel AST
walkers (`DeclCheck.cpp:85` vs `:257`, NK coverage across the four tree
walkers 49/45/44/35); the sub vs method multi-dispatch candidate loops
ranking on different scales — definedness-smiley rejection and the +1000
narrowness bonus exist only on the method side (`Interpreter.cpp:12236` vs
`:13146`); `.IO.cleanup`/`is-absolute` re-deriving IO::Spec with fewer cases
(`MethodCallPart3.cpp:1490`, `:1508` vs `IOSpec.cpp:36`, `:403`); two
byte-identical tmpdir copies in one file (`IOSpec.cpp:318`, `:515`); and the
numify-a-Str rules now in three full copies plus a fourth partial one in the
stat op's mode strings.

## Verified silent bugs (re-checked live on this build)

- **`let` in a method does not restore on unwind**: `runLetRestoresOf`
  (`Interpreter.cpp:12081`) is wired into the sub paths (`:12684–12705`) but
  never into `invokeMethod` (`:13047`). Probe: `let $x; $x = 5; die` inside a
  method leaves `$x == 5` after the catch. Last remnant of the sub/method
  twin-convergence arc.
- **`sleep-till` is a no-op stub** (`Builtins.cpp:10389`): returns True
  immediately; a 0.3 s wait measured 0 s.
- `isa-ok` keeps a private, drifted ancestry map beside the shared
  `typeAncestry` (`Builtins.cpp:~8200` vs `:95`).
- `"z9".parse-base(2)` returns a bare undefined value, not a proper Failure.
- `1,4,9...100` invents a step (21 elements) where Rakudo dies "unable to
  deduce" — Roast asserts the throw.
- `gcd(12,8)` as an undeclared listop silently prints empty; DeclCheck misses
  the form.
- Lookahead cannot see in-flight captures: `"aab" ~~ /(.) <?before $0>/`
  fails; Rakudo matches. (Regex-engine surgery; not cooldown-sized.)

## Costs the guard cannot see

The sweep proved the gate kernels flat, so none of these is a regression —
they are standing per-op costs on paths **no kernel exercises**, found while
reading the recent hot-path diffs. Each wants the interleaved-A/B discipline
(alternate every run, min of 9+, never judge against recorded absolutes).

- **Regex literal in value position** (since `899976b`):
  `rejectObsoleteRegex` (`Interpreter.cpp:14083`) takes a static-mutex lock,
  a map lookup keyed by the whole pattern, and a CowStr materialisation per
  evaluation; stacked on it, `boolify(Regex)` (`:10179`) walks the env chain
  for `$/` with a by-value ~376-byte argument. `if /…/` in any real loop pays
  both. Fix shape: memoise the verdict on the RegexLit node (the
  `NumLit::ratCache` pattern); cache the `$/` slot on the frame. **And add a
  regex-in-a-loop kernel to the guard** — this whole class is invisible to it.
- **`Callable` grew ~72 bytes mid-struct** (`Value.h:307`, `e25ccec`):
  `mixinRoles`/`mixinAttrs` sit before the fields every call reads and are
  consulted in exactly one place (`MethodCallPart2.cpp:2925`). Move behind a
  null-by-default pointer; cheapest structural A/B in the set.
- Dispatch/return ballast, batchable: `scoreCandidate` walks both parameter
  vectors per candidate for static facts (`Interpreter.cpp:9997`) — cache
  `hasSubSig`/`hasPlainNamed` bits on the Callable; `protoBodyOf` linear scan
  per multi dispatch (`:12289`, `:13221`) — resolve at install;
  `checkRetType`'s string-keyed subset lookup per return (`:12776`) — resolve
  `retType` once at declaration; `protoStack_` TLS probed on every block
  statement (`:6385`, `:26272`) — a plain counter on `tctx_` stays in a
  register; indirect method calls build their argument vector twice
  (`:25814`); `%h{k}++` resolves the base lvalue twice (`:22089`) —
  pre-existing, 100k redundant env lookups in the guard's own hash kernel;
  `evalIndex`'s hash arm spends a `std::string` and three `rfind`s where one
  bool on the Hash would do (`:24369`, `:24541`).

## Batch plan

1. **Silent wrong answers** — `let`-in-method restore; `sleep-till` real
   wait; `slurp`/`spurt`/`mkdir` sub-method parity incl. `:bin`; `.IO.open`
   throws like the sub; `jsonEncode` key escaping; negative-subscript
   unification; `parse-base` Failure; undeducible sequence dies. Gate:
   `t/run.raku` + a regression test per item; Roast where semantics move.
2. **Kill the twins** — reduce metaop through one implementation first (it
   changes compiled answers; wants an `--exe` leg in `t/`); Z/X one element
   model; hyper-assign write-back everywhere; construction walk delegated;
   can/lookup list shared; EVAL/EVALFILE predicate as one function;
   numify-Str consolidated; tmpdir/cleanup/is-absolute onto IO::Spec; JSON
   escapers onto JsonLite. Gate: full Roast + `t/run.raku` per sub-batch.
3. **Measured micro-opts** — regex-literal memoisation + `$/` slot cache
   (with the new guard kernel); Callable mixin fields behind a pointer;
   cached signature facts; resolved return types; protoStack counter; single
   lvalue resolve in `%h{k}++`. Gate: interleaved A/B each, then
   `perf-guard --check`.

## Already done in this round

- CI back to green at `6c25a94` (Windows/OpenBSD/slim fixes, budgets
  re-derived; `t/run.raku` 962/962).
- `build-arm64/` scratch-rebuilt at HEAD (single-vintage, ready to record).
- Slim-budget comment arch labels corrected (`build/` is x86_64 here).
- Perf false-red protocol written into the working notes: fresh-build the
  baseline commit before bisecting a red gate.
