# Raku++

A from-scratch implementation of the [Raku](https://raku.org) programming
language in **C++17, with no third-party dependencies** — a hand-written lexer,
parser, and tree-walking evaluator that runs real Raku (classes, roles, grammars,
regexes, multi-dispatch, junctions, lazy sequences, a bignum tower,
Unicode-correct strings, and concurrency), can also **compile** a program to a
standalone native binary, and — as **[Raku.js](rakujs)** — **runs in the browser**
via WebAssembly, no server required. It is not a fork of Rakudo and shares no code with it;
it targets the *language*, measured against [**Roast**](https://github.com/Raku/roast),
the official Raku test suite.

**Status:** current release **v2.0.0** (2026-08-07) — *other people's code,
honestly counted*:

| | v2.0.0 | at v1.8.0 |
|---|---:|---:|
| Ecosystem distributions passing their own `zef` install-time test suite | **50 / 59** | 32 / 59 |
| Official documentation examples byte-identical on both engines | **952** | 945 |
| Roast, per individual test — of ~218,700 the suite declares | **197,090 (90%)** | 197,060\* |
| Roast, all-or-nothing — files fully passing, of 1,462 | **594 (41%)** | 633\* |

The per-test figure counts the tests in files that abort before running (their
`plan N` is read from source); on the all-or-nothing bar a file counts only if
*every* assertion in it passes. \*The v1.8.0 Roast figures were **inflated**:
`subtest "…" => {…}` bodies — the Pair form, most of the suite's subtests —
silently never ran and auto-passed. v2.0.0 fixes that, which removed ~2,340
vacuous passes and 39 "fully passing" files, then re-earned the total honestly;
compare the two columns knowing the new one clears a stricter bar.
Early-stage, growing test-first. See [the highlights](docs/guide/HIGHLIGHTS.md)
for the key features in bullets, [the overview](docs/guide/OVERVIEW.md) for
a one-page tour, [the full guide](docs/guide/GUIDE.md) for the complete picture,
[COUNTING.md](docs/status/COUNTING.md) for exactly how these are defined, or the
[CHANGELOG](CHANGELOG.md) for what each release brought.

## Install

### Homebrew (macOS)

```sh
brew tap ash/rakupp
brew install rakupp        # or: brew install --HEAD rakupp   (latest main)
```

Apple Silicon installs a **prebuilt binary** (no compile); Intel builds from
source. Homebrew itself requires the Xcode Command Line Tools — if `brew install`
says to install them, run `xcode-select --install` first.

### Prebuilt binaries (macOS, Linux, Windows)

Every release ships self-contained archives on the
[Releases page](https://github.com/ash/rakupp/releases/latest):
`rakupp-macos-universal.tar.gz` (Apple Silicon + Intel, macOS 11+),
`rakupp-linux-x86_64.tar.gz` (static libstdc++ — no dependencies), and
`rakupp-windows-x64.zip` (static CRT — no redistributable needed). Unpack
keeping the `bin/ lib/ include/` layout together (that's what `--exe` uses)
and put `bin/` on your `PATH`.

### Build from source

```sh
# Needs a C++17 compiler + CMake → produces build/rakupp
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build

# Install onto $PATH (binary + the runtime that --exe links against)
cmake --install build --prefix ~/.local   # → ~/.local/{bin,lib,include/rakupp}
```

On Windows (MSVC), build from a *Developer Command Prompt* and pass the
configuration to the build step — the Visual Studio generator is
multi-config, so `-DCMAKE_BUILD_TYPE` alone is not enough:

```sh
cmake -S . -B build
cmake --build build --config Release      # → build/Release/rakupp.exe
```

### GNU Guix (Linux)

The repository is also a Guix channel
([PR #6](https://github.com/ash/rakupp/pull/6), contributed by
[@4zv4l](https://github.com/4zv4l)). Build directly from a checkout:

```sh
guix build -f .guix/modules/rakupp-package.scm
```

or add the channel to `~/.config/guix/channels.scm` and install:

```scm
(channel
  (name 'rakupp)
  (url "https://github.com/ash/rakupp")
  (branch "main"))
```

```sh
guix pull && guix install rakupp
```

### Nix / NixOS

NixOS can't run the generic prebuilt Linux binary (it has no global ELF
interpreter — [issue #5](https://github.com/ash/rakupp/issues/5)), so build
from source through the repository's flake:

```sh
nix run github:ash/rakupp -- -e 'say 42'
```

```sh
nix profile install github:ash/rakupp
```

From a checkout, `nix build` produces `./result/bin/rakupp`.

## Quick start

```sh
# Run
rakupp -e 'say "hello, world"'            # a one-liner  (build/rakupp if not installed)
rakupp path/to/program.raku               # a file
echo 'say 42' | rakupp                     # from stdin
```

`rakupp` locates the runtime library `--exe` needs relative to its own binary, so
it works from any directory whether run out of `build/` or from an install
prefix. If you copy the binary somewhere on its own, point it back with
`RAKUPP_HOME=<prefix>`.

```sh
build/rakupp -e 'say (1..100).grep(*.is-prime).sum'    # → 1060
```

## Common options

| Option | Meaning |
|---|---|
| `FILE` / `-e 'CODE'` / *(stdin)* | Run a program from a file, a one-liner, or standard input |
| `-I <path>` / `-M <module>` | Add a module search directory / load a module first (both repeatable) |
| `-n` / `-p` / `-a` / `-F<sep>` | The perl one-liner family: line loop, autoprint, autosplit into `@F` (clusters: `-lane`) |
| `-i[.ext]` | With `-n`/`-p`: edit the argument files in place (`-pi.bak` keeps backups) |
| `--profile[=FILE]` | Routine-level wall-time profile after the run (`.json` for machine-readable) |
| `--exe SRC -o OUT` | Native-compile to a standalone binary (also `--bundle`, `--aot`) |
| `--highlight [SRC]` | Syntax-highlight Raku to HTML (`--html`) or terminal (`--ansi`) |
| `--lint SRC` | Static-analyze without running: unused variables, unreachable code, etc. ([LINT.md](docs/guide/LINT.md)) |
| `-c` / `--ast SRC` | Syntax-check only / print the parsed AST |
| `--cpp SRC [-O]` | Print the C++ that `--exe` transpiles to (add `-O` to see the optimized codegen) |
| `--help`, `--version` | Show help / version |

Flags are position-independent and cluster like perl's (`rakupp -pi.bak -e
'$_ = $_.subst("a", "b")' *.txt` works as you'd hope). `RAKUPP_PARALLEL=1`
opts into true CPU parallelism for `start`/worker threads (default
coordinates under a GIL). Full reference: [CLI.md](docs/guide/CLI.md).

## Modules

Raku++ runs modules from the ecosystem — it reads the **same store [zef](https://github.com/ugexe/zef)
populates**, so a module you `zef install` (via Rakudo) is picked up by `use`
with no extra setup:

```raku
use JSON::Fast;                             # installed with `zef install JSON::Fast`
say to-json({ name => 'Ada' }, :!pretty);   # {"name":"Ada"}
```

It also loads your own module files from `lib/` (and `-I` / `RAKULIB` / `use lib`
paths). A `use` that cannot be found or fails to compile is **fatal**, as in
Rakudo: the program stops and exits non-zero, rather than carrying on without
the module. Full guide: **[MODULES.md](docs/guide/MODULES.md)**.

## Run Raku in the browser — Raku.js

▶ **Try it live: [raku.online](https://raku.online/)** · **Learn it interactively: [raku.online/tour](https://raku.online/tour/)**

**[Raku.js](rakujs)** is the *same* interpreter compiled to **WebAssembly** with
Emscripten — the exact semantics as native `rakupp`, running entirely client-side
with no server. It powers an in-page [playground](rakujs/playground) (editor +
live output, with all the [examples/](examples) built in) and can be embedded in
any static page to make Raku snippets runnable — handy for docs, tutorials, or a
course. Build it with `rakujs/build.sh`; details in
[rakujs/README.md](rakujs/README.md).

## Documentation

### Start here

- **[presentation/](presentation)** — a slide deck introducing Raku++ and its ecosystem. Download [rakupp-presentation.pdf](presentation/rakupp-presentation.pdf) for a quick flip-through, or open [`index.html`](presentation/index.html) in a browser for the interactive, keyboard-navigable version. The quickest visual tour.
- **[HIGHLIGHTS.md](docs/guide/HIGHLIGHTS.md)** — the key features, in bullets, on one page.
- **[OVERVIEW.md](docs/guide/OVERVIEW.md)** — a one-page tour: what Raku++ is, its goals, capabilities, and how it compares to Rakudo.
- **[GUIDE.md](docs/guide/GUIDE.md)** — the full overview: goals, status, the compile modes, running against Roast, architecture.

### Language reference

- **[FEATURES.md](docs/guide/FEATURES.md)** — inventory of supported language features, by theme.
- **[MODULES.md](docs/guide/MODULES.md)** — working with modules: how `use` finds modules installed by zef, the search-path order, and writing your own. ([internals/MODULE-LOADING.md](docs/internals/MODULE-LOADING.md) is the internals companion: what `use` does inside the compiler, and where it diverges from Rakudo.)
- **[CACHING.md](docs/guide/CACHING.md)** — the precompiled parse: opt-in caching of a module's (or a program's) parsed form (`use XML` 16.0 ms → 5.7 ms), what each switch is measurably worth, and what invalidates an entry.
- **[REFERENCE.md](docs/guide/REFERENCE.md)** — exhaustive lookup sheet: every operator, built-in subroutine, and method, each with a verified example.
- **[COOKBOOK.md](docs/guide/COOKBOOK.md)** — a cookbook of runnable one-liner snippets, each verified against `rakupp`.
- **[faq/](docs/guide/faq/)** — short answers to questions people actually ask ("how do I capture a command's output?"), every snippet verified against both Raku++ and Rakudo, with the differences called out.
- **[UNICODE.md](docs/guide/UNICODE.md)** — Unicode support: graphemes (UAX #29), normalization, UCA collation, character introspection — the data pipeline and measured coverage.
- **[ASYNC.md](docs/guide/ASYNC.md)** — concurrency & async: promises, supplies, channels, threads, and the two execution modes.
- **[PARALLEL-SPEEDUP.md](docs/guide/PARALLEL-SPEEDUP.md)** — how to measure whether `start` actually made a program faster: the method, two runnable benchmarks, and why a shared-counter loop shows 1.44× where a contention-free fan-out shows 3.62×.
- **[NETWORKING.md](docs/guide/NETWORKING.md)** — talking over the network: async TCP clients and servers, HTTP, graceful shutdown with `signal`, and HTTPS/TLS via `IO::Socket::Async::SSL`.
- **[FFI.md](docs/guide/FFI.md)** — NativeCall: calling C from Raku. Whether you need to install anything (no — `libffi` is found at run time, with a fallback when it is missing), whether it works compiled as well as interpreted (yes, one marshaller serves both), the type map, structs and unions, callbacks, and variadic C functions, whose spelling matches Rakudo's. Also how to *prove* a call reached C, how to trace crossings live, and why a wrong declaration answers instead of failing.
- **[METAPROGRAMMING.md](docs/internals/METAPROGRAMMING.md)** — language-mutation coverage: custom operators, precedence traits, phasers, MOP, macros/slangs.
- **[NQP.md](docs/internals/NQP.md)** — the `use nqp` compatibility subset: what it covers, how it compiles (no NQP grammar involved), and why it costs nothing when unused. Lets ecosystem modules like JSON::Fast run.

### Code to read and run

- **[examples/](examples)** — complete example programs (Mandelbrot, Game of Life, a JSON grammar, a quine, …); see [examples/README.md](examples/README.md). [examples/lint/](examples/lint) demos the `--lint` analyzer, one rule per file.
- **[showcase/](showcase)** — mid-size showcase programs: a Scheme interpreter built on a Raku grammar, and a pastebin HTTP server on raw sockets; see [showcase/README.md](showcase/README.md).
- **[rakujs/](rakujs)** — **Raku.js**: the interpreter compiled to **WebAssembly** to run Raku in the browser with no server; includes a playground page with all the examples. Same interpreter as native, compiled with Emscripten; see [rakujs/README.md](rakujs/README.md).

### Under the hood

- **[ARCHITECTURE.md](docs/internals/ARCHITECTURE.md)** — how it's built, and what happens to a program in each run mode.
- **[PARSING.md](docs/internals/PARSING.md)** — the front end: from source text to AST — the lexer, the Pratt parser, and how user-defined operators (factorial `postfix:<!>`, custom precedence) are parsed in a single pass.
- **[RUNTIME.md](docs/internals/RUNTIME.md)** — the runtime model: how statically-typed C++ runs dynamic Raku — the `Value` type, variables and containers, calls and dispatch, and lazy/infinite sequences.
- **[MEMORY.md](docs/guide/MEMORY.md)** — memory demands and limits: reserved vs. resident, stack sizes and measured recursion depths per mode (interpreter / `--exe` / wasm), and the data-side guardrails.
- **[LINT.md](docs/guide/LINT.md)** — the `--lint` static analyzer: the rules it applies, warnings vs. notes, exit codes, and why it stays conservative on Raku's dynamic constructs.
- **[OPTIMIZATION.md](docs/internals/OPTIMIZATION.md)** — the `--exe -O` optimizer: what it does and how fast it gets.
- **[HTTPS.md](docs/guide/HTTPS.md)** — the story of getting one real HTTPS request working: the chain of general bugs from "OpenSSL won't load" to `HTTP/1.1 200 OK` over TLS, and the NativeCall surface it exercised (documented in [FFI.md](docs/guide/FFI.md)).
- **[DOGFOODING.md](docs/status/DOGFOODING.md)** — the Raku tools Raku++ uses to build, test, and measure itself.
- **[ECOSYSTEM.md](docs/status/ECOSYSTEM.md)** — the projects built on this interpreter (Raku.js, raku.online with its tour and spec sub-sites, raku-corpus), how they connect, and the release runbook for rebuilding wasm and redeploying the sites after a new version.

### Measurements & status

- **[ROAST.md](docs/status/ROAST.md)** — Roast suite overview and per-section statistics.
- **[COUNTING.md](docs/status/COUNTING.md)** — how the pass-rate numbers are defined and computed (the authoritative methodology).
- **[BENCHMARKS.md](docs/status/BENCHMARKS.md)** — a fair speed comparison with Rakudo on the shared subset.
- **[NATIVE.md](docs/guide/NATIVE.md)** — interpreter vs compiled (`--exe`) on the example programs; every example compiles natively with identical output.
- **[COMPILERS.md](docs/guide/COMPILERS.md)** — which compiler and architecture to use (arm64 vs x86_64 on macOS, GCC vs Clang, MSVC vs MinGW on Windows), for building Raku++ and for `--exe`.
- **[ROADMAP.md](docs/status/ROADMAP.md)** — done / in-progress / next.
- **[CHANGELOG.md](CHANGELOG.md)** — release notes for tagged releases.

### The story

- **[MILESTONES.md](docs/status/MILESTONES.md)** — the dated timeline: the headline moments and numbers, release by release. Start here for the arc at a glance.
- **[docs/dev/JOURNEY.md](docs/dev/JOURNEY.md)** — a memoir of how this was built: the method and the principles.
- **[LONGREAD.md](LONGREAD.md)** — the long-form story: the whole arc from empty directory to ~82% of Roast, a native compiler, and a browser playground.

## Author

Raku++ is created by [Andrew Shitov](https://andrewshitov.com). Read the
announcement:
[Raku++ — the fastest Raku compiler](https://andrewshitov.com/2026/07/13/raku-the-fastest-raku-compiler/).

## License

[Artistic License 2.0](LICENSE) — the same license Raku itself uses.
