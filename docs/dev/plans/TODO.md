# What is open

*Started 2026-10-03. Last updated 2026-10-23.*

This is the one list of work that has been started and is not finished, across
every plan in this directory. Each line gives the plan that owns the details
and the next concrete step. When an item is finished, remove its line; the
history is in the plan and in git. When work starts on something new, add a
line here at the same time as the plan.

The work moves on a broad front: one speed item and one correctness item per
sitting, plus whatever tail is cheapest, so that no front falls behind.

---

## 1. Speed (v6)

The same items grouped by theme, with a complexity estimate for each:
[SPEED-BACKLOG.md](SPEED-BACKLOG.md).

- [ ] **Parallel `.hyper` / `.race` and hyper kernels** (2026-10-09): `.map` /
  `.grep` on a HyperSeq run on the `hyper for` scheduler (runParallel), and
  `»op«` / `».method` over plain Ints and Nums run natively, split over cores
  (InterpreterRegex.cpp). Over 1M Nums on the M3: `.hyper.map` 0.32 → 0.16 s,
  `(4 «*» @a) «*» @b` 92 → 25 ms, `hyper for @a` 0.53 → 0.09 s (its workers had
  queued on one stripe lock per element). On main as 0f15e190 + f0de6b3e;
  perf-guard A/B of that tree against 6f62635c within 5% (fib +4.7%, code
  placement only); `t/run.raku` 1381/1381 on 5df4494a. Left: worker scaling is ~2.2× on 7
  workers — per-call interpretation and allocator frees, no lock.
  (`my @d = <fresh list>` takes over a uniquely owned buffer since 4a1edb88.)
- [ ] **Call-path cost shipped in v5.3.0**: against a v5.2.1 build, `privmeth`
  +9.5%, `strpass` +9.0%, `method` +8.6%, `attrread` +5.8% (2026-10-08, two
  interleaved rounds; retired instructions +6-7%), built up over ~120 commits.
  The profile names a `thread_local` loopNest_ touched twice per iteration in
  runLoopBody, setupRwLinks on calls with no rw parameter, and SlotStripe on
  single-threaded access. loopNest_ is gone (2026-10-11: it and
  loopFirstBody_ live in `t_poll`; `loopsum` walking −3.3% retired
  instructions). Next: the other two, A/B against v5.2.1, then
  `perf-guard --record` (v5.3.0 kept v5.2.0's baseline).
- [ ] **Interpreted slowdowns shipped in v5.4.0** (2026-10-13, local builds of
  v5.3.0 and v5.4.0 in one sitting): `mainwhen` +11% / +13% retired
  instructions, a fixed ~10M instructions at both 200k and 2M iterations, so a
  one-off cost, not per iteration; `sortby` +8% / +8% instructions, growing
  with the input (+6% at 120k words); perf-guard `strscan` and `substrloop` +6%
  wall with +1% instructions. Not bisected; instruction counts are a
  deterministic bisect metric here.
- [ ] **`regex` slower in v5.5.0, and no single commit did it** (2026-10-21,
  Apple M4, local builds of v5.4.0 and v5.5.0 in one sitting): `regex` +6%
  interpreted and +7% compiled. With the loop raised tenfold, so that startup
  no longer counts, both modes lose the same 4.0% a match (interp 203.3 →
  211.4 ms, `--exe` 134.9 → 140.3 ms), so the cost is in the regex runtime
  both share (`regexMatch`, `Regex::search`/`matchNode`, Match building,
  `$/`), not in the tree-walker; the rest is about 0.1 ms more startup. Along
  `main`'s first-parent line it arrives in steps of about 1%: `6021c2fa`
  +0.1%, `36098d73` +1.0%, `cc12af7c` +2.1%, `e9841fec` +2.1%, `403a7e70`
  +4.0%. `git bisect` named `e9841fec` on the 50k kernel, which is startup
  noise; on the tenfold kernel it equals its parent. `166d51b9` and
  `39a80444` only touch regexes with code blocks or `:x`/`:c`. A flat
  `sample` profile of both `--exe` binaries shows the same hot functions in
  the same proportions, so the next step is instruction counts per commit
  across the three stretches, not wall time.
- [ ] **The call path is slower in v5.5.0, accepted at the release** (2026-10-21,
  Apple M4, perf-guard, three interleaved rounds of v5.4.0's local build and
  v5.5.0's, no overlap between the two): `junctionwide` +8.6%, `subcall`
  +7.3%, `strpass` +7.2%, `regexloop` +5.7% (the regex item above),
  `privmeth` +4.5%, `method` +4.0%. The costs admitted when the commits
  landed do not add up to it: `subcall` +1.7% is O25 (cce315ef, every routine's
  own `$_`), `method` +0.5% f7d4dc67, the method kernels +0.7–0.9% dca29704.
  Not bisected; instruction counts per commit, as for `regex`. The perf-guard
  baseline is still the M3's (v5.2.0); re-record it on a quiet machine of
  record and say so in the CHANGELOG.
- [ ] **Append — strings made of pieces**: [APPEND-PLAN.md](APPEND-PLAN.md),
  2026-10-06. Long strings may be views of a shared growable buffer, so
  prepending, `$k = $s` before each append and a `.substr` loop are linear on
  every engine (all 13 shapes of `tools/bench/append-shapes.raku`). A0–A6 done;
  owed: `perf-guard` A/B against `a124d91a` on a quiet machine. Left in the
  plan: `eq`/printing reading views directly, and a non-ASCII grapheme table
  for views (a non-ASCII `.substr` loop still flattens each view).
- [ ] **Graph.diameter (#47)**: 20×20 grid 17.4 s against Rakudo's 19.1 s
  (was 28.8 s), 2026-10-07: accessors before the built-in ladder, an
  `$!attr = …` lane, checked default construction, cached class/Mu/Bool type
  accepts for parameters and returns. What is left is per-call cost: a method
  or closure call is ~0.2 µs against Rakudo's ~0.08 (LeftistHeap makes ~6 per
  merge step). Next: a lean `invokeMethod` path like callPlainSub (typed `$`
  positionals, no phasers), then a block variant for closures; `Less` as a
  term walks every scope looking for a shadowing lexical.
- [ ] **Parallel scaling**: [PARALLEL-SCALING-PLAN.md](PARALLEL-SCALING-PLAN.md).
  P0–P3, P5 and P6 landed in 458886c2. A variable no other thread can reach
  skips the stripe, and a loop kernel may run while workers are live.
  `cpu-fanout` N=4 went from 3.26× to 3.65× (C: 3.81×), N=8 from 3.16× to
  5.32×, and example 3 from 0.41× to 3.84×. Next: the gates not yet run
  (`t/run.raku`, the module battery, the adopters gate); then P7 (`--exe`
  probes), P8 (PARALLEL-SPEEDUP.md, faq/threads.md, ASYNC.md, JIT.md, CNP.md,
  the book), and P2's raw-pointer audit.
- [ ] **`ub-iterate-during-push/parallel` died once on linux-x86_64**
  (fc6abf32's CI run, 2026-10-11; green on the run before and after). A death
  by signal, which the stress runner then reported as `exit 0`; it now names
  the signal. 200 parallel runs on macOS never hit it, and every for-over-array
  copy and the `.push` fast path take the container's stripe. The August
  crash of this program (221d4763) was also Linux-only: glibc surfaces a torn
  copy that macOS's allocator forgives. Next: the signal and a backtrace from
  the next CI hit. (The `.kv`/`.join` fast arms copy elements unstriped — a
  latent torn copy of the same kind, not this program's path.)
- [ ] **TSan: the `private-*` stress programs are tolerated, not clean.**
  12 of the 13 programs 458886c2 added to `t/stress/` report under Linux TSan
  in parallel mode, the same with `RAKUPP_PRIVATE_SLOTS=0` and on 458886c2's
  parent (where `private-rw-link` and `private-stash` also segfault), so the
  private-slot work is not what they find. They hit the sites `ub-torn-values`
  hits, which is why that program has been skipped under TSan since August.
  `%tsan-parallel-reads` in `t/stress/run.raku` tolerates their reports and
  still fails a case when a store held no lock (f13d2620 fixed the two
  races of that kind: the relaxed `liveWorkers_` gate and `Promise.status`).
  To take them off, the stripe has to cover the slot-flag reads ahead of a
  store (`readonly`, `natBits`, `t`, `hashKind` in `evalAssign`'s lane,
  `evalAssignInner` and `selfCatAssign`), the result copy of an unsunk
  assignment (`*lv` in `evalAssignInner`, the lane's `*slot` returns, made
  after the store releases the stripe), and the cell-tag read in
  `Value::deref` from `padPtrIn`, `Env::local` and `Env::find`. The cost lands
  only while workers are live and the slot is shared. Then the entries leave
  `%tsan-parallel-reads` one by one, and `ub-torn-values` can come off
  `%tsan-parallel-skip`.
- [ ] **Interpreter at native speed**: 17 of 35 tasks open.
  [INTERP-SPEED-PLAN.md](INTERP-SPEED-PLAN.md). Task 9 (fused leaves) has its
  native and Rat leaves since 2026-10-11 (`types/mandel-native` 1.34 →
  0.84 s); its `loopsum` criterion is not met walking (7.0× `--exe`, needs
  4.15×), and what is left there is task 4's, not leaves. Next: quicken
  `evalAssign`/`evalIndex`/`evalUnary`, then task 10 (automatic `--cnp`
  tier-up). Task 1's quiet-machine baseline is still owed.
- [ ] **Loop and sub kernels beyond scalars**: [KERNEL-PLAN.md](KERNEL-PLAN.md).
  Tasks 2–4 and the `--help` text done 2026-10-03 (Nums, Rats as exact slots,
  `**`, `min`/`max`, pure methods; `**`/`min`/`max` and Num seeding in the
  `--exe` lanes). Task 1 done 2026-10-04 (arrays and hashes in place with a
  bounded undo log) except `%h{…}:exists` and `for @a` as a source. Next:
  task 5 (`--exe` parity, now also Rats, the methods and containers), the
  UNBOX-PLAN note of task 8, then 6–7. Owed:
  `perf-guard --check` on a quiet machine (inconclusive at load 4 on
  2026-10-03; in cycles `fib`/`mainwhen` level, `objnew` +1.5% from code
  placement).
- [ ] **`--cnp` copy-and-patch**: [CNP-PLAN.md](CNP-PLAN.md). P5, the
  platform matrix: only the linux-aarch64 release carries stencils. Next are
  P5a (one table per architecture in the universal macOS build), P5b
  (`-fno-jump-tables` for ELF x86-64) and P5c (the gate in CI, which closes
  P1), then OpenBSD, riscv64 and Windows. P2 arena allocation; P3 `.kv`, the statement
  modifier, `.map`, closures, `[+]`, whole-array assignment, `given`, `return`;
  the threaded-program gap; P4 make it the default and remove `--jit`.
- [ ] **`--types`**: paused 2026-09-29. [TYPES-PLAN.md](TYPES-PLAN.md). N1 the
  switch, N2 the checked rule, N3 measure on Roast and the battery, N7 Str.
- [ ] **V6 phases not started**: [V6-PLAN.md](V6-PLAN.md). P0 steady-state
  measurements on a quiet machine; P4 compile and load time (the precomp key
  is a decision for the maintainer); P5 `--exe` size and fallback coverage;
  P7 `use` in the web editions (#83).
- [ ] **`tctx_` thread-local lookups**: 20% of a walking `loopsum`'s samples
  on 2026-10-11 (29% before loopNest_ and loopFirstBody_ moved into
  `t_poll`): one `tctx_` read each in runLoopBody, execPlainBlock, exec and
  evalAssign per iteration. Next: pass the context down
  (INTERP-SPEED-PLAN task 4).
- [ ] **`--slim` review**: what `--slim` actually removes. First round
  2026-10-17: `unicode_props2_gen` (Age, Line_Break, East_Asian_Width and the
  other enumerated properties, 318 KB) was never cut; it rides `unicode-props`
  now, and `--slim=-all` hello is under the 12.5 MB line again by 245 KB
  without a re-pin. A linker map of the rest finds only the interpreter
  itself. Left: every `uniprop` call keeps both `unicode-props` and
  `unicode-names` whatever it names, because SlimScan's noteName treats a call
  like a bare `&uniprop`. [SLIM-PLAN.md](SLIM-PLAN.md) P6 (make `auto` the
  default) and P7 (`--aot`) follow.
- [ ] **Small tails**:
  - [VALUE32-PLAN.md](VALUE32-PLAN.md): one measurement decides B versus C.
  - [JUNCTION-PLAN.md](JUNCTION-PLAN.md): Ph3, list-associative `|`, `&`, `^`.
  - [DISPATCH-PERF-PLAN.md](DISPATCH-PERF-PLAN.md): a per-class
    construction plan.
  - [UNBOX-PLAN.md](UNBOX-PLAN.md): P3–P5, partly overtaken by `--cnp`.
- [ ] **Compiled float code at C's speed**: [NUM-UNBOX-PLAN.md](NUM-UNBOX-PLAN.md).
  Where `-O` leaves the box it already matches C, and elsewhere it is 6–190×
  slower. P0, a wrong answer, is done: the F64 loop lane answered `Inf` for a
  zero divisor, where both engines and Rakudo fail. Next come a refusal
  census, Num twins of routines, native arrays under `--exe`, element lanes,
  and small arrays that never leave their routine.
- [ ] **`--exe` native module bodies**: the remaining cost is method dispatch
  (the `RT.methodCall` ladder).
- [ ] **Built-in methods in interpreted loops**: each walked methodCall's
  whole chain, 150–250 ns a call. The commonest are answered at its entry
  now (`.push`, `.Num`, `.Int`, `.Str`, `.defined`, `.elems`, `.WHICH`,
  `.join`, `.kv`, Hash `.values`, a CStruct's fields). Next: `.map`, `.flat`
  and `.any` on a Seq; a CStruct's CArray field built once per pointer, not
  per read (Rakudo keeps child objects, but object `eqv` and `.Capture` read
  every attribute key). The `fib` integer kernel moves ±5% with code
  placement in unrelated files: pin its alignment so perf-guard stops seeing
  it. (2026-10-14: perf-guard read it +3.8% after the V6 P1 batch, and 12
  interleaved runs of fib alone read 51.3 against 50.2 ms, the batch ahead —
  the wobble, seen again.)
- [ ] **Run-time quadratics the scaling gate found** (2026-10-10): seven
  common operations whose cost per call grew with the size of what they work
  on. Each is a `known` shape in `t/scaling/run.raku`, a TAP TODO, so the gate
  stays green; take a shape's mark off when it reports linear (the summary
  says so). Five are linear since 2026-10-11 (`(elem)` looks the key up,
  `.index` with a position skips the decode, `[~]` appends into one buffer,
  Buf `.push`/`.append` let go of the second reference before appending).
  Left, growth for 8× the size (linear is about 8×):
  - `@a.unshift` / `.prepend`, ×60 (`run/array-unshift`): each call moves
    every element. Next: headroom at the front of the ValueList storage.
  - `$s ~~ m:c/…/` in a loop, ×58 (`run/str-match-continue`): each match pays
    for the position it starts from (`match-offsets-linear.raku` covers a
    neighbouring shape).

## 2. Correctness (v5 error batches)

- [ ] **A bare `<:Name>` the engine does not know matches every character**:
  `'A' ~~ /<:Bogus_Thing>/` is True (Rakudo 2026.09: False). The last line of
  uniMatchesProp's bare form answers True for a name it cannot place, on
  purpose ("an unmodelled script"); the same rule made `<:sc<Latn>>` and
  `<:Uppercase_Letter>` match everything until 2026-10-17, when the value form
  and the long category names stopped reaching it. Short script codes in the
  bare form (`<:Latn>`) still do. Deciding needs a look at who relies on the
  rule; the alias table (`ucd::valueAliasTable`) can resolve `<:Latn>` now.
- [ ] **Natives against Rakudo 2026.09**, found 2026-10-15 while fixing
  Windfall's native-parameter divergences (b7c47a57), each measured:
  - A boxed Int never reaches a native-int multi candidate in Rakudo:
    `multi m(int $x) {…}; multi m(Str $x) {…}; m(2**62)` is "Cannot resolve
    caller m(Int:D)". Raku++ dispatches it to the `int` one.
  - A `uint` refuses a negative Int at or below -2**31 ("Cannot unbox
    negative bigint into native unsigned integer": MoarVM keeps those as
    bigints), parameter and `my uint` alike; Raku++ wraps every negative.
  - Assignment into a native: a Str into `my int` / `my num` is
    X::TypeCheck::Assignment (Rakudo: X::AdHoc "This type cannot unbox to a
    native integer: P6opaque, Str"); `my str $x = 1` stores "1" and
    `my int $x = @a` the element count, where Rakudo refuses both.
  - `my atomicint` is not a native container: it stores 2**70 and 1.5, and
    `$a * 2` at 2**62 is 2**63 (Rakudo: int.min). Parser::isNativeIntTypeName
    leaves `atomicint` out; adding it made an `atomicint` PARAMETER wrap but
    left the container unchecked, so it was not done alone.
  - NativeCall's C spellings as parameters keep their widths in Rakudo:
    `bool $x` given 2**62 binds 0 (one byte), `long $x * 2` wraps at 64 bits.
    Raku++ binds the value unchanged (it does refuse what they cannot unbox).
  - `.REPR` answers P6opaque for a CArray and a Pointer (Rakudo: CArray,
    CPointer), and autothreads over a Junction (Rakudo: P6opaque).
  - `.resume` resumes an exception thrown by `die`, or by the engine (Rakudo:
    "This exception is not resumable").
- [ ] **HyperSeq edges against Rakudo 2026.09** (found 2026-10-09; `.raku`
  and `(1..Inf).hyper.map` fixed 2026-10-10): `.elems` then `.list` answers
  twice where Rakudo's `.elems` consumes it (X::Seq::Consumed);
  `@a.hyper.invert` dies at once where Rakudo answers a HyperSeq and dies only
  when read.
- [ ] **Battery: re-measure after the 2026-10-10 fixes** (44/59 at v5.3.0).
  Color, Encode and Trap pass their own suites again (a method `proto` kept
  in its dispatcher; `my class X::…` published in X's stash; a `:w` handle
  writing at its own position — t/regression/battery-proto-new-x-stash-
  write-position.raku). Next: the whole battery on a machine that has it.
- [ ] **Raku.js runs worker bodies inline, on the main registers**: with no
  pthreads, BigStackThread runs a `start`/`react` worker body on the caller,
  and the body's `loadCtx(empty)` + `dynStack.push_back(spawnScope)` replaced
  the main thread's registers for the rest of that program. Since 2026-10-10
  the inline run saves and restores tctx_ (BigStackThread::runInline_), as
  SlangTctxGuard does. Owed: a WASM build and rakujs/smoke.cjs with a program
  that reads a lexical after awaiting a `start` (no emcc on the Mac it was
  written on).
- [ ] **`rk_run` skips the CLI's compile-time checks** (decision): rakuppRunOn
  is called without declCheck, so Raku.js and every embedder run programs the
  CLI refuses (undeclared variables, #32; calls that can never bind,
  d78d1cb0). Turning it on changes the browser's behaviour, including two
  Raku Koans solutions.
- [ ] **V5 batches B1–B6**: only B0 (the instruments) has landed.
  [V5-PLAN.md](V5-PLAN.md).
  - B1: regressions (battery scan to 0).
  - B2: crashes (Text::Markdown, the `evalCall` race, the Rat cache race,
    `--exe` Inf/NaN, the layout-hash symbol).
  - B3: silent wrong answers.
  - B4: ecosystem and MOP clusters (custom HOW, Attribute MOP, Red #77).
  - B5: JS gate disagreements to 0, the `--slim` size gate.
  - B6: measure and tag.
- [ ] **Array elements are itemized only when written one at a time**:
  `my @w = [1, 2], [3, 4]; @w[0] = [5, 6]` gives `($[5, 6], [3, 4])` for
  `@w.Seq`, `@w.Slip`, `@w.values`, `@w[*]` and `|@w` (Rakudo: `$` on both).
  So `cross(|@w)` and `cross(@w)` cross one element whole and spread the
  other, where Rakudo takes both as items. `@w.List` strips them now, which
  covered Math::NIntegrate (2026-10-09). `List(@w)` is the Array unchanged
  since 2026-10-11 (it flattened deeply).
  `.kv`, `.pairs` and `.antipairs` of an Array itemize its nested Arrays and
  Hashes since 2026-10-10 (`[[1, 2], 3].kv` is `(0, $[1, 2], 1, 3)`); the
  rest wait on the decision. Next: decide whether an Array slot always reads
  as itemized.
- [ ] **Construction does not track which attributes BUILD initialized**:
  `has $.x is required; submethod BUILD() {}` with `.new(:x(5))` is
  X::Attribute::Required since 2026-10-11 (judged on what BUILD left in the
  slot), but `submethod BUILD(:$!x) {}` with no argument still dies here where
  Rakudo counts the bind as initializing (`C.new.x` is Any); and `has Int:D $.x is required;
  submethod BUILD(:$!x) {}` with no argument is X::Attribute::Required where
  Rakudo reports the binder's type check. Found 2026-10-07 with Graph (#47);
  the `T:D` cases are judged after BUILD now. Next: an "initialized" bit per
  slot, set by binding and assignment.
- [ ] **`S17-promise/nonblocking-await.t` crashes ~1 run in 10** (the
  ledgered flake, PARALLEL-PLAN / CYCLE-GC-PLAN): after test 6, on
  `$p.break('bust')`, SIGSEGV (exit 134 or 139) in `~ClassInfo` under
  mixinValue, in a `start` worker inside `die`. A hundred workers die at
  once, each mixing into its exception and writing `classes_[nc->name] = nc`
  (InterpreterOperators.cpp) with no lock, so one replaces and frees the
  class another is using. 6 of 60 at 49c98013. Found 2026-10-15; not started.
- [ ] **`t/race/varcell.raku` fails again**: 4–17 wrong answers in 200 runs at
  `e4ee17cd`, where the 2026-10-03 fix measured 0 in 200. Something since then
  reopened the first-wave promotion race (`g(:v($named))`, `a => $paired`,
  `($listed, 1)` on eight fresh workers). Found 2026-10-08; not started.
- [ ] **Module code can name the program's lexicals**: a module's
  `sub peek() { $secret2 }` compiles and reads the program's `my $secret2`, and
  `::('$secret')` finds one (Rakudo: X::Undeclared at compile time, and no
  such symbol). The module unit scope chains to `global_`. Found 2026-10-08;
  the parallel-scaling sweep accounts for it, the semantics are not fixed.
- [ ] **A `use` inside a routine runs when the routine is CALLED**: Rakudo
  compiles the module at compile time (its BEGIN blocks run then), while here
  nothing happens until `f` runs. Re-checked 2026-10-10 on 2026.09: the
  module's names stay invisible outside the routine there too (`GG::Path`,
  `::("GG::Cls")` are unknown), and its mainline runs late — so the
  difference is the timing of BEGIN-time side effects, not name visibility.
- [ ] **Left from the pseudo-package-over-label fix**: a bare `OUR` is a
  type named OUR (Rakudo: the current package, so `next OUR` names
  `GLOBAL:U`); `$CALLER::y` inside a loop block finds an outer variable
  (Rakudo 2026.09: "Cannot access '$y' through CALLER, because it is not
  declared as dynamic";
  `MY::<$x>` and `&next`/`&last`/`&redo` as Subs fixed 2026-10-10). Found 2026-10-07; not
  started.
- [ ] **Rakuglaze**: 3326 of 3326 pass (2026-10-09: round 6, the eleven
  failures Haiku batches 04 to 12 brought in;
  t/regression/rakuglaze-round-6.raku). Seen in round 6, not fixed: a spaced `f - 5` with `f`
  an UNKNOWN name is still `f() - 5` (declared subs, subs declared later in
  the file since 2026-10-11, and say/put/print/note/dd take the prefix). `('A'..∞).is-lazy` is ruled True in the
  suite (Rakudo answers False, yet treats the range as lazy everywhere else).
  Open from the write-through work: a list of an Array's elements is a copy
  that a write is MIRRORED from, so a later change to the array is not seen
  through it (`my $r = @a.reverse; @a[0] = 9; $r` still shows the old value);
  and compiled code does not refuse a write into an immutable List
  (`my $l = (1,2,3); $l[0] = 5` under --exe). Seen during round 5, not fixed:
  - `%m{$i}{$i} = $i++` stores `{1 => {1 => 0}}` (Rakudo: `{0 => {1 => 0}}`):
    the outer key is read after the right-hand side, and a key with side
    effects is evaluated twice (`%m{k()}{k()} = k()`). The stray `0 => Any`
    is gone (2026-10-10); the rest needs the base container held across the
    right-hand side.
  - `for $l.list` over a quit Supply's list raises before the values;
    `for $l` and `for @$l` read them first, as Rakudo does.
  - An INIT default that reads an earlier parameter (`$y = INIT { $x }`)
    still runs per call.
  - `sub k { 1 } sub v { 2 }` on one line parses; Rakudo says "Strange text
    after block".
  - `Int:D()` given a Str type object dies at bind; Rakudo is not consistent
    there (it dies, or returns an undefined value).
  Also a `my class` used earlier in its OWN block is no longer reported as a
  post-declaration.
- [ ] **NativeCall CArray gaps**: all of the list fixed 2026-10-10
  (t/regression/nativecall-names-and-null-fields.raku) but identity: a CArray
  read back from a CStruct field knows its length and writes through, yet is
  not `===` the one stored (Rakudo keeps the child object itself); a typed
  `Pointer[T].new($addr).raku` keeps its `[T]` where Rakudo drops it.
- [ ] **#110 constructor type checks**: the module battery gate was never run,
  for #110 or for the nominal-check fixes of 2026-10-07. Open: native width is
  not enforced; `has Array[Int]` is not checked. Seen beside those fixes:
  two matching unrelated types (Positional and Iterable for an Array) go to
  the first declared where Rakudo reports X::Multi::Ambiguous. Multi dispatch
  ranks a narrower type first (`Int $x` over `Numeric $x where * > 0`) since
  2026-10-11, but only between candidates of the same positional arity with
  no slurpy or optional parameter (sameShapeForTypeRank); other shapes still
  let a `where` or a smiley outrank a narrower type.
- [ ] **Supply closers on aarch64**: the three closers of the class fixed in
  925260b1 (the inner-tap closers and tapSupply's `ended` write) take the
  supplier's lock since 2026-10-10 (tapSupply leaves its key in the record,
  closeTapRecord). Owed: the Linux TSan run of `t/stress` that would show it.
- [ ] **Cro and #116 follow-ups**: `NativeLibs EXPORT failed: No such
  method 'dispatcher'` and LibCurl::EasyHandle's "Redeclaration of return
  type" no longer reproduce (2026-10-10: NativeLibs loads, LibCurl's
  t/01-load passes). Left, and needing Cro installed to see: a bare `Node`
  resolving through the `classAliases_` tail (two modules each declaring
  `Node` resolve right inside their own package), and nested protos under
  LTM. Seen beside them: two `use`d modules exporting the same `Node` are
  "Redeclaration of symbol 'Node'" in Rakudo; here the last one wins.
- [ ] **Phaser order**: perf-guard was not run for 55b4f18a. `temp` is
  restored BEFORE the LEAVE phasers since 2026-10-11, as Rakudo 2026.09 does
  (2026.08 did not): `my $x = 1; sub s { temp $x = 2; LEAVE say $x }; s()`
  says 1, and so does a LEAVE in a `.map` block. Not checked: gather bodies.
  Which order to keep is a decision (below): Roast does not test it.
- [ ] **Semantics sheets, step two**: about 11 sheets.
  [../findings/semantics/](../findings/semantics/).
- [ ] **Divergences found in sweeps and not fixed**:
  - A Buf copied into another variable does not see a later `.push`:
    `my $c = Buf.new(1); my $d = $c; $c.push(2); $d` is `Buf:0x<01>` (Rakudo
    `<01 02>`: a Buf is a reference). Its bytes are a copy-on-write string,
    so the push copies them away from `$d`. Found 2026-10-11.
  - `1 + (* + 1)` held in a variable is a Num here, where Rakudo dies (Numeric
    of a WhateverCode); `"a".encode ~ "b".encode` is a Buf here and a utf8
    in Rakudo. Found 2026-10-11 (`"a" ~ $buf` dies as Rakudo's since then).
  - `use` inside a sub loads at call time.
  - Role conflicts are undetected or reported late.
  - An exported `infix:<+++>`.
  - Outside `react`, a `Proc::Async` with `:w` feeds its stdout taps only when
    the start promise is awaited, so an interactive driver never sees a reply
    before `close-stdin` (found with #121).
  - A slurpy hash or a capture does not write back: `sub q(*%n) { %n<x> = 31 };
    q(x => $v)` and `sub r(|c) { c<x> = 33 }` leave `$v` alone (Rakudo: 31,
    33). Named `is rw` / `is raw` parameters do (2026-10-04).
  - A multi candidate wrapped with `.wrap` keeps taking part in dispatch with
    its `where`: `multi wr(Int $n where * > 0)` beside `multi wr(Int $n)`,
    the first wrapped, answers `wr(-1)` from the second; Rakudo dies binding
    the wrapped candidate ("Constraint type check failed").
  - An Array BOUND into a lexical array's slot reads back itemized:
    `@b.BIND-POS(0, [10])` and `@c[0] := [10]` give `$[10]` for `@c[0]`
    (Rakudo `[10]`; `@c.raku` agrees). Every element read itemizes an Array
    slot (itemizeElem), so a bound slot needs a "no container" mark of its own.
  - An `is default` array's `.sum` counts the default, 15 for
    `my @a is default(7); @a[2] = 1` (Rakudo 2026.08: 1, its sum skips the
    default — every other read gives 7, so this one is left as it is).
- [ ] **Quote and word-list divergences left by f27fa12f and ed499880** (qq
  closures that hold the delimiter; qqw interpolating before it splits).
  Found 2026-10-10 against Rakudo 2026.08. Fixed 2026-10-11: a one-word
  q-family list is a List (`qw[a]`), `qq ｢…｣` / `q "…"` after a blank,
  `qq'a…'` as an identifier, `"X".lc("a")` an arity error.
  - A qww/qqww span holding the delimiter ends the quote: `qqww[a "b]c" d]`,
    `qww[a "b]c" d]` and `qqww｢a "b｣c"｣` are parse errors (Rakudo:
    `("a", "b]c", "d")`). The lexer's word-list scan knows no spans.
  - A negated adverb glued to the opening bracket is the delimiter here:
    `qq:!c[…]`, `qqw:!s[…]`, `qqww:!b[…]` work. Rakudo refuses them ("Argument
    not allowed on negated pair") and needs a space.
  - A quote word after prefix `|` is not a quote: `f |qw[a b]` is "Undefined
    routine 'qw'", `f |qqw[a $x b]` "Two terms in a row".
  - A `qq｢…｣` inside another one's closure fails (`qq｢{qq｢{"｣"}｣}｣`; Rakudo:
    `｣`). copyCodeBlock reads `｢…｣` as a plain Q span and never sees the `qq`.
  - `Q:c{{a {{1}} b}}` runs the nested `{{1}}` as a closure (Rakudo: text).
    Without the backslash feature parseInterpString has no literal brace.
  - Under `qq:!b`, `\{` still escapes the brace: `qq:!b [a \{"]"} b]` is a
    parse error (Rakudo: `a \] b`, a backslash and then a closure).
  - `«a\ b c»` is `("ab", "c")` (Rakudo: `("a", "b", "c")`). The lexer takes
    `\ ` for an unspace, since it does not track `«…»` depth (Lexer.h would
    need a counter, as angleWords_ is for `<…>`); qqww has it right.
  - `«/c ""»`: the empty span is `""` here and `IntStr.new(0, "")` in Rakudo.
  - `qqww{a {1+1} $x}`: Rakudo makes the nested braces words of their own
    (`"{", "1+1", "}"`); here `"{1+1}"` is one word.
- [ ] **Typed element stores left by b21ea7a6** (coercion types on every
  element store; Hash.append/push merging). Found 2026-10-11 against Rakudo
  2026.09. An op-assign into an element (`%h<a> //= "x"`, `@a[0] ~= "x"`)
  checks and coerces since 2026-10-11, and `Int(Any) ~~ Int` is True.
  - A push onto a typed array names no container: "for an element of the
    container" where Rakudo says `@a` (MethodCallTail's elemCheck passes "").
  - `my Int() %h; %h<a> = Str` fails X::TypeCheck::Assignment here; Rakudo
    tries the coercion and dies X::AdHoc ("Cannot create an Int from a 'Str'
    type object"). coerceElemValue leaves an undefined value alone.
  - Append onto an entry bound with `:=` (`%h<a> := $x`) keeps a Hash value
    whole (`[{:x(1)}, 5]`; Rakudo `[:x(1), 5]`, and Rakudo also writes the
    merged Array through to `$x`).
  - Rakudo quirks, left as they are: `splice` into a `my Int() @a` stores the
    uncoerced Str (`Array[Int(Any)].new("7")`; here X::TypeCheck::Splice), and
    `.append` of a Hash onto an existing Array value drops the Hash
    (`[1, 2]`; here `[1, 2, :y(2)]`).
- [ ] **Cell promotion while workers are live**: `varCell` (Pair, list
  literal, `given`) no longer promotes a variable's slot while `start` workers
  run, because the in-place rewrite raced unlocked readers; there the Pair
  holds the value, so `$p.value = …` does not write back. The other
  promoteToCell sites (`:=` binding, loop variables in src/InterpreterCore.cpp)
  still rewrite in place. Gate: `t/race/varcell.raku`.
- [ ] **Roast tracks A3**: retire the remaining copy-back mechanisms
  (`rwLinks`, `copyOutRw`, `builtinTopicWB_`, `ArgWriter`).
  [ROAST-TRACKS-PLAN.md](ROAST-TRACKS-PLAN.md). The B1 coroutine code on
  x86-64 and Windows is only compile-checked; ASan and TSan runs are owed.
- [ ] **Package-qualified names left by #169** (found 2026-10-14 against
  Rakudo 2026.09, writing its regression). A qualified CALL through an
  imported class name works since 3c6cf2d5 (`C::create()` is
  `&M::C::create`); two neighbours do not.
  - A qualified VARIABLE through an imported class name does not follow the
    import: `module M { class C is export { our $v = 5 } }; import M;
    say $C::v` says (Any) (Rakudo 5). The type object `C` is named `M::C`;
    the variable lookup needs the same retry the Call evaluator now makes
    after pkgRelativeRoutine (src/InterpreterCore.cpp), for every sigil and
    for `C::<$v>`.
  - A subscript adverb on a stash subscript does not parse in parentheses:
    `say (N::<&g>:exists)` is "expected ) (got ':')" (Rakudo True), while
    `(%h<a>:exists)` parses. Unparenthesized it goes wrong quietly:
    `say N::<&g>:exists` prints `&gexists => True`, and `my $x =
    N::<&g>:exists` stores `&g`. The `Pkg::<…>` term in src/Parser.cpp
    does not take the postfix adverb a normal subscript does.
- [ ] **`map` and `grep` over a finite list are eager; Rakudo's are lazy**
  (found 2026-10-14 by fez; checked against Rakudo 2026.09). b6ac7079 made
  the one shape that broke real code lazy: a `.first`/`.head` over a plain
  map/grep chain marks it (LazyChainMark, lazyChainSource in
  src/InterpreterCalls.cpp) and only the elements it reads are mapped. The
  general case is still eager, and shows in two places:
  - A Seq kept for later runs its block where it is written: `my $s =
    (1..3).map({ print "m$_ " }); say "made"` prints `m1 m2 m3 made`
    (Rakudo `made m1 m2 m3`).
  - `for` over a map runs the whole map before the body's first pass:
    `for (1..3).map({ print "m$_ " }) { print "b$_ " }` is `m1 m2 m3 b1 b2
    b3` (Rakudo `m1 b1 m2 b2 m3 b3`), and so for grep. Beyond the order of
    side effects, this costs: `for @files.map(*.slurp) { last if … }` reads
    every file, and an endless or expensive tail is paid before `last`.

  The pieces exist — the lazy map/grep arms of methodCallTail, the
  `return`-block map that is already a lazy Seq (mapBlockReturnsOut), and
  lazyChainSource's in-place reading of an Array — so the change is to let
  map/grep hand back such a Seq generally and have each eager consumer
  (list assignment, sinking, `.elems`, …) drain it, and `for` pull from it.
  The cost is the reason it was not done in passing: the eager loop is the
  fast path (aliasing, arity > 1, FIRST/NEXT/LAST, the AST-level map fast
  paths), and a lazy view adds a std::function call and a buffer push per
  element. Do it with perf-guard numbers for a map-heavy kernel beside it,
  and keep the eager loop for consumers that drain anyway.
  - Found on the way, on any lazy source (a `gather`, a lazy map): `.first`
    with `:end` scans FORWARD — `gather { take 1; take 2; take 3
    }.first(* > 0, :end)` is 1 (Rakudo 3) — and `.head(*-1)` reads the
    WhateverCode as 0 and answers `()` (Rakudo `(1 2)`). Both are the lazy
    `first`/`head` arms of methodCallTail (src/MethodCallTail.cpp);
    lazyChainConsumer keeps the lazy chain away from both shapes, which is
    why b6ac7079 does not hit them.

## 3. Compiled backends

- [ ] **`--exe -O` and `--cnp` answer regressions wrongly** (the t/exe and
  `t/jit/run.raku --cnp` gates, 2026-10-15, the same on 5549bf83):
  - `-O`: t/regression/nfc-join-window.raku's "e + acute" join counts 2
    codes where the plain build counts 1, and
    typed-subtree-native-and-rat-leaves.raku's `$un != $neg - $zero` (a
    `uint8` 255 against an `int8` -1) loses nativeMixedSignNe's False.
  - `--cnp`: typed-subtree-native-and-rat-leaves.raku's 27, "a FatRat leaf
    keeps the general path", fails.
  - Both gates' other difference, lookbehind-lexical-token-window.raku, is a
    timing line the program prints.
- [ ] **`--exe` / AOT findings** (t/aot):
  - `&?ROUTINE.name` is empty and `callframe(0).line` is 0.
  - `$Mod::var` of a unit module fails.
  - A `-> &f` loop parameter fails.
  - Assigning to a read-only parameter is allowed.
  - `my Int @b` loses its constraint.
  - A non-literal `enum` in a sub gives 0.
  - 18 AOT differences in the corpus.
  - Two `-O` differences in t/exe.
  - `--exe` refuses multidim slices.
  - A natively compiled sub binds Nil, Any, a type object, or (named) a
    plain scalar to an `@` parameter as a one-element list, and a `:@x!`
    candidate takes `x => Nil`; the interpreter and Rakudo refuse all of them.
  - Refused since 2026-10-03, so these programs are bundled rather than wrong:
    `state` variables (they compiled as `my`, and `$++` did not compile at
    all) and assignment to a slice (`@a[0, 1] = 7, 8` filled one element).
    Native versions are owed: a `static` is right for a top-level sub or loop
    but not for a cloned closure.
  - Read-write loops: only `for <container>.kv -> $k, $v is rw` is compiled
    (in JS too). A one-variable `<->` aliases the element mid-body in the
    interpreter, so a copy-back cannot stand in for it; `$h.kv` (a Pair is
    possible there) stays with the interpreter.
  - Rakudo dies on `<`, `<=>`, `%` and `mod` over a Pointer; Raku++ answers.
  - A `*` under a user operator (`*!`, `* quack 5`) is refused and bundled;
    the emitter does not build the curry natively.
  - An `EVAL` that names a local of the native code around it fails:
    `sub f { my $x = 1; EVAL '$x = 5'; $x }` dies "Variable '$x' is not
    declared" in a natively compiled program (2026-10-16, the same on
    fda1712a). A routine's native body under bundling (a module's, or a
    bundled program's own) stays interpreted instead when its body holds an
    EVAL or a run-time name lookup (Codegen aotIneligible); the whole-program
    path still needs the same refusal, or a way to publish its locals.
  - A `my $*X` in a routine's native body under bundling (rtAotDeclDyn) was
    not where a dynamic lookup finds it — `my $*OUT = FakeIO.new; code();
    $*OUT.Str` read the real STDOUT (roast S16-io/say.t; a module's sub the
    same on fda1712a). Such a routine now keeps its interpreted body
    (Codegen dynDecl, routine-wide); a native version is owed, starting from
    why a full dynamic walk skips the frame the body declared it in.
- [ ] **`--target=js`**: [TRANSPILE-PLAN.md](TRANSPILE-PLAN.md). The corpus
  gate has never been green. Open: the slot-binding half of the container
  model; `use` refused (exit 5, see V6 P7); `t/js/run.raku` does not pass MAIN
  arguments; port `Test`; `:nth`/`:x`/`:m`; P5 sites.
- [ ] **WASM build**: `rakujs/node-build` is a stale July build. The fallback
  cannot rescue `use`d modules. Switching to `-fwasm-exceptions` is offered,
  not done.
- [ ] **Backtraces P4** (optional): the column caret and JS parity.
  [BACKTRACE-PLAN.md](BACKTRACE-PLAN.md).
- [ ] **`--fmt`**: the R1-inside-a-multi-line-regex family.
  [FMT-PLAN.md](FMT-PLAN.md).

## 4. Embedding and bindings

`librakupp` and the six bindings (Python, JavaScript, Go, Rust, C++, Wolfram
Language) are committed and gated on every push, and
[bindings/README.md](../../../bindings/README.md) still marks them as work in
progress. As of v5.2.0, the release CI builds four Python wheels (macOS
universal2, manylinux_2_28 x86_64 and aarch64, win_amd64) and smoke-tests each
one installed in a venv. The Windows export surface and the Windows bindings
gate now pass in CI. `pip install rakulang` installs them from PyPI (5.2.0 and
5.2.1 are published); each release reaches PyPI when Actions → PyPI is run
with its tag, after the release run is green.

- [ ] **Spreadsheets**: `=RAKU(...)` in Google Sheets and `=RAKU.EVAL(...)` in
  Excel, on Raku.js rather than `librakupp`:
  [bindings/spreadsheets/README.md](../../../bindings/spreadsheets/README.md),
  started 2026-10-08. Excel runs sideloaded on a Mac from
  raku.online/embed/excel/. In Sheets the engine runs in a sidebar, loaded
  once in the browser, and `=RAKU` passes messages through the document's
  cache: an engine inside Apps Script cost about 3.5 s a cell (measured in a
  real spreadsheet), since every formula is a fresh run. `test/sheets.mjs`
  plays that round trip in Node, `test/sidebar-harness.html` the sidebar page
  in a browser, and `test/excel-harness.html` the add-in. Next in Sheets: the
  sidebar in a real spreadsheet (its worker, a custom function writing the
  cache, a formula entered again running again, the time a cell takes), then
  the add-on as a test deployment. Then: does a `RAKU.EVAL` cell calculate in a new Excel session
  without the pane click (if not, start the add-in with the document); a
  template spreadsheet to copy; the store listings, whose material is in
  [store/README.md](../../../bindings/spreadsheets/store/README.md) — the
  accounts, consoles and screenshots are the publisher's. It ships the 5.2.0
  Raku.js in `rakujs/playground`, which needs two workarounds in
  `rakusheet.raku`; rebuild Raku.js for the current engine.
- [ ] **npm**: `rakulang` is not published. The binding is `bun:ffi`, so it
  runs on Bun only. A Node host needs the napi addon, which waits on A5
  ([EMBED-PLAN.md](EMBED-PLAN.md) E4). `package.json` still says 0.1.0.
- [ ] **crates.io**: `rakulang` is not published. `Cargo.toml` still says
  0.1.0.
- [ ] **Go**: the module path is the bare `rakulang`, so `go get` cannot
  fetch it. It needs a repository-qualified path or its own repository.
- [ ] **Versioning**: one rule for binding versions. Python follows the
  engine (5.2.1); JavaScript and Rust are at 0.1.0.
- [ ] **Announcement**: remove the work-in-progress notice once the packages
  above are installable by name.
- [ ] **More than one interpreter per process**: E5 in
  [EMBED-PLAN.md](EMBED-PLAN.md); move the remaining globals into
  `Interpreter`.
- [ ] **Static extension registry**: A4 in [ABI-PLAN.md](ABI-PLAN.md),
  `rakupp_ext_register(&mod)` for WASM and `--exe`. A5, compiled bindings,
  "only where measured".
- [ ] **Python per-leaf slowdown**: lazy `Match` access from Python
  (`m["line"][i]["ip"].str()`, one `rk_call` per leaf) is a quarter to a half
  slower with the 5.2.0 library than with 4.0.1, while the engine-side walk
  did not get slower. [PYTHON-BINDING-PERF-2026-10-03.md](../findings/PYTHON-BINDING-PERF-2026-10-03.md).
  Next: profile a tight `rk_call` loop on both libraries, then bisect
  v4.0.1..v5.2.0 with shared-library builds.
- [ ] **Python guide, `*!`**: the factorial example in
  [bindings/python/README.md](../../../bindings/python/README.md) maps with
  `{ $_! }` because the published 5.2.1 wheel does not curry `*!`. Switch it
  to `.map(*!)` once a release carries 14e72451, and re-run the README's
  programs against that wheel.
- [ ] **Python modules and objects**: `raku.use("Geo")`, `raku.main`,
  `rakulang.Object` and keyword arguments to `call` (2026-10-05,
  `rakulang/object_shim.raku`, `bindings/python/tests/`). The shim works
  around Raku++ gaps that Rakudo 2026.09 does not have; each wants an engine
  fix, after which the workaround can go. Fixed in the engine by 2026-10-09,
  so their workarounds can go once a wheel carries the fix (the comment above
  `rk-py-look` still gives the first as its reason): `my \x = Nil` bound `Any`,
  `try` around a throw returned `Any`, `(0..*).map(...)` was a `List`, and
  `Pair.^mro` lacked Pair. Still open: `MY::` after `use` lists the
  imported subs but not the imported classes, constants and enum values;
  `Geo.WHO` lacks the stub `Shape` of a nested `class Geo::Shape::Circle`;
  `Pair`, `Range`, `Complex` report no `.^attributes`, and `Exception`
  reports a `$!message` accessor; `Date.^can('new')`
  and the core types' `.^methods(:local)` are empty; `use M:ver<1.2+>` does
  not match a `use lib` module declared `:ver<1.2.3>`; JSON::Fast's
  `&to-json.signature` is `:()`. Also: the README's `raku.call("area", 3)`
  error text is 5.2.1's; the current engine says Rakudo's runtime message
  ("Too few positionals passed..."), so re-run the README's programs against
  the next wheel.
- [ ] **Grammars as a service**: G3 host callbacks and G4 the native Match
  walker, only if a workload needs them. [GRAMMAR-PLAN.md](GRAMMAR-PLAN.md).
- [ ] **Hosts**:
  - Jupyter: its gaps wait on an `rk_interrupt` ABI.
  - MCP: embedded sessions never join start-workers.
  - LSP: diagnostics (on the token: LintFinding::subject, ParseError::got),
    hover, completion and go-to-definition (src/LspIndex.cpp, token-based,
    REFERENCE.md baked via referenceGuide()) since 2026-10-03,
    t/regression/lsp-hover-completion.raku. Open: find references, rename,
    signature help, documentSymbol (imenu/outline; lsp-mode's by-name xref
    needs it), cross-file names from `use`d modules, type-aware method
    completion; VS Code never re-checked after hover/completion landed; the
    extension is unpublished. `vsce package` fails from a copy of the folder
    outside the repo ("entrypoint(s) missing"); it works in place. Emacs:
    eglot and lsp-mode verified in Emacs 31.1 (batch runs against raku-mode
    from MELPA), guide docs/guide/integrations/EMACS.md; lsp-mode completion
    checked at the candidate level only (batch completion-at-point does not
    insert under lsp-mode). Fixed on the way: `diagnosticProvider: false`
    made lsp-mode pull diagnostics, and the REPL under TERM=dumb
    (`M-x run-raku`) is plain (t/regression/repl-dumb-terminal.raku).
- [ ] **libffi**: by-value structs (§6) and linking libffi rather than loading
  it (§10, which matters for Windows). [LIBFFI-PLAN.md](LIBFFI-PLAN.md).

## 5. Ecosystem

- [ ] **321 cannot run on main** (adopters gate 6d, `tools/adopters.list`): its
  code relies on four things Raku++ v4.0.1 accepted and Rakudo refuses, so
  12 of 13 files and the standalone build are listed as known failures and
  only `t/00-canonical` is guarded. Next: the author hears of it (the
  maintainer's call), then `--head` on their fix, move the pin, clear the line.
- [ ] **Distributions passing their own tests**: 1,019 of 2,547 at v5.0.0.
  Next: a fresh grouping by cause of the ~1,175 that fail their own tests.
  Known layers underneath:
  - Text::CSV `CSV(out => Supply)`.
  - Testo `&!group()`.
  - `CUR::FileSystem.resolve`.
  - A role value parameter inside a sub.
  - AttrX::Mooish.
  - Cro::HTTP and HTTP::UserAgent block about 40 in a cold store.
- [ ] **Slangs**: Slang::Kazu 8 of 25 (a positional-capture subrule loses
  `.made`); Text::CSV 31 of 33; tier 4 (Otherwise, Qwiratry) not started.
  [SLANG-PLAN.md](SLANG-PLAN.md).
- [ ] **Larger walls**: PDF 16 of 49 test files (encryption, `ByteString`,
  filters, tie); Red #77 (compile-time lexical declaration).
- [ ] **Missing modules, listed and installed on request**:
  [DEPS-PLAN.md](DEPS-PLAN.md), 2026-10-14. A missing module's error lists
  every module the program lacks and offers `rakupp install app.raku` and
  `rakupp --auto app.raku`; only `--auto` fetches (opt-in since the
  revision). `t/deps/run.raku` 52/52 (in CI), Roast list unchanged, the typo
  guard refuses 4 of 9,698 index names. The archives a plan needs are
  fetched side by side by one `curl --parallel` (D6, 2026-10-14; any error
  falls back to the per-dist fetch). Next: a failed fetch should remove the
  dependencies it installed, `RkConfig.deps` (D5), and Cro::HTTP's cold
  install (14.7 s fetched in turn) re-timed.
- [ ] **Not started**: [LOCKFILE-PLAN.md](LOCKFILE-PLAN.md) (`rakupp.lock`,
  `--frozen`) and [UUID-PLAN.md](UUID-PLAN.md) (draft; the distribution's name
  is open).

## 6. Tooling, release and CI

- [ ] **`--sandbox`**: [SANDBOX-PLAN.md](SANDBOX-PLAN.md), 2026-10-08. S0–S2
  committed (on top of `425d70df`, not pushed): the interpreter's checks, `RkConfig.sandbox`,
  `--mcp --sandbox`, and the OS layer (Seatbelt; Landlock + seccomp), fail
  closed with `--sandbox=language` as the explicit opt-down. Gate 341/341 on
  macOS and Linux. Owed: a full `t/run.raku`, `perf-guard --check` on a quiet
  machine. Next: S3 (limits); OpenBSD pledge + unveil when there is a machine.
- [ ] **IDE: language server, then debugger**: [IDE-PLAN.md](IDE-PLAN.md),
  2026-10-07, nothing built yet. Next: L0 (a Raku LSP client for the tests,
  one `try` around every handler, JsonLite, one lex per document version,
  `tools/lsp-bench.raku`). The debugger (Part D) must not slow a normal run:
  D0 reuses the existing `--trace` branch and is checked by disassembly as
  well as timing.
- [ ] **riscv64 release archive**: `linux-riscv64` in release.yml, 2026-10-08.
  Cross-compiled on an x86-64 runner against an Ubuntu 22.04 sysroot (glibc
  2.35), gated on Ubuntu 22.04 riscv64 under QEMU. The same recipe passed
  every gate on the Mac (Homebrew Clang, Docker QEMU); the job has never run
  in CI. Next: push and read its first run. On the release after that, copy
  tools/install.sh to raku.online (it now maps riscv64 to the archive) and
  add the archive to raku.online/install/.
- [ ] **v5.2.1 after the tag**: tagged and on PyPI 2026-10-03. Left: bump
  the Homebrew tap, and republish raku.online (the release's dashboard point
  from the artifact, the front page's version, the Roast map). RELEASING.md
  steps 5 and 6.
- [ ] **Roast on slow hardware**: on a SpacemiT K1 board (Banana Pi F3,
  riscv64) interpreter work runs 10–25× slower than on an M-series Mac (glibc's
  `memcmp` and 64-bit `%` are ~20× slower there, plain integer code 3×), so 21
  files outrun the 10 s per-file limit. With `ROAST_TIMEOUT=60` the sweep
  passes 1,424 of the 1,425 files (2026-10-08, about 200 s). The other is the
  shaped-native-array file (section 2): it grows to the board's whole 3.7 GB
  before the kernel kills it, and the swapping it causes slows every file
  beside it (a sweep took 205–287 s depending on how much ran alongside).
  cas-int.t is the slowest file there, 25 s alone (1.1 s on the Mac). Tests
  with their own wall-clock watchdog (`sleep 1`, a 5 s `Supply.interval`)
  can still fail under load. Next: decide whether the harness scales its
  limit by a measured speed factor, so the default run passes there too.
- [ ] **Docker**: the ghcr.io package is still private; it is the
  maintainer's step to make it public.
- [ ] **Gate 6b is red on three koans by design**: `next OUTER` under a label
  named OUTER now dies as on Rakudo, so Raku Koans'
  control-flow/loop-control-and-phasers fails `tools/koans-gate.raku` until
  the course renames that label. And a call that can never bind is now a
  compile-time error as on Rakudo, so 08-subroutines/02-signature
  (`dies-ok { say-hello(42) }`) and 05-slurpy-and-named-parameters
  (`dies-ok { needs() }`) no longer compile; their solutions need an argument
  the compiler cannot type (`my $n = 42; say-hello($n)`) or an EVAL. How to
  tell the course is the maintainer's call. Found 2026-10-07.
- [ ] **`.deb`**: `tools/make-deb.raku` plus release.yml's "Debian package
  (Linux)" step, tested in Ubuntu 24.04 against the 5.2.0 layout, not yet run
  in CI. The first release that carries it makes the README and INSTALL.md
  download lines true; the CHANGELOG entry goes with that release. An APT
  repository is the next step, if asked for.
- [ ] **setup-rakupp Action**: written, not committed; its repository does not
  exist yet.
- [ ] **Extra suites as gates**: Rakudo's own t/ and mutsu's t/, run by
  `tools/run-roast.raku --suite=rakudo|mutsu`; pass lists for both engines in
  [docs/status/suite-lists/](../../status/suite-lists/README.md) (2026-10-04).
  The work: 2,031 mutsu files and 170 Rakudo files that Rakudo passes and
  Raku++ does not (`comm -23 *.rakudo.list *.rakupp.list`).
  - Parked from the audited LANG work: typed shaped arrays report unassigned
    cells as existing (`my Int @i[3]; @i[1]:exists` — a typed shaped array is
    filled with its type object, which `:exists` cannot tell from a stored
    one); a closure sequence's seed is stored before it is pulled, so
    `my @f = 1, {…} ... *; @f.join` is `1...` (Rakudo `...`; the Seq form is
    fixed). (`IO::Path.add` flattening and an enum member named `q` answer as
    Rakudo's, re-checked 2026-10-11.)
  - Rakudo t/ classified 2026-10-05 (145 LANG, 38 RakuAST, 5 6.e; 41 excluded).
  - Parked from the Rakudo t/ LANG work: an `anon subset` shares its name's
    entry in the one global subset table, so a later `subset Foo of Str`
    replaces what `$anon` checks (`anon enum` and the redeclaration error are
    fixed); takes from the thunk of a sunk `xx *` inside a
    gather; a Label's `.gist` with its source excerpt; `Format.handle-iterator`;
    an operator declared LATER in a block is not seen by uses before it
    (`$i++` then `sub postfix:<++>`); a generic `T $x is copy` does not check
    assignments against the instantiated T; a native passed to a raw (`\v`)
    parameter or put in a Pair is not snapshotted; sigilless names bound
    through a list (`my (\a, \b) := ($x, $y)`) are not assignable through
    (`a = 10` dies; `my \c := $u` works), siglist-decl-rvalue.t. (A nested
    `constant T` and `X::Comp` on a compile-time error answer as Rakudo's,
    re-checked 2026-10-11; `.does` against a user role that shadows a core
    one was fixed that day.)
  - `t/02-rakudo/attribute-typeobject-default.t` SEGFAULTS (rc 139) in its last
    test, a lock-free `cas` queue fed by 4 `start`s — every run, on the 5.2.1
    build as well, so not a regression.
  - Known open: `our native size_t is Int is ctype<…> is repr<P6int> { }`
    (native type declarations) does not parse; the 12 wrapped "You have to
    pass an explicitly typed …" binding hints are not produced.
  - Thread races these suites show (flaky, not regressions): concurrent
    `@a[$i] = …` from 50 `start`s loses writes; a module sub's `state $n++`
    across 100 `start`s loses one; `once` in a sub fires twice across
    threads; sibling `.then` callbacks run out of registration order; a
    `Proc::Async`'s `.ready` can wake before `.started` reads True; nested
    subscript stores from several threads lose writes
    (concurrent-nested-subscript-store.t, on the 5.2.1 build too).
- [ ] **run-roast leaves grandchildren behind**: a timed-out file's own child
  processes outlive it. mutsu's suite starts `rakudo -e` one-liners (one a
  left-recursive grammar that never ends), and two kept a core each busy for
  25 minutes after the run. Kill the file's process group, not just the file.
- [ ] **User candidates beside a core ROUTINE (decision needed)**:
  `multi sub abs(Int $x) { … }; abs(-3)` is X::Multi::Ambiguous in Rakudo
  2026.09 — the user candidate ties the core `(Int:D \a)` (the message also
  lists `(Real:D $a)` and `(\a)`); here the user candidate silently answers.
  Operators already do this right: userInfixOverCore models the core
  candidate an operator meets and reports the tie. The dilemma:
  - *Match Rakudo*: model CORE's candidate signatures for named routines,
    routine by routine (hundreds of them), and reproduce the tie. Faithful,
    but a large table to keep in step with CORE, and its only effect is to
    turn programs that run today into errors.
  - *Keep the leniency*: a program written for Rakudo never declares such a
    candidate (it would die there), so nothing portable depends on the error;
    the cost is a divergence on code that only runs here.
  A middle path is a small table for the numeric builtins most often
  extended (`abs`, `sqrt`, `floor`, `ceiling`, `round`, `sign`).
- [ ] **Two regex divergences from Rakudo 2026.09** (found writing
  t/regression/regex-search-prefilters.raku; both older than the prefilters):
  `"STRASSE" ~~ / :i 'straße' /` matches `STRASSE` here and `STRASS` there,
  and `"a\r\nb" ~~ / \r\n /` matches here but not there (the CRLF is one
  grapheme, which a two-atom pattern cannot match). The second is
  deliberate: 892e6c61 fused `\r\n` into the CR LF grapheme to pass Rakudo's
  own t/02-rakudo/regex-crlf-grapheme.t and mutsu's
  regex-crlf-literal-escape-pair.t, which the 2026.08 and 2026.09 releases fail. Decide
  which Rakudo to follow before changing it. Also `"x\r\n" ~~ / x $$ /` is
  False here, True on Rakudo 2026.08.
- [ ] **An Array in a sequence's seed list**: the endpoint check reads it.
  With `@f = lazy 0, 1, 1`, `(@f, 2, 3 ... 8)` leaves `@f` reified, so it
  shows `[0 1 1]` where Rakudo shows `[...]`; the cause is that `@f ~~ 8`
  reads a lazy `@f` whole (Rakudo: False, `@f` still lazy). Worse, `(@f, 2 ...
  2)` is `()` whether `@f` is lazy or not (Rakudo `($[...], 2)` and `($[0, 1,
  1], 2)`), and `(@f, 2 ... 3)` stops after the Array (Rakudo `([...], 2,
  3)`). Rakudo meets the
  endpoint with an Array seed by its element count: `([0, 1], 2 ... 2)` is
  `($[0, 1],)`. In `seqOp` (src/Interpreter.cpp). Found 2026-10-09.
- [ ] **Roast harness**: run Rakudo at the 120 s budget; COUNTING.md's figures
  are from the 60 s budget. The Lock around spawns in tools/run-roast.raku can
  go now that the engine's pipes are close-on-exec before the fork
  (t/race/spawn-pipe-cloexec.raku).
- [ ] **CLI**: the sandbox, `--profile=heap`, `--race`, inline script
  dependencies, `--explain`, `--lint --fix`.
  [CLI-BORROW-PLAN.md](CLI-BORROW-PLAN.md).
- [ ] **Weekly freshness tooling**: `tools/eco-fresh.raku` and its history
  file. [FRESHNESS-PLAN.md](FRESHNESS-PLAN.md).

## 7. Docs and sites

- [ ] **Plan headers out of date**: PARALLEL-PLAN, NATIVE-MATH-PLAN,
  NATIVE-MODULES-PLAN, RAKU-EYE-PLAN and THOUSAND-PLAN say less is done than
  is. VERSIONS.md has no v5.1/v5.2 entries, and its v4 header still says
  "forming".
- [ ] **Abandoned-looking plan**: [GRAMMAR-SPEED-PLAN.md](GRAMMAR-SPEED-PLAN.md)
  (last touched 2026-08-13). Close it, or fold what is left into
  INTERP-SPEED.
- [ ] **Rakugrid**: the series is not wired into the dashboard.
- [ ] **Behind the Docs**: its corrections are not yet applied back to the
  semantics sheets.

## Decisions waiting on the maintainer

- The overflow.t trade-off (partial-file sweep).
- The precomp key (V6 P4).
- Whether `--cnp` becomes the default.
- Default `--exe` without `-O`: unconditional fast operators, or `-O` by
  default (WINDFALL-PLAN.md).
- The UUID distribution's name.
- The rakuglaze repository's license.
- The binding version scheme (section 4).
- Telling the 321 author that their code breaks on Raku++ after v4.0.1
  (section 5).
- Whether whole files get EVAL's strict statement separation: Rakudo
  refuses `sub k { 1 } sub v { 2 }` or `if 1 { } say 2` on one line
  ("Strange text after block"); Raku++ refuses it only in an EVAL
  (`enforceStmtSep`), so files that rely on the leniency would stop compiling.
- `temp` against LEAVE: since 2026-10-11 a `temp` is restored BEFORE the
  block's LEAVE phasers run (runLeavePhasers' drainTempRestores), following
  Rakudo 2026.09; Rakudo 2026.08 and Raku++ before then restored it after, so
  a LEAVE saw the temporary value. Roast tests neither order. The old order is
  arguably the more useful one (a LEAVE can log or clean up the temporary
  state). Keep 2026.09's order, or go back? (section 2, "Phaser order")
