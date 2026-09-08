# Raku++ Architecture

This document explains how Raku++ is built and, in particular, **what happens to
a source program** as it flows through the compiler in each of its run modes
(interpret, bundle, AOT, native compile).

For *what* the language supports see [FEATURES.md](../guide/FEATURES.md); for *how fast*
it runs see [BENCHMARKS.md](../status/BENCHMARKS.md) and, for the `--exe -O` code
generator's optimizer, [OPTIMIZATION.md](OPTIMIZATION.md). Language-mutation
(custom operators, precedence traits, phasers, the MOP) is in
[METAPROGRAMMING.md](METAPROGRAMMING.md), and the concurrency/async model in
[ASYNC.md](../guide/ASYNC.md). This file is about the *how*.

---

## 1. The big picture

Raku++ is a hand-written **C++17** implementation with no third-party
dependencies. Source text goes through a classic front end and then splits by
mode:

```
  source ─► Lexer ─► Parser ─► AST ─► DeclCheck ─┬─ interpret ─► tree-walk (Interpreter)
                                        (gate)   ├─ --aot ─────► AstEmit (rebuild AST) ─► cc ─► binary (walks embedded tree)
                                                 └─ --exe ─────► Codegen (AST → C++)    ─► cc ─► binary (native, no interpreter)

  source ─────────────────────────────── --bundle ───► embed source bytes            ─► cc ─► binary (parses at run time)
```

Everything except the CLI entry point is compiled into a **static library,
`librakupp_rt.a`** ("the runtime"). Both the `rakupp` executable and the
programs produced by `--bundle`/`--aot`/`--exe` link against it — so the interpreter and
compiled programs share one implementation of `Value` semantics, built-ins,
method dispatch, regexes and Unicode.

There's effectively a fifth target: that same runtime compiled to **WebAssembly**
is **[Raku.js](../../rakujs/README.md)**, the interpreter running in the browser with
no server. (The value/execution model the runtime implements is in
[RUNTIME.md](RUNTIME.md); the source→AST front end in [PARSING.md](PARSING.md).)

---

## 2. The front end (shared by every mode)

> This section is the overview. For the full front-end model — the lexer's
> context-sensitivity (regex vs. division, `spaceBefore`), the Pratt expression
> parser, and how user-defined operators are parsed in a single pass — see
> **[PARSING.md](PARSING.md)**.

### Lexer — `src/Lexer.{h,cpp}`, `src/Token.h`

A hand-written tokenizer (Raku's grammar is too irregular for a generated one).
It produces a flat `std::vector<Token>`. It carries Raku-specific state that a
naive lexer can't: `Token::spaceBefore` (whitespace significance — `f()` vs
`f ()`, postcircumfix vs block), quote forms (`q//`, `qq//`, `qw<>`, heredocs),
regex-vs-division context, `«…»`/`｢…｣` brackets, and Unicode identifiers.

### Parser — `src/Parser.{h,cpp}`, `src/Ast.h`

Recursive-descent for statements, a **Pratt (precedence-climbing) core** for
expressions. It builds the AST: a tree of `Node`s tagged by a single `NK` enum
(`IntLit`, `Binary`, `Call`, `IfStmt`, `SubDecl`, `ClassDecl`, …). The top-level
result is a `Program` (a `vector<StmtPtr>`). String interpolation is itself
parsed here (`parseInterpString`) into an `InterpStr` node whose parts are
sub-expressions.

The AST is deliberately simple and uniform — one `NK` switch drives both the
interpreter's `eval`/`exec` and the code generator.

You can see both stages directly: `RAKUPP_DUMPTOKENS=1 rakupp prog.raku` prints
the token stream, and `rakupp --ast prog.raku` prints the parsed AST as an
indented text tree (`src/AstDump.cpp`).

### Declaration check — `src/DeclCheck.{h,cpp}`

The one pass that sits between the parser and every mode. It asks whether each
variable the unit mentions is declared somewhere in its lexical chain, and a
program that fails is **refused before it starts** — the same answer the
interpreter's `X::Undeclared` used to give when execution happened to reach the
reference, moved to where a compiler gives it. It runs on the interpreter path,
`-c`, `--cpp` and the three compile modes; the REPL (each line is its own unit)
and the embedding API's `rk_run` (whose interpreter may already hold globals the
host installed) are exempt, and `RAKUPP_NO_DECLCHECK=1` turns it off.

It is built to be one-sided. An AST walk with position-insensitive lexical
scoping produces candidates; a scan of the source text then drops any candidate
the text spells as a declaration anywhere, which covers the binders the parser
does not keep (`repeat … -> $x`, a destructuring `with … -> ($x)`) and the
pseudo-package qualifier `stripPseudoPkg` erases from `$OUR::x`. `EVAL`, a
symbolic reference, `require` and an unresolvable import each end the check for
the whole unit; `no strict` suppresses it lexically instead — the flag rides the
pass's scope stack, so it is restored where the scope ends, and `findLaxVars`
hands the names it covered to the native backend, which has no local to emit for
them. The exemption list is not re-implemented: it calls
`isSpecialVar` in `Interpreter.cpp`, the same predicate the throw sites use, so
the static answer cannot drift from the runtime one. The full account is in the
book, [Chapter 39](../book/ch/38-tooling.md); the user-facing description is in
[../guide/CLI.md](../guide/CLI.md).

---

## 3. The runtime library — `librakupp_rt.a`

This is the heart. Key pieces:

| File | Role |
|---|---|
| `Value.{h,cpp}` | The universal runtime value (see below), coercions, gist/Str. |
| `BigInt.{h,cpp}` | Arbitrary-precision integers (base-1e9) for the exact number tower. |
| `Interpreter.{h,cpp}` | Tree-walking evaluator: `eval`/`exec`, scopes, calls, dispatch, `applyArith`, codegen helpers — plus module loading (`loadModule`: each `use`d module is lexed, parsed to its own AST and executed once, in every mode including `--exe`; see [MODULE-LOADING.md](MODULE-LOADING.md)) and the concurrency runtime (thread-local execution registers, interpreter compute on all cores by default since v3, and a CPython-style GIL kept as the `RAKUPP_GIL=1` escape hatch; see [ASYNC.md](../guide/ASYNC.md)). |
| `Builtins.cpp` | Named built-ins, the `Test` module (TAP), and the head of the method dispatcher `methodCallInner`. |
| `MethodCallPart2/3/Tail.cpp` | The rest of that dispatch chain. **Ordered segments, not categories** — the chain is order-sensitive, so an arm belongs where its priority is, not where it reads nicely. Each returns `std::optional<Value>`; `nullopt` means "not handled here". |
| `MethodCallSegment.h` | The common include prologue those segments share, so one cannot drift from its siblings. |
| `BuiltinsShared.h` | Helpers that were file-static in `Builtins.cpp` until the split needed them in more than one translation unit. Internal to `src/`. |
| `MethodName.h` | `MName` — the method name with its length and first eight bytes cached, so most of the several-hundred literal comparisons in the chain are an integer compare. |
| `Regex.{h,cpp}` | Recursive-descent regex/grammar engine with a backtracking matcher. |
| `Unicode.*`, `unicode_*_gen.cpp`, `unicode_names.cpp` | The Unicode subsystems: UAX #29 grapheme segmentation, NFC/NFD/NFKC/NFKD normalization, UCA collation (DUCET), names, categories, scripts, properties. All tables generated from pinned UCD/UCA **17.0** data in `tools/ucd/` — the names/categories generator is written in Raku and run by rakupp itself (see [UNICODE.md](../guide/UNICODE.md) and [DOGFOODING.md](../status/DOGFOODING.md)). |
| `IOSpec.cpp` | `IO::Spec::*` path semantics (Unix/Win32). |
| `Highlight.{h,cpp}` | The parse-aware syntax highlighter behind `--highlight` (HTML + ANSI). |
| `Runtime.{h,cpp}` | Shared entry points: `rakuppRun` (lex+parse+interpret) and `rakuppRunProgram` (interpret a prebuilt AST, for `--aot`). |
| `Codegen.*` | `--exe` back end: transpiles the AST to native C++ (with an optional `-O` optimizer — see [OPTIMIZATION.md](OPTIMIZATION.md)). |
| `codegen/Js.*`, `js-rt/` | `--target=js` back end: transpiles to JavaScript over a hand-written runtime. |
| `AstEmit.cpp` | `--aot` back end: serializes the AST and emits C++ carrying the bytes. |
| `AstDump.cpp` | `--ast` AST printer. |
| `main.cpp` | CLI: the run modes, the compile drivers (`--bundle`, `--aot`, `--exe`, `--target=js`) and the tooling subcommands. |

### The value model, and where it is described

Every Raku value at runtime is one `Value` struct with a `VT` tag; a lexical
scope is an `Env`; a class is a `ClassInfo` and a routine a `Callable`. Those
four types are the runtime, and describing them is a chapter's work rather than
a paragraph's:

- **[Chapter 8, `Value`](../book/ch/08-value.md)** — the tag, the payload slot,
  and why the struct's size is a recurring subject.
- **[Chapter 12, Containers](../book/ch/12-containers.md)** and
  **[Chapter 14, Calls](../book/ch/14-calls.md)** — `Env`, lexical pads and the
  frame pool.
- **[Chapter 17, Objects](../book/ch/17-objects.md)** — `ClassInfo`, roles and
  the metamodel.

This file used to carry a miniature of all four. It is gone for the same reason
§4's walk-through is: the miniature described `Value` as it was before the
payload slots collapsed into one tagged member, and `Env` as it was before
lexical pads, and neither was visible from here as wrong.

---

## 4. What happens to a program — by mode

The same program through each of the five back ends — interpret, `--bundle`,
`--aot`, `--exe` and `--target=js` — with the generated code for each, is the
Internals book's Part VII. It is not repeated here:

- **[Chapter 25, Four Ways to Run a Program](../book/ch/25-run-modes.md)** —
  the walk-through, and what each mode produces.
- **[Chapter 26, The Code Generator](../book/ch/26-codegen.md)** and
  **[Chapter 27](../book/ch/27-optimizer.md)** — `--exe` and `-O`.
- **[Chapter 30b, The JavaScript Back End](../book/ch/30b-codegen-js.md)** —
  `--target=js`.

This file used to carry its own copy of that walk-through, and it is worth
recording why it does not any more: the copy went stale in a way that was
invisible from here. It described `--aot` as emitting one builder function per
AST node — a design deleted in August 2026 for silently dropping fields — and its
`--exe` sample emitted a helper the generator had stopped calling. Both were
correct when written and neither was ever going to be found beside a chapter
saying the same thing. What follows from §5 is the material this file is *for*:
why the split exists, and what the build produces.

---
## 5. Why interpreter-first, and the compile split

Raku has genuinely dynamic features (`EVAL`, runtime grammars, `BEGIN`-time
code), so the reference implementation is VM-based and Raku++ started the same
way: get the language correct under Roast first. User-defined operators (`sub
infix:<…>` and friends, with precedence traits) *are* supported — they're
resolved during the single parse pass — but the deeper grammar-mutating layer
(macros, `RakuAST`, full slangs) is not (see
[METAPROGRAMMING.md](METAPROGRAMMING.md)). That keeps the language rakupp handles
static enough to compile ahead of time — which is what mode 3 exploits. The
remaining dynamic/heavy constructs (grammars) are exactly the ones that stay
bundled.

---

## 6. Build layout

```
CMakeLists.txt        # librakupp_rt (all of src/ except main.cpp) + the rakupp exe
src/main.cpp          # CLI: interpret / -e / --bundle / --aot / --exe + compile drivers
build/librakupp_rt.a  # the runtime, linked into rakupp AND into --bundle/--aot/--exe binaries
build/rakupp          # the CLI
tools/run-roast.raku  # the self-hosted Roast harness (run by rakupp)
tools/run-bench.raku  # the benchmark harness
tools/bench/*.raku    # benchmark programs
```

Re-run `cmake -S . -B build` after adding a source file (the glob is
`CONFIGURE_DEPENDS` but caches); then `cmake --build build`.
