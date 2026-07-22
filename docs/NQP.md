# `use nqp` — the NQP compatibility subset

Some ecosystem modules — most importantly **JSON::Fast**, which sits under ~170
other distributions — are written in ordinary Raku but reach for **NQP ops** in
their hot paths: `nqp::iseq_i`, `nqp::substr`, `nqp::push_s`, and friends. NQP
("Not Quite Perl") is Rakudo's bootstrap language; its `nqp::*` ops are the
low-level primitives the Raku runtime is built on. Rakudo documents them as
**internal and unspecified** — every implementation provides whatever subset its
ecosystem happens to need.

Raku++ provides that subset so those modules run. This document explains what it
covers, how it works, and — importantly — why it costs **nothing** for the
programs that don't use it.

## We do not parse NQP

The single most important thing to understand: **`nqp::foo(...)` is already
valid Raku syntax** — a call to a package-qualified routine named `nqp::foo`.
Raku++'s normal parser reads it with no special help. There is **no NQP grammar,
no NQP compiler, no QAST** anywhere in Raku++. NQP-the-language stays entirely
out of scope.

What we add is a **semantic layer**: when a program opts in with `use nqp`, the
parser recognizes those already-parsed `nqp::` calls and gives them meaning,
compiling them to dedicated AST nodes instead of leaving them as calls to
undefined routines.

## Opting in

```raku
use nqp;                       # (or `use MONKEY-GUTS`)
my int $x = nqp::add_i(2, 3);  # 5
```

Without `use nqp`, `nqp::add_i(...)` is exactly what it looks like — a call to a
routine that doesn't exist — and running it gives the usual
`Undefined routine 'nqp::add_i'`. Nothing changes for you.

## Zero-cost when unused

This is a hard design constraint, not an aspiration. A program that never says
`use nqp` must pay **nothing** — no parse-time cost, no runtime branch, no
memory, no behavioral difference of any kind.

It holds by *construction*, not by optimization:

- The parser carries one boolean, `useNqp_`, that starts **false** and is set to
  true only by seeing `use nqp` (or `MONKEY-GUTS`). Every `nqp::` recognition
  site is guarded by it, so in a normal program those checks are a single
  already-false branch on a token that begins with `nqp::` — which no ordinary
  identifier does.
- The special AST node the subset uses (`NqpOp`) is **only ever constructed**
  inside that `useNqp_`-guarded path. A program without `use nqp` builds no such
  node, so the interpreter's `NqpOp` evaluation arm is never reached, and the
  ~250 lines that implement the ops are dead code for that run.
- There are no new fields on any hot struct, no new work in any hot loop, and no
  change to how normal calls, strings, or numbers are handled.

The result is verified: a normal program's output and timings are byte- and
cycle-identical to a build without the subset. (`t/regression/nqp-subset.raku`
asserts the zero-cost invariant directly — that `nqp::add_i` still reports
"Undefined routine" without `use nqp`.)

## How the ops compile

Inside a `use nqp` unit, a recognized `nqp::op(...)` becomes one of three
things at **parse time**:

1. **Constants fold to literals.** `nqp::const::CCLASS_NUMERIC` is replaced by
   the integer `8` right in the parser — it never exists as a call. All the
   `CCLASS_*` character-class flags and `NORMALIZE_*` modes are handled this way.

2. **Control forms become native or lazy nodes.** These can't be ordinary calls
   because their arguments must *not* all evaluate eagerly:
   - `nqp::if(c, t, e)` / `nqp::unless(c, t, e)` compile to Raku++'s native
     ternary node — the untaken branch never runs.
   - `nqp::while` / `nqp::until` / `nqp::stmts` / `nqp::ifnull` become `NqpOp`
     nodes whose evaluator drives its own argument evaluation (looping,
     sequencing, null-coalescing) instead of receiving pre-evaluated values.

3. **Leaf ops become eager `NqpOp` nodes.** Everything else — integer math,
   string and list primitives — evaluates its arguments normally and then does
   the low-level operation over Raku++'s own `Value`s.

If an `nqp::` op **outside** the implemented subset appears, it is left as an
ordinary call and fails loudly at runtime (`Undefined routine`) rather than
silently misbehaving. The subset grows by measured demand, never by guessing.

### In code

The whole mechanism is three small pieces. First, in the parser, once a term has
been read as a package-qualified call named `nqp::…` — which the *normal* parser
does, no special casing — the guarded branch reroutes it (condensed from
`src/Parser.cpp`):

```cpp
// term already lexed as the qualified name `nqp::add_i`, args in callArgs.
// useNqp_ is false unless this unit said `use nqp` — so a normal program
// never takes any of these branches.
if (useNqp_ && name.compare(0, 5, "nqp::") == 0) {
    // nqp::const::CCLASS_NUMERIC  ->  the literal 8, folded here at parse time
    if (name.compare(0, 12, "nqp::const::") == 0) {
        long long cv;
        if (nqpConstValue(name.substr(12), cv))
            return std::make_unique<IntLit>(cv);
    }
    // nqp::op(...)  ->  a dedicated NqpOp node (nqp::if/unless become a
    // native Ternary instead — makeNqpOp returns that). Anything not in the
    // subset returns null and stays an ordinary Call that dies at runtime.
    if (ExprPtr n = makeNqpOp(name.substr(5), callArgs))
        return n;
}
```

The node itself is deliberately tiny — an opcode and its argument expressions
(`src/Ast.h`):

```cpp
enum class NqpOpc : uint16_t { Stmts, While, Until, IseqI, AddI, Substr, /* … */ };

struct NqpOp : Expr {
    NqpOpc op;
    std::vector<ExprPtr> args;
    explicit NqpOp(NqpOpc o): Expr(NK::NqpOp), op(o) {}
};
```

Finally the evaluator. The main `eval` switch gains exactly one arm, reached
only when an `NqpOp` node exists — which only happens under `use nqp`:

```cpp
case NK::NqpOp: return evalNqpOp(static_cast<NqpOp*>(e));
```

`evalNqpOp` handles the lazy control ops *before* touching their arguments, then
evaluates the rest eagerly (condensed from `src/Builtins.cpp`):

```cpp
Value Interpreter::evalNqpOp(NqpOp* n) {
    auto& a = n->args;
    switch (n->op) {                       // lazy forms drive their own args
        case NqpOpc::While:                // nqp::while(cond, body…)
            while (boolify(eval(a[0].get())))
                for (size_t i = 1; i < a.size(); i++) eval(a[i].get());
            return Value::nil();
        // … Until / Stmts / IfNull likewise …
        default: break;
    }
    ValueList v;                           // everything else: eval args once,
    for (auto& e : a) v.push_back(eval(e.get()));   // then do the primitive
    auto I = [&](size_t i){ return v[i].toInt(); };
    switch (n->op) {
        case NqpOpc::AddI:  return Value::integer(I(0) + I(1));  // int64, no bignum
        case NqpOpc::IseqI: return Value::integer(I(0) == I(1));
        case NqpOpc::Substr: /* codepoint-indexed slice over v[0].toStr() */;
        // … ~40 more leaf ops …
        default: throw RakuError{Value::str("nqp"), "op not in this build's subset"};
    }
}
```

So `nqp::add_i($a, $b)` under `use nqp` is: parse as a qualified call → recognize
→ build `NqpOp{AddI, [$a, $b]}` → at runtime, evaluate both args and return their
int64 sum. Without `use nqp`, none of it happens: the same source stays a call to
an undefined routine, and `evalNqpOp` is never linked into any code path the run
reaches.

### Native compilation (`--exe`)

The subset compiles too — a `use nqp` program native-compiles rather than
falling back to interpreter bundling. The eager leaf ops share their
implementation with the interpreter through a free `rtNqpOp(NqpOpc, ValueList&)`
in the runtime library, so the compiled binary runs the *exact same* op logic
(`src/Builtins.cpp`); the codegen just emits a call to it:

```cpp
// generated for  nqp::add_i(1, 2)
([&]()->Value{ ValueList __na = ValueList{Value::integer(1LL), Value::integer(2LL)};
               return rtNqpOp(NqpOpc(10), __na); }())
```

The lazy control forms don't route through a runtime call — they emit native
C++ directly (a real `while`, a statement sequence), preserving their
non-eager evaluation in the compiled code. Interpreter and compiled binary
produce byte-identical output (`t/regression/nqp-codegen.raku` asserts it), and
a program *without* `use nqp` emits zero references to any of this.

## What's covered

Around 50 ops, chosen from the actual inventory of the modules in the
[ecosystem battery](dev/ECOSYSTEM-TOP50.md):

| Group | Ops |
|---|---|
| **Control (lazy)** | `if` `unless` `while` `until` `stmts` `ifnull` |
| **Integer** (int64, no bignum promotion) | `iseq_i` `isne_i` `islt_i` `isle_i` `isge_i` `isgt_i` `add_i` `sub_i` `mul_i` `bitand_i` |
| **String** (codepoint-indexed) | `ordat` `eqat` `substr` `chars` `concat` `join` `index` `chr` `strfromcodes` `strtocodes` `findnotcclass` `iscclass` |
| **List** | `list` `list_i` `list_s` `elems` `atpos` `atpos_i` `bindpos` `bindpos_i` `push` `push_i` `push_s` `pop_s` `shift_i` `splice` |
| **Hash** | `hash` `bindkey` |
| **Object / attr** | `create` `istype` `getattr` `bindattr` `p6bindattrinvres` `p6scalarwithvalue` `null` `isnanorinf` |
| **Constants** | `const::CCLASS_*` (character classes), `const::NORMALIZE_*` (normalization modes) |

Integer ops use plain `int64` semantics (matching NQP's native ints — no
overflow-to-bignum), and string ops index by codepoint. The character-class ops
(`iscclass` / `findnotcclass`) cover the classes real modules actually query
(numeric, alphabetic, word, whitespace, hexadecimal, …).

## Scope and non-goals

- **Not a general NQP runtime.** This is a compatibility shim sized to the
  ecosystem, not a reimplementation of NQP. Ops nobody's modules use aren't
  implemented until a module needs them.
- **Semantics match Rakudo's observable behavior**, not its internal
  representation — `nqp::create(IterationBuffer)` gives back a plain Raku++
  buffer, because what the calling module does with it is all that matters.
- **`use nqp` is an opt-in for module code**, not an invitation for application
  code. It exists so third-party modules load and run; there's no reason to
  reach for `nqp::` ops in a program you're writing, and no support commitment
  for ops beyond the measured subset.

## Where it lives

- Parser recognition + constant folding + op mapping: `Parser::useNqp_`,
  `Parser::nqpConstValue`, `Parser::makeNqpOp` (`src/Parser.cpp`).
- The AST node and op enumeration: `NqpOp` / `NqpOpc` (`src/Ast.h`).
- Evaluation: `Interpreter::evalNqpOp` (`src/Builtins.cpp`).
- Findings and residual work: [dev/MODULE-FINDINGS.md](dev/MODULE-FINDINGS.md).
