# Changelog

Release notes for tagged releases. Numbers are measured, not projected;
methodology for all Roast figures is in [docs/COUNTING.md](docs/COUNTING.md).

## Unreleased

- **New `--lint` mode**: a static analyzer that parses a program and reports
  likely mistakes without running it — unused variables, unused lexical
  routines, redeclarations, unreachable code, self-assignment, constant
  `if`/`unless` conditions, and numeric comparison of a string literal (all
  warnings), plus unused parameters and redundant trailing `return` (notes).
  Exits 1 on any warning, so it drops into CI or a pre-commit hook. The rules
  are deliberately conservative — interpolation and regex pattern text count as
  uses, and `EVAL`/symbolic references stand the "unused" rules down — to keep
  false positives near zero on Raku's dynamic constructs. Rule reference in
  [docs/LINT.md](docs/LINT.md); one-rule-per-file demos in
  [examples/lint/](examples/lint/).

## v0.9.0 — 2026-07-19

Everything since v0.7.1 (2026-07-16), 147 commits. Every change is gated on the
full Roast suite with no fully-passing file regressions.

### Headline

- **Roast: 533 / 1,462 files fully pass** (was 501). Passing assertions grew from
  171,817 to **187,749** — **87.5%** of the 214,569 declared tests, and 96.9% of
  the tests that actually ran.
- **`--exe` native binaries now have interpreter-parity recursion depth on every
  platform**: the generated `main()` runs the whole program on the same 1 GiB
  big-stack thread the interpreter uses (macOS/Windows also carry link-time stack
  flags). Deep recursion that the interpreter handles no longer crashes a native
  build.
- **Windows `--exe` works out of the box** (GitHub issue #1 closed): the generated
  `main()` no longer collides with the CRT `__argv` macro, MSVC builds default to
  the static CRT so native links don't fail `LNK2038`, a compiler is found on
  `PATH`, and `vcvars` is bootstrapped when `cl` isn't in the shell.

### Language & runtime

- Reduction metaops thunk their operands: `[&&]`/`[||]`/`[//]`/`[andthen]`/… and
  their `[\op]` scans short-circuit without evaluating later operands.
- 6.e array/hash multislices with star/list adverbs (`@a[*;0;*]:delete`,
  `%h{*;"b";"c"}`), and `@a[*-1, *-2]` list-slices now resolve `*`/`*-1` against
  the length.
- Set/Bag/Mix family: `for`-loops over `.values`/`.kv`/`.pairs` of a
  `SetHash`/`BagHash`/`MixHash` alias the weights (a weight of 0, or negative for
  MixHash, removes the element); `.ACCEPTS`/`.STORE`/`.Capture`, the coercer
  calls, and `class MySet is Set` subclassing.
- `Pair.value` is a writable container; typed-container multi dispatch
  (`multi f(Int @a)`); a slurpy multi candidate is now correctly the least-narrow
  tiebreaker.
- Regex `m:nth(N)`/ordinal/`:nth(*)`/list-and-`:global` counted adverbs; `Buf`
  `subbuf` Callable/`*` forms, `Buf.new(Range)`, `.allocate` fills; compile-time
  "Useless use … in sink context" warnings on the mainline.
- Containers: `@a = …` / `%h = …` refill the existing container in place, so
  bindings, captures, and closures track the reassignment.
- Correctness fixes from the pre-release review: `.Int` on a string/match wider
  than int64 is now exact (was 0); a brace character in a string inside an
  embedded regex code block parses correctly.

### Native compile (`--exe`) & the browser

- Caught builtin errors answer `.message` inside a native `CATCH` (was a bare type
  payload); `exceptionFor` synthesizes real exception objects for `X::`-named
  payloads. Block-final `if`/`given` is a pointy block's value; `Less`/`Same`/
  `More` and `PromiseStatus` resolve to real enum values under native name-term
  lookup.
- New [docs/MEMORY.md](docs/MEMORY.md): reserved-vs-resident memory and the
  measured recursion depths per mode (interpreter / `--exe` / WebAssembly).
- New [docs/COMPILERS.md](docs/COMPILERS.md): which compiler and architecture to
  use — arm64 vs. x86_64/Rosetta on macOS, Clang vs. GCC (with a measured
  ~1.3–2× gap on this codebase), MSVC vs. MinGW on Windows — both for building
  Raku++ and for the compiler `--exe` invokes.
- New showcases on the WebAssembly playground: a JavaScript/TypeScript
  interpreter, a Scheme, and a Forth, each written in Raku.

### Concurrency

- `react`/`whenever` no longer hangs when an eager `start { $s.emit(…); $s.done }`
  runs before the react taps the supply — the Supplier records its done state so a
  late tap closes immediately.
- A `.then` registered on a `start`/Promise before its worker settles now fires
  (was silently dropped).
- `CurrentThreadScheduler.cue` rejects `:every`, as in Rakudo.

### Robustness (pre-release review)

An independent multi-reviewer pass over the sources fixed eight default-build
defects: the regex greedy quantifier no longer overflows the stack on long runs
(`/\d+/` over millions of chars), `substr-eq` with only an adverb no longer reads
out of bounds, plus the correctness and concurrency items listed above.

### Known limitations

- **`RAKUPP_PARALLEL=1` (the opt-in GIL-free mode) is experimental and not
  production-safe.** Under it, `Channel`, the shared Rat-literal cache, and
  worker-side `class`/`EVAL` are not fully synchronized and can race. The default
  cooperative-GIL build (what ships and what every example uses) is unaffected.
- **Native (`--exe`) recursion is uncatchable if it overflows**: a compiled
  program that recurses past its stack dies with a signal rather than a catchable
  `X::Recursion` (the interpreter throws). See docs/MEMORY.md.
- A native `given`/`when`/`CATCH` with a bare `my` declaration *between* clauses
  fails to compile (a `goto` past an initializer) instead of falling back to
  bundling; wrap the declaration in its own block.
- The parse-only entry points (`--cpp`/`--ast`) can overflow the stack on
  pathologically deep bracket nesting; ordinary execution is shielded.

## v0.7.1 — 2026-07-16

Everything since v0.5.1 (2026-07-13), ~100 commits. (A 0.7.0 tag was cut but
never published — its Windows build was broken — and is folded into this
release.)

### Headline

- **Roast: 501 / 1,462 files fully pass** (was 419) — the 500-files milestone.
  Passing assertions grew from 157,293 to **171,817**; the declared-test
  denominator also grew (191,546 → 213,617) because parse fixes keep
  surfacing plans that previously died unannounced, so per-test percentage
  moves less than the absolute counts (~80% declared).
- Ten zero-regression campaign batches (R1–R3, NM1–NM6), each gated on the
  full suite with no pass-list drops and equal-or-faster benchmarks.

### Language & runtime

- **`$*SCHEDULER.cue`** is implemented: `:at`/`:in`/`:every`/`:times`/`:stop`/
  `:catch`, `Cancellation` (`.cancel`/`.cancelled`), `.loads`,
  `.uncaught_handler`, `CurrentThreadScheduler`; NaN/±Inf delay semantics and
  argument-combination errors match the spec. Cued jobs run on worker threads
  with a drift-free deadline clock.
- **`subtest 'desc' => { … }` (the Pair form) now executes its body** — it
  used to pass vacuously. Landed together with seven batches of the
  pre-existing bugs it exposed (Rat 0-denominator cluster, `categorize-list`/
  `classify-list`, `.toggle`, non-flattening `:=`, strict `fails-like`,
  `substr-eq`, Capture semantics, …).
- `return` is `Routine`-only (a bare block's `return` returns from the
  enclosing routine); cooperative `return` works inside a method's loops.
- `&?BLOCK` / `&?ROUTINE` resolve lazily from the frame (`&?ROUTINE` outside
  a routine is a parse error, as in Rakudo).
- `X but VALUE` mixins compose a constant method named by the value's type.
- Weighted `pick`/`roll` on Bag/Mix draw without materializing pools;
  `roll(*)` is an infinite lazy sequence.
- DateTime: exact (non-float) seconds, leap-second table, fixed-offset
  timezones, single-numeric POSIX constructor.
- Junctions: `all`/`none` autothread outside `any`/`one`; whatever-curry wins
  over junction autothreading; the standard matcher-method exemptions
  (`grep`, `first`, `classify`, `comb`, `subst`, …).
- MAIN: Rakudo-compatible dispatch strictness, generated `$*USAGE` (including
  `#=` declarator-pod option descriptions), `sub USAGE` takes over the
  failure path, CLI arguments bind as allomorphs.
- Implicit `$a`/`$b` in paramless blocks removed (post-GLR semantics sweeps:
  element itemization, `Z`-comma, stacked zip/cross metaops, one-level
  operands, min/max flattening, rotor pairs, rw loop params, …).
- New builtins and methods across the campaign — inventories now stand at
  **179 subroutines / 505 methods** ([docs/REFERENCE.md](docs/REFERENCE.md)):
  `Lock::Async`, minimal `IO::CatHandle`, `FileHandle.encoding`,
  `List.lazy`, `cross(:with)`, the hyperbolic-trig family
  (`sech`/`cosech`/`cotanh` + inverses), `%%` by zero throwing
  `X::Numeric::DivideByZero`, Set↔Bag↔Mix coercions, `TYPE ~~ TYPE` role
  smartmatch, and more.
- `SIGPIPE` is ignored process-wide: TCP servers survive client disconnects.
- EVAL-only statement strictness ("two terms in a row") with typed
  `X::Syntax::Confused` parse errors.

### Parser

- A `}` at end of line terminates the statement (Rakudo's rule) — previously
  `x => {…}` followed by an `if`/`else` chain could silently re-parse as a
  statement modifier.
- The tight-paren reduce call `[+](…)` takes only its parens — it used to
  swallow the rest of the enclosing comma list, inflating some test files'
  emitted-test counts for years.
- Variable subscript adverbs (`%h{$k}:exists`-family with variable keys),
  adverbed zen slices, dative method syntax (`name $obj: args`), `INIT` as an
  expression, contextualizer circumfixes, comma-list shapes.

### Native codegen (`--exe`)

- `s///`, `$0` captures, and post-GLR slips compile natively (the pastebin
  showcase no longer needs `--bundle`).

### Raku.js — new subproject

- The unmodified C++ interpreter compiled to WebAssembly, with a browser
  playground: worker-based execution with live streaming output and a Stop
  button, syntax-highlighting editor, theme switcher, 24 bundled examples.
  Live at [raku.online](https://raku.online/).
- First performance measurements (experimental; [rakujs/README.md](rakujs/README.md)):
  1.3–6.8× slower than the native interpreter on a clean host, dominated by
  the `-fexceptions` call trampolines; Node vs Bun comparison included.

### Real-world output parity

- **Perl Weekly Challenge corpus** (10,428 community solutions run under
  both engines): byte-identical stdout+status went **2,663 → 4,056** across
  15 fix batches ([docs/dev/PWC-DIVERGENCES.md](docs/dev/PWC-DIVERGENCES.md)).
- **Raku course**: the generator reproduces the full 1,483-page course
  byte-for-byte identically to Rakudo after two rounds of divergence fixes.

### Showcases & tests

- Seven new showcase programs, each with a README: **lisp** (a Scheme on a
  Raku grammar), **pastebin** (HTTP on raw sockets), **markdown** (grammar →
  HTML), **chat** (concurrent TCP), **forth** (a stack machine), **kvstore**
  (a key-value protocol), **rakus** (a static HTTP file server).
- New `t/` regression suite (47 checks: golden example outputs + showcase
  behaviour), wired into CI on the POSIX platforms.

### Performance

- Cold start is **~2 ms** (best of 200 spawns; previously documented ~12 ms).
- Full benchmark refresh against Rakudo v2026.06
  ([docs/BENCHMARKS.md](docs/BENCHMARKS.md)): the interpreter is ahead on 8
  of 9 kernels (fib remains Rakudo's, 1.7×), `--exe` ahead on all 9.
- A perf regression found and reversed mid-campaign: eager `&?BLOCK`/
  `&?ROUTINE` frame bindings cost +40% on call-heavy code; the lazy
  resolution above restored the baseline.

### Platforms & CI

- MSVC: `clock_gettime(CLOCK_REALTIME)` replaced with `std::chrono` (this is
  what broke the unpublished 0.7.0 tag's Windows build); srand seeding
  widened to 64-bit (was UB on LLP64 and wasm32).
- Windows suite: portable process cleanup in `t/run.raku`.

## v0.5.1 — 2026-07-13 and earlier

Pre-changelog releases: v0.5.1, v0.5.0, v0.1.0. History is in git and the
docs as they stood at each tag.
