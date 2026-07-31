# Node specialization — fast paths for the shapes hot loops are made of

A follow-on from [PERF-CAMPAIGN.md](PERF-CAMPAIGN.md), which established where
interpreter time goes: **heap allocation ~31%, `Value` copies ~11%**, and that
the only changes which have ever paid are the ones that *remove work or
allocation*. This is such a change. Measured on 2026-07-31.

## What it is

`evalBinary` and `evalIndex` recognise a handful of syntactic **shapes**, record
that verdict on the node, and take a short path that skips what the general one
must do for the general case:

| shape | example |
|---|---|
| `$var OP literal` | `$n < 2`, `$n - 1` |
| `literal OP $var` | `2 * $n` |
| `$var OP $var` | `$a + $b` |
| `@arr[$var]` / `@arr[literal]` | `@a[$i]`, `@a[0]` |

Three costs disappear on those paths:

1. **The variable's copy.** `eval()` on a `VarExpr` returns the value *by value* —
   376 bytes, five `std::string`s, eleven refcount increments. The fast path
   takes a `Value*` from the environment and hands it to `applyArith`, which
   already took `const&`.
2. **The literal's reconstruction.** `1` in `$n - 1` was rebuilt into a fresh
   `Value` on every visit. It is now built once and kept on the node.
3. **The probes that cannot apply.** Every binary op ran two `std::string`
   comparisons for DateTime/Duration handling plus the hyper and Z/X checks.
   None can match two plain scalars.

For `@arr[$i]` the win is the same in kind: the general path opens by copying
the whole container into a local `Value` in order to read one element out of it.

## Why these shapes, and not constant folding

The idea started as "let `-O` fold the tree before walking it". That was
measured first, with [tools/ast-opportunity.raku](../../tools/ast-opportunity.raku),
which reads `rakupp --ast` and counts the patterns a tree optimizer would
rewrite. Across 48 real files — the showcase interpreters, the examples, the
tools — 51,353 AST nodes:

| pattern | sites | per 1k nodes |
|---|---:|---:|
| constant folding (both operands literal) | 37 | 0.7 |
| **binary with ONE literal operand** | **1,282** | **25.0** |
| `$x = $x + …` self-update | 24 | 0.5 |
| constant condition | 0 | 0.0 |

Classical constant folding has essentially nothing to fold in real Raku: 0.07%
of nodes, and none of it in a loop. Dead branches: none at all. What *is*
plentiful is operands — the shapes above — and those sit in loop conditions and
index arithmetic, where they run millions of times.

That is the whole reason this is a specialization pass and not a folding pass.
Re-run the tool before assuming any other classical optimization is worth
building; the static count is an upper bound on the opportunity and it took two
minutes to get.

## What "cached" means here

"Cache" is a misleading word for this, so be precise about it: in most contexts
caching means *remembering a computed result so you do not recompute it*.
**Nothing about the result is remembered.** What is remembered is a fact about
the source code.

The entire mechanism is two extra fields on each `Binary` node — one byte and
one pointer. There is no table, no map, no key, nothing global:

```cpp
mutable signed char fastShape = -1;   // which shape this node is
mutable const void* litVal = nullptr; // the literal's Value, for shapes 1 and 2
```

Walk one node, `$n - 1`, through its life:

**After parsing** the tree holds a `Binary` whose fields say nothing yet:

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

So the two fields hold two different kinds of thing:

- **`fastShape` is a classification of the syntax** — "this expression is
  written as variable-operator-literal". That is a fact about the *text of the
  program*. It is true before the program starts and stays true until it exits,
  because source code does not rewrite itself.
- **`litVal` is the literal's value** — the `1` in `$n - 1`. This is cached in
  the ordinary sense, but a literal is a constant by definition: if the source
  says `1`, it is 1 forever.

**`$n` appears in neither field** — not its value, not its address, not the
scope it was found in. Every evaluation does a fresh lookup by name through the
current environment and a fresh type check on whatever comes back. Which is why
all of this behaves normally:

```raku
my $v = 1;
for ^4 { say $v + 1; $v = $v ~~ Int ?? "10" !! 1 }   # 2, 11, 2, 11 — ONE node
```

One AST node, four evaluations, a variable that changes both value and type
underneath it, and the right answer each time (`1+1`, `"10"+1`, …). The visits
where `$v` holds a `Str` still take the fast path because `Str` is in the guard;
had it become a `DateTime`, that visit alone would have fallen through to the
general path.

Why store anything at all, then? Because the classification is not free — it is
several branches: check both child node kinds, check the variable name's second
character, check the literal is not a bignum or a Rat. Asking that once per
*node* instead of once per *evaluation* is the whole optimization. A `fib(29)`
run evaluates `$n - 1` about 1.6 million times and answers the question once.

It is less a cache than a **sticky note on the node**, written the first time
anyone reads that node: "this one is shape 1, and here is the constant." The
precedent sits directly above it in the struct — `simpleOp` does exactly the
same thing for "is this a plain operator, or one of the special-cased ones?",
also decided once, also from the syntax alone.

## The guards

The fast path is declined, and the untouched general path runs, unless:

- the name is a plain lexical — second character a letter or `_`, which excludes
  every twigilled and special name (`$*dyn`, `$?FILE`, `$^a`, `$/`), each of
  which has its own lookup rules;
- the value is `Int`, `Num`, `Str` or `Bool` with an **empty `hashKind`**, which
  excludes Proxy, Junction, DateTime, Duration, Failure, and undefined values;
- for a subscript: no adverb, no `:exists`/`:delete`, not multidim, not a slice,
  a plain materialised `Array` (no `ext`, so no lazy sequence), and a
  **non-negative** in-range integer index.

`Rat` is deliberately left out even though it would probably qualify; so is
anything with a shared payload, so that a cached literal can never be mutated
through.

Falling through costs nothing incorrect: the only things the fast path evaluates
are variable lookups and cached literals, neither of which has side effects, so
nothing is evaluated twice.

## Why it is not a new node kind

The obvious implementation is a new `NK::` for each shape — a superinstruction —
rewritten into the tree by a pass. This is not that, deliberately. A new node
kind has to be taught to the parser, `AstDump`, `AstEmit` (`--aot` serialises
the tree), `Codegen`, `Lint`, and every `switch` over `NK`, and any of those
missing a case is a silent wrong answer. Caching a verdict *inside* the existing
node needs none of them, degrades to the old behaviour by construction, and
leaves `--aot`, `--exe` and the AST dump seeing exactly the tree they saw before.

It also needs no `-O` flag. The transformation cannot change semantics — same
`applyArith`, same operands, and `tagTemporal` provably no-ops for the values the
guard admits — so it is always on, decided lazily per node, exactly like the
`simpleOp` field that was already there.

## Measured

Alternating A/B, medians of 7, both binaries interleaved so drift hits each
equally (see PERF-CAMPAIGN's note on this):

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

The control is the point of the table. It is a loop of pure method dispatch,
containing nothing the specialization can touch; it does not move. Without it
the other rows would only be evidence that the machine was quieter the second
time.

`perf-guard --check` against the recorded v1.5.1 baseline agrees: fib −14.6%,
asg −11.9%, hash −10.7%, loopsum +0.5%.

Roast is unchanged: **196,590 assertions, 631 files fully passing** — identical
to the run before the change, with not one file moving in either direction.
Local suite 243/243.

## What went wrong on the way

Both mistakes were invisible to reasoning and obvious to measurement. They are
the reason this document exists.

**The first version made the control 5.7% SLOWER.** It bound the right operand
through a `std::optional` so the literal could be used by reference — which cost
the *general* path its copy elision, since `Value r = eval(...)` constructs in
place where `emplace` move-constructs. The lesson is structural: add an early
exit, never restructure the shared code underneath it. The final version leaves
everything after the fast path byte-identical.

**The literal cache was a reassignable `shared_ptr`.** Under `RAKUPP_PARALLEL` a
second thread rebuilding it could free the object while this thread held a raw
pointer into it. It is now allocated once and never freed: the node outlives
every evaluation, so there is nothing to reclaim before exit, and a racing
double-build leaks one `Value` instead of dangling.

**A negative subscript nearly changed behaviour.** The first `evalIndex` version
wrapped negative indices from the end, copying logic from a branch further down
that plain Arrays never reach. `my $i = -1; @a[$i]` must throw `X::OutOfRange`.
Rakudo could not have caught this — it rejects the spelling at compile time — so
it was found by diffing against the **previous rakupp binary**, which is now the
habit: for a change that is supposed to be semantically invisible, the baseline
binary is the oracle, not another implementation.

## Codegen already had this

Worth knowing before anyone ports it: `--exe` does not need it. Codegen keeps
variables in C++ locals, so there is no lookup and no copy, and it calls
`applyArith` directly rather than going through `evalBinary`, so the temporal
and hyper probes never existed there. With `-O` it goes further still — a typed
int lane that works on the raw `long long` and never materialises a `Value` for
the arithmetic at all:

```cpp
if (!(rtIntBox(v_sn))) break;
long long __ln3; if (rakupp::add_ovf(v_sn.i, 1LL, &__ln3)) break;
if (rtIntSlot(v_sn)) v_sn.i = __ln3; else v_sn = Value::integer(__ln3);
```

That is the same idea one level deeper. What this change really did was close
part of the gap between the interpreter and what codegen had been doing all
along — which is why the win was large.

Two residual costs do remain in **non-`-O`** codegen, and they were measured
rather than guessed. Taking the generated C++ for a 3M-iteration literal-heavy
loop, hand-editing one temporary at a time, compiling each variant with
identical flags and interleaving the runs (medians of 7, outputs verified
identical):

| variant | time | delta |
|---|---:|---:|
| as generated | 640.5 ms | — |
| op name hoisted to a `static const std::string` | 636.1 | −0.7% |
| **literal hoisted to a `static const Value`** | **598.1** | **−6.6%** |
| both | 602.0 | −6.0% |

So the op-string temporary is free — it is a one-character SSO string, and
hoisting it is within noise. The whole win is `Value::integer(1LL)` being
rebuilt every iteration: a 376-byte object with five strings and eleven
shared_ptrs, constructed and destroyed three million times to hold the number 1.

That is the same finding as on the interpreter side, in a different costume, and
it suggests the same fix: emit each distinct literal once at file scope and
reference it. Not implemented — it only affects `--exe` *without* `-O` (with
`-O` the int lane never builds the Value at all), and it needs its own pass
through the `--exe` golden tests and `run-optbench`.

## Extending it

The same treatment fits other shapes. In rough order of expected value:

- `Assign` where the target and the left operand are the same variable
  (`$x = $x + 1`) — write through the slot instead of building and storing.
- `%h{$k}` and `%h<lit>`, the associative twin of the array path.
- `Unary` on a plain lexical (`-$x`, `!$x`, `++$i`).
- Method calls on a plain lexical invocant, which is where the *other* half of
  the profile lives.

Whatever is added, the two rules that made this one safe still apply: guard on
the runtime value every time, and leave the general path underneath untouched.
