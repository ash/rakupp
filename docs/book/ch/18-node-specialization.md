# Node Specialization

This chapter is a single optimisation, described in full, because it is the
clearest worked example in the project of the method the whole thing runs on:
count the opportunity before building anything, remove work rather than adding
cleverness, and keep a control in the measurement.

## What it does

`evalBinary` and `evalIndex` recognise a handful of syntactic **shapes**, record
that verdict on the node, and take a short path.

| shape | example |
|---|---|
| `$var OP literal` | `$n < 2`, `$n - 1` |
| `literal OP $var` | `2 * $n` |
| `$var OP $var` | `$a + $b` |
| `@arr[$var]` / `@arr[literal]` | `@a[$i]`, `@a[0]` |

Three costs disappear on those paths.

**The variable's copy.** `eval()` on a `VarExpr` returns the value *by value* —
hundreds of bytes, several strings, eleven refcount increments. The fast path
takes a `Value*` out of the environment and hands it to `applyArith`, which
already took `const&`.

**The literal's reconstruction.** The `1` in `$n - 1` was rebuilt into a fresh
`Value` on every visit. It is now built once and kept on the node.

**The probes that cannot apply.** Every binary operation ran two string
comparisons for `DateTime`/`Duration` handling, plus the hyper and `Z`/`X`
checks. None of them can match two plain scalars.

For `@arr[$i]` the win is the same in kind: the general path opens by copying
the whole container into a local `Value` in order to read one element out of it.

## Why these shapes, and not constant folding

The idea started as "let `-O` fold the tree before walking it". That was
measured first, with `tools/ast-opportunity.raku`, which reads `rakupp
--dump-ast` and counts the patterns a tree optimizer would rewrite. Across
48 real files — the showcase interpreters, the examples, the tools — 51,353 AST
nodes:

| pattern | sites | per 1k nodes |
|---|---:|---:|
| constant folding (both operands literal) | 37 | 0.7 |
| **binary with ONE literal operand** | **1,282** | **25.0** |
| `$x = $x + …` self-update | 24 | 0.5 |
| constant condition | 0 | 0.0 |

Classical constant folding has essentially nothing to fold in real Raku: 0.07%
of nodes, and none of it in a loop. Dead branches: none at all. What *is*
plentiful is the operand shapes above, and they sit in loop conditions and index
arithmetic, where they run millions of times.

That table is the whole reason this is a specialisation pass and not a folding
pass. It took two minutes to produce, and it prevented building the wrong thing.
**Re-run the tool before assuming any other classical optimisation is worth
building**; the static count is an upper bound on the opportunity.

## What "cached" means here

"Cache" is a misleading word for this, so be precise. In most contexts caching
means remembering a computed result so you do not recompute it. **Nothing about
the result is remembered.** What is remembered is a fact about the source code.

The entire mechanism is two extra fields on each `Binary` node — one byte and
one pointer. No table, no map, no key, nothing global:

```cpp
// src/Ast.h — Binary
mutable DecidedOnce<signed char> fastShape{-1};   // which shape this node is
mutable DecidedOnce<const void*> litVal{nullptr}; // the literal, shapes 1 and 2
```

Walk one node, `$n - 1`, through its life.

**After parsing**, the fields say nothing:

```
op        = "-"
lhs      -> VarExpr("$n")
rhs      -> IntLit(1)
fastShape = -1            <- nobody has looked at this node yet
litVal    = nullptr
```

**On the first evaluation** the interpreter asks a question *about the syntax*:
is the left child a plain lexical, and the right child a scalar literal? Both
yes, so it writes the answer down:

```
fastShape = 1             <- "variable, operator, literal"
litVal   -> Value(1)      <- the 1 from the source text
```

**On every evaluation after that**, including the millionth, the node reads
`fastShape == 1` and goes straight to: look up `$n`, check its type, apply the
operator. It never asks the question again.

So the two fields hold two different kinds of thing.

- **`fastShape` is a classification of the syntax** — "this expression is
  written as variable-operator-literal". That is a fact about the *text of the
  program*. It is true before the program starts and stays true until it exits,
  because source code does not rewrite itself.
- **`litVal` is the literal's value.** This is cached in the ordinary sense, but
  a literal is a constant by definition: if the source says `1`, it is 1 forever.

**`$n` appears in neither field** — not its value, not its address, not the
scope it was found in. Every evaluation does a fresh lookup by name through the
current environment and a fresh type check on whatever comes back. Which is why
all of this behaves normally:

```raku
my $v = 1;
for ^4 { say $v + 1; $v = $v ~~ Int ?? "10" !! 1 }   # 2, 11, 2, 11
```

One AST node, four evaluations, a variable that changes both value and type
underneath it, and the right answer each time. The visits where `$v` holds a
`Str` still take the fast path, because `Str` is in the guard; had it become a
`DateTime`, that visit alone would have fallen through to the general path.

Why store anything at all, then? Because the classification is not free — it is
several branches: check both child node kinds, check the variable name's second
character, check the literal is not a bignum or a `Rat`. Asking that once per
*node* instead of once per *evaluation* is the whole optimisation. A `fib(29)`
run evaluates `$n - 1` about 1.6 million times and answers the question once.

It is less a cache than a **sticky note on the node**, written the first time
anyone reads it. The precedent sits directly above it in the same struct:
`simpleOp` does exactly the same thing for "is this a plain operator, or one of
the special-cased ones?", also decided once, also from the syntax alone.

## The guards

The fast path is declined, and the untouched general path runs, unless:

- **the name is a plain lexical** — second character a letter or `_`, which
  excludes every twigilled and special name (`$*dyn`, `$?FILE`, `$^a`, `$/`),
  each of which has its own lookup rules;
- **the value is `Int`, `Num`, `Str` or `Bool` with an empty `hashKind`**, which
  excludes `Proxy`, junctions, `DateTime`, `Duration`, `Failure`, allomorphs and
  undefined values;
- **for a subscript**: no adverb, not multidimensional, not a slice, a plain
  materialised `Array` (no `ext`, so no lazy sequence), and a **non-negative**
  in-range integer index.

`Rat` is deliberately left out even though it would probably qualify; so is
anything with a shared payload, so that a cached literal can never be mutated
through.

Falling through costs nothing incorrect. The only things the fast path evaluates
are variable lookups and cached literals, neither of which has side effects, so
nothing is ever evaluated twice.

## Why it is not a new node kind

The obvious implementation is a new `NK::` per shape — a superinstruction —
rewritten into the tree by a pass. This is deliberately not that.

A new node kind has to be taught to the parser, `AstDump`, `AstEmit` (which
serialises the tree for `--aot`), `Codegen`, `Lint`, and every `switch` over
`NK`; any of those missing a case is a silent wrong answer. Caching a verdict
*inside* the existing node needs none of them, degrades to the old behaviour by
construction, and leaves the AST dump and both compiling back ends seeing
exactly the tree they saw before.

It also needs no `-O` flag. The transformation cannot change semantics — same
`applyArith`, same operands — so it is always on, decided lazily per node.

## Measured

Alternating A/B, medians of seven, both binaries interleaved so that machine
drift hits each equally:

| kernel | base | specialised | delta |
|---|---:|---:|---:|
| vars — `$a OP $b` | 840.8 ms | 686.5 | **−18.3%** |
| lits — `$a OP 1` | 728.9 | 600.0 | **−17.7%** |
| fib | 478.6 | 422.4 | **−11.7%** |
| asg | 395.1 | 349.5 | **−11.5%** |
| index — `@a[$i]` | 195.3 | 178.4 | **−8.7%** |
| hash | 42.6 | 40.0 | −6.2% |
| loopsum | 208.6 | 209.6 | +0.5% |
| **ctl — method dispatch only** | 327.9 | 327.0 | **−0.3%** |

**The control is the point of the table.** It is a loop of pure method
dispatch, containing nothing the specialisation can touch, and it does not move.
Without it the other rows would only be evidence that the machine was quieter
the second time.

Roast came out identical: 196,590 assertions, 631 files fully passing, with not
one file moving in either direction.

## What went wrong on the way

Three mistakes, all invisible to reasoning and obvious to measurement. They are
the reason this chapter exists.

**The first version made the control 5.7% *slower*.** It bound the right
operand through a `std::optional` so the literal could be used by reference —
which cost the *general* path its copy elision, since `Value r = eval(...)`
constructs in place where `emplace` move-constructs. The lesson is structural:
**add an early exit, never restructure the shared code underneath it.** The
final version leaves everything after the fast path byte-identical.

**The literal cache was a reassignable `shared_ptr`.** Under parallel execution
a second thread rebuilding it could free the object while this thread held a raw
pointer into it. It is now allocated once and never freed: the node outlives
every evaluation, so there is nothing to reclaim before exit, and a racing
double-build leaks one `Value` instead of dangling.

**A negative subscript nearly changed behaviour.** The first `evalIndex`
version wrapped negative indices from the end, copying logic from a branch
further down that plain arrays never reach. `my $i = -1; @a[$i]` must throw
`X::OutOfRange`. Rakudo could not have caught this — it rejects the spelling at
compile time — so it was found by diffing against the **previous rakupp
binary**, which is now the habit: for a change that is supposed to be
semantically invisible, the baseline binary is the oracle, not another
implementation.

## Codegen already had this

Worth knowing before anyone ports it: `--exe` does not need it. The code
generator keeps variables in C++ locals, so there is no lookup and no copy, and
it calls `applyArith` directly rather than going through `evalBinary`, so the
temporal and hyper probes never existed there. With `-O` it goes one level
deeper still, into a raw `int64` lane that never materialises a `Value` for the
arithmetic at all (Chapter 26).

What this change really did was close part of the gap between the interpreter
and what the code generator had been doing all along — which is why the win was
large.

## Extending it

The same treatment fits other shapes, in rough order of expected value:

- `Assign` where the target and the left operand are the same variable
  (`$x = $x + 1`) — write through the slot instead of building and storing;
- `%h{$k}` and `%h<lit>`, the associative twin of the array path;
- `Unary` on a plain lexical (`-$x`, `!$x`, `++$i`);
- method calls on a plain lexical invocant, which is where the *other* half of
  the profile lives.

Whatever is added, the two rules that made this one safe still apply: **guard on
the runtime value every time, and leave the general path underneath untouched.**
