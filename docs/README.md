# docs/

All Raku++ documentation. The [top-level README](../README.md) is the entry
point; everything else lives here, in four places:

| Directory | What is in it |
|---|---|
| **[guide/](guide/)** | the manual — how to *use* Raku++ |
| **[internals/](internals/)** | how it works inside |
| **[book/](book/)** | *Raku++ Internals* — the same ground at book length, as a PDF |
| **[status/](status/)** | how good it is: conformance, speed, roadmap |
| **[dev/](dev/)** | working notes: plans, findings, experiments |

---

## guide/ — the manual

### Start here

- **[guide/HIGHLIGHTS.md](guide/HIGHLIGHTS.md)** — the key features, in bullets, on one page.
- **[guide/OVERVIEW.md](guide/OVERVIEW.md)** — a one-page tour: what Raku++ is, its goals, capabilities, and how it compares to Rakudo.
- **[guide/GUIDE.md](guide/GUIDE.md)** — the full overview: goals, status, the compile modes, running against Roast, architecture.
- **[guide/faq/](guide/faq/)** — short answers to questions people actually ask, every snippet verified against both engines, with the Raku++/Rakudo differences called out.

### Looking things up

- **[guide/FEATURES.md](guide/FEATURES.md)** — inventory of supported language features, by theme.
- **[guide/REFERENCE.md](guide/REFERENCE.md)** — the exhaustive, example-driven lookup sheet: every operator, subroutine and method with a verified example.
- **[guide/COOKBOOK.md](guide/COOKBOOK.md)** — a cookbook of runnable one-liner snippets, each verified against `rakupp`.

### Topics

- **[guide/UNICODE.md](guide/UNICODE.md)** — Unicode support: graphemes, normalization, UCA collation, character introspection.
- **[guide/ASYNC.md](guide/ASYNC.md)** — concurrency & async: promises, supplies, channels, threads, and the two execution modes.
- **[guide/PARALLEL-SPEEDUP.md](guide/PARALLEL-SPEEDUP.md)** — how to measure whether `start` actually made a program faster, with two runnable benchmarks and the numbers they produce.
- **[guide/NETWORKING.md](guide/NETWORKING.md)** — TCP over `IO::Socket::Async`, graceful shutdown with `signal()`, and TLS through the system OpenSSL.
- **[guide/FFI.md](guide/FFI.md)** — NativeCall: calling C from Raku++. Whether you need to install libffi (no — it is found at run time, and there is a fallback when it is missing), whether it works compiled as well as interpreted (yes, one marshaller serves both), the type map, structs and unions, callbacks, and variadic C functions, whose spelling matches Rakudo's.
- **[guide/HTTPS.md](guide/HTTPS.md)** — the story of one real HTTPS request, from "OpenSSL won't even load" to a live `HTTP/1.1 200 OK` over TLS.
- **[guide/MODULES.md](guide/MODULES.md)** — working with modules: the ones you write, and the ones zef installs from the ecosystem.
- **[guide/EXTENSIONS.md](guide/EXTENSIONS.md)** — native extension modules, the XS analogue: a distribution ships C, the build step compiles it against Raku++'s C ABI at install time, and the routines become ordinary Raku subs — so the module versions independently of the compiler. Why an extension never sees `Value`, the handle lifetime rules, how to write one that still runs on Rakudo (and why `&::('rakupp-ext-load')` rather than a plain call), packaging with `Build.rakumod`, the `Rakupp::` naming convention, and the current limits.
- **[guide/LINT.md](guide/LINT.md)** — `rakupp --lint`: the static-analysis rules that run over the AST without executing the program.

### Running and shipping programs

- **[guide/CLI.md](guide/CLI.md)** — the command line: position-independent flags, the perl one-liner family (`-n`/`-p`/`-a`/`-F`, `-i` in-place editing, `-0777`), `-M`, the `--profile` wall-time profiler, and a perl↔rakupp cookbook with the deliberate divergences listed.
- **[guide/NATIVE.md](guide/NATIVE.md)** — the `--exe` native compiler: interpreter vs. compiled on the example programs (byte-identical output).
- **[guide/COMPILERS.md](guide/COMPILERS.md)** — which compiler and architecture to use: arm64 vs. x86_64 on macOS, GCC vs. Clang, MSVC vs. MinGW on Windows — both for building Raku++ and for the compiler `--exe` invokes.
- **[guide/CACHING.md](guide/CACHING.md)** — the precompiled parse: opt-in caching of parsed ASTs, with two switches (`--precomp-modules`, `--precomp-files`) because they are worth measurably different amounts. What is stored (the AST, not bytecode), where it lives, exactly what invalidates an entry, and how it compares with Rakudo's `.precomp` and Python's `__pycache__`.
- **[guide/MEMORY.md](guide/MEMORY.md)** — memory demands and limits: reserved vs. resident, stack sizes and measured recursion depths per mode (interpreter / `--exe` / wasm), and the data-side guardrails.

---

## book/ — the compiler book

- **[book/Raku++-Internals.pdf](book/Raku++-Internals.pdf)** — *Raku++
  Internals*, 261 pages in nine parts: the front end, the value model, the
  interpreter, the regex and grammar engine, Unicode, the four run modes and the
  native code generator, the boundaries (modules, `use nqp`, NativeCall, the
  extension ABI, concurrency), and the tooling built on the AST. It covers
  several areas that have no page in `internals/` — `Value` in depth, the regex
  engine itself, NativeCall's internals, the extension ABI, and the concurrency
  runtime — and carries the reasons and measurements behind the designs, plus an
  "honest limitations" section per chapter. Built with
  `rakupp docs/book/build.raku`; see **[book/README.md](book/README.md)**.

---

## internals/ — how it works

- **[internals/ARCHITECTURE.md](internals/ARCHITECTURE.md)** — how it's built, and what happens to a program in each run mode.
- **[internals/PARSING.md](internals/PARSING.md)** — the front end: from source text to AST — the lexer, the Pratt parser, and how user-defined operators (and other in-program grammar tweaks) are handled in a single pass.
- **[internals/RUNTIME.md](internals/RUNTIME.md)** — the runtime model: how statically-typed C++ runs dynamic Raku — what a `Value` is, how variables and containers relate, calls and dispatch, and lazy/infinite sequences.
- **[internals/METAPROGRAMMING.md](internals/METAPROGRAMMING.md)** — language-mutation coverage: custom operators, precedence traits, phasers, MOP, macros/slangs.
- **[internals/NQP.md](internals/NQP.md)** — the `use nqp` compatibility subset that lets ecosystem modules (JSON::Fast, …) run: what it covers, how it compiles, and why it's zero-cost when unused.
- **[internals/MODULE-LOADING.md](internals/MODULE-LOADING.md)** — how modules work *inside* the compiler: what `use Foo;` does at parse time (a text scan, for operators only) versus at run time, the `Env` a module lives in during its load and what survives after, why a module's AST is executed once and then kept alive only as storage, why calling into a module is not a distinct operation, and the divergence table — starting with the fact that a module's whole environment is published to the global scope. (The *user-facing* guide is [guide/MODULES.md](guide/MODULES.md).)
- **[internals/OPTIMIZATION.md](internals/OPTIMIZATION.md)** — the `--exe -O` optimizer: the codegen passes and how fast they get.
- **[internals/DISPATCH.md](internals/DISPATCH.md)** — call dispatch in `--exe` code: what each call shape costs (measured), the cached-builtin/inline-string-compare cuts, and what's deliberately left on the table.
- **[internals/REGEX-LTM.md](internals/REGEX-LTM.md)** — Longest-Token Matching: what the declarative prefix is, the probe and NFA rankers, the gap-aware hybrid contract behind `RAKUPP_LTM=1`, subrule expansion, proto dispatch, and the oracle-confirmed corner cases.
- **[internals/STRINGS.md](internals/STRINGS.md)** — `CowStr`, the copy-on-write string behind every `Str` value: why a `std::string` member made every operation O(length), the two arms and the 64-byte threshold, why promotion is eager, the property cache on the immutable body — and the C++ survey behind it: the standard library had a COW string and C++11 removed it, what `string_view`/SSO/`shared_ptr` do and don't cover, how `folly::fbstring`, `QString` and `absl::Cord` compare, and the rules for working with the type.
- **[internals/NODE-SPECIALIZATION.md](internals/NODE-SPECIALIZATION.md)** — the interpreter fast paths for `$a OP $b`, `$n OP literal` and `@a[$i]`: what is cached (the syntactic SHAPE, never the variable or its value), the guards that decline it, why it is not a new node kind, the measured numbers with a control kernel, and the three mistakes made getting there.

### In the browser

- **[../rakujs/README.md](../rakujs/README.md)** — **Raku.js**: the same runtime compiled to **WebAssembly** to run Raku in the browser (build, deploy, performance).
- **[../rakujs/TUTORIAL.md](../rakujs/TUTORIAL.md)** — writing real browser Raku programs on the WebAssembly build (feeding input, reading output, workers).
- **[../rakujs/STACKED-INTERPRETERS.md](../rakujs/STACKED-INTERPRETERS.md)** — the showcase interpreters running *inside* Raku.js, in the browser.
- **[../rakujs/COURSE-PLAY-BUTTONS.md](../rakujs/COURSE-PLAY-BUTTONS.md)** — how course.raku.org turns its solution code into runnable, editable Raku.js editors.

---

## status/ — how good it is

- **[status/ROAST.md](status/ROAST.md)** — Roast suite overview and per-section statistics.
- **[status/COUNTING.md](status/COUNTING.md)** — how the pass-rate numbers are defined and computed (the authoritative methodology).
- **[status/BENCHMARKS.md](status/BENCHMARKS.md)** — a fair speed comparison with Rakudo on the shared subset.
- **[status/ROADMAP.md](status/ROADMAP.md)** — done / in-progress / next.
- **[status/MILESTONES.md](status/MILESTONES.md)** — a running timeline of the headline moments: the dates, the numbers, and what landed.
- **[status/ECOSYSTEM.md](status/ECOSYSTEM.md)** — the map of the constellation around the interpreter: what each satellite project is, where it lives, and how they relate.
- **[status/DOGFOODING.md](status/DOGFOODING.md)** — the Raku tools Raku++ uses to build, test, and measure itself.

---

## dev/ — working notes

See **[dev/README.md](dev/README.md)** for the annotated list. In short:

- **[dev/RELEASING.md](dev/RELEASING.md)** — the release checklist: the Roast, local-suite, **performance** and compiler-agreement gates that must pass before a version is bumped, and why each one exists.
- **[dev/plans/](dev/plans/)** — what we intend to build, and what we decided not to.
- **[dev/ecosystem/](dev/ecosystem/)** — the v2.0 campaign: the measured top-50, the plan, the triage log, the wishlist.
- **[dev/findings/](dev/findings/)** — living logs of what is broken and where we diverge from Rakudo.
- **[dev/experiments/](dev/experiments/)** — things measured and recorded, several of them reverted.
- **[dev/JOURNEY.md](dev/JOURNEY.md)** — a memoir of how Raku++ was built. Historical, not maintained as current reference.

---

## Elsewhere in the repository

- **[../README.md](../README.md)** — the project entry point.
- **[../CHANGELOG.md](../CHANGELOG.md)** — every release, with what changed.
- **[../LONGREAD.md](../LONGREAD.md)** — the round-by-round story of the build.
- **[../examples/README.md](../examples/README.md)** — the example programs.
- **[../showcase/README.md](../showcase/README.md)** — the mid-size showcase programs.
- **[../t/README.md](../t/README.md)** — the example + showcase regression suite.
