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

1. **Silent wrong answers** — **LANDED 2026-08-27** (same day): `let`-in-method
   restores on all three unwind arms; `sleep-till` deleted outright — it was a
   rakupp-only stub of a routine Raku never had (`sleep-until` was already
   real); every bare `Value::typeObj("Failure")` in the tree (8 sites) became
   an ARMED Failure via the new `armedFailure` helper (parse-base, .UInt,
   .base-repeating, 0**-negative, Str decrement); undeducible sequences throw
   X::Sequence::Deduction with `.from` — deduction reads the LAST THREE seeds,
   as Rakudo does (`1,1,1,2,3 ...` continues +1); `jsonEncode` routes keys and
   values through `jfEscape`; negative subscript reads throw, with the ONE
   measured soft case `@empty[*-1]` (Cro's last-chunk-if-any idiom) answering
   a Failure; `.IO.open` and sub `slurp` are now DELEGATIONS to their richer
   twins (two twin clusters gone early); method `spurt` gained
   `rejectNulPath` + `:createonly`/`:x`. The Cro hang the batch surfaced was
   NOT the batch: rakupp lacked CORE's ProtocolType enum (PROTO_TCP = 6,
   PROTO_UDP = 17) and the lenient bareword path had been feeding setsockopt
   a quiet wrong level; the enum is in at Rakudo values. Gates:
   `t/run.raku` 565/565 with the new
   `t/regression/batch1-silent-wrong-answers.raku` (32 checks, 28 verbatim
   Rakudo-parallel); full Roast 198,847/218,728 (90.9%) — +168 passes over
   the recorded 198,679, no section down, timeouts = the known S17 family.
2. **Kill the twins** — **LANDED 2026-08-27** (same day), in three
   sub-batches:
   *B2a (semantics)*: `rtReduce` folds through `applyReduce` — compiled
   `[<] 3,1,2` now answers False like the interpreter, scans and the R-metaop
   included, verified byte-identical across interpreted/compiled/Rakudo on a
   12-case battery; Z/X collapsed to ONE `zxOp` (one-level element model
   everywhere — the applyArith copies deep-flattened, and that path is what
   compiled code folds with; the endless-Z lazy view now reaches every
   ladder; S03-metaops/cross.t +1); hyper compound assignment to one
   implementation with the parenthesised write-back (the free applyArith
   ladder reaches it through g_cbInterp — the same global the NativeCall
   trampolines use).
   *B2b (mechanical)*: the `.can`/`.^lookup` probe shares one unsafe-names
   list and one NotFound dance (`probeMethodExists`); the EVAL/EVALFILE
   predicate is one `nameEvalsCode` in Ast.h and BOTH gaps are closed
   ($path.EVALFILE no longer slips the flat-scan guard, `.EVAL`-as-method now
   suppresses Lint's dynamic-name rules); exceptionToJson escapes through
   JsonLite's dumpStr; IO::Spec's two byte-identical tmpdirs are one. The
   review's Codegen:1180 JSON-escape claim did NOT verify (that line is
   sequence codegen) — dropped.
   *B2c (construction)*: the default constructor's attribute walk is ONE
   `runAttrDefaults`; the `is DateTime`/`is Date` `.new` and `.now`/`.today`
   arms delegate to it, boxing the parent FIRST (BUILDPLAN order).
   `class D is DateTime { has $.a = 1; has $.b = $!a * 2 }` used to die
   "no self available" — now a=5 b=10, byte-identical to Rakudo. (The
   quanthash-arm concern dissolved: Rakudo itself runs no attr defaults for
   `Set.new(1,2)` positionals.)
   Demoted with data: the numify trio's drift is LATENT — 15 probe inputs
   (radix prefixes, allomorphs, Unicode minus, Complex) byte-identical on
   both engines across val() and numeric prefix — so its consolidation is
   hygiene-when-touched, not a correctness batch. Follow-ups spawned:
   `»«` strict-length enforcement (rakupp cycles where Rakudo dies —
   pre-existing, both modes); compiled parenthesised hyper-assign write-back
   (needs Codegen support for lvalue lists; pre-existing gap).
   Gates: `t/run.raku` 566/566 with the new
   `t/regression/batch2-twin-consolidation.raku`; full Roast
   198,828/218,677 (90.9%) — flat vs batch 1 modulo S17 timeout flutter and
   one stochastic advent test (verified identical on the pre-batch binary);
   the affected S03-metaops/S12-class/S32-temporal slice measured
   1090/1183 before and after B2c exactly.
3. **Measured micro-opts** — **LANDED 2026-08-27** (same day), every change
   held to the interleaved-A/B discipline (alternate every run, min of 9+,
   ratio-judged; fresh arm64 builds; the pre-batch binary snapshotted as the
   fixed reference):
   - *Guard kernel `regexloop`* added first — `if /\d/` under a topic in a
     200k loop — so the class the review found invisible stays visible. Its
     baseline records at the next `--record` (still pending from the
     false-red).
   - *Regex-literal closed-pattern cache* (`RegexLit::closedPat`, the
     `NumLit::ratCache` publish-once discipline): a literal with no `$`/`@`
     publishes its final pattern after the first evaluation, skipping the
     splice copy, both scans, and `rejectObsoleteRegex`'s mutex+map probe
     per iteration; plus boolify(Regex) reads truthiness BEFORE moving the
     Match into `$/` (no ~376-byte copy). regexloop **−3.4%/−4.4%/−4.2%**
     across three independent A/B rounds; X::Obsolete still fires.
   - *Callable slimmed*: `mixinRoles`/`mixinAttrs` (~72 bytes mid-struct,
     read in one place) moved behind one lazily-allocated deep-copying
     pointer. fib **−2.2%/−1.8%** (two rounds); the Path::Finder
     `does Constraint($p)` mixin shape verified identical pre/post.
   - *protoDepth on tctx_*: the per-block-statement "inside a proto body?"
     probe reads the already-loaded ExecContext instead of a second TLS
     wrapper + guard. A block-statement kernel (300k `{ … }` iterations)
     measured **−1.9%**.
   - *Withdrawn with evidence*: the `%h{k}++` "double base resolve" claim is
     stale — the second resolve is already gated behind `newv <= 0` (the
     quanthash-removal path) and never runs for counting loops; the
     `evalIndex` junctionKind `std::string` is an SSO default-construct
     behind an early Int-key short-circuit (~one branch). hash A/B: −1.0%
     (noise).
   - *Deferred as designed-not-measured*: checkRetType return-type
     resolution caching (the chain is alias→set→classes→subsets→native→
     closure-constant, and negative caching is unsafe under EVAL-defined
     types), scoreCandidate signature-fact bits, protoBodyOf resolve-at-
     install — none has a kernel that measures it; per this batch's own
     rule they wait for one (a multi-with-subsets loop kernel) rather than
     land unmeasured.
   Gates: t/run.raku green on the batch build; S06-multi/S05 roast slice
   flat; all four A/B kernels improved or noise, none regressed.

## Post-review arc: issue #37 (fez/zef), 2026-08-27

The cooldown's coda, driven by github.com/ash/rakupp/issues/37 ("fail to
install fez"). Seven general fixes, each hiding the next, all pinned by
`t/regression/issue37-fez-zef-install.raku`:

1. The lexer fuses `<->` / `<=>` / `<+>` into single operator tokens; tight
   after a colonpair name they are the angle-quoted VALUE
   (`:replacement<->` — fez's actual parse blocker at Fez::CLI:1200).
2. `verSatisfies` treats bare `"*"` as anything (it segmented to [0] and
   rejected every candidate).
3. A non-literal paren `use`-adverb (`:ver($?DISTRIBUTION.meta<version> //
   '*')`, zef's self-pinning spelling on every module) is UNCONSTRAINED —
   the raw expression text used to become the requirement string.
4. `$?DISTRIBUTION` in a `-I`-loaded module is an object with a quiet empty
   meta, never the bare undefined (whose `.meta` died mid-load and read as
   "Could not find <module>").
5. `my Bool @a = Nil` is one default element, as Rakudo's ([Any]; the typed
   element-type spelling — Rakudo's [Bool] — is a noted residual).
6. Invoking the undefined value is the Any identity coercion — zef's own
   02-checkbuild.t reaches `Fez::CLI::<&MAIN>('checkbuild')` through a
   package-stash miss and Rakudo passes it VACUOUSLY the same way.
7. `my Hash() %options` — the EMPTY coercion parens (`(Any)` shorthand)
   parse as part of the declaration (Text::Table::Simple's option builder;
   they used to fall out as a sink `()` and take the variable with them).

Plus one regex-engine fix from the same sweep: a Junction eigenstate that is
an INTERPOLATING regex (`rx/ <$_> /`) now matches in the env it closed over
(threading dropped the rxVal and `<$_>` read the match SUBJECT — fez's
Fez::Util::Glob matched everything; 25 of its 27 tests failed, now 27/27).

End-to-end, isolated HOME: `rakupp install fez` (tests pass) → `fez version`
100.0.2 exit 0; `rakupp install --no-test zef` → `zef install
Text::Table::Simple` (fetch → test → install, exit 0) → the module loads and
renders. fez's suite 4/4; zef's own suite 1/10 → 8/10 (remaining:
distribution-depends-parsing.rakutest — deep `:any[…]` alternative-dependency
resolution in Zef::Client — and install.rakutest). Residuals noted:
`my Str() @a` coerces the list whole, not per element;
`.decode('ascii', :replacement<…>)` parses and runs but ignores the
replacement semantics; `use-ok` imports into the caller's scope (an imported
MAIN prints usage after a test file's plan — harmless to TAP and exit).

## Release-gate corrections (2026-08-27, cutting v3.20.0)

The release gates corrected the review's own work twice — both worth
recording as method lessons:

1. **The file-list gate caught batch 1 unifying negative subscripts in the
   wrong direction.** The oracle probe `try { my $v = @a[$neg] }` sinks the
   Failure as the block's value and detonates it — indistinguishable from an
   eager throw. Roast's S02-types/nested_arrays.t is unambiguous: it maps
   out-of-range reads INTO an array and asserts `isa-ok …, Failure`. Four
   roast files died mid-plan under the throw, and the per-run TOTALS never
   showed it (the batches gained more than the four files lost) — only the
   RELEASING.md file-list diff did. All three sites now answer the armed
   Failure with Rakudo's 0..^Inf range; ASSIGN-POS still throws.
   **Lesson: when probing Failure-vs-throw, read the value's type without
   sinking it — and gate on the file list, not the totals.**
2. **X::Sequence::Deduction fires when generation needs the step, not at
   analysis.** advent2012-day14's `@primes ...^ * > sqrt $n` never needs
   deduction — its code endpoint fires inside the seed prefix — while a
   bounded numeric endpoint needs the step up front (misc.t agrees both
   ways). The first placement of the eager-bounded check also split an
   else-if chain and briefly broke descending sequences; the batch-1
   regression file caught it within minutes. day14 reaches 3/6 (from 1/6);
   full 6/6 needs lazy seed streaming (`@lazy ...^ code` pulling its source
   instead of deducing) — chipped as a follow-up. v3.7.0 passed the file on
   a guessed step that put 9 into a list of primes, harmlessly for that
   program — the file-list diff documents it as the release's one
   understood non-full regression.

## Already done in this round

- CI back to green at `6c25a94` (Windows/OpenBSD/slim fixes, budgets
  re-derived; `t/run.raku` 962/962).
- `build-arm64/` scratch-rebuilt at HEAD (single-vintage, ready to record).
- Slim-budget comment arch labels corrected (`build/` is x86_64 here).
- Perf false-red protocol written into the working notes: fresh-build the
  baseline commit before bisecting a red gate.

## Post-release repair, same night (v3.20.1 shipped with two battery drops)

The user noticed the republished site was stale (wasm, dashboard) — pulling
that thread found the release notes carried a **fictitious battery figure**.
The docs said 97/105 "on a grown tier-2 list"; no such list or run existed.
The rel-battery.log rows appear twice (stderr progress note + stdout table,
interleaved), the release-day tally counted rows instead of the runner's own
summary line — the true reading was **48 / 59, a 2-dist regression from
v3.7.0's 50/59** that the fiction hid. Corrected in README.md and
CHANGELOG.md; the honest sitting is committed to the battery repo (7833cb4)
and charted on the dashboard.

Both drops were real engine bugs, found, fixed and regression-tested the
same night (t/regression/pred-answer-regex-and-modifier-temp.raku):

1. **A predicate block's Regex ANSWER must match the element and set the
   caller's `$/`** — `{ .defined && /re/ }` returns the `&&` RHS as the
   object (Rakudo agrees: `(True && /A+/)` is a Regex, `$/` untouched);
   the consumer's boolification is what matches, against the element, with
   `$/` written through to the caller (Rakudo's Regex.Bool + getlexcaller).
   The v3.20.0 regex-literal rework (899976b) read the answer's plain truth:
   always-true, `$/` never set. Broke HTTP::Tiny's multipart-boundary parse
   (`~$/` = "") and, under load, Log::Async's frame test. Fix: a shared
   `predAnswerTruthy(I, res, elem)` helper (Builtins.cpp, declared in
   BuiltinsShared.h) used by matcherAccepts' Code arm, both lazy and eager
   grep/first, toggle, the Supply tap chain, and the sequence Code endpoint
   — at those sites the closure frame is already popped, so setMatchVar
   lands the match exactly where Rakudo puts it.

2. **`temp`/`let` under a statement modifier restored itself instantly.**
   `temp $x = … unless $c;` — the modifier's flattened branch runs via
   execBlock IN the enclosing env, and execBlock drained its own temp marks
   even for these scope-sharing pseudo-blocks, un-tempting the variable the
   moment the one-line branch finished. Latent (v3.7.0 behaves the same);
   exposed when the CURI store from the issue-37 work made
   `try require ::('Terminal::ANSIColor')` start succeeding, turning
   Data::Dump's colorizor live — its `temp $colorizor = sub {''} unless
   $color` neutralizer was the no-op, and ANSI codes leaked (9/9 → 1/9).
   Fix: an execBlock that created no scope of its own (`blockEnv ==
   saved.get()`) owns no temp/let unwinds — SIZE_MAX tempMark defers them
   to the enclosing block, matching Rakudo (modifier temp holds to the
   enclosing block's exit; block-form temp still restores at its own).

With both fixes the battery re-measures at 50/59 — v3.7.0 parity. Also from
tonight: the concurrent `raku-dc` session (issue #38) independently fixed
`sub MAIN(:@x)` single-occurrence listification and tightened named-@/%
scoring to Rakudo's rule; coordination notes live in the session logs.
