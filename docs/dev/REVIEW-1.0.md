# Pre-1.0 independent review — 2026-07-12

## MAJOR finding (2026-07-13): subtest Pair-form was a silent no-op

`subtest 'desc' => { … }` (the standard idiom) passed the block as a Pair
*value* that the `subtest` builtin never extracted — so the block **never
ran**, the subtest emitted nothing, and it auto-passed as an empty subtest.
Effect: every roast file using `subtest '…' => {…}` had those subtests as
no-ops that auto-passed. The 418-file baseline was **inflated** — ~36 files
were "passing" only because entire subtest bodies were dead. Fixing the Pair
form makes them run, which (a) fixed the then.t deadlock and io-cathandle
crash once their subtests actually executed, and (b) exposes a large surface
of pre-existing engine bugs living inside those subtests (`.isNaN` not
implemented on Rat, sprintf 6.e space-flag `% b`→`0`, etc.).

### Codegen silent-wrongs (DONE — 2026-07-13)

- **Mangling injectivity** (`38fb8a1`): mangleVar/mangleSub/methodFn are now
  injective (sigil-tagged prefix + `_HH` hex escaping). `$a-b`/`$a_b` no longer
  collide; `$x`/`@x`/`%x` coexist; operator-named subs compile.
- **`[=]` closure capture** (`7df69b1`): a closure that assigns to a captured
  local now bails to the interpreter bundle (correct output) instead of a
  miscompile/silent-snapshot. Only true assignment to a captured scalar local
  was broken; top-level globals and `.push` on captured arrays already worked.
  Remaining known limitation: a closure that READS a captured local which is
  mutated later in the enclosing scope still snapshots (narrow; not detected).

### Pair-form campaign (DEFERRED, tracked — updated 2026-07-13)

Status: the Pair-form fix stays **held out of the tree** so the gate does not
regress. Applying it alone measures **381** full-pass (down from 419) — **38
files** carry pre-existing bugs inside subtests that only run once the Pair
form works. The plan is to fix those bugs *first*, banked as gate-safe commits
(dormant until the Pair form lands), then land the Pair form + honest doc
update in one batch once the drop is small.

The one-line change (in `subtest`, extract the Code from a `desc => {…}` Pair):
```cpp
else if (v.t == VT::Pair) { desc = v.s; if (v.pairVal && v.pairVal->t == VT::Code) code = *v.pairVal; }
```

**Recovered so far (7 files, fixes committed dormant):**
- `sprintf-{e,f,o,s,x}` — version-aware `#`/`-0`/`0`-fill (6.c/6.d keep the
  historical "bogus" forms, 6.e the modern ones; both asserted by roast).
  Commit `c5fc267`.
- `S03-smartmatch/any-num` — `'NaN' ~~ NaN` is True; `&minmax` registered.
- `S24-testing/10-is-approx` — named `:rel-tol`/`:abs-tol`.
Also partially fixed (not yet fully green): `fails-like` (live Failure now
recognized; exception-type/message matching still incomplete), `ords` (values
now NFC-composed; `.ords.iterator` protocol still failing), `.does(Callable)`
on Code, smartmatch on type objects (blocked — `applyArith` is a free function
with no class registry).

**31 files still down**, by category:
- Rat/Num 0-denominator (Inf/NaN) semantics: `rat`, `stress`, `complex` —
  arithmetic on `<1/0>`/`<0/0>`, `Int()` coercion must throw
  X::Numeric::DivideByZero, `===`/`==` on 0-den Rats, `.raku.EVAL` round-trip.
- roles/class 6e: `mro-6e`, `roles-6e` (BUILD order), `submethods-6e`,
  `rw`(S12/S14), `walk` (`.^walk`), `attributes`, `unpackability`.
- IO: `chdir`, `seek`, `io-spec-unix` (IO::Spec path split), `print`, `prompt`.
- Test/misc: `is_deeply` (needs a real Junction type — architectural),
  `fails-like`, `any-callable`, `any-type`, `hyperwhatever`, `constant-6.d`,
  `categorize-list`, `toggle`, `subst` (S///, :Nth:g, list-$/),
  `cur-current-distribution`, `numbered-alias`, `lock`, `supplier-preserving`,
  `misc-6.c`.
Full current list: `comm -23` the 419 pass-list against a Pair-form gate.


## Fix progress

- **Wave 1 (batch B1–B11): DONE** — committed `1c317ab`. One BodyScope RAII +
  hoist-at-block-entry + RedoEx catches + positional dispatch guards +
  assertion adverb scoping. Gate clean, 23 examples byte-identical, S05 up.
- **Wave 2a (recursion zombie): DONE** — committed `ebe460b`. Stack-headroom
  guard replaces the fixed 60k cap. The kill-proof SIGBUS mechanism is closed.
- **Wave 3 (silent wrong answers): DONE** — committed `fd630a3`. coerceArray
  clone (interp/native array-assignment now agree with Rakudo), toLL
  saturation, LLONG_MIN%-1 guard, Value::rat zero-denominator via ratZ, fatRat
  preservation, valueCmp exact (int64 fast path + BigInt cross-multiply).
  Gate clean, benches at baseline (a first cut regressed sortnums via
  applyArith-per-compare; the int64 fast path fixed it).
  Still open from wave 3's review section: codegen mangling injectivity and
  `[=]` closure capture (both silent-wrong in the NATIVE backend) — these are
  codegen, grouped with the wave-5 codegen items below.
- **Wave 4 (exception safety): DONE** — committed `11cb561` (with F1–F4).
  callCallableRaw ScopeGuard (ENTER inside try + CATCH body wrapped), supply
  try/catch pop, path-based temp restore, LEAVE-vs-cooperative-flags.
- **Wave 5 concurrency F1–F4: DONE** — committed `11cb561`. awaitPromise/
  runReactLoop unlock-before-relock (ABBA), drainWorkers lock, fork hygiene
  (argv-before-fork, FD_CLOEXEC, process-group kill, EINTR waitpid).
- **Wave 5 regex/TAP/sprintf/IO: gating** (uncommitted). Regex step-cap
  (8M budget, shared across start positions — the /[a*]* b/ hang is gone) +
  capFrom/firstCode rollback across `|`/`||` (fixed `<(` .from) + StepLimit
  caught at entry. TAP: subtest counter advances (skip-rest correct), depth
  indent, inner-plan indent, `subtest "d" => {…}` Pair form. `is()` compares
  stringified (Rakudo), not numeric. sprintf width/precision capped at 10M
  (no int-overflow UB / GB pads). FileHandle write handles flush at program
  exit if `.close` is forgotten; :w creates the file at open.
  REMAINING wave-5 (more invasive, documented): UTF-8 byteset classes split
  codepoints on negated/`\N` classes; codegen mangling injectivity ($a-b/$a_b
  collide) and `[=]` closure capture cells (native silent-wrongs); Env cycle
  collector (needs design). Regex `$`-before-newline and lookbehind O(n²)
  logged as known limitations.
- **Wave 2b (`%*ENV`-in-`start` deadlock): DEFERRED to a native debug session.**
  UPDATE: root cause FOUND via sample — worker holds the GIL, then a safe
  point or the dynamic-var path re-acquires a mutex it already holds
  (`__ulock_wait` under evalIndex). Still needs the exact lock pinned on
  native arm64 (Rosetta blocks lldb pause).
  Reframed: it is a *killable* deadlock (kill -9 works), NOT a kill-proof
  zombie — that property belonged only to the recursion SIGBUS. `sample`
  confirms the worker holds the GIL and self-blocks in `__ulock_wait` inside
  `evalIndex` while resolving `%*ENV`, main waiting in `yieldToWorker`. The
  exact mutex needs an interactive debugger; `lldb` cannot pause the process
  under Rosetta ("could not pause execution"). NEXT: attach `lldb` to a native
  arm64 build (build-arm64, no Rosetta) or a Linux box, `bt all`, identify the
  lock the worker re-acquires while holding the GIL, fix the lock order. Do
  NOT ship a guessed GIL-handoff change.


A full review round before tagging v1.0: eight finder angles over the
uncommitted native-codegen batch, plus six independent module reviews over the
committed codebase (interpreter core, concurrency, value model, regex engines,
codegen/--exe pipeline, front-end/IO). Every finding marked *verified* was
reproduced by a reviewer against a freshly built binary, most with a one-line
program. File:line references are to the working tree as of this date.

## Verdict in one paragraph

The architecture holds up: the tree-walker's mainline discipline, the BigInt
core, the `__int128` Rat fast path, the CPS regex design, and the parser's
malformed-input behavior all passed adversarial review. What is *not* yet
1.0-grade clusters in four places: (1) the uncommitted batch has ten verified
bugs, all in the new lexical-sub / dispatch / loop-control machinery — fix
before committing; (2) two independent "kill-proof zombie" mechanisms — the
`%*ENV`-in-`start` GIL deadlock and the recursion guard sitting 3× beyond the
physical stack — both reachable from one-liners; (3) a set of silent
wrong-answer bugs (interpreter array-assignment aliasing now *diverging from
the native backend*, codegen closure capture-by-value, mangling collisions,
double-based sort comparisons); (4) exception-safety in the newer interpreter
subsystems (supply, temp, ENTER/CATCH) where hand-rolled restore sequences
skip cleanup on throw.

## Part 1 — the uncommitted batch (fix before commit)

All verified. Ranked.

| # | Where | Bug | Repro sketch |
|---|---|---|---|
| B1 | Codegen.cpp:456, 1047, 1597 | Expression `redo` (and next/last escaping all native loops) throws a signal nothing catches → SIGABRT | `loop { $x++; $x<3 and redo; last }` aborts natively |
| B2 | Codegen.cpp:227 | `subClosure` captures locals by const value: stale reads (silent wrong answer) and uncompilable C++ on captured-var assignment, with no bundle fallback | `{ my $x=1; sub f { say $x }; $x=2; f() }` → 1 vs 2 |
| B3 | Codegen.cpp:1461, 1359 | Multi-dispatch type guards index raw `__a` while arity uses `rtPosCount` — leading named arg kills dispatch | `f(:flag, 5)` → "No matching multi candidate" |
| B4 | Codegen.cpp:231 | `emitBlockClosure` doesn't reset `loopDepth_` → bare `break;` inside a lambda: C++ compile error | `for … { .map({ last if …; $_ }) }` |
| B5 | Codegen.cpp:739 | `envSubs` never scoped — a nested sub name hijacks *all* later calls program-wide, including builtins | `sub outer { sub chars($x) {…} … }; say chars("hello")` dies |
| B6 | Codegen.cpp:737 | `envSubs` registered *after* body emission — recursive lexical sub binds to a same-named builtin: silent wrong answer | block-local `sum` prints 7 vs 10 |
| B7 | Codegen.cpp:736 | Nested sub shadowing a top-level name silently dropped | inner `f` calls resolve to top-level `f` |
| B8 | Codegen.cpp:1401 | `hoisted` never reset per body — second sub with expression-position `my` loses its declaration: clang error | two subs with `(my $x = …)` |
| B9 | Codegen.cpp:738 | Block-local subs not hoisted to block entry — forward calls die at *runtime* (previously rejected at build time) | `{ say g(3); sub g($n) {…} }` |
| B10 | Codegen.cpp:404 | Bareword multis become type objects; `isKnownTypeName` pre-empts user subs named like types | `multi f() {42}; my $v = f` → `(f)` |
| B11 | Regex.cpp:367 | The new scoped `:i` misses lookahead/lookbehind inners — `:i` leaks out of `<?before …>` | `/ <?before :i a> A /` matches lowercase |

Root-cause fixes rather than 11 patches: (a) one RAII `BodyScope` guard owning
`boundSpecials`/`hoisted`/`loopDepth_` (+`envSubs` scoping) applied at every
body-emission site — fixes B4, B8, and the leak class behind B5; (b) register
lexical subs into the env *at block entry* (mirroring the interpreter's
`hoistSubs`) and *before* body emission — fixes B6, B7, B9; (c) catch `RedoEx`
in `loopBody` + mirror the interpreter's three top-level control catches in
generated `main()` — fixes B1; (d) capture closures with shared cells (or at
minimum `[=]() mutable` + documented copy semantics) — B2; (e) hoist
`rtPosCount` once and index positionals via `rtPos` in guards — B3; (f) the
same save/restore for assertion inners in the regex parser — B11.

Also from the cleanup angles: the new per-loop try/catch wraps the loopsum
benchmark kernel — bench before landing or emit the wrapper only when the body
can actually throw a control signal; `rtPosCount` double-scan per guard;
`Regex::icase_` is now write-only dead state; the interp's NameTerm tail /
UseStmt exec should call `rtNameTerm`/`rtUse` instead of duplicating them
(that duplication pattern is what produced B10).

## Part 2 — pre-1.0 blockers in the committed codebase

### The zombie mechanisms (both verified, both explain the campaign's UE processes)
- **`%*ENV` (any dynamic touched via that path) inside `start {}` deadlocks
  the GIL handoff** — main parks in `yieldToWorker`, worker blocks mid-lookup;
  the wedged process survives `kill -9`. Interpreter.cpp:903/6532.
  One-liner: `my $p = start { %*ENV<PATH>.chars }; say await $p;`
- **Recursion guard (60,000) is ~3× past physical stack exhaustion** (~13 KB
  of C++ stack per interpreter frame ≈ 19k frames on the 256 MiB stack) — deep
  recursion SIGBUSes under Rosetta into the same kill-proof state instead of
  throwing X::Recursion. Interpreter.cpp:2551. Fix: cap ≈16k, or size from
  the actual stack.

### Concurrency (module verdict: F1–F4 are blockers; parallel mode stays experimental)
- F1 ABBA deadlock: `awaitPromise`/`runReactLoop` reacquire the GIL while
  holding `ps->m`; keep/break and second awaiters lock in the opposite order.
  One-line fix (unlock before relock), two sites. Interpreter.cpp:1039, 1060.
- F2 `drainWorkers` (GIL path) iterates/clears `workers_` unlocked during the
  2 s grace while a live worker can push — UAF at teardown. Interpreter.cpp:1087.
- F3 fork-safety: child mallocs between `fork` and `execvp` in a threaded
  process — child can deadlock pre-exec; `qx` (no timeout) then hangs forever.
  Builtins.cpp:147, 263.
- F4 pipes lack `FD_CLOEXEC` (concurrent spawns inherit each other's
  write-ends → EOF never arrives), timeout kill misses grandchildren
  (no process group). Builtins.cpp:142-193.

### Memory unsafety / exception-safety (interpreter core)
- `supply { … die }` leaves a dangling `ValueList*` on `supplyStack` (both
  gather sites do this correctly; supply missed the try/catch). Builtins.cpp:4960.
- `temp @a[$i]` stores a raw `Value*`; array growth invalidates it — restore
  writes freed memory (verified silent wrong value). Interpreter.cpp:5891.
- ENTER phaser that dies skips scope/dynStack restore — caller's CATCH runs in
  the callee's scope (verified: reads callee's parameters); rethrow from a
  routine-level CATCH does the same. Interpreter.cpp:3134-3195. One RAII
  ScopeGuard for cur/curStateEnv/dynStack fixes the family.
- Every activation of a sub with a named inner sub leaks its Env
  (shared_ptr cycle; measured 359 MB / 300k calls). Needs a weak backref or
  frame-exit cycle break — the one fix here needing design thought.
- Cooperative return/next/last truncates multi-statement LEAVE phasers to
  their first statement (flags still set during runLeavePhasers). Interpreter.cpp:1622.
- Expression-position `return` keeps evaluating the rest of the statement
  (side effects Rakudo never runs). Interpreter.cpp:5557.

### Silent wrong answers (value model + codegen)
- **Interpreter array assignment aliases** (`my @b = @a`; also `is copy`
  params) while native codegen copies — the backends diverge on core
  semantics. TRIAGE #1, now upgraded: fix `coerceArray` to clone like
  `rtArrayVal`. Interpreter.cpp:707.
- `LLONG_MIN % -1` hangs (UB in the small-int `%` fast path). Interpreter.cpp:4042.
- `BigInt::toLL` silently overflows → `@a[2**64]` wraps to index 0 and
  returns an element. BigInt.cpp:169, Value.cpp:66. Saturate like toNum.
- `Value::rat()` rewrites zero denominators to 1 — `-(1/0)`, `(1/0).abs`, and
  codegen Rat literals corrupt. Value.h:96.
- sort/min/max compare exact numerics through doubles — `(2**53, 2**53+1).max`
  picks the wrong element. Value.cpp:327. Give valueCmp an exact Int/Rat branch.
- Codegen mangling is many-to-one: `$a-b`/`$a_b` → same C++ symbol (verified
  silent shadowing), `$x`/`@x` collide (clang redefinition). Make the mangle
  injective (keep sigil, hex-encode non-identifier bytes). Codegen.cpp:60.
- Codegen `[=]` closure capture: stale snapshots + const-assign compile
  failures (same family as B2 but pre-existing for blocks). Model captured
  mutables as shared cells.
- clang failure in `--exe` has no fallback to the interpreter bundle — emit
  bugs become raw compiler errors for users. main.cpp:211.

### Regex engines
- **No step/depth cap in the backtracking matcher** — measured 56 s on a
  28-char input, C++ stack overflow (crash) on megabyte inputs with
  alternation-under-quantifier. A global step counter + depth cap closes it.
- `<(` (CapStart) not rolled back on backtracking — corrupts `.from`/match
  text through `|`, `||`, quantifiers (verified).
- Byteset classes consume single *bytes*: negated classes and `\N`/`\h` split
  UTF-8 codepoints — corrupt captures on any non-ASCII text (verified);
  cpRange/uprop branches can also step past the string end on truncated UTF-8.
- Lazy `mutable` byteset caches are a data race the day a pattern→Regex cache
  is added (today each match compiles a fresh Regex, so latent). Build the
  byteset eagerly at parse time.
- Deviations: `$` matches before a trailing newline (Rakudo: Nil);
  unterminated groups match silently (`ok_` stays true).

### TAP/scoring integrity (front-end/IO)
- Subtest counter never advances (`emitTest` early-returns before
  `testNum_++`) — `skip-rest` in a subtest over-emits; plan-vs-ran inside
  subtests unverifiable. Interpreter.cpp:1503.
- Inner subtest `plan` prints an unindented top-level `1..N` — a real TAP
  consumer rejects our own output; nested subtests flatten to one indent
  level. Builtins.cpp:4169. (Interp-side sibling of the fixed codegen doubling.)
- `is()` compares numerically where Rakudo string-compares —
  `is 1/3, 0.333333` fails here, passes in Rakudo: direct roast-scoring skew
  in both directions. Builtins.cpp:4184.
- `sprintf` width/precision unbounded: signed-overflow UB + multi-GB
  allocations from a format string. Builtins.cpp:748.
- FileHandle writes buffer until `.close` and every write-error path is
  swallowed — `$fh.say(…)` without close writes *nothing*, exit 0.
  Builtins.cpp:3042. (The stray `buffer` / `mode\tr` / `path\tt` files in the
  repo root are consistent with this surface misfiring.)
- Radix literals overflow silently: `:16<FFFFFFFFFFFFFFFFFF>` → -1 (decimal
  literals already have the BigInt fallback; radix path needs it). Parser.cpp:949.
- Lexer UTF-8 advance can read ≤2 bytes past the buffer on a truncated
  multibyte tail; unterminated strings lex silently. Lexer.cpp:507/234.

## What held up well (reviewed and cleared)

Thread-local GIL park contexts and the park/notify protocol proper; worker
reaping; gensym uniqueness and the v_/u_/m_ namespaces; clang command-line
quoting (`shq` everywhere it matters); all 59 `cesc` call sites (including the
new NUL/length-aware form); `rtPos`/`rtNamed` named-arg-aware binding; the
scoped `:i` change everywhere except assertion inners; BigInt carry/divmod
and the `__int128` Rat fast path; parser/lexer robustness under ~35-case
fuzzing (zero crashes/hangs); gather's push/pop discipline, FrameGuard and
LoopGuard RAII; TAP plan/bail-out/TODO plumbing at top level.

## Suggested order

1. **Batch fixes B1–B11** (one BodyScope guard + hoist-at-entry + RedoEx
   catches + guard indexing + assertion save/restore), re-run examples +
   parity sweep + triple gate, commit the batch.
2. **Zombie killers**: recursion cap to fit the stack; the `%*ENV`-in-start
   deadlock. These two alone remove the machine-poisoning failure mode.
3. **Silent-wrong-answer wave**: coerceArray clone, toLL saturation,
   `LLONG_MIN % -1` guard, `Value::rat` zero-den, valueCmp exact branch,
   mangling injectivity, closure cells.
4. **Exception-safety wave**: ScopeGuard for ENTER/CATCH, supply try/catch,
   path-based temp restore, LEAVE-vs-cooperative-flags.
5. **Concurrency F1–F4** (small, mechanical), regex step cap + capFrom +
   UTF-8 bytesets, TAP integrity (subtest counters/indent, is() semantics),
   sprintf caps, FileHandle flush-on-write or destructor flush.
6. Defer, documented: Env cycle collector design, parallel-mode symbol
   freezing vs runtime `use`, `$`-before-newline, lookbehind O(n²).

Waves 1–2 are the gate for committing the current work; 3–5 are the honest
bar for calling a tag "1.0" — all are localized fixes, none violate the
no-architecture-changes constraint.
