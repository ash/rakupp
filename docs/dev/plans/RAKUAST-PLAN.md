# RakuAST in rakupp — a design note (not implemented)

**Status: deferred, deliberately.** Nothing here is built. This records the
design we settled on and the measurements behind it, so the next person to look
at RakuAST does not have to re-derive them.

Written 2026-07-31 (rakupp v1.5.2+); every measurement and probe below
re-verified 2026-08-18 against **Rakudo 2026.07** — still the newest release —
and **rakupp 3.14.0**. Nothing in the design changed; the drift in the numbers
is noted where it happened. `grep -rn RakuAST src/` is still empty.

## What RakuAST is

Raku source as a tree of `RakuAST::` objects, reachable from Raku itself.
Four operations:

```raku
use experimental :rakuast;      # or use v6.e.PREVIEW

'say "Hello"'.AST               # source → tree
$ast.DEPARSE                    # tree   → source
$ast.EVAL                       # tree   → compiled and run
CHECK { $*CU }                  # the tree of the program being compiled, mutable
```

It exists because Rakudo's parse-time structures were private and unstable, so
no module could safely touch them. RakuAST replaces them with a documented,
roast-tested hierarchy — about 150 names directly under `RakuAST::`, more with
the nested namespaces, against ~40 node kinds in the whole of our `src/Ast.h`.

Upstream status, re-checked 2026-08-18: **unchanged**. 2026.07 is still the
latest release; RakuAST is still opt-in (`RAKUDO_RAKUAST=1`), and `$*RAKU.version`
is still `v6.d` by default — both confirmed by probing the local Rakudo, not by
reading release notes. The 2026.07 announcement records "a *lot* of work on
performance optimizations, and feature parity with the legacy compiler for
ecosystem modules", and the bootstrapped build reached 1228 of 1345 spectest
files at the time of the grant report. Converging, not arrived — and it has not
arrived in the year since either.

## Why it is worth doing at all — and why it is not urgent

Against: on our declared-Roast metric this is worth close to nothing.
`docs/internals/METAPROGRAMMING.md` records that roast contains **one**
incidental `RakuAST::` reference, and that is still exactly true — one file,
`S32-str/format.t:52`, asserting `Formatter.AST(...) ~~ RakuAST::Node`. No
test-count argument exists for building it.

The other thirteen roast mentions of the word are not tests of RakuAST at all;
they are `#?rakudo todo` markers of the form *"fixed in RakuAST"* /
*"passes correctly in RakuAST"* (S02-literals/pairs.t, S09-typed-arrays/hashes.t,
S12-subset/type-subset.t, S26-documentation/, S32-hash/adverbs.t). Those record
places where the *legacy* frontend is wrong and the new one is right — a signal
about Rakudo's transition, not work for us.

For: it is ecosystem compatibility. A module that uses RakuAST does not run
here at all — it is not a degradation, it is a hard failure. That is the whole
case, and it is a real one, but it is a *later* case than the module suites we
are working through now.

## The thing that decides the architecture

**Rakudo never executes RakuAST.** It parses to RakuAST and then compiles it
away — RakuAST → QAST → bytecode. The tree's granularity costs compile time
once, and then it is gone.

rakupp is a tree-walker. Our tree is not an intermediate representation, it is
the thing that runs, on every visit, forever. So "just use RakuAST as our AST"
means something completely different for us than it does for them.

### Measured: how much more granular RakuAST is

Same function, both representations:

```raku
sub fib($n) { $n < 2 ?? $n !! fib($n-1) + fib($n-2) }
```

| representation | nodes |
|---|---:|
| rakupp (`--ast`) | 17 |
| RakuAST (`.AST`, counted with `visit-children`) | 39 |

2.3× overall (the 2026-07-31 count said 38; one node of walker difference, same
conclusion), and the surplus sits exactly in the hot loop:

- **6 × `RakuAST::ArgList`, 4 × `RakuAST::ApplyInfix`, 4 × `RakuAST::Infix`,
  4 × `RakuAST::Var::Lexical`, 4 × `RakuAST::Name`, 3 × `RakuAST::IntLiteral`** —
  the full 2026-08-18 histogram, largest first.
- **4 × `RakuAST::Infix`** — every operator is a separate heap object. Our
  `Binary` holds `std::string op` inline plus the `simpleOp` dispatch cache
  (`src/Ast.h:124`), which exists *because* operator dispatch is hot enough to
  be worth memoising.
- **4 × `RakuAST::Name` + 6 × `RakuAST::ArgList`** — call plumbing; our `Call`
  holds the name inline.
- **2 × `RakuAST::Statement::Expression`**, plus `Blockoid`,
  `ParameterTarget::Var`, `Type::Setting` around the signature.

In the inner expression `fib($n-1) + fib($n-2)` we visit 9 nodes; the RakuAST
subtree under the `+` is 19. **~2.1× the node visits in the hottest loop**, each
an extra pointer chase into a separately allocated object. (Count it by finding
the `ApplyInfix` whose `.infix.operator eq '+'` inside the full sub — `.AST` on
the bare fragment throws *Undeclared routine: fib*, because `.AST` runs the
usual compile-time checks.)

Caveat, stated plainly: that is a structural node count, not a benchmark. We did
not build a RakuAST-walking rakupp and time it. It predicts direction and rough
magnitude, nothing finer. If a number is ever needed, the cheap decisive
experiment is to split `op` out of `Binary` into its own allocated node — the
single dominant change — and run `perf-guard --check` on fib/asg/loopsum.

### Measured: where our time actually goes

| | 2026-07-31 | 2026-08-18 |
|---|---:|---:|
| parse `showcase/perl/perl.raku` (1,635 lines) | 13.7 ms | 47 ms |
| full run of it over `examples/quicksort.pl` | 34.7 ms | 121 ms |
| `fib(29)`, one line of source | 755 ms | 1,457 ms |

The second column is ~2–3× slower in absolute terms for a boring reason:
`build/rakupp` is currently a **x86_64 binary running under Rosetta on an M3**
(`file build/rakupp`), as is the Homebrew Rakudo it is compared against. Read
the ratios, not the milliseconds — and note that a re-measurement here is only
meaningful once the build dir is native again.

Tree *construction* is a real share of short runs and startup — and matters more
in the WASM playground, where startup is the whole experience. Tree *walking* is
everything else. A RakuAST-shaped internal tree would pay both. `--exe` is
indifferent either way: the tree becomes C++ once.

## The design: a view on demand, with text as the bridge

Three options were on the table. The middle column is what we chose.

| | internal tree | cost when unused | needs a RakuAST→internal compiler |
|---|---|---|---|
| A. RakuAST *is* our AST | replaced | — (always paid) | no |
| **B. RakuAST as a view** | **unchanged** | **zero** | **no** |
| C. B + reverse bridge | unchanged | zero | yes |

**B is the plan, with C's capability obtained through DEPARSE rather than a
compiler.** The insight is that we already own a parser, so the way back from a
RakuAST tree is not a compiler at all:

```
our AST --.AST--> RakuAST --(user mutates)--> RakuAST
                                                 |
                                              .DEPARSE
                                                 v
                                             source text
                                                 |
                                        our existing parser
                                                 v
                                             our AST → run
```

The reverse direction is spelled `EVAL($ast.DEPARSE)`.

Nothing on the hot path changes. Parsing, node layout, and the eval switch are
untouched; a program that never says `.AST` never executes a line of this.

### The prerequisite already holds

A deparsed tree is not self-contained. It came from code that lived somewhere,
and its text mentions names from that somewhere — deparse any part of

```raku
sub fib($n) { $n < 2 ?? $n !! fib($n-1) + fib($n-2) }
```

and you get text referring to `$n` and to `fib`. Rakudo's `.EVAL` on a RakuAST
node compiles it **in the current lexical scope**; if our `EVAL` compiled the
text in a fresh empty scope instead, every one of those names would be
undeclared and the bridge would only ever carry literals.

It does not. These four run identically under rakupp and Rakudo — re-run on both engines
2026-08-18, `42 / 42 / 99 / 21` from each — and each answer is one that is only
reachable if the fragment saw the enclosing scope:

```raku
use MONKEY-SEE-NO-EVAL;

# 1. reads an outer lexical — an empty scope would throw "undeclared", not 42
my $x = 41;  say EVAL q[$x + 1];                     # 42

# 2. resolution reaches ROUTINES too, not just variables — a deparsed tree is
#    mostly calls, so this is the case that carries the weight
sub f($n) { $n * 2 };  say EVAL q[f(21)];            # 42

# 3. the strong one: `say` runs OUTSIDE the EVAL and still sees 99, so the
#    fragment wrote to the real container — the scope is shared, not a snapshot
my $y = 1;  EVAL q[$y = 99];  say $y;                # 99

# 4. `$p` is a parameter, a lexical in g's call frame: 7 × 3 proves the capture
#    works at depth, which is where `.EVAL` gets called from in real code
sub g($p) { EVAL q[$p * 3] };  say g(7);             # 21
```

Both engines agreeing is the actual claim — not that 42 is right in the
abstract, but that we already behave the way the reference implementation does,
so the design can lean on it with no new work.

Boundary, easy to over-read: this establishes that EVAL of *text* lands in the
right scope. It says nothing about whether `.DEPARSE` produces *faithful* text.
That is the next section.

## Where text leaks, and what to do about it

Text carries **syntax** faithfully and **values** only by luck. RakuAST nodes
may hold live runtime objects — `RakuAST::Literal.from-value($obj)` — and that
is exactly what compile-time transforms produce (the canonical constant-folder
example ends in `RakuAST::Literal.new($_)` over a computed value).

`DEPARSE` renders those through `.raku`. Measured on Rakudo, and reproduced
unchanged on 2026-08-18:

```
[1,2,3]   →  [1, 2, 3]                             # re-parses EQUAL but not IDENTICAL
closure   →  -> $x { #`(Block|3303385474920) ... }  # body replaced by an address comment
```

For the array: after the round trip `eqv` is True, `===` is **False**, and a
later `push` on the original is invisible to the copy. The closure case is
worse — that text re-parses into a block with an empty body. No error. Silently
wrong.

So the rule is: deparse-and-reparse is exact for trees that came *from* source,
and lossy for trees with values *spliced into* them.

**Mitigation — bind, do not render.** When `.DEPARSE` meets a `Literal` whose
value is not faithfully source-able, put the object in a side table and emit a
reference to a synthetic lexical:

```
$RAKUAST-LIT-7            # instead of  -> $x { #`(Block|…) ... }
```

then EVAL the text in a scope where those names are bound to the real objects.
Identity preserved, closures preserved, still no compiler. Text stays the
bridge, with a value channel running alongside it.

## Could the view be 1:1 with Rakudo's tree?

Asked 2026-08-18 — and **answered, then deferred with the rest**: the user's
call the same day was that RakuAST in any form is a detour from the main goal.
Nothing below is scheduled work. It is here so that whoever picks this up starts
from the measurements instead of the question.

It was worth answering because if it can, the gate at the end of
this document gets much stronger: instead of "our deparse re-parses to something
equivalent", the property becomes **"for source S, our `S.AST` is
node-for-node identical to Rakudo's `S.AST`"** — a direct diff against the
reference implementation, measurable as a percentage, per corpus file.

Short answer: **yes in principle, and the obstacles are enumerable rather than
structural** — but only if the comparison is defined on the *tree*, and only if
our parser starts recording a handful of surface facts it currently discards.

### RakuAST is not a normalised tree — it records surface syntax

This is the fact that decides the work. Thirteen pairs of *same meaning,
different spelling*, compared by walking both trees:

| pair | Rakudo | rakupp |
|---|---|---|
| `say(1)` / `say 1` | **different** (`Call::Name` vs `Call::Name::WithoutParentheses`) | same |
| `say 1 unless $x` / `unless $x { say 1 }` | **different** (`StatementModifier::Unless` vs `Statement::Unless`) | same *dump*, but `IfStmt::modifier` holds it |
| `.say for @a` / `$_.say for @a` | **different** (`Term::TopicCall`) | same |
| `%h<k>` / `%h{'k'}` | **different** (`Postcircumfix::LiteralHashIndex` vs `::HashIndex`) | same |
| `sub f($a) returns Int` / `sub f($a --> Int)` | **different** (`Trait::Returns` vs a `Type::Simple` in the signature) | same (`SubDecl::retType` merges `of` / `returns` / `-->`) |
| `do { $x }` / `($x)` | different | different |
| `while $x` / `until !$x` | different | different |
| `for @a { }` / `for @a -> $_ { }` | different | different |
| `$x .= Str` / `$x = $x.Str` | different | different |
| `$x.Str` / `$x."Str"()` | different | different |
| `@a[0]` / `@a.[0]` | same | same |
| `q{a}` / `Q[a]` / `"a"` / `'a'` | **same** — all four are `QuotedString` + `StrLiteral` | different (`StrLit` vs `InterpStr`) |
| `<a b>` / `qw{a b}` | same classes, different `processors` (`<words val>` vs `<words>`) | same |

Two things follow. First, quoting flavour is one worry we do *not* have: Rakudo
throws it away too. Second, extra precision on our side is free — we simply do
not emit it — while missing precision is fatal to an exact match.

### What we would have to start recording

Checked against `src/Ast.h`, not against the `--ast` dump — the dump prints less
than the nodes hold, and reading the dump alone overstates the gap. We already
keep `IfStmt::isUnless` and `::modifier`, the same `modifier` flag on
`WhileStmt` / `ForStmt` / `GivenStmt`, `Unary::postfix`, `ForStmt::vars`,
`MethodCall::mutate` and `::methodExpr`. Genuinely absent today:

1. **parens vs listop on `Call`** — no flag exists (`ListExpr::parenned` is a
   different thing).
2. **implicit topic on `MethodCall`** — `.say` synthesises a `VarExpr $_`
   indistinguishable from a written `$_.say`.
3. **angle vs brace subscript on `Index`** — `%h<k>` and `%h{'k'}` both reduce
   to a `StrLit` index.
4. **which spelling produced a return type** — `SubDecl::retType` is one string
   for `of` / `returns` / `-->`.

Each is a bool or a small enum set at parse time. None is on the hot path, none
changes evaluation, and none costs a byte at run time beyond the node itself.
That is the whole bill found so far; it is a bill for *parse-time bookkeeping*,
which is exactly the cheap kind.

### The oracle must be the tree, not the text

Neither of the obvious comparison surfaces survives contact with real code:

- **`.DEPARSE` has holes in Rakudo itself.** `showcase/perl/perl.raku` cannot be
  deparsed at all: *"Deparsing RakuAST::Regex::Nested objects not yet
  implemented"*. Bisected, the trigger is the regex goal-matching operator —
  `/ a ~ b c /`. 23 of 24 sampled `examples/` and `showcase/` files deparse
  fine; the regex-heavy one does not.
- **`.raku` has the same hole** (*"No .raku method implemented for
  RakuAST::Regex::Nested objects yet"*), which is a pity, because otherwise it
  is an excellent oracle: a canonical, aligned constructor dump —
  `RakuAST::VarDeclaration::Simple.new(sigil => "\$", …)` — that is exactly the
  tree and nothing else.

So the harness should walk `visit-children` and serialise class name **plus
scalar attributes**. Attributes are not optional: `<a b>` and `qw{a b}` have
identical class shapes and differ only in `processors`.

### Two traps in building that harness

- **`.AST` runs the compiler, not just the parser.** Undeclared variables throw
  (*"Variable '$x' is not declared"*), so every corpus snippet must be
  self-contained — and `BEGIN` blocks **execute** at `.AST` time. A probe of
  `BEGIN { say 1 }` printed `1` while merely building the tree.
- **Rakudo synthesises nodes with no source token**: `Blockoid`,
  `StatementList`, `Type::Setting`, and an implicit `VarDeclaration::Simple` for
  each `sub`. Exactness means reproducing those too.

### How big the target actually is

| corpus | nodes | distinct `RakuAST::` classes |
|---|---:|---:|
| `showcase/perl/perl.raku` (1,635 lines) | 22,046 | 107 |
| 44 files from `examples/` + `showcase/` | 24,442 | 125 |

39 classes cover 95% of all nodes; 26 of the 125 appear three times or fewer.
So the vocabulary for *real* code is ~125 of the ~150 names, and the shape of
the work is a steep head and a long thin tail — the head gets you a view that
works, the tail is what 1:1 costs.

The honest caveat: RakuAST is still under active parity work upstream, so a 1:1
claim is pinned to a Rakudo version and needs re-checking each release. That
argues for the diff being a *reported percentage per corpus*, not a boolean.

## Scope for a first cut

In, roughly in order:

1. `.AST` on `Str` — build the RakuAST view from our tree. Comparable in kind to
   `src/AstDump.cpp` (160 lines) and `src/AstEmit.cpp` (243 lines), which
   already walk this tree for `--ast` and `--aot`. This is a third emitter,
   against a much larger target vocabulary.
2. `.DEPARSE` — RakuAST → source, including the synthetic-lexical escape above.
3. `.EVAL` on a RakuAST tree — `EVAL(.DEPARSE)` in the caller's scope.
4. `visit-children` / `@*LINEAGE` — walking is most of what published code does
   with RakuAST, and it costs little once the nodes exist.

Out, for a first cut:

- **`CHECK { $*CU }`.** Same trick works — deparse the mutated compilation unit,
  re-parse it, run that instead, one extra parse for programs that use it — but
  it is the most invasive part of the surface and the least used. A version
  without it is still useful.
- The full class vocabulary. Cover what real code touches; let the rest throw a
  clear "not implemented" rather than a wrong answer.

## The gate

The Raku docs themselves warn that `.DEPARSE` "may not create valid executable
code". Since this deparser would be ours, the property test writes itself:

> for a corpus of source: `parse → .AST → .DEPARSE → parse` must yield an
> equivalent tree, **and** running both must produce identical output.

Three corpora already exist to point it at — `showcase/`, raku-corpus, and
roast. That gate is what makes the feature trustworthy; without it, a deparser
is a generator of plausible-looking wrong programs.

A strictly stronger gate is available if the view is built for exactness: diff
our tree against Rakudo's `.AST` node for node, and report the match rate per
corpus file. See *Could the view be 1:1 with Rakudo's tree?* above for what that
costs and why the diff has to be run on the tree rather than on `.DEPARSE`.

## Sources

- <https://dev.to/lizmat/series/23109> — "RakuAST for Early Adopters", five
  parts; the motivation piece is *So why is there RakuAST in the first place?*,
  and *Walking* / *Shaking the RakuAST Tree* cover traversal and mutation.
- <https://news.perlfoundation.org/post/sseifert_rakuast_final> — the grant
  report, with the bootstrap figures.
- <https://docs.raku.org/type/RakuAST> — the type documentation, including the
  `.AST` / `.DEPARSE` caveats quoted above.
- <https://rakudo.org/post/announce-rakudo-release-2026.06> — the optimizer
  stage and constant folding of pure infix operators.
- <https://rakudo.org/post/announce-rakudo-release-2026.07> — the newest release
  as of 2026-08-18: performance work and ecosystem-module parity for RakuAST,
  still with no switch of the default frontend.
