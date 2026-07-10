# Raku++ — Guide

*The full guide. For a 30-second overview and quick start, see [README.md](README.md).*

A from-scratch implementation of the [Raku](https://raku.org) programming
language in **C++17, with no third-party dependencies**. Raku++ is a
tree-walking interpreter today, designed to grow a compiler backend later.

## What this is

Raku++ (`rakupp`) is an independent Raku language engine: a hand-written
lexer, parser, and evaluator that runs real Raku programs — classes, roles,
grammars, regexes, multi-dispatch, junctions, lazy sequences, an
arbitrary-precision number tower, and Unicode-correct strings. It is not a
fork of Rakudo and shares no code with it; it targets the *language*, not any
particular runtime.

## Why & the goal

Raku is a large, expressive language, and its only mature implementation is
Rakudo (on the MoarVM/JVM backends). Raku++ exists to answer a simple
question: **how far can a clean, dependency-free C++ implementation get on its
own?** — and, in doing so, to provide a small, readable, embeddable Raku engine
that starts fast and is easy to build anywhere a C++17 compiler runs.

The correctness target is [**Roast**](https://github.com/Raku/roast), the
official Raku test suite. The guiding motto:

> **Any compiler that can run Roast can be officially called a Raku compiler.**

So the goal is not to reimplement Rakudo's internals or its private helpers,
but to implement the Raku *specification* well enough that Roast passes. Every
feature is driven by a failing Roast test, and progress is measured in Roast
files fully passing and Roast assertions passing — not in lines of code.

The long-term goal, in order:

1. **Correctness** — pass as much of Roast as an interpreter reasonably can.
2. **Self-hosting tooling** — the test harness itself is written in Raku and
   run *by Raku++* (see below).
3. **A compiler backend** — once the language is correct, compile the
   statically-compilable subset.

## Status

Raku++ has grown well beyond its initial MVP, but is still an early-stage
implementation. Against the full Roast suite of **1,464 `.t` files**:

| Files | Count | Share of suite |
|---|---:|---:|
| **Fully passing** | **307** | **21%** |
| Partially passing | 614 | 42% |
| No TAP output (parse error / unimplemented) | 542 | 37% |
| Timeouts | 3 | 0.2% |

Two numbers describe where Raku++ stands, and they measure different things:

- **Per-test — ~57% of all declared tests pass (131,732 / ~231,098).** This is the
  headline: the honest per-test figure, counting every test the suite declares —
  including those in files that abort before running (their `plan N` is read from
  source, all failing), so parse-error files can't hide. One subsystem (S15,
  Unicode) is ~91k of the total. Of just the tests that *do* run, ~69% pass
  (131,732 / 189,486) — that variant counts only assertions in files that produce
  TAP, so it flatters by ignoring the ~31k tests in aborting files.
- **Coverage — 307 / 1,464 files fully pass (~21%).** The stricter all-or-nothing
  bar: a file counts only if every assertion passes. Over a third of the suite
  produces no TAP at all yet (a parse error or unimplemented construct aborts the
  file before any assertion runs).

Run the harness (below) for live numbers as features land. See
[ROADMAP.md](ROADMAP.md) for what's done and what's next,
[ROAST.md](ROAST.md) for a per-section breakdown, and
[COUNTING.md](COUNTING.md) for exactly how every figure is defined and computed.

## Beyond Roast: running a real application

Passing spec tests is one bar; running a real, multi-module program is another.
The standing goal is to run [**covid.observer**](https://github.com/ash/covid.observer)
— a substantial Raku web-stats generator (a dozen `CovidObserver::*` modules, a
MySQL-backed data layer via `DBIish`, HTML/JS chart generation) — unmodified on
Raku++.

Current results:

- **The whole codebase compiles and loads** — every module, including `DBIish`.
- **`MAIN` multi-dispatch works** — the command-line usage and subcommand
  routing behave correctly.
- **The database layer works** — the `generate` subcommand runs through all the
  `DBIish` + MySQL queries (`get-countries`, `get-per-day-stats`,
  `get-vaccinations`, …) without error.
- **It reaches HTML/chart generation** and currently stops at the first missing
  built-in string method (`.trans`). Each such gap is a small, well-scoped
  addition; getting here already exercised `now`, `qw//` word lists, `flatmap`,
  and cross-module symbol resolution across a large codebase.

This is a moving target tracked as features land — it is not passing end-to-end
yet, but it drives real-world hardening that Roast alone doesn't.

## Unicode

Raku defines strings as sequences of *graphemes*, and getting that right is one
of Raku++'s strongest areas — the Unicode synopsis (S15) is its highest
assertion coverage, ~95% passing. Everything here is driven by tables generated
from **Unicode 15.1**:

- **Normalization** — NFC / NFD / NFKC / NFKD (canonical and compatibility,
  including algorithmic Hangul composition), plus the `Uni` type.
- **Grapheme-correct `.chars`** — UAX #29 grapheme clustering, so emoji,
  regional-indicator flags (🇦🇺), ZWJ sequences, and combining marks each count
  as a single character.
- **Properties in regex and code** — general category and script:
  `/<:Nd>/`, `/<:L>/`, `/<:Latin>/`, negation `/<:!Nd>/`, and
  `"a".uniprop('Script')`.
- **Names and numeric values** — `"\c[LATIN SMALL LETTER A]"`, `.uniname`,
  `.unival` / `.univals`.

**Gaps:** binary properties, exact per-codepoint Script data (scripts are
currently approximated by Unicode block), and the full NFG grapheme-break test
battery.

## Documentation

- **[OVERVIEW.md](OVERVIEW.md)** — a one-page tour: what Raku++ is, goals, capabilities, comparison to Rakudo.
- **[FEATURES.md](FEATURES.md)** — inventory of supported language features, by theme.
- **[EXAMPLES.md](EXAMPLES.md)** — a cookbook of runnable snippets, each verified against `rakupp`.
- **[ASYNC.md](ASYNC.md)** — concurrency & async cookbook: promises, supplies, channels, threads, and the two execution modes (GIL by default, opt-in true CPU parallelism via `RAKUPP_PARALLEL`).
- **[ARCHITECTURE.md](ARCHITECTURE.md)** — how it's built, and what happens to a program in each run mode.
- **[ROADMAP.md](ROADMAP.md)** — done / in-progress / next.
- **[ROAST.md](ROAST.md)** — Roast suite overview and per-section statistics.
- **[COUNTING.md](COUNTING.md)** — authoritative definition of the pass-rate figures.
- **[BENCHMARKS.md](BENCHMARKS.md)** — a fair speed comparison with Rakudo on the shared subset.
- **[OPTIMIZATION.md](OPTIMIZATION.md)** — the `--exe -O` optimizer: passes, C++ level forwarding, numbers.
- **[docs/JOURNEY.md](docs/JOURNEY.md)** — a memoir of how this was built: the Roast / real-project / docs loops, the clean-room stance on Rakudo, and reaching `--exe`.

## Building

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

This produces `build/rakupp`. (Re-run the `cmake -S . -B build` configure step
after adding new source files.)

## Running

```sh
build/rakupp path/to/program.raku     # run a file
build/rakupp -e 'say "hello"'         # run a one-liner
echo 'say 42' | build/rakupp          # run from stdin
build/rakupp -I lib program.raku      # add lib dirs to the module search path
```

### Command-line options

`rakupp --help` prints the full list; the summary:

| Option | Meaning |
|---|---|
| `FILE [ARGS…]` | Run a program from a file (extra args become `@*ARGS`) |
| `-e 'CODE' [ARGS…]` | Run a one-liner |
| *(no file)* | Read the program from standard input |
| `-I <path>`, `-I<path>` | Add a directory to the module search path (repeatable) |
| `--bundle SRC -o OUT` | Compile to a standalone binary: embed source + interpreter |
| `--aot SRC -o OUT` | Compile: parse ahead of time, embed the AST |
| `--exe SRC -o OUT` | Native-compile to C++ (fastest; falls back to bundling) |
| `--ast SRC` | Print the parsed AST as an indented tree |
| `--cpp SRC` | Print the C++ that `--exe` transpiles to |
| `--highlight [SRC]` | Syntax-highlight Raku — `--html` (default) or `--ansi`; a `pygmentize` drop-in |
| `--help`, `-h` | Show help |
| `--version`, `-V` | Show the version |

The compile modes (`--bundle` / `--aot` / `--exe`) each accept `FILE` or `-e CODE`
plus `-o OUT` — see [Four ways to run a program](#four-ways-to-run-a-program) below.

| Environment variable | Meaning |
|---|---|
| `RAKULIB=dir1:dir2` | Extra module search dirs (like `-I`), colon-separated |
| `RAKUPP_PARALLEL=1` | True CPU parallelism for `start`/worker threads (default: GIL) — see [ASYNC.md](ASYNC.md#the-two-modes-gil-default-and-true-parallelism) |
| `RAKUPP_DUMPTOKENS=1` | Dump the lexer token stream before running |

`-I <path>` (or `-I<path>`, repeatable) prepends directories to the module
search path, so `use Foo` finds `<path>/Foo.rakumod` — the same as Rakudo's
`-I`. `RAKULIB` (colon-separated) does the same via the environment.

To inspect how a program parses, dump its AST as an indented text tree:

```sh
build/rakupp --ast program.raku
build/rakupp --ast -e 'say 2 + 2 * 3'
```

(`RAKUPP_DUMPTOKENS=1` similarly dumps the lexer's token stream.)

By default rakupp runs concurrency under a GIL (correct semantics, no CPU
parallelism for pure-Raku work). Set `RAKUPP_PARALLEL=1` to let `start`/worker
threads run interpreter code on all cores — CPU-bound fan-out scales ~3× on 8
cores, 0 Roast regressions. See [ASYNC.md](ASYNC.md#the-two-modes-gil-default-and-true-parallelism)
for the trade-offs (chiefly: guard your own shared mutable data with a `Lock`).

```sh
build/rakupp program.raku                 # GIL mode (default)
RAKUPP_PARALLEL=1 build/rakupp program.raku   # true CPU parallelism
```

## Four ways to run a program

Because Raku++ doesn't implement the grammar-mutating parts of Raku (custom
slangs, parse-time operator definitions, runtime grammar edits), the language it
*does* handle is static enough to compile ahead of time. So beyond interpreting,
it offers three ways to produce a standalone native executable:

```sh
build/rakupp program.raku                     # 1. interpret (default)
build/rakupp --bundle program.raku -o program # 2. bundle: embed source + interpreter
build/rakupp --aot    program.raku -o program # 3. AOT: parse ahead, embed the AST
build/rakupp --exe    program.raku -o program # 4. native compile: transpile to C++
./program arg1 arg2                            # (2)-(4) run on their own — no rakupp needed
```

Every mode also accepts `-e 'CODE'` in place of a file — e.g.
`build/rakupp --exe -e 'say [+] 1..100'` (compile modes default to `a.out`).

**2. Bundle (`--bundle`)** — embeds the program's **source** and links it against
the runtime static library (`librakupp_rt.a`). The binary re-lexes, parses and
tree-walks that source at every startup — it *is* the interpreter in a box. Works
for the **entire language**; the win is distribution (no `rakupp` needed), not
speed. (First launch of any fresh binary costs a one-time ~0.4 s of OS validation
on macOS; later runs start in ~10 ms.)

**3. AOT (`--aot`)** — genuinely ahead-of-time: it **parses the program at build
time** and emits C++ that rebuilds that exact AST at startup, then interprets the
embedded tree — so the binary does **no lexing or parsing** at run time, and
**parse errors are caught at build time**. Also works for the **whole language**
(grammars included), since the interpreter runs the embedded tree. It still
tree-walks, so it runs at the same speed as `--bundle`. It emits one C++ builder
per AST node, so **build time and the generated file grow with program size**
(a few hundred lines of Raku → tens of thousands of lines of C++); for anything
large, prefer `--bundle`. `--aot`'s only edge over `--bundle` is build-time parse
checking — pick `--bundle` for fast builds, `--exe` for fast execution.

**4. Native compile (`--exe`)** — transpiles the program's AST to C++ that
implements it directly (calling the runtime only for `Value` semantics), then
compiles that to native code. **No interpreter inside** — real ahead-of-time
compilation, so hot code (loops, recursion, arithmetic) runs several times
faster (e.g. `fib` is ~3× the interpreter's speed, level with Rakudo).

`--exe` handles the **whole supported language**: it native-compiles nearly
everything and, for the few remaining constructs, transparently falls back to
bundling that program with the interpreter (mode 2) so it always produces a
correct binary. Compiled natively:

- scalars, `@`-arrays and `%`-hashes, indexing and autovivification;
- arithmetic / string / comparison / logical / chained-comparison operators,
  ranges, reductions (`[+]`), postfix `++`/`--`, ternary, string interpolation;
- `if`/`unless`/`while`/`until`/`loop`/`repeat`, `for` over ranges and lists
  (incl. multiple loop vars `-> $x, $y`), `given`/`when`/`default`,
  `with`/`without`, `if EXPR -> $x`, list-destructuring `my ($a, $b) = …`,
  `:=` binding, `enum`s;
- subroutines and calls with **full signatures** — positional, named `:$x`,
  optional/default, slurpy `*@`/`*%` — **`multi` dispatch** (by type & arity),
  `&sub` references, closures / pointy blocks, `WhateverCode` (`* + 1`,
  `*.method`, `~*`, `@a[*-1]`), user operators (`sub postfix:<!>` → `5!`);
- **classes** — attributes (`$.`/`$!`, incl. `@`/`%` attributes), defaults,
  methods, `self`, accessors, single inheritance;
- method calls (so the whole method library — `.map`, `.grep`, `.sort`,
  `.subst`, `.comb`, string/Unicode methods, …), `say`/`print`/`put`,
  regex match & substitution, `@*ARGS`, type objects (any bareword), junctions,
  and deterministic dynamic vars (`$*CWD`, `$*DISTRO`, `$*OUT`, `$*RAKU`, …);
- **concurrency** — `Promise`/`start`/`await`, `Supply`, `Supplier`,
  `react`/`whenever`/`supply` (the synchronous model);
- `do`, `try`, `gather`/`take`, `EVAL`, phasers
  (`BEGIN`/`INIT`/`ENTER`/`LEAVE`/`END`), and `CATCH` blocks (`when`/`default`).

The main remaining fallback is **grammars** — where an interpreter-in-a-box is
the right tool (they *are* the grammar engine). A few other constructs still
bundle: `where`-constrained parameters, `multi` *methods*, index adverbs
(`:exists`/`:delete`), state-dependent dynamic vars (`$*EXECUTABLE`, `$*PROGRAM`),
and `CATCH` inside a sub body. Fallback is always safe — it produces a correct
binary that bundles the interpreter. See [ROADMAP.md](ROADMAP.md).

## Measuring against Roast

The test harness is **written in Raku and run by Raku++ itself** — a
self-hosting milestone: the compiler is complete enough to run its own
spec-suite runner.

Point it at a [Roast](https://github.com/Raku/roast) checkout with the `ROAST`
environment variable (it defaults to `/Users/ash/roast` if unset):

```sh
export ROAST=/path/to/roast
build/rakupp tools/run-roast.raku             # run the whole suite, print a summary
build/rakupp tools/run-roast.raku S03-operators   # only paths matching a substring
build/rakupp tools/run-roast.raku S04             # filter by path substring
```

The harness runs every file, parses its TAP output, and classifies it as
fully-pass / partial / no-TAP / timeout, reporting aggregate assertion counts.
It streams a per-file line as each test finishes.

## Architecture

```
src/
  Token.h / Lexer.*      Hand-written lexer (Raku needs a custom one).
  Ast.h                  AST node definitions (one NK enum, dispatched in the interpreter).
  Parser.*               Recursive-descent statements + a Pratt expression core.
  Value.*                Runtime value type and coercions (incl. BigInt/Rat number tower).
  Interpreter.*          Tree-walking evaluator, scopes, calls, control flow.
  Builtins.cpp           Named builtins, the Test module (TAP), and method dispatch.
  Regex.*                Regex/grammar engine (recursive-descent + backtracking matcher).
  Unicode.* / unicode_gen.cpp   Normalization, grapheme segmentation, properties (UCD 15.1).
  Runtime.*              Shared entry point (parse + interpret); the static library.
  Codegen.*              Native backend: transpiles the AST to C++ (`--exe`).
  main.cpp               CLI entry point (interpret, `-e`, `--aot`, `--exe`).
```

The runtime (everything except `main.cpp`) builds into a static library,
`librakupp_rt.a`, which the `rakupp` CLI and every `--bundle`/`--aot`/`--exe`
binary link against.

For a detailed walk-through — including exactly what happens to a source program
in each run mode, with a real transpilation example — see
**[ARCHITECTURE.md](ARCHITECTURE.md)**.

**Why interpreter-first:** Raku has features that resist static compilation
(`EVAL`, runtime grammars, `BEGIN`-time code), so the reference implementation
(Rakudo) is interpreter/VM based. We follow the same path: get the language
correct under Roast first, then add compiler back ends for the statically
compilable subset — [bundling, ahead-of-time AST embedding, and native
transpilation](#four-ways-to-run-a-program) have all landed; widening the
native-codegen subset toward the whole language is the
next direction.

## License

[Artistic License 2.0](LICENSE) — the same license Raku itself uses.
