# Raku++ — Overview

*A tour of what Raku++ is, what it can do, and how it compares — one level up
from the [README](README.md), one level down from the [full guide](GUIDE.md).*

---

## What it is

**Raku++ is a from-scratch implementation of the [Raku](https://raku.org)
programming language, written in C++17 with no third-party dependencies.** It is
a hand-written lexer, recursive-descent/Pratt parser, and tree-walking evaluator
that runs real Raku — classes, roles, grammars, regexes, multi-dispatch,
junctions, lazy sequences, an arbitrary-precision number tower, and
Unicode-correct strings — and can additionally **compile** a program to a
standalone native binary.

It is **not a fork of Rakudo** and shares no code with it. It targets the Raku
*language*, measured against [**Roast**](https://github.com/Raku/roast), the
official specification test suite. The guiding motto:

> *Any compiler that can run Roast can be officially called a Raku compiler.*

## At a glance

| | |
|---|---|
| **Language** | Raku, defaulting to 6.d (with 6.e features available) |
| **Written in** | C++17, zero third-party dependencies |
| **Size** | a hand-written front end + a `Value`-based runtime, all in `src/` |
| **Runs as** | an interpreter **and** an ahead-of-time / native compiler |
| **Startup** | ~3 ms cold (vs Rakudo's ~100 ms) |
| **Correctness target** | the Roast suite — ~20% of files fully pass today |
| **Not** | a Rakudo fork, a transpiler-to-something-else, or feature-complete |

## Goals & philosophy

1. **Correctness first.** Every feature is driven by a failing Roast test.
   Progress is measured in Roast files passing, not lines of code.
2. **Clean-room and dependency-free.** A small, readable, embeddable engine that
   builds anywhere a C++17 compiler runs — no Rakudo internals, no libraries.
3. **Interpreter-first, then compile.** Get the dynamic language correct under a
   tree-walker, then compile the statically-analysable subset to native code
   (the parts that stay dynamic — grammars — stay interpreted).
4. **Honest reporting.** Numbers are raw and current; limitations are stated
   plainly (see the per-doc "Gaps" notes).

## What it can do

A high-level inventory — the full list is in [FEATURES.md](FEATURES.md), with
runnable snippets in [EXAMPLES.md](EXAMPLES.md).

- **Core language** — scalars/arrays/hashes, control flow, `given`/`when`,
  statement modifiers, closures, phasers (`BEGIN`/`END`/`ENTER`/`LEAVE`/`CATCH`).
- **Numbers** — an exact tower: arbitrary-precision `Int` (hand-rolled bignum),
  exact `Rat`, `Num`, `Complex`. `0.1 + 0.2 - 0.3` is exactly `0`.
- **Objects & MOP** — `class`/`role`/`grammar`, multi-method dispatch, `BUILD`,
  enums, `.^name`/`.^methods`/`.^mro`, `augment` (on user *and* built-in types),
  `does`/`but` mixins, routine `.wrap`.
- **Operators & metaprogramming** — all six user-operator categories
  (`sub infix:<…>` … `postcircumfix`), working precedence traits, meta-operators
  over user ops, function composition, feeds. (See
  [METAPROGRAMMING.md](METAPROGRAMMING.md).)
- **Regexes & grammars** — a CPS backtracking engine, `token`/`rule`/`regex`,
  `.parse`/actions, substitution, Unicode property classes.
- **Signatures & dispatch** — `multi`/`proto`, `where`/type/literal constraints,
  sub-signature destructuring, `callsame`/`nextsame`, coercion-type params.
- **Unicode** — NFC/NFD/NFKC/NFKD, grapheme-correct `.chars` (UAX #29), names and
  numeric values, category/script properties — from generated UCD 15.1 tables.
- **Concurrency** — real `std::thread`s under a CPython-style GIL: promises,
  `Supply`/`react`/`whenever`, `Channel`, `Thread`, `Lock`, `atomicint`. Opt into
  true CPU parallelism with `RAKUPP_PARALLEL=1`. (See [ASYNC.md](ASYNC.md).)
- **I/O & system** — files, `IO::Path`, `run`/`shell` subprocesses, and a
  **NativeCall** C FFI (`is native` via `dlsym`) covering libc + `<math.h>`
  (integer and floating-point scalars, no `libffi`).
- **Tooling** — a parse-aware syntax highlighter (`--highlight`, HTML + ANSI) and
  a self-hosted Roast harness written in Raku and run *by* Raku++.

**Not there yet:** macros / `RakuAST` / slangs, `libffi`-grade NativeCall
(structs / callbacks / mixed int+float signatures), some `IO`/`POD` corners, and
true lock-free parallel atomics.

## Four ways to run a program

One front end, four back ends (details in [ARCHITECTURE.md](ARCHITECTURE.md)):

| Mode | Command | What you get |
|---|---|---|
| **Interpret** | `rakupp prog.raku` | tree-walk the AST — the default; handles the whole language |
| **Bundle** | `rakupp --bundle prog.raku -o prog` | standalone binary embedding the source; re-parses at startup |
| **AOT** | `rakupp --aot prog.raku -o prog` | binary embedding the *parsed AST*; no run-time parsing |
| **Native** | `rakupp --exe prog.raku -o prog` | transpile the AST to C++ and compile it — no interpreter inside |

`--exe -O` turns on the code generator's optimizer (see
[OPTIMIZATION.md](OPTIMIZATION.md)). Any construct the native compiler can't yet
transpile (mainly grammars) transparently falls back to bundling, so `--exe`
never refuses a program.

## How it compares to Rakudo

Rakudo is the mature, reference Raku implementation (on MoarVM/JVM). Raku++ is a
young, independent engine with a different set of trade-offs — **not** a claim to
be "better."

| | Raku++ | Rakudo |
|---|---|---|
| Maturity | early-stage (~20% of Roast) | the reference implementation |
| Implementation | tree-walker + native compiler, C++17 | VM-based (MoarVM/JVM), NQP/Raku |
| Dependencies | none | a toolchain + runtime |
| Cold startup | ~3 ms | ~100 ms |
| Native compile | yes (`--exe`) — **beats Rakudo on every benchmark**, 2.4×–17× | JIT at run time |
| Interpreted speed | slower on hot loops | faster |
| Grammar-mutation (macros/slangs) | not implemented | full |
| Coverage | growing, test-first | complete |

The honest summary: **Raku++ starts instantly and its compiled binaries are
fast**, while **Rakudo is complete and faster in pure interpreted mode**. See
[BENCHMARKS.md](BENCHMARKS.md) for the numbers and methodology.

## Status & how it's measured

The one number to quote is **Roast coverage: ~20% of files fully pass** (currently
294 / 1,464). Separately, among files that *do* run, ~131k of ~189k assertions
pass — a correctness signal on the attempted subset, not a coverage figure. The
two mean different things and are explained in [ROAST.md](ROAST.md), which also
has the per-synopsis breakdown. Run the self-hosted harness for live numbers:

```sh
build/rakupp tools/run-roast.raku          # whole suite
build/rakupp tools/run-roast.raku S05      # filter by path
```

## Where to go next

- **[README.md](README.md)** — install and first run.
- **[GUIDE.md](GUIDE.md)** — the full guide: goals, status, run modes, real-app hardening.
- **[FEATURES.md](FEATURES.md)** / **[EXAMPLES.md](EXAMPLES.md)** — what works, with runnable snippets.
- **[ARCHITECTURE.md](ARCHITECTURE.md)** — how it's built and what each run mode does.
- **[ROAST.md](ROAST.md)** — coverage, per-synopsis stats, and how the numbers are defined.
- **[BENCHMARKS.md](BENCHMARKS.md)** / **[OPTIMIZATION.md](OPTIMIZATION.md)** — speed vs Rakudo; the `-O` optimizer.
- **[ASYNC.md](ASYNC.md)** / **[METAPROGRAMMING.md](METAPROGRAMMING.md)** — concurrency; language-mutation coverage.
- **[ROADMAP.md](ROADMAP.md)** — done / next.
- **[history/JOURNEY.md](history/JOURNEY.md)** — a memoir of how this was built.
