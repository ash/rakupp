# What is open

*Started 2026-10-03. Last updated 2026-10-05.*

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
- [ ] **An assignment statement that stores a Failure does not throw**:
  Rakudo sinks the assignment (`$a = f();` with `f` failing throws; `my $b =
  f()` does not). Found 2026-10-03 with the Num kernels; not started.
- [ ] **Rakuglaze**: 2175 of 2176 pass (round 5, 2026-10-03). Open:
  `@a.reverse` hands out copies where Rakudo's List holds the array's own
  containers (`@a.reverse.List[0] = 'x'` writes into `@a`); this needs
  containers in arrays, so it waits for the container-model work (Roast
  tracks A3). `('A'..∞).is-lazy` is ruled True in the suite (Rakudo answers
  False, yet treats the range as lazy everywhere else). Seen during round 5,
  not fixed:
  - `%m{$i}{$i} = $i++` stores `0 => Any` as well as `1 => {1 => 0}`
    (Rakudo: `{0 => {1 => 0}}`).
  - `for $l.list` over a quit Supply's list raises before the values;
    `for $l` and `for @$l` read them first, as Rakudo does.
  - An INIT default that reads an earlier parameter (`$y = INIT { $x }`)
    still runs per call.
  - `sub k { 1 } sub v { 2 }` on one line parses; Rakudo says "Strange text
    after block".
  - `Int:D()` given a Str type object dies at bind; Rakudo is not consistent
    there (it dies, or returns an undefined value).
  Also `my \y = $s; y.VAR` is Int (a parameter's is Scalar now), and a `my
  class` used earlier in its OWN block is no longer reported as a
  post-declaration.
- [ ] **#110 constructor type checks**: the module battery gate was never run.
  Open: `A.new ~~ Cool` is True; a List is accepted into Array-typed slots; Mu
  and Junction are accepted into `Any`; native width is not enforced;
  `has Array[Int]` is not checked.
- [ ] **Supply closers on aarch64**: three closers of the class fixed in
  925260b1 are still unlocked (the inner-tap closers and tapSupply's `ended`
  write). The fix needs the supplier lock key passed through tapSupply.
- [ ] **Cro and #116 follow-ups**:
  - `all(@p)».status` collapses the junction.
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
  - Role conflicts are undetected or reported late.
  - Seq, Range, Map and Hash answer one candidate for `.^can('elems')`.
  - An exported `infix:<+++>`.
  - Stacked postfixes: with only `postfix:<!>` declared, `3!!` is a parse
    error (Rakudo 720), and `3!²` drops the `²` (Rakudo 36).
  - `sub f($n) { 1..$n }; f(* + 1)` curries the range by value: Raku++
    answers a WhateverCode, Rakudo a Range.
  - `for @$list.kv -> $i, $x is rw` over a List: Rakudo dies binding `$x`
    (not a container); Raku++ runs it and the writes go nowhere.
  - A `sub EXPORT` inside a `unit module` runs on `use`; Rakudo does not call
    it there (it has to be outside the module).
  - Outside `react`, a `Proc::Async` with `:w` feeds its stdout taps only when
    the start promise is awaited, so an interactive driver never sees a reply
    before `close-stdin` (found with #121).
  - An enum TYPE object outside binding: `Color ~~ Int` and `Color ~~
    Enumeration` are False, `Color.^mro` is `(Any) (Mu)` (Rakudo: Color, Int,
    Cool, Any, Mu), `my Int $v = Color` dies, and `~Color` is the pair list
    (Rakudo warns and gives "").
  - A type object coerced to a number: `Str.Int`, `Num.Rat`, `Rat.Num` and
    the rest answer 0 where Rakudo dies ("must be an object instance"), so
    `sub f(Int() $x) {}; f(Str)` binds 0. Rakudo is not uniform here (`Any.Int`
    and `Cool.Num` are 0 with a warning).
  - A slurpy hash or a capture does not write back: `sub q(*%n) { %n<x> = 31 };
    q(x => $v)` and `sub r(|c) { c<x> = 33 }` leave `$v` alone (Rakudo: 31,
    33). Named `is rw` / `is raw` parameters do (2026-10-04).
  - `@a[$i++] = $i` evaluates the value before the subscript: Raku++ stores
    0 at index 0, Rakudo 1 (the subscript first). The same for `%h{…}`.
  - A multi candidate wrapped with `.wrap` keeps taking part in dispatch with
    its `where`: `multi wr(Int $n where * > 0)` beside `multi wr(Int $n)`,
    the first wrapped, answers `wr(-1)` from the second; Rakudo dies binding
    the wrapped candidate ("Constraint type check failed").
  - Two `IO::Handle.new` share one WHICH (the handle payload is shared), and
    `$supplier.Supply` answers the same Supply each call (Rakudo: a new one).
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
  fix, after which the workaround can go: `my \x = Nil` binds `Any`; `try`
  around a throw returns `Any`, not `Nil`; `MY::` after `use` lists the
  imported subs but not the imported classes, constants and enum values;
  `Geo.WHO` lacks the stub `Shape` of a nested `class Geo::Shape::Circle`;
  `(0..*).map(...)` returned from a sub is a `List`, not a `Seq`; `Pair`,
  `Range`, `Complex` report no `.^attributes`, `Exception` reports a
  `$!message` accessor, and `Pair.^mro` is `(Any Mu)`; `Date.^can('new')`
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
- [ ] **Data::Native**: `Callable :sorted-keys` is refused.
  [DATA-PLAN.md](DATA-PLAN.md).
- [ ] **Not started**: [LOCKFILE-PLAN.md](LOCKFILE-PLAN.md) (`rakupp.lock`,
  `--frozen`) and [UUID-PLAN.md](UUID-PLAN.md) (draft; the distribution's name
  is open).

## 6. Tooling, release and CI

- [ ] **v5.2.1 after the tag**: tagged and on PyPI 2026-10-03. Left: bump
  the Homebrew tap, and republish raku.online (the release's dashboard point
  from the artifact, the front page's version, the Roast map). RELEASING.md
  steps 5 and 6.
- [ ] **Docker**: the ghcr.io package is still private; it is the
  maintainer's step to make it public.
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
  The work: 2,033 mutsu files and 185 Rakudo files that Rakudo passes and
  Raku++ does not (`comm -23 *.rakudo.list *.rakupp.list`).
  - Parked from the audited LANG work: typed shaped arrays report unassigned
    cells as existing (`my Int @i[3]; @i[1]:exists`); Rat-endpoint slices
    (`@a[0..^2.5]` iterates the range and truncates in Rakudo); a closure
    sequence's seed is stored before it is pulled (`(1, {…} ... *).join` is
    `...` in Rakudo); `IO::Path.add` does not deep-flatten (methodCallPart3 is at
    its size ceiling); an enum member named `q` does not shadow the `q{…}` quote.
  - Rakudo t/ classified 2026-10-05 (145 LANG, 38 RakuAST, 5 6.e; 41 excluded).
  - Parked from the Rakudo t/ LANG work: `anon subset`/`anon enum` still
    install their name (subsets live in one global table); a nested block's
    `constant T` does not overwrite `OUR::<T>` (the GLOBAL frame is also the
    mainline's lexical scope); takes from the thunk of a sunk `xx *` inside a
    gather; a Label's `.gist` with its source excerpt; `Format.handle-iterator`;
    an operator declared LATER in a block is not seen by uses before it
    (`$i++` then `sub postfix:<++>`); a generic `T $x is copy` does not check
    assignments against the instantiated T; a native passed to a raw (`\v`)
    parameter or put in a Pair is not snapshotted; `my Int $x is default(5);
    $x = "s"` is not refused (the default value displaces the type record in
    `varDefault`); a compile-time error is not `~~ X::Comp` (Rakudo mixes it in);
    `.does` ignores a user role that shadows a core one.
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
    `Proc::Async`'s `.ready` can wake before `.started` reads True.
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
  grapheme, which a two-atom pattern cannot match).
- [ ] **A lazy list inside a sequence's seed list** gists as its elements:
  `(@f, 2, 3 ... 8)` with `@f = lazy 0, 1, 1` shows `[0 1 1]` where Rakudo
  shows `[...]` (the seed list is read out to find the start values).
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
- The UUID distribution's name.
- The rakuglaze repository's license.
- The binding version scheme (section 4).
- Telling the 321 author that their code breaks on Raku++ after v4.0.1
  (section 5).
