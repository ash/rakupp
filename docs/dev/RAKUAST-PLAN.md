# RakuAST in rakupp — a design note (not implemented)

**Status: deferred, deliberately.** Nothing here is built. This records the
design we settled on and the measurements behind it, so the next person to look
at RakuAST does not have to re-derive them. Date of the numbers: 2026-07-31,
against Rakudo 2026.07 and rakupp at v1.5.2+.

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

Upstream status at time of writing: still opt-in (`RAKUDO_RAKUAST=1`), 6.d is
still the default language version, and the bootstrapped build reaches 1228 of
1345 spectest files. Converging, not arrived.

## Why it is worth doing at all — and why it is not urgent

Against: on our declared-Roast metric this is worth close to nothing.
`docs/METAPROGRAMMING.md` already records that roast contains **one**
incidental `RakuAST::` reference. No test-count argument exists for building it.

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
| RakuAST (`.AST`, counted with `visit-children`) | 38 |

2.2× overall, and the surplus sits exactly in the hot loop:

- **4 × `RakuAST::Infix`** — every operator is a separate heap object. Our
  `Binary` holds `std::string op` inline plus the `simpleOp` dispatch cache
  (`src/Ast.h:124`), which exists *because* operator dispatch is hot enough to
  be worth memoising.
- **4 × `RakuAST::Name` + 6 × `RakuAST::ArgList`** — call plumbing; our `Call`
  holds the name inline.
- **2 × `RakuAST::Statement::Expression`**, plus `Blockoid`,
  `ParameterTarget::Var`, `Type::Setting` around the signature.

In the inner expression `fib($n-1) + fib($n-2)` we visit 9 nodes; the RakuAST
shape is ~16. **~1.8× the node visits in the hottest loop**, each an extra
pointer chase into a separately allocated object.

Caveat, stated plainly: that is a structural node count, not a benchmark. We did
not build a RakuAST-walking rakupp and time it. It predicts direction and rough
magnitude, nothing finer. If a number is ever needed, the cheap decisive
experiment is to split `op` out of `Binary` into its own allocated node — the
single dominant change — and run `perf-guard --check` on fib/asg/loopsum.

### Measured: where our time actually goes

| | |
|---|---:|
| parse `showcase/perl/perl.raku` (1,635 lines) | 13.7 ms |
| full run of it over `examples/quicksort.pl` | 34.7 ms |
| `fib(29)`, one line of source | 755 ms |

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

It does not. These four run identically under rakupp and Rakudo — each answer is
one that is only reachable if the fragment saw the enclosing scope:

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

`DEPARSE` renders those through `.raku`. Measured on Rakudo:

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

## Sources

- <https://dev.to/lizmat/series/23109> — "RakuAST for Early Adopters", five
  parts; the motivation piece is *So why is there RakuAST in the first place?*,
  and *Walking* / *Shaking the RakuAST Tree* cover traversal and mutation.
- <https://news.perlfoundation.org/post/sseifert_rakuast_final> — the grant
  report, with the bootstrap figures.
- <https://docs.raku.org/type/RakuAST> — the type documentation, including the
  `.AST` / `.DEPARSE` caveats quoted above.
- <https://rakudo.org/post/announce-rakudo-release-2026.06> — current status;
  the optimizer stage and constant folding of pure infix operators.
