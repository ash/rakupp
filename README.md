# Raku++

A from-scratch implementation of the [Raku](https://raku.org) programming
language in **C++17, with no third-party dependencies** — a hand-written lexer,
parser, and tree-walking evaluator that runs real Raku (classes, roles, grammars,
regexes, multi-dispatch, junctions, lazy sequences, a bignum tower,
Unicode-correct strings, and concurrency), can also **compile** a program to a
standalone native binary, and — as **[Raku.js](rakujs/)** — **runs in the browser**
via WebAssembly, no server required. It is not a fork of Rakudo and shares no code with it;
it targets the *language*, measured against [**Roast**](https://github.com/Raku/roast),
the official Raku test suite.

**Status:** measured per individual test, **~85% of Roast passes** — 181,070 of
~213,203 tests the suite declares, counting the tests in files that abort before
running (their `plan N` is read from source). On the stricter all-or-nothing bar,
**523 / 1,462 files fully pass (~36%)** — a file counts only if *every* assertion
in it passes. Early-stage, growing test-first. See [the highlights](docs/HIGHLIGHTS.md)
for the key features in bullets, [the overview](docs/OVERVIEW.md) for
a one-page tour, [the full guide](docs/GUIDE.md) for the complete picture,
[COUNTING.md](docs/COUNTING.md) for exactly how these are defined, or the
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
| `-I <path>` | Add a directory to the module search path (repeatable) |
| `--exe SRC -o OUT` | Native-compile to a standalone binary (also `--bundle`, `--aot`) |
| `--highlight [SRC]` | Syntax-highlight Raku to HTML (`--html`) or terminal (`--ansi`) |
| `--ast SRC` | Print the parsed AST |
| `--cpp SRC [-O]` | Print the C++ that `--exe` transpiles to (add `-O` to see the optimized codegen) |
| `--help`, `--version` | Show help / version |

`RAKUPP_PARALLEL=1` opts into true CPU parallelism for `start`/worker threads
(default coordinates under a GIL). Full option and environment-variable reference:
[the guide](docs/GUIDE.md#command-line-options).

## Run Raku in the browser — Raku.js

▶ **Try it live: [course.raku.org/playground](https://course.raku.org/playground/)**

**[Raku.js](rakujs/)** is the *same* interpreter compiled to **WebAssembly** with
Emscripten — the exact semantics as native `rakupp`, running entirely client-side
with no server. It powers an in-page [playground](rakujs/playground/) (editor +
live output, with all the [examples/](examples/) built in) and can be embedded in
any static page to make Raku snippets runnable — handy for docs, tutorials, or a
course. Build it with `rakujs/build.sh`; details in
[rakujs/README.md](rakujs/README.md).

## Documentation

### Start here

- **[HIGHLIGHTS.md](docs/HIGHLIGHTS.md)** — the key features, in bullets, on one page.
- **[OVERVIEW.md](docs/OVERVIEW.md)** — a one-page tour: what Raku++ is, its goals, capabilities, and how it compares to Rakudo.
- **[GUIDE.md](docs/GUIDE.md)** — the full overview: goals, status, the compile modes, running against Roast, architecture.

### Language reference

- **[FEATURES.md](docs/FEATURES.md)** — inventory of supported language features, by theme.
- **[REFERENCE.md](docs/REFERENCE.md)** — exhaustive lookup sheet: every operator, built-in subroutine, and method, each with a verified example.
- **[COOKBOOK.md](docs/COOKBOOK.md)** — a cookbook of runnable one-liner snippets, each verified against `rakupp`.
- **[UNICODE.md](docs/UNICODE.md)** — Unicode support: graphemes (UAX #29), normalization, UCA collation, character introspection — the data pipeline and measured coverage.
- **[ASYNC.md](docs/ASYNC.md)** — concurrency & async: promises, supplies, channels, threads, and the two execution modes.
- **[METAPROGRAMMING.md](docs/METAPROGRAMMING.md)** — language-mutation coverage: custom operators, precedence traits, phasers, MOP, macros/slangs.

### Code to read and run

- **[examples/](examples/)** — complete example programs (Mandelbrot, Game of Life, a JSON grammar, a quine, …); see [examples/README.md](examples/README.md).
- **[showcase/](showcase/)** — mid-size showcase programs: a Scheme interpreter built on a Raku grammar, and a pastebin HTTP server on raw sockets; see [showcase/README.md](showcase/README.md).
- **[rakujs/](rakujs/)** — **Raku.js**: the interpreter compiled to **WebAssembly** to run Raku in the browser with no server; includes a playground page with all the examples. Same interpreter as native, compiled with Emscripten; see [rakujs/README.md](rakujs/README.md).

### Under the hood

- **[ARCHITECTURE.md](docs/ARCHITECTURE.md)** — how it's built, and what happens to a program in each run mode.
- **[PARSING.md](docs/PARSING.md)** — the front end: from source text to AST — the lexer, the Pratt parser, and how user-defined operators (factorial `postfix:<!>`, custom precedence) are parsed in a single pass.
- **[RUNTIME.md](docs/RUNTIME.md)** — the runtime model: how statically-typed C++ runs dynamic Raku — the `Value` type, variables and containers, calls and dispatch, and lazy/infinite sequences.
- **[OPTIMIZATION.md](docs/OPTIMIZATION.md)** — the `--exe -O` optimizer: what it does and how fast it gets.
- **[DOGFOODING.md](docs/DOGFOODING.md)** — the Raku tools Raku++ uses to build, test, and measure itself.

### Measurements & status

- **[ROAST.md](docs/ROAST.md)** — Roast suite overview and per-section statistics.
- **[COUNTING.md](docs/COUNTING.md)** — how the pass-rate numbers are defined and computed (the authoritative methodology).
- **[BENCHMARKS.md](docs/BENCHMARKS.md)** — a fair speed comparison with Rakudo on the shared subset.
- **[NATIVE.md](docs/NATIVE.md)** — interpreter vs compiled (`--exe`) on the example programs; every example compiles natively with identical output.
- **[ROADMAP.md](docs/ROADMAP.md)** — done / in-progress / next.
- **[CHANGELOG.md](CHANGELOG.md)** — release notes for tagged releases.

### The story

- **[docs/dev/JOURNEY.md](docs/dev/JOURNEY.md)** — a memoir of how this was built: the method and the principles.
- **[LONGREAD.md](LONGREAD.md)** — the long-form story: the whole arc from empty directory to ~82% of Roast, a native compiler, and a browser playground.

## Author

Raku++ is created by [Andrew Shitov](https://andrewshitov.com). Read the
announcement:
[Raku++ — the fastest Raku compiler](https://andrewshitov.com/2026/07/13/raku-the-fastest-raku-compiler/).

## License

[Artistic License 2.0](LICENSE) — the same license Raku itself uses.
