# Raku++

A from-scratch implementation of the [Raku](https://raku.org) programming
language in **C++17, with no third-party dependencies** — a hand-written lexer,
parser, and tree-walking evaluator that runs real Raku (classes, roles, grammars,
regexes, multi-dispatch, junctions, lazy sequences, a bignum tower,
Unicode-correct strings, and concurrency) and can also **compile** a program to a
standalone native binary. It is not a fork of Rakudo and shares no code with it;
it targets the *language*, measured against [**Roast**](https://github.com/Raku/roast),
the official Raku test suite.

**Status:** measured per individual test, **~82% of Roast passes** — 157,898 of
~193,638 tests the suite declares, counting the tests in files that abort before
running (their `plan N` is read from source). On the stricter all-or-nothing bar,
**433 / 1,462 files fully pass (~30%)** — a file counts only if *every* assertion
in it passes. Early-stage, growing test-first. See [the highlights](docs/HIGHLIGHTS.md)
for the key features in bullets, [the overview](docs/OVERVIEW.md) for
a one-page tour, [the full guide](docs/GUIDE.md) for the complete picture, or
[COUNTING.md](docs/COUNTING.md) for exactly how these are defined.

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
| `--cpp SRC` | Print the C++ that `--exe` transpiles to |
| `--help`, `--version` | Show help / version |

`RAKUPP_PARALLEL=1` opts into true CPU parallelism for `start`/worker threads
(default coordinates under a GIL). Full option and environment-variable reference:
[the guide](docs/GUIDE.md#command-line-options).

## Documentation

- **[HIGHLIGHTS.md](docs/HIGHLIGHTS.md)** — the key features, in bullets, on one page.
- **[OVERVIEW.md](docs/OVERVIEW.md)** — a one-page tour: what Raku++ is, its goals, capabilities, and how it compares to Rakudo.
- **[GUIDE.md](docs/GUIDE.md)** — the full overview: goals, status, the compile modes, running against Roast, architecture.
- **[FEATURES.md](docs/FEATURES.md)** — inventory of supported language features, by theme.
- **[REFERENCE.md](docs/REFERENCE.md)** — exhaustive lookup sheet: every operator, built-in subroutine, and method, each with a verified example.
- **[COOKBOOK.md](docs/COOKBOOK.md)** — a cookbook of runnable one-liner snippets, each verified against `rakupp`.
- **[examples/](examples/)** — complete example programs (Mandelbrot, Game of Life, a JSON grammar, a quine, …); see [examples/README.md](examples/README.md).
- **[UNICODE.md](docs/UNICODE.md)** — Unicode support: graphemes (UAX #29), normalization, UCA collation, character introspection — the data pipeline and measured coverage.
- **[ASYNC.md](docs/ASYNC.md)** — concurrency & async: promises, supplies, channels, threads, and the two execution modes.
- **[ARCHITECTURE.md](docs/ARCHITECTURE.md)** — how it's built, and what happens to a program in each run mode.
- **[DOGFOODING.md](docs/DOGFOODING.md)** — the Raku tools Raku++ uses to build, test, and measure itself.
- **[ROADMAP.md](docs/ROADMAP.md)** — done / in-progress / next.
- **[ROAST.md](docs/ROAST.md)** — Roast suite overview and per-section statistics.
- **[COUNTING.md](docs/COUNTING.md)** — how the pass-rate numbers are defined and computed (the authoritative methodology).
- **[BENCHMARKS.md](docs/BENCHMARKS.md)** — a fair speed comparison with Rakudo on the shared subset.
- **[NATIVE.md](docs/NATIVE.md)** — interpreter vs compiled (`--exe`) on the example programs; every example compiles natively with identical output.
- **[OPTIMIZATION.md](docs/OPTIMIZATION.md)** — the `--exe -O` optimizer: what it does and how fast it gets.
- **[METAPROGRAMMING.md](docs/METAPROGRAMMING.md)** — language-mutation coverage: custom operators, precedence traits, phasers, MOP, macros/slangs.
- **[docs/dev/JOURNEY.md](docs/dev/JOURNEY.md)** — a memoir of how this was built.

## Author

Raku++ is created by [Andrew Shitov](https://andrewshitov.com). Read the
announcement:
[Raku++ — the fastest Raku compiler](https://andrewshitov.com/2026/07/13/raku-the-fastest-raku-compiler/).

## License

[Artistic License 2.0](LICENSE) — the same license Raku itself uses.
