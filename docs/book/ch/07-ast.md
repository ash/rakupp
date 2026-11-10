# The AST

The parser's output is a `Program`: a vector of statement pointers. Everything
downstream — the interpreter, the two compiling back ends, the linter, the
highlighter, the serialiser — consumes that one tree.

```cpp
// src/Ast.h — abridged
struct Node {
    NK kind;
    bool inParens = false;    // written inside ( … )
    bool blockPart = false;   // a string's {…} part, inlined
    int line = 0;
    virtual ~Node() = default;
};
struct Expr : Node {
    using EvalFn = Value (*)(Interpreter&, Expr*);
    mutable DecidedOnce<EvalFn> handler{nullptr};
};
struct Stmt : Node { std::string label; /* … three run-time slots … */ };
using ExprPtr = std::unique_ptr<Expr>;
using StmtPtr = std::unique_ptr<Stmt>;
struct Program { std::vector<StmtPtr> stmts; /* … what the unit declares … */ };
```

The base classes are no longer bare. `inParens` is read by nothing that runs
the tree; it is there so the `RakuAST` view can render a written `(1 + 2) * 3`
with its parentheses, and it sits in the bytes a 16-bit `NK` leaves before
`line`, so no node grew. `handler` is the interpreter's: an expression whose
shape has been decided can be given a function to call in place of the switch
on its kind (Chapter 13). A `Stmt` has three slots that belong to one run and
are never serialised — where a loop's tier-up site, a body's integer kernel and
a module routine's native body hang. A `Program` also carries what the parser
learned about the unit as a whole: the type names, sigilless terms and labels
it declares, whether it imports a module, and its language revision.

## A class hierarchy here, a fat struct there

The runtime's `Value` is a single struct with a tag, the same 32 bytes whatever
it holds (Chapter 8). The AST is the opposite: a real C++ class hierarchy, one
`struct` per node type, each with exactly the fields it needs. Both choices are
right, for opposite reasons.

An AST node is **built once, lives for the whole run, and is visited by a
`switch` on its tag**. Nodes are few, long-lived, and heterogeneous — some carry
two pointers, `ClassDecl` carries forty fields including twelve vectors. A fat
struct there would waste far more than it saved, and the virtual destructor
costs nothing that matters when construction happens once.

A `Value`, by contrast, is created and destroyed millions of times per second
and must be cheap to copy into vectors. Same program, opposite trade.

The `NK` enum tags every node so both the interpreter and the code generator
can `switch` rather than dispatch virtually:

```cpp
// src/Ast.h
enum class NK : uint16_t {
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
(Chapter 14) and the multi-dispatcher (Chapter 16).

**`SubDecl`** carries the routine: name, parameters, alternate signatures
sharing one body, the body, non-built-in traits kept as unevaluated
expressions, the `multi`/`proto`/`method`/`submethod`/private flags, export and
`our` scoping, the declared return type, declarator pod, and the six fields
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
mutable PublishedOnce<const void*> litVal{nullptr};
```

```cpp
// src/Ast.h — Index
mutable DecidedOnce<signed char> fastShape{-1};
mutable DecidedOnce<long long> litIdx{0};
```

and similarly:

```
Block::hoistNeed          ForStmt::hasStateCache
NumLit::ratCache          Param::sigSimple, natSpec, typeKnown
CodeInfo::arityShape, catchScan
```

That list is a sample. `src/Ast.h` declares sixty-three such fields, and
`src/Value.h` twenty-nine more, on a routine's `CodeInfo` and on `ClassInfo`.

**Two templates, and the difference between them is a real bug.** `DecidedOnce`
is fine when the cached thing *is* the value: two threads compute the same
`signed char`, either store wins, and a reader that sees either is correct. A
*pointer* is different. The writer does two things — build the payload, then
publish the pointer — and under relaxed ordering a reader may see the pointer
without seeing the bytes it points at. On x86 the store buffer usually hides
that; arm64 is weakly ordered and does not, and this project ships arm64
binaries. ThreadSanitizer reported it as a read of a `Value`'s type tag racing
that `Value`'s constructor.

So the pointer-valued fields, `Binary::litVal` and `NumLit::ratCache`, and the
flags that guard a payload written just before them, `CodeInfo::arityShape` and
`catchScan`, are `PublishedOnce`: release on
publish, acquire on read, and the publish is a compare-exchange, so a racing
double-build has one winner and the loser frees its copy instead of leaking it.
The template deliberately has the same drop-in shape as `DecidedOnce`, so a
field can be upgraded without touching a use site; only the ordering changes.

One field in that list is now vestigial rather than decided: `StrLit::nfcDone`
is permanently `true` and kept only so the precomp cache format is unchanged.
Normalization used to happen lazily on first evaluation, in place — and the AST
is shared by every thread, so it moved into the constructor.


What they hold is a **fact about the program text**, not a cached result. "Is
this binary's left child a plain lexical and its right child a scalar literal?"
is true before the program starts and stays true until it exits, because source
code does not rewrite itself. Answering it costs several branches; answering it
once per node instead of once per evaluation is the entire optimisation, and
Chapter 19 measures what that is worth.

Two invariants keep them safe:

- **A value is never cached, only a classification** — except where the value is
  a literal, which is a constant by definition.
- **The sentinel means undecided**, so a fresh tree and a tree that has been
  running for an hour behave identically. That is what lets the serialiser skip
  these fields entirely (Chapter 30).

The `DecidedOnce<T>` wrapper is a relaxed atomic, for the reason given in
Chapter 2: under parallel execution several threads may compute the same
idempotent answer concurrently, and a plain field would make that a data race.

## Two nodes that only exist sometimes

**`NqpOp`** implements the `use nqp` compatibility subset. It exists *only*
when the parser saw `use nqp` and then met an `nqp::` call:

```cpp
// src/Ast.h
enum class NqpOpc : uint16_t { Stmts, While, Until, IfNull, IseqI, AddI,
                               Substr, Chars, Concat, /* 167 more */ };
struct NqpOp : Expr {
    NqpOpc op;
    std::vector<ExprPtr> args;
};
```

A program that never says `use nqp` builds no such node, so the interpreter's
`NqpOp` arm is never reached and the implementation is dead code for that run.
Chapter 35 is about why that zero-cost property is structural rather than an
optimisation.

**`AllomorphLit`** exists because a numeric word inside a `<…>` list is
simultaneously a number and its own source spelling: `<42>` is an `IntStr`,
`<3.14>` a `RatStr`. The node keeps both the parsed numeric expression and the
original text.

## Borrowed pointers, and why the tree is immortal

A `Callable` — the runtime object behind every sub, block and method — does not
own its code:

```cpp
// src/Value.h — CodeInfo and Callable, excerpt
struct CodeInfo : RefCounted {
    const std::vector<Param>* params = nullptr;   // borrowed from AST
    const std::vector<StmtPtr>* body = nullptr;    // borrowed from AST
    // …
};
struct Callable {
    const CodeInfo& ci() const { return *info.p; }
    std::shared_ptr<Env> closure;
    // …
};
```

A routine's code — the signature and body, what was declared about it, and the
caches decided at its first call — is a `CodeInfo`, which every closure made
from that code shares copy-on-write. The `Callable` adds what differs between
them. Only the environment is owned. The parameter list and the body are raw pointers
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
(Chapter 33). Nothing is ever released from it. That is a deliberate, bounded
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
and node specialisation was (Chapter 19).

## The two emitters that must know every field

Two back ends consume the tree structurally rather than semantically, and both
break loudly if a field is added and forgotten.

**`AstEmit.cpp`** (`--aot`) used to be the weaker of the two. It emitted C++
that rebuilt the tree with one builder function per node, listing each node
type's fields by hand, and that was quietly lossy: a field nobody listed did
not survive, and the binary ran a program subtly unlike the source.
`my Int(Str) $n = "42"` compiled into something that threw a type error,
because the coercion was not among the fields emitted, and diagnostics lost
their line numbers the same way. It now carries the tree as bytes written by
the serialiser below, and knows no field at all. It still throws `AstEmitError`
for a tree the serialiser refuses, and the driver answers by bundling the
source instead.

**`AstSerial.cpp`** (the precompiled parse, and now `--aot` and the modules a
binary embeds) writes and reads a binary encoding. Its defence is the strong
one, and worth copying: **both directions are driven by one
visitor per node type**, so a field cannot be written but not read. That is the
failure mode which makes a format like this quietly wrong rather than loudly
broken. A version constant in the header is bumped whenever the shape changes
— it stands at 62 — and a mismatched entry is ignored rather than
reinterpreted.
