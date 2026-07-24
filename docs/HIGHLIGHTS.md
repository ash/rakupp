# Raku++ — Highlights

*The key features on one page, in bullets. More detail: [OVERVIEW.md](OVERVIEW.md)
(prose tour) → [GUIDE.md](GUIDE.md) (full picture) → [FEATURES.md](FEATURES.md)
(exhaustive inventory).*

## What it is

- A **from-scratch implementation of [Raku](https://raku.org)** — not a Rakudo
  fork, shares no code with it.
- **C++17, zero third-party dependencies** — hand-written lexer, parser,
  evaluator, bignum, Unicode tables, regex engine. Builds anywhere CMake and a
  C++17 compiler run.
- Both an **interpreter and a compiler**: the same binary tree-walks a script
  or compiles it to a standalone native executable.
- Measured against **[Roast](https://github.com/Raku/roast)**, the official
  Raku specification suite: **~90% of all declared tests pass**
  (194,901 / 216,222); **~41% of files fully pass** (598 / 1,462).
  Definitions and caveats: [ROAST.md](ROAST.md), [COUNTING.md](COUNTING.md).

## Language

- **Exact numbers** — arbitrary-precision `Int`, exact `Rat` (`0.1 + 0.2 - 0.3`
  is exactly `0`), `Num`, `Complex`, allomorphs.
- **Objects** — `class` / `role` / `grammar`, multi-method dispatch, `BUILD` /
  `TWEAK`, enums, `does` / `but` mixins, `augment` (even on built-in types),
  `.^name` / `.^methods` / `.^mro` introspection, routine `.wrap`.
- **Regexes & grammars** — a backtracking engine with Unicode property
  classes, named captures, `token` / `rule` / `regex`, `.parse` with action
  classes and `make` / `.made`.
- **Signatures** — `multi` / `proto`, type / literal / `where` constraints,
  named & slurpy params, sub-signature destructuring, coercion types
  (`Str(Cool)`), `callsame` / `nextsame`.
- **Functional** — closures, currying with `*` (WhateverCode), junctions
  (`any` / `all` / `one`), lazy sequences (`1, 2, * + * ... Inf`),
  `gather` / `take`, feeds and function composition.
- **Metaprogramming** — user-defined operators in all six categories
  (`sub infix:<...>` through `postcircumfix`), working precedence traits,
  meta-operators over user ops ([METAPROGRAMMING.md](METAPROGRAMMING.md)).
- **Concurrency** — real OS threads: `start` / `await`, `Supply` / `react` /
  `whenever`, `Channel`, `Lock`, atomics; opt-in true CPU parallelism with
  `RAKUPP_PARALLEL=1` ([ASYNC.md](ASYNC.md)).
- **Unicode** — grapheme-correct strings (UAX #29 incl. emoji ZWJ), NFC/NFD/
  NFKC/NFKD, UCA collation (`unicmp`), names, properties — generated
  UCD/UCA 17.0 tables ([UNICODE.md](UNICODE.md)).
- **Phasers & control** — `BEGIN` / `END` / `ENTER` / `LEAVE` / `FIRST` /
  `NEXT` / `LAST` / `CATCH` / `CONTROL`, labeled loops, `temp` / `let`.
- **System & FFI** — files and `IO::Path`, `run` / `shell` subprocesses,
  NativeCall C FFI (`is native`) for libc / `<math.h>` without libffi.

## Four ways to run a program

- `rakupp prog.raku` — **interpret** (the default; covers the whole language).
- `rakupp --bundle prog.raku -o prog` — standalone binary, re-parses at startup.
- `rakupp --aot prog.raku -o prog` — standalone binary embedding the parsed AST.
- `rakupp --exe prog.raku -o prog` — **transpile to C++ and compile native**;
  anything unsupported falls back to `--bundle` automatically
  ([ARCHITECTURE.md](ARCHITECTURE.md), [NATIVE.md](NATIVE.md)).

…and a fifth: **in the browser**. As **[Raku.js](../rakujs/)** the interpreter
compiles to **WebAssembly** — same semantics as native, running client-side with
no server, with an embeddable in-page playground.

## Speed

- **~2 ms cold start** — a tiny native binary with no VM to spin up, fast enough
  to shell out to in a loop.
- Competitive interpreter performance and native-compiled hot code; raw
  numbers and methodology in [BENCHMARKS.md](BENCHMARKS.md) and
  [OPTIMIZATION.md](OPTIMIZATION.md).

## Platforms

- **macOS** (one universal binary: Apple Silicon + Intel), **Linux** x86_64
  (static libstdc++ — no runtime deps), **Windows** x64 (static CRT — no
  redistributable needed).
- Prebuilt archives on [GitHub Releases](https://github.com/ash/rakupp/releases);
  macOS also via `brew install ash/rakupp/rakupp`.
- CI builds and smoke-tests all three on every push.
- **The browser**, too — compiled to WebAssembly as **[Raku.js](../rakujs/)**
  (no server; embeddable playground).

## Tooling

- A **self-hosted Roast harness** — written in Raku, run by Raku++ itself
  (full 1,464-file suite in ~3½ minutes, [DOGFOODING.md](DOGFOODING.md)).
- A parse-aware **syntax highlighter** (`--highlight`, HTML + ANSI).
- `--doc` (POD rendering), `-c` (parse-only check), `--cpp` (show generated C++).

## Examples & real programs

- **[examples/](../examples/)** — 24 complete programs (Mandelbrot, Game of
  Life, a JSON grammar, a calculator, an echo server, a quine, …); every one
  also compiles natively with `--exe`, with byte-identical output
  ([NATIVE.md](NATIVE.md)).
- **[showcase/](../showcase/)** — eight mid-size programs, each leaning on a
  different part of the language: a **Scheme** interpreter and a **Forth**, both
  on Raku grammars; a **Markdown→HTML** converter and a **JSON** parser/formatter;
  and four servers on raw sockets — a **pastebin**, a concurrent **chat** server,
  a **key-value** store with its own protocol, and a static-file HTTP server
  (`rakus`). The Markdown and JSON showcases also run **in the browser** (with a
  regex/grammar explorer) via WebAssembly ([showcase/web/](../showcase/web/)).
- Real applications run **unmodified**: the
  [raku-course](https://github.com/ash/raku-course) static-site generator
  regenerates the entire ~1500-page Raku course byte-for-byte identically to
  Rakudo (including the real zef-installed `YAMLish` module), and the
  [covid.observer](https://github.com/ash/covid.observer) site builder runs
  end-to-end — dogfooding beyond what Roast exercises
  ([DOGFOODING.md](DOGFOODING.md)).

## Not there yet

- Macros / `RakuAST` / slangs; `libffi`-grade NativeCall (structs, callbacks);
  some `IO` / POD corners; lock-free parallel atomics. The plan:
  [ROADMAP.md](ROADMAP.md).
