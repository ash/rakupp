# `use nqp`: a Compatibility Subset

Some ecosystem modules — most importantly `JSON::Fast`, which sits under about
170 other distributions — are written in ordinary Raku but reach for **NQP ops**
in their hot paths: `nqp::iseq_i`, `nqp::substr`, `nqp::push_s`.

NQP is Rakudo's bootstrap language, and its `nqp::` operations are the low-level
primitives the Raku runtime is built on. Rakudo documents them as **internal and
unspecified**; every implementation provides whatever subset its ecosystem
happens to need.

Raku++ provides that subset. This chapter is about how, and — more interestingly
— about why it costs **nothing** for the programs that do not use it.

## There is no NQP compiler here

The single most important thing to understand: **`nqp::foo(...)` is already
valid Raku syntax.** It is a call to a package-qualified routine named
`nqp::foo`. The normal parser reads it with no special help.

There is no NQP grammar, no NQP compiler, and no QAST anywhere in Raku++. What
is added is a **semantic layer**: when a program opts in with `use nqp`, the
parser recognises those already-parsed calls and gives them meaning, compiling
them to dedicated AST nodes instead of leaving them as calls to undefined
routines.

```raku
use nqp;                       # (or `use MONKEY-GUTS`)
my int $x = nqp::add_i(2, 3);  # 5
```

Without `use nqp`, `nqp::add_i(...)` is exactly what it looks like — a call to a
routine that does not exist — and running it gives the usual undefined-routine
error.

## Zero cost when unused

This is a hard design constraint, not an aspiration, and it holds by
**construction** rather than by optimisation.

- The parser carries one boolean, `useNqp_`, that starts false and is set only
  by seeing `use nqp` or `MONKEY-GUTS`. Every recognition site is guarded by it,
  so in a normal program those checks are a single already-false branch on a
  token that begins with `nqp::` — which no ordinary identifier does.
- The `NqpOp` node is **only ever constructed** inside that guarded path. A
  program without `use nqp` builds no such node, so the interpreter's `NqpOp`
  arm is never reached and the implementation is dead code for that run.
- There are no new fields on any hot struct, no new work in any hot loop, and no
  change to how normal calls, strings or numbers are handled.

A regression test asserts the invariant directly: that `nqp::add_i` still
reports "undefined routine" without `use nqp`.

## How the ops compile

Inside a `use nqp` unit, a recognised `nqp::op(...)` becomes one of three things
**at parse time**.

**Constants fold to literals.** `nqp::const::CCLASS_NUMERIC` is replaced by the
integer `8` in the parser; it never exists as a call. All the character-class
flags and normalization modes are handled this way.

**Control forms become native or lazy nodes,** because their arguments must not
all evaluate eagerly. `nqp::if` and `nqp::unless` compile to Raku++'s own
ternary node, so the untaken branch never runs. `nqp::while`, `nqp::until`,
`nqp::stmts` and `nqp::ifnull` become `NqpOp` nodes whose evaluator drives its
own argument evaluation.

**Leaf ops become eager `NqpOp` nodes** — integer maths, string and list
primitives — which evaluate their arguments normally and then do the low-level
operation over Raku++'s own `Value`s.

An `nqp::` op **outside** the implemented subset is left as an ordinary call and
fails loudly at run time rather than silently misbehaving. The subset grows by
measured demand, never by guessing.

## In code

The parser branch, condensed:

```cpp
// src/Parser.cpp — the term is already lexed as the qualified name `nqp::add_i`
if (useNqp_ && name.compare(0, 5, "nqp::") == 0) {
    if (name.compare(0, 12, "nqp::const::") == 0) {
        long long cv;
        if (nqpConstValue(name.substr(12), cv))
            return std::make_unique<IntLit>(cv);      // folded here
    }
    if (ExprPtr n = makeNqpOp(name.substr(5), callArgs))
        return n;                                     // an NqpOp, or a Ternary
}
```

The node is deliberately tiny:

```cpp
// src/Ast.h
enum class NqpOpc : uint16_t {
    Stmts, While, Until, IseqI, AddI, Substr, /* … */ };
struct NqpOp : Expr {
    NqpOpc op;
    std::vector<ExprPtr> args;
};
```

and the evaluator handles the lazy forms *before* touching their arguments:

```cpp
// src/Builtins.cpp — condensed
Value Interpreter::evalNqpOp(NqpOp* n) {
    auto& a = n->args;
    switch (n->op) {                        // lazy forms drive their own args
        case NqpOpc::While:
            while (boolify(eval(a[0].get())))
                for (size_t i = 1; i < a.size(); i++) eval(a[i].get());
            return Value::nil();
        default: break;
    }
    ValueList v;                            // everything else: eval once
    for (auto& e : a) v.push_back(eval(e.get()));
    auto I = [&](size_t i){ return v[i].toInt(); };
    switch (n->op) {
        case NqpOpc::AddI:  return Value::integer(I(0) + I(1));   // int64
        case NqpOpc::IseqI: return Value::integer(I(0) == I(1));
        // … about 40 more leaf ops …
    }
}
```

## The argument buffers

An nqp-heavy program is nqp-heavy in the extreme: a tokenizer written in Raku
runs about 1.5 million operations on a 278 KB document. Building a fresh
`ValueList` per operation is a malloc and a free per op, for a vector of one to
four values.

```cpp
// src/Interpreter.h — ExecContext
std::deque<ValueList> nqpArgs;   // one reusable buffer per nesting depth
size_t nqpDepth = 0;
```

The buffers are kept, so their capacity is kept; the contents are still cleared
on the way out, so argument lifetimes are exactly what they were.

It is a **deque**, not a vector, and the comment says why: an argument's own
evaluation can re-enter `evalNqpOp`, and growing a vector would reallocate under
the outer frame's live reference. A deque never moves the elements it already
holds.

That is a small illustration of a recurring theme — the reusable-buffer
optimisation is easy, and the container choice that makes it *correct* under
re-entrancy is the part that takes thought.

## Native compilation

The subset is a first-class part of the transpiler, not just the interpreter. A
`use nqp` program native-compiles like any other; it does **not** fall back to
bundling.

```console
$ rakupp --cpp -e 'use nqp; say nqp::add_i(1, 2)'
…
rtCallB(RT, __bfp0, "say", ValueList{
    ([&]()->Value{ ValueList __na = ValueList{Value::integer(1LL),
                                              Value::integer(2LL)};
                   return rtNqpOp(NqpOpc(10), __na); }())});
```

Two emission strategies, mirroring how the ops evaluate:

- **Eager leaf ops** share their implementation with the interpreter through a
  free function `rtNqpOp(NqpOpc, ValueList&)` in the runtime.
  `Interpreter::evalNqpOp` delegates to it and the generator emits a call to the
  *same* function, so there is exactly one copy of the op logic and the compiled
  binary cannot drift from the interpreted meaning.
- **Lazy control forms** do not route through a runtime call — they emit native
  C++ directly, a real `while` and a statement sequence, preserving their
  non-eager evaluation.

The zero-cost guarantee extends here too: a program without `use nqp` emits zero
references to `rtNqpOp` or any nqp machinery.

## What is covered

Around fifty operations, chosen from the actual inventory of the modules in the
ecosystem compatibility battery:

| Group | Ops |
|---|---|
| **Control (lazy)** | `if` `unless` `while` `until` `stmts` `ifnull` |
| **Integer** (int64, no bignum promotion) | `iseq_i` `isne_i` `islt_i` `isle_i` `isge_i` `isgt_i` `add_i` `sub_i` `mul_i` `bitand_i` |
| **String** (codepoint-indexed) | `ordat` `eqat` `substr` `chars` `concat` `join` `index` `chr` `strfromcodes` `strtocodes` `findnotcclass` `iscclass` |
| **List** | `list` `list_i` `list_s` `elems` `atpos` `atpos_i` `bindpos` `push` `push_i` `push_s` `pop_s` `shift_i` `splice` |
| **Hash** | `hash` `bindkey` |
| **Object / attr** | `create` `istype` `getattr` `bindattr` `p6bindattrinvres` `null` `isnanorinf` |
| **Constants** | `const::CCLASS_*`, `const::NORMALIZE_*` |

Integer ops use plain `int64` semantics, matching NQP's native integers — **no
overflow to bignum** — and string ops index by codepoint rather than by
grapheme. Both of those are deliberate: an NQP op is supposed to be the
low-level primitive, and a module reaching for one is reaching past Raku's
semantics on purpose.

## Scope and non-goals

- **Not a general NQP runtime.** This is a compatibility shim sized to the
  ecosystem. Operations nobody's modules use are not implemented until a module
  needs them.
- **Semantics match Rakudo's observable behaviour, not its internal
  representation.** `nqp::create(IterationBuffer)` gives back a plain Raku++
  buffer, because what the calling module does with it is all that matters.
- **`use nqp` is an opt-in for module code**, not an invitation for application
  code. There is no reason to reach for `nqp::` operations in a program you are
  writing, and no support commitment for operations beyond the measured subset.

## Why this shape is the right one

The temptation with a compatibility layer is to implement the source language.
That would have meant an NQP grammar, an NQP compiler, and a second execution
model to keep in step with the first — for a feature whose entire purpose is to
let a handful of modules load.

Recognising that `nqp::foo(…)` is *already* valid syntax in the host language
turned a compiler project into about 250 lines of parser branch and evaluator
arm. The general principle generalises: **before implementing a foreign
language, check whether its surface syntax is already legal in yours.**
