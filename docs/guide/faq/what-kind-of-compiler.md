# FAQ — what kind of compiler is Raku++?

The questions every compiler gets asked before any other, answered one at a
time: one-pass or multi-pass, LL or LR, is there an IR, is there a VM, is it a
tree-walker or a compiler. Short answers here; the long one is the Internals
book's [Chapter 3](../../book/ch/03-classification.md), which is this page with
the reasoning left in.

**The one-sentence answer:**

> A hand-written, single-pass, syntax-directed **recursive-descent parser with a
> Pratt (precedence-climbing) expression core and a dynamically extensible
> operator table**, feeding an **AST tree-walking interpreter**, with an optional
> **source-to-source back end** that emits C++.

Nothing in that sentence is generated, and every clause in it has a question
below.

---

## Passes and shape

### Is it one-pass or multi-pass?

Both, depending on which part you mean. The **parse** is single-pass: the parser
walks the token vector once, left to right, and never revisits a node. The
**implementation** is a pipeline of separated stages:

```
source ─► Lexer ─► Parser ─► AST ─► DeclCheck ─┬─ tree-walk          (default)
                                               ├─ AST serialised     (--aot)
                                               ├─ emit C++           (--exe)
                                               └─ emit JavaScript    (--target=js)

source ───────────────────────────────────────── embed source bytes  (--bundle)
```

The lexer runs to completion before a parser exists, so this is not the classic
one-pass compiler that emits code as it reads. Three things qualify even the
"single-pass parse" claim: the lexer pre-scans the source for quote-word `sub`
declarations before tokenizing; two seams re-lex the unread tail of the unit
(after a `use` that shadows a quote word, or activates a slang); and an active
slang runs the user's own code mid-parse. Chapter 3's "The pass structure" has
each one.

### Is there a front end / back end split?

Yes, and it is unusually clean, because everything except the CLI compiles into
one static library — `librakupp_rt.a`. The interpreter and every binary produced
by `--exe`/`--aot`/`--bundle` link the *same* runtime, so `Value` semantics,
built-ins, dispatch, regexes and Unicode have exactly one implementation.

### Is there a semantic-analysis pass?

One, and it is narrow. `DeclCheck` walks the AST between the parser and every
back end and asks whether each variable the unit mentions is declared somewhere
in its lexical chain. A program that fails is refused before any of it runs:

```raku
say "before";
say $undeclared;
```

```
===SORRY!=== Error while compiling /tmp/decl.raku
Variable '$undeclared' is not declared
at /tmp/decl.raku:2
------> say ⏏$undeclared;
```

`before` is never printed. Rakudo refuses the same program with the same
`===SORRY!===` shape (it adds a "Perhaps you forgot a 'sub'" hint and marks the
spot with `<HERE>` instead of `⏏`).

That is the whole of semantic analysis. There is no type-checking pass, no
control-flow analysis, no data-flow analysis — `DeclCheck` answers one question
and hands the back end nothing to optimise with.

---

## The parser

### Recursive descent, or table-driven?

Recursive descent, hand-written, for statements — a keyword ladder on the
leading token. Expressions use a **Pratt** (precedence-climbing) core instead,
because Raku's operator table runs to roughly two dozen precedence levels and
pure recursive descent would spend a mutually recursive function on each of
them.

### Is it LL(1)?

No. It is top-down and in the LL family, but neither LL(1) nor any fixed LL(k).
The honest description is *recursive descent with bounded local backtracking*:
it rewinds at exactly four places in `src/Parser.cpp`, each a short speculative
probe that restores the position on failure.

### Is it LR, LALR or SLR?

No. Those are bottom-up and table-driven, and a static table cannot be built for
a grammar that changes while it is being parsed — see "extensible" below.

### Is it a PEG or packrat parser?

No. It memoizes nothing, so there is no packrat, and there is no ordered-choice
formalism. It is not GLR or Earley either: no parse forest is built, and
ambiguity is settled where it is met rather than afterwards.

### Is the grammar a CFG? Is there a grammar file?

There is no grammar file, no parser generator, and no state table — the lexer
and parser are hand-written C++. And the grammar is not context-free. Declaring
`sub infix:<…>` registers a new operator with its own precedence and
associativity, so tokens after that point parse differently from tokens before
it. The nearest term in the literature is an **adaptive** (extensible) grammar,
which is why top-down parsing is the only tractable choice here — as it is for
every language that lets users declare operators.

### Is the lexer separate?

Completely. `Lexer::tokenize()` is a character-level scanner that runs to
completion and returns a flat `std::vector<Token>`; only then is a parser built
over it. There is **no lexer hack** — the lexer never asks the parser anything,
because by the time the parser exists the lexer has finished.

Raku's context-sensitivity still has to go somewhere, and it goes into the
lexer's own one-token history: a bare `/` opens a regex in term position and
divides in operator position, decided by inspecting the previous token alone.

You can see the token stream. Take this two-line program, which the rest of the
page reuses:

```raku
my $x = 1 + 2;
say $x;
```

```bash
RAKUPP_DUMPTOKENS=1 rakupp prog.raku
```

```
L1 kind=9 [my]
L1 kind=10 [$x]
L1 kind=20 [=]
L1 kind=1 [1]
L1 kind=20 [+]
L1 kind=1 [2]
L1 kind=17 [;]
L2 kind=9 [say]
L2 kind=10 [$x]
L2 kind=17 [;]
L3 kind=0 []
3
```

The trailing `3` is the program running afterwards — the dump is a side channel,
not a mode.

### Is there error recovery?

No. The first error ends compilation; the driver prints `===SORRY!===` and exits
1, matching Rakudo's shape. (The lexer has a tolerant mode, but it defers a lex
error to the parser so the message can carry better context — not so that
compilation continues.)

---

## The middle: is there an IR?

### Is there an intermediate representation?

No — and this is the single most load-bearing fact about the design. There is no
bytecode, no three-address code, no SSA, no control-flow graph, no register
allocation and no lowering pass. The AST is the sole representation the whole
way through, and one `NK` enum switch drives both the interpreter's
`eval`/`exec` and the code generator.

### Is there a parse tree *and* an AST?

Just the one tree. `rakupp --ast` prints it — the same program as above:

```bash
rakupp --ast prog.raku
```

```
Program
  Assign      │  =
    VarExpr   │   $x [decl my]
    Binary    │   +
      IntLit  │    1
      IntLit  │    2
  Call        │  say
    VarExpr   │   $x
```

### Is there constant folding?

Not yet. Note that `1 + 2` survives as a `Binary` node with two `IntLit`
children in the tree above, and reaches the generated C++ unfolded:

```cpp
v_sx = applyArith("+", Value::integer(1LL), Value::integer(2LL));
```

`constant` declarations are not folded at parse time either; they become
declaring nodes the interpreter binds.

### What optimisations are there, then?

`-O` is three passes, all **AST-level pattern matching at emit time**: direct
arity calls, inlined `int64` arithmetic, and guarded native-int expression
lanes. Those are local, peephole-class transformations by any standard
classification. Everything classical — inlining, constant propagation, loop
transforms, instruction scheduling, register allocation — is delegated to the
host C++ compiler, which is the point of emitting C++ rather than machine code.
[OPTIMIZATION.md](../../internals/OPTIMIZATION.md) is the detail, and
[optimizer.md](optimizer.md) is why `-O` is not the default.

---

## Execution

### Tree-walking interpreter, or a compiler?

Tree-walking, in three of the five modes. `eval(Expr*)` returns a `Value` and
`exec(Stmt*)` runs a statement, straight over the AST. There is no bytecode and
no separate operand stack — **the C++ stack is the Raku stack**, which is why
the entry point runs your program on a thread with a very large stack.

| Mode | What it is, taxonomically |
|---|---|
| default | Tree-walking interpreter |
| `--bundle` | Source embedded in the binary, parsed at startup |
| `--aot` | AST rebuilt at startup, still tree-walked |
| `--exe` | Source-to-source compiler — a transpiler to C++ |
| `--target=js` | Source-to-source compiler — a transpiler to JavaScript |

### Is there a VM?

No. No instruction set, no opcode dispatch loop, no operand stack. This is the
clearest structural difference from Rakudo, which compiles to bytecode for
MoarVM.

### Is there a JIT?

No. Nothing is compiled at run time. `--exe` compiles ahead of time, by emitting
C++ and shelling out to a C++ compiler.

### So what does `--exe` actually produce?

A native binary, via C++ source. You can read the intermediate C++ without
building it:

```bash
rakupp --cpp prog.raku
```

The emitted code calls into the same `librakupp_rt.a` the interpreter uses, so
compiled and interpreted programs cannot disagree about semantics.
[compiling.md](compiling.md) is the practical guide to the modes.

### Is node specialisation a compilation step?

No. It caches a decision on the syntactic shape of a node — a fast path on a
tree walk. No amount of it produces an instruction stream.

---

## Runtime

### Is there a garbage collector?

No. Memory is `shared_ptr` reference counting, which buys a ~1.5 MB floor and no
stop-the-world pause, and costs you unreclaimed reference cycles and a free that
lands on your own clock. [garbage-collection.md](garbage-collection.md) is the
full trade.

### Static or dynamic typing?

Dynamic, as Raku is. Every runtime value is one `Value` struct with a `VT` tag;
type constraints are checked where they are met — at binding, assignment and
dispatch — not by a pass before the program runs. There is no type inference and
no static type error. A constraint that cannot possibly hold still gets as far
as running:

```raku
say "before";
my Int $x = "hello";
```

```
before
Type check failed in assignment to $x; expected Int but got Str ("hello")
  (X::TypeCheck::Assignment)
  in block <unit> at /tmp/ty.raku line 2
```

`before` prints first. Rakudo agrees line for line here, minus the exception
name — this is the language's design, not an implementation shortcut. Contrast
the undeclared variable further up, which *is* refused before anything runs.

### Lexical or dynamic scoping?

Lexical, through a chain of `Env` scopes, with Raku's dynamic variables
(`$*foo`) as the deliberate exception that searches the caller chain instead.

---

## About the implementation itself

### Is it self-hosted or bootstrapped?

No. The compiler is C++17 and is built by a C++ compiler; it does not compile
itself. A good deal of the *tooling around* it is written in Raku and run by
`rakupp` — the Roast harness, the benchmark harness, the Unicode table
generators — which is dogfooding rather than bootstrapping. See
[DOGFOODING.md](../../status/DOGFOODING.md).

### Is it really hand-written?

Yes, in the compiler-writer's sense: no parser generator, no grammar file. That
is the same sense GCC, Clang and Go use of their own front ends.
[hand-written.md](hand-written.md) is the longer answer, including who wrote the
code.

### How big is it, and what does it depend on?

About 130,000 lines of hand-written C++ in `src/`, plus about 82,000 lines of
generated Unicode tables. The only dependency is a threads library; there is no
third-party code in the build.

---

## See also

- **[Chapter 3, Where It Sits in the Taxonomy](../../book/ch/03-classification.md)**
  — this page with the reasoning, the comparison against Rakudo, CPython and
  Clang, and the limitation list that falls out of the classification.
- **[ARCHITECTURE.md](../../internals/ARCHITECTURE.md)** — the pipeline and the
  build layout.
- **[implementations.md](implementations.md)** — Raku++, Rakudo and mutsu
  compared structurally: tree-walk vs bytecode VM, JIT vs ahead-of-time,
  refcounting vs a collector.
- **[COMPILERS.md](../COMPILERS.md)** — the *other* compiler question: which C++
  compiler to build with, and which one `--exe` invokes.
