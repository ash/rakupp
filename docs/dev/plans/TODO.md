# What is open

*Started 2026-10-03. Last updated 2026-10-03.*

This is the one list of work that has been started and is not finished, across
every plan in this directory. Each line gives the plan that owns the details
and the next concrete step. When an item is finished, remove its line; the
history is in the plan and in git. When work starts on something new, add a
line here at the same time as the plan.

The work moves on a broad front: one speed item and one correctness item per
sitting, plus whatever tail is cheapest, so that no front falls behind.

---

## 1. Speed (v6)

- [ ] **Interpreter at native speed**: 18 of 35 tasks open.
  [INTERP-SPEED-PLAN.md](INTERP-SPEED-PLAN.md). Next: quicken
  `evalAssign`/`evalIndex`/`evalUnary`, then task 9 (fused integer leaves) and
  task 10 (automatic `--cnp` tier-up). Task 1's quiet-machine baseline is still
  owed.
- [ ] **Loop and sub kernels beyond scalars**: written 2026-10-02, not started.
  [KERNEL-PLAN.md](KERNEL-PLAN.md). Tasks 1–8: arrays and hashes, Num and Rat
  slots, `**` and pure methods, `--exe` parity, Str in sub kernels, closures in
  `--exe`.
- [ ] **Owed from the 2026-10-02 kernel work**: re-time the variations on
  d3c06426, the final run-bench table, `perf-guard --check`, and the
  BENCHMARKS.md prose about mutsu and `fib`.
- [ ] **`--cnp` copy-and-patch**: [CNP-PLAN.md](CNP-PLAN.md). P1 on ELF waits
  on a Linux CI run of the gate; P2 arena allocation; P3 `.kv`, the statement
  modifier, `.map`, closures, `[+]`, whole-array assignment, `given`, `return`;
  the threaded-program gap; P4 make it the default and remove `--jit`.
- [ ] **`--types`**: paused 2026-09-29. [TYPES-PLAN.md](TYPES-PLAN.md). N1 the
  switch, N2 the checked rule, N3 measure on Roast and the battery, N7 Str.
- [ ] **Packed native arrays**: [PACKED-ARRAY-PLAN.md](PACKED-ARRAY-PLAN.md).
  Batch 3: `--cnp` on packed registers, the reductions, the census.
- [ ] **V6 phases not started**: [V6-PLAN.md](V6-PLAN.md). P0 steady-state
  measurements on a quiet machine; P4 compile and load time (the precomp key
  is a decision for the maintainer); P5 `--exe` size and fallback coverage;
  P7 `use` in the web editions (#83).
- [ ] **`tctx_` thread-local lookups**: about 19% of loop samples, measured in
  the dispatch probes and not fixed.
- [ ] **`--slim` review**: what `--slim` actually removes. The 12.5 / 12.75 MB
  size budgets are a stopgap until this is done. [SLIM-PLAN.md](SLIM-PLAN.md)
  P6 (make `auto` the default) and P7 (`--aot`) follow.
- [ ] **Small tails**:
  - [VALUE32-PLAN.md](VALUE32-PLAN.md): one measurement decides B versus C.
  - [VALUEHASH-SMALL-PLAN.md](VALUEHASH-SMALL-PLAN.md): step 4, the other 8 of
    11 allocations per object.
  - [JUNCTION-PLAN.md](JUNCTION-PLAN.md): Ph3, list-associative `|`, `&`, `^`.
  - [DISPATCH-PERF-PLAN.md](DISPATCH-PERF-PLAN.md): Ph6, plus a per-class
    construction plan.
  - [UNBOX-PLAN.md](UNBOX-PLAN.md): P3–P5, partly overtaken by `--cnp`.
- [ ] **`--exe` native module bodies**: the remaining cost is method dispatch
  (the `RT.methodCall` ladder).

## 2. Correctness (v5 error batches)

- [ ] **V5 batches B1–B6**: only B0 (the instruments) has landed.
  [V5-PLAN.md](V5-PLAN.md).
  - B1: regressions (battery scan to 0).
  - B2: crashes (Text::Markdown, the `evalCall` race, the Rat cache race,
    `--exe` Inf/NaN, the layout-hash symbol).
  - B3: silent wrong answers.
  - B4: ecosystem and MOP clusters (custom HOW, Attribute MOP, Red #77).
  - B5: JS gate disagreements to 0, the `--slim` size gate.
  - B6: measure and tag.
- [ ] **Rakuglaze**: 2113 of 2178 pass. Open: indexing an infinite Str range,
  anonymous enums, `.VAR` on sigilless names, EVAL with CONTROL.
- [ ] **#110 constructor type checks**: the module battery gate was never run.
  Open: `A.new ~~ Cool` is True; a List is accepted into Array-typed slots; Mu
  and Junction are accepted into `Any`; native width is not enforced;
  `has Array[Int]` is not checked.
- [ ] **Supply closers on aarch64**: three closers of the class fixed in
  925260b1 are still unlocked (the inner-tap closers and tapSupply's `ended`
  write). The fix needs the supplier lock key passed through tapSupply.
- [ ] **Cro and #116 follow-ups**:
  - `all(@p)».status` collapses the junction.
  - `Promise.WHICH`.
  - A bare `Node` resolves through the `classAliases_` tail.
  - `NativeLibs EXPORT failed: No such method 'dispatcher'`.
  - Nested protos under LTM.
- [ ] **LibCurl::EasyHandle**: "Redeclaration of return type" since the
  GLOBAL-leak fix; not investigated.
- [ ] **Phaser order**: `temp` is restored after LEAVE; builtin callbacks
  (`.map`) and gather bodies keep the old order. perf-guard was not run for
  55b4f18a.
- [ ] **Native kinds**: still not refused through
  `.new(v => $int)`, `my @a = $int, 1`, `push` and hash construction.
- [ ] **Semantics sheets, step two**: about 11 sheets.
  [../findings/semantics/](../findings/semantics/).
- [ ] **Divergences found in sweeps and not fixed**:
  - `use` inside a sub loads at call time.
  - `$x := Int; $x = 5` lives.
  - Role conflicts are undetected or reported late.
  - Seq, Range, Map and Hash answer one candidate for `.^can('elems')`.
  - `.append($h<k>)` spreads.
  - `[ %( … ) ]` does not flatten.
  - An exported `infix:<+++>`.
  - `my $x = INIT expr` discards the INIT value.
  - `fail` in BUILD.
  - An exception in END is swallowed.
  - Stacked postfixes: with only `postfix:<!>` declared, `3!!` is a parse
    error (Rakudo 720), and `3!²` drops the `²` (Rakudo 36).
  - `sub f($n) { 1..$n }; f(* + 1)` curries the range by value: Raku++
    answers a WhateverCode, Rakudo a Range.
- [ ] **Roast tracks A3**: retire the remaining copy-back mechanisms
  (`rwLinks`, `copyOutRw`, `builtinTopicWB_`, `ArgWriter`).
  [ROAST-TRACKS-PLAN.md](ROAST-TRACKS-PLAN.md). The B1 coroutine code on
  x86-64 and Windows is only compile-checked; ASan and TSan runs are owed.

## 3. Compiled backends

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
  - Rakudo dies on `<`, `<=>`, `%` and `mod` over a Pointer; Raku++ answers.
  - A `*` under a user operator (`*!`, `* quack 5`) is refused and bundled;
    the emitter does not build the curry natively.
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
gate now pass in CI. Those four wheels are on PyPI as `rakulang` 5.2.0; a
later release reaches PyPI by running Actions → PyPI with its tag.

- [ ] **npm**: `rakulang` is not published. The binding is `bun:ffi`, so it
  runs on Bun only. A Node host needs the napi addon, which waits on A5
  ([EMBED-PLAN.md](EMBED-PLAN.md) E4). `package.json` still says 0.1.0.
- [ ] **crates.io**: `rakulang` is not published. `Cargo.toml` still says
  0.1.0.
- [ ] **Go**: the module path is the bare `rakulang`, so `go get` cannot
  fetch it. It needs a repository-qualified path or its own repository.
- [ ] **Versioning**: one rule for binding versions. Python follows the
  engine (5.2.0); JavaScript and Rust are at 0.1.0.
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
- [ ] **Grammars as a service**: G3 host callbacks and G4 the native Match
  walker, only if a workload needs them. [GRAMMAR-PLAN.md](GRAMMAR-PLAN.md).
- [ ] **Hosts**:
  - Jupyter: its gaps wait on an `rk_interrupt` ABI.
  - MCP: embedded sessions never join start-workers.
  - LSP: needs a review. Diagnostics span whole lines; only full-document
    sync; hover, completion, go-to-definition and formatting are not written;
    the VS Code extension is unpublished.
- [ ] **libffi**: by-value structs (§6) and linking libffi rather than loading
  it (§10, which matters for Windows). [LIBFFI-PLAN.md](LIBFFI-PLAN.md).

## 5. Ecosystem

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
- [ ] **Data::Native**: README.md and the modules guide do not link
  DATA-NATIVE.md; `Callable :sorted-keys` is refused.
  [DATA-PLAN.md](DATA-PLAN.md).
- [ ] **Not started**: [LOCKFILE-PLAN.md](LOCKFILE-PLAN.md) (`rakupp.lock`,
  `--frozen`) and [UUID-PLAN.md](UUID-PLAN.md) (draft; the distribution's name
  is open).

## 6. Tooling, release and CI

- [ ] **Docker**: the ghcr.io package is still private; it is the
  maintainer's step to make it public.
- [ ] **setup-rakupp Action**: written, not committed; its repository does not
  exist yet.
- [ ] **Extra suites as gates**: Rakudo's own t/ and mutsu's t/. Surveyed
  2026-10-01, not built.
- [ ] **Roast harness**: set `FD_CLOEXEC` on both pipe ends before fork in
  `spawnChildStart`. Run Rakudo at the 120 s budget; COUNTING.md's figures are
  from the 60 s budget.
- [ ] **`pick-rakupp`**: prefer the newest build, so a stale build directory
  cannot be picked.
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
- The UUID distribution's name.
- The rakuglaze repository's license.
- The binding version scheme (section 4).
