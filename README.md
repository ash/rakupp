# Raku++

A from-scratch implementation of the [Raku](https://raku.org) programming
language in **C++17, with no third-party dependencies** — a hand-written lexer,
parser, and tree-walking evaluator that runs real Raku (classes, roles, grammars,
regexes, multi-dispatch, junctions, lazy sequences, a bignum tower,
Unicode-correct strings, and concurrency) and can also **compile** a program to a
standalone native binary. It is not a fork of Rakudo and shares no code with it;
it targets the *language*, measured against [**Roast**](https://github.com/Raku/roast),
the official Raku test suite.

**Status:** measured per individual test, **~57% of Roast passes** — 131,320 of
~231,000 tests the suite declares, counting the tests in files that abort before
running (their `plan N` is read from source). On the stricter all-or-nothing bar,
**300 / 1,464 files fully pass (~20%)** — a file counts only if *every* assertion
in it passes. Early-stage, growing test-first. See [the overview](OVERVIEW.md) for
a one-page tour, [the full guide](GUIDE.md) for the complete picture, or
[COUNTING.md](COUNTING.md) for exactly how these are defined.

## Quick start

```sh
# Build (needs a C++17 compiler + CMake) → produces build/rakupp
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build

# Run
build/rakupp -e 'say "hello, world"'      # a one-liner
build/rakupp path/to/program.raku         # a file
echo 'say 42' | build/rakupp              # from stdin

# Install onto $PATH (binary + the runtime that --exe links against)
cmake --install build --prefix ~/.local   # → ~/.local/{bin,lib,include/rakupp}
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
[the guide](GUIDE.md#command-line-options).

## Documentation

- **[OVERVIEW.md](OVERVIEW.md)** — a one-page tour: what Raku++ is, its goals, capabilities, and how it compares to Rakudo.
- **[GUIDE.md](GUIDE.md)** — the full overview: goals, status, the compile modes, running against Roast, architecture.
- **[FEATURES.md](FEATURES.md)** — inventory of supported language features, by theme.
- **[EXAMPLES.md](EXAMPLES.md)** — a cookbook of runnable snippets, each verified against `rakupp`.
- **[ASYNC.md](ASYNC.md)** — concurrency & async: promises, supplies, channels, threads, and the two execution modes.
- **[ARCHITECTURE.md](ARCHITECTURE.md)** — how it's built, and what happens to a program in each run mode.
- **[ROADMAP.md](ROADMAP.md)** — done / in-progress / next.
- **[ROAST.md](ROAST.md)** — Roast suite overview and per-section statistics.
- **[COUNTING.md](COUNTING.md)** — how the pass-rate numbers are defined and computed (the authoritative methodology).
- **[BENCHMARKS.md](BENCHMARKS.md)** — a fair speed comparison with Rakudo on the shared subset.
- **[OPTIMIZATION.md](OPTIMIZATION.md)** — the `--exe -O` optimizer: what it does and how fast it gets.
- **[METAPROGRAMMING.md](METAPROGRAMMING.md)** — language-mutation coverage: custom operators, precedence traits, phasers, MOP, macros/slangs.
- **[history/JOURNEY.md](history/JOURNEY.md)** — a memoir of how this was built.

## License

[Artistic License 2.0](LICENSE) — the same license Raku itself uses.
