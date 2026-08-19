\part{Ground}

# What Raku++ Is

Raku++ is a Raku implementation written from scratch in C++17. It has no
third-party dependencies: no parser generator, no regex library, no bignum
library, no ICU, no libffi at link time, no garbage collector. The binary it
produces is one file, and the same source builds it on macOS, Linux, the BSDs,
Windows, and — compiled to WebAssembly — inside a browser tab.

That constraint is not asceticism for its own sake. It has a specific
consequence which shapes almost every chapter of this book: **when something is
slow or wrong, the code responsible is in the tree**, and it can be changed. A
good deal of what follows is the story of doing exactly that.

## The pipeline

```
                            ┌─ interpret ─► tree-walk (Interpreter)
                            │
 source ─► Lexer ─► Parser ─┼─ --aot ─► AstEmit ─► cc ─► binary
                  (─► AST)  │              (rebuilds the tree, walks it)
                            │
                            └─ --exe ─► Codegen ─► cc ─► binary
                                           (native; no interpreter inside)

 source ─────────── --bundle ─► embed the bytes ─► cc ─► binary
                                   (parses and walks at run time)
```

A single front end feeds four back ends. The front end is a hand-written
character-level lexer and a recursive-descent parser with a precedence-climbing
core for expressions; it produces a `Program`, which is a vector of statement
nodes. Everything downstream consumes that one artifact.

Everything except the command-line driver is compiled into a static library,
**`librakupp_rt.a`** — "the runtime". The `rakupp` executable links against it,
and so does every binary the compiling modes produce. That is why a feature
implemented once behaves identically whether a program is interpreted or
natively compiled: there is one implementation of `Value` semantics, one set of
built-ins, one method dispatcher, one regex engine, one Unicode subsystem.

## The four modes, in one paragraph each

**Interpret** (the default) lexes, parses, and tree-walks. Startup is about two
milliseconds; throughput pays the per-node dispatch cost. This is the mode the
language is developed against, because it is the only one that can run every
construct.

**`--bundle`** embeds the source bytes in a generated C++ stub and links it
against the runtime. The result is standalone, but at run time it still lexes,
parses and tree-walks — it is the interpreter in a box. The win is
distribution, not speed.

**`--aot`** parses at build time and emits C++ that *rebuilds the identical
AST* at startup, then interprets it. Parse errors surface at build time rather
than run time; execution speed is the same as bundling, because it is the same
tree walk. It emits one builder function per AST node, so the generated code
grows with the program.

**`--exe`** parses at build time and transpiles the tree into C++ that
implements the program directly: native control flow, native calls, with the
runtime called only for value semantics. With `-O` it additionally runs its own
optimizer before the C++ compiler sees anything. Anything it cannot transpile
throws a `CodegenError`, which the driver catches and answers by bundling the
whole program instead — so `--exe` never refuses a program.

Chapter 25 works one small program through all four.

## What the design is optimised for

Three decisions dominate everything else, and each gets a full chapter later.

**One value type.** Every Raku value at runtime is the same C++ struct,
`Value`, carrying a one-byte tag and a field for every kind of payload. Not a
union, not a `std::variant`, not a class hierarchy — a *fat struct*. Chapter 8
argues the case, which is stronger than it first looks, and prices the memory
it costs.

**The C++ stack is the Raku stack.** There is no bytecode and no separate
operand stack. `eval(Expr*)` returns a `Value`; `exec(Stmt*)` runs a statement.
This makes the implementation small and makes native recursion depth the Raku
recursion budget, which is why the entry point runs the program on a thread
with a very large stack.

**Compile time runs nothing.** The parser executes no user code. `BEGIN`
becomes a tagged block that the interpreter schedules after the parse;
`constant` is not folded; `use` does not load anything during parsing. There is
exactly one parse-time side effect — registering a user-declared operator in the
parser's own tables — and Chapter 6 is about why that one is enough to support
custom operators with real precedence in a single forward pass, and why it is
also the reason macros and slangs are not supported.

## What it deliberately is not

It is not the reference implementation. Rakudo is, and Raku++ is measured
against Rakudo and against the Roast suite continuously rather than
aspirationally. The current position is roughly ninety per cent of declared
Roast assertions.

It has no JIT and no garbage collector. Lifetime is `shared_ptr` reference
counting, which means a reference cycle leaks; the interpreter breaks the
specific cycle that a self-closured nested sub would create, and otherwise the
process is short-lived enough for this to be a real but tolerable limitation.

It does not implement compile-time metaprogramming. `macro`, `quasi`,
`RakuAST`, and swapping the grammar for a lexical scope are all absent, for the
structural reason given above. What *is* supported is everything that can be
done by adding an entry to a table during a single forward pass: all six
custom-operator categories with precedence and associativity traits, plus the
whole runtime meta-object surface (`augment`, `.^add_method`, `does`/`but`
mixins, `&routine.wrap`).

## The ecosystem around the compiler

Several things in this book are not in the `rakupp` binary but are load-bearing
for it, and appear where they are relevant:

| Thing | What it is |
|---|---|
| **Raku.js** | the same runtime compiled to WebAssembly — the interpreter in a browser |
| **the Roast harness** | `tools/run-roast.raku`, written in Raku, run by rakupp against the spec suite |
| **`perf-guard`** | `tools/perf-guard.raku`, the release gate that fails a regression instead of eyeballing one |
| **the showcase interpreters** | JavaScript, Perl 5, Python 3 and Lisp interpreters *written in Raku*, used as beyond-Roast tests |
| **`JSON::Native`** | the first native extension module, and the proof the extension ABI works |

The showcase programs deserve a note here because they recur. An interpreter
for another language, written in Raku and run by Raku++, exercises grammars,
recursion, string handling, closures and dispatch simultaneously and at a scale
no unit test reaches. Each of them, when first written, produced a list of
genuine bugs in Raku++; several fixes in later chapters were found that way.

## A map of this book

| Part | Covers |
|---|---|
| I | this chapter, the layout of the source, and where the design sits in the compiler taxonomy |
| II | lexer, parser, user-defined operators, the AST |
| III | `Value`, strings, interning, numbers, containers |
| IV | the tree walk, calls, control flow, dispatch, objects, laziness |
| V | the regex engine, the grammar engine, longest-token matching |
| VI | Unicode: graphemes, normalization, collation |
| VII | the four run modes, the code generator, `-O`, what a binary keeps, serialization, the browser |
| VIII | modules, `use nqp`, NativeCall, the extension ABI, concurrency |
| IX | tooling built on the AST, and how any of this is proved |
