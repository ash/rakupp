# The AST

The parser's output is a `Program`: a vector of statement pointers. Everything
downstream — the interpreter, the two compiling back ends, the linter, the
highlighter, the serialiser — consumes that one tree.

```cpp
// src/Ast.h
struct Node { NK kind; int line; virtual ~Node() = default; };
struct Expr : Node { using Node::Node; };
struct Stmt : Node { using Node::Node; std::string label; };
using ExprPtr = std::unique_ptr<Expr>;
using StmtPtr = std::unique_ptr<Stmt>;
struct Program { std::vector<StmtPtr> stmts; };
```

## A class hierarchy here, a fat struct there

The runtime's `Value` is a single struct with a tag and a field for every
payload (Chapter 7). The AST is the opposite: a real C++ class hierarchy, one
`struct` per node type, each with exactly the fields it needs. Both choices are
right, for opposite reasons.

An AST node is **built once, lives for the whole run, and is visited by a
`switch` on its tag**. Nodes are few, long-lived, and heterogeneous — some carry
two pointers, `ClassDecl` carries seventeen fields including four vectors. A fat
struct there would waste far more than it saved, and the virtual destructor
costs nothing that matters when construction happens once.

A `Value`, by contrast, is created and destroyed millions of times per second
and must be trivially copyable into vectors. Same program, opposite trade.

The `NK` enum tags every node so both the interpreter and the code generator
can `switch` rather than dispatch virtually:

```cpp
// src/Ast.h
enum class NK {
    IntLit, NumLit, StrLit, InterpStr, BoolLit, VarExpr, ListExpr,
    Assign, Binary, Unary, Call, MethodCall, Index, Ternary, Range,
    Pair, BlockExpr, ArrayLit, HashLit, NameTerm, RegexLit, SubstLit,
    ChainExpr, SymbolicRef, AllomorphLit, NqpOp,
    ExprStmt, VarDecl, SubDecl, IfStmt, WhileStmt, ForStmt, LoopStmt,
    Block, ReturnStmt, LastStmt, NextStmt, RedoStmt, UseStmt, EmptyStmt,
    GivenStmt, WhenStmt, RepeatStmt, Whatever, ClassDecl, SelfTerm,
    EnumDecl, NamedRegexDecl, SubsetDecl,
};
```

Forty-nine kinds for a language of Raku's size is small, and the reason is that
most of Raku's surface syntax lowers into a handful of these. A hyper operator,
a metaoperator, a junction constructor and a set operation are all `Binary`
nodes with a different `op` string. A `given`/`when` chain is `GivenStmt` plus
`WhenStmt`. Anything spelled as an operator, including every user-declared one,
is a `Call`.

## The nodes that carry the most

Three node types hold most of the language's declarative weight.

**`Param`** is not a `Node` at all — it is a plain struct, because signatures
are lists rather than trees — and it is the densest thing in the header:

```cpp
// src/Ast.h — Param, abridged
struct Param {
    std::string name;         // includes the sigil; empty for anonymous
    char sigil = '$';
    std::string type;         // constraint name; "" = unconstrained
    ExprPtr whereExpr;        // `where` clause
    ExprPtr litVal;           // literal parameter: MAIN('population')
    ExprPtr defaultVal;
    std::string namedKey;     // external name for :name($var)
    char slurpyKind = 0;      // 'f' = *@, 'n' = **@, '1' = +@
    bool named, slurpy, optional, required, invocant;
    int defConstraint;        // 0 none, 1 = :D, 2 = :U
    bool coerce;              // Int(Str)
    bool isRw, isCopy, isRaw;
    std::shared_ptr<std::vector<Param>> subSig;   // destructuring
    // …plus decided-once caches, below
};
```

None of that is validated at parse time. It is data for the binder
(Chapter 13) and the multi-dispatcher (Chapter 15).

**`SubDecl`** carries the routine: name, parameters, alternate signatures
sharing one body, the body, non-built-in traits kept as unevaluated
expressions, the `multi`/`proto`/`method`/`submethod`/private flags, export and
`our` scoping, the declared return type, declarator pod, and the four fields
that describe an `is native` declaration.

**`ClassDecl`** covers `class`, `role`, `grammar`, `module` and `package` at
once: parents and roles, attributes, methods, grammar rules, the
`parameterized` flag for `role R[T]`, the `:ver`/`:auth`/`:api` adverbs both as
strings *and* as expressions (because `module Zef:ver($?DISTRIBUTION.meta<version>)`
is legal and must be evaluated when the declaration runs), the `is repr(…)`
layout for NativeCall, and a statement body for the package forms.

## Fields that are answers about the syntax

Several nodes carry a small mutable field that the parser leaves undecided and
the first evaluation fills in:

```cpp
// src/Ast.h — Binary
mutable DecidedOnce<signed char> simpleOp{-1};
mutable DecidedOnce<signed char> fastShape{-1};
mutable DecidedOnce<const void*> litVal{nullptr};
```

```cpp
// src/Ast.h — Index
mutable DecidedOnce<signed char> fastShape{-1};
mutable DecidedOnce<long long> litIdx{0};
```

and similarly:

```
Block::hoistNeed          ForStmt::hasStateCache
StrLit::nfcDone           NumLit::cacheN, cacheD
Param::sigSimple, natSpec, typeKnown
Callable::arityShape, catchScan
```


What they hold is a **fact about the program text**, not a cached result. "Is
this binary's left child a plain lexical and its right child a scalar literal?"
is true before the program starts and stays true until it exits, because source
code does not rewrite itself. Answering it costs several branches; answering it
once per node instead of once per evaluation is the entire optimisation, and
Chapter 18 measures what that is worth.

Two invariants keep them safe:

- **A value is never cached, only a classification** — except where the value is
  a literal, which is a constant by definition.
- **The sentinel means undecided**, so a fresh tree and a tree that has been
  running for an hour behave identically. That is what lets the serialiser skip
  these fields entirely (Chapter 28).

The `DecidedOnce<T>` wrapper is a relaxed atomic, for the reason given in
Chapter 2: under parallel execution several threads may compute the same
idempotent answer concurrently, and a plain field would make that a data race.

## Two nodes that only exist sometimes

**`NqpOp`** implements the `use nqp` compatibility subset. It exists *only*
when the parser saw `use nqp` and then met an `nqp::` call:

```cpp
// src/Ast.h
enum class NqpOpc : uint16_t { Stmts, While, Until, IfNull, IseqI, AddI,
                               Substr, Chars, Concat, /* ~50 more */ };
struct NqpOp : Expr {
    NqpOpc op;
    std::vector<ExprPtr> args;
};
```

A program that never says `use nqp` builds no such node, so the interpreter's
`NqpOp` arm is never reached and the implementation is dead code for that run.
Chapter 30 is about why that zero-cost property is structural rather than an
optimisation.

**`AllomorphLit`** exists because a numeric word inside a `<…>` list is
simultaneously a number and its own source spelling: `<42>` is an `IntStr`,
`<1/3>` a `RatStr`. The node keeps both the parsed numeric expression and the
original text.

## Borrowed pointers, and why the tree is immortal

A `Callable` — the runtime object behind every sub, block and method — does not
own its code:

```cpp
// src/Value.h — Callable, excerpt
const std::vector<Param>* params = nullptr;   // borrowed from the AST
const std::vector<StmtPtr>* body = nullptr;   // borrowed from the AST
std::shared_ptr<Env> closure;                 // owned
```

Only the environment is owned. The parameter list and the body are raw pointers
into the tree. Calling a sub walks those AST statements directly; there is no
copy, no bytecode, and no intermediate representation.

The consequence is that **the AST must outlive every value that refers to it**.
For the main program that is automatic. For anything parsed later it is not, so
the interpreter keeps them alive explicitly:

```cpp
// src/Interpreter.h
std::vector<std::shared_ptr<Program>> keptPrograms_;   // EVAL'd ASTs
```

and `loadModule` pushes each module's `Program` onto the same vector
(Chapter 29). Nothing is ever released from it. That is a deliberate, bounded
leak: the number of distinct compilation units a process loads is small, and
the alternative — refcounting subtrees — would cost on every call.

## Seeing the tree

```sh
rakupp --dump-ast prog.raku
RAKUPP_DUMPTOKENS=1 rakupp prog.raku
```

`AstDump.cpp` prints the tree as an indented plain-text outline, one line per
node with the fields that matter for that kind. It is the first thing to reach
for when a parse produces something unexpected, and it is also how
`tools/ast-opportunity.raku` counts syntactic patterns across a corpus — the
tool that decided, with numbers, that constant folding was not worth building
and node specialisation was (Chapter 18).

## The two emitters that must know every field

Two back ends consume the tree structurally rather than semantically, and both
break loudly if a field is added and forgotten.

**`AstEmit.cpp`** (`--aot`) emits C++ that rebuilds the identical tree. It
throws `AstEmitError` on any construct it cannot reconstruct, and the driver
answers by bundling the source instead — so a missed field degrades to a slower
binary rather than a wrong one.

**`AstSerial.cpp`** (the precompiled parse) writes and reads a binary encoding.
Its defence is stronger, and worth copying: **both directions are driven by one
visitor per node type**, so a field cannot be written but not read. That is the
failure mode which makes a format like this quietly wrong rather than loudly
broken. A version constant in the header is bumped whenever the shape changes,
and a mismatched entry is ignored rather than reinterpreted.
