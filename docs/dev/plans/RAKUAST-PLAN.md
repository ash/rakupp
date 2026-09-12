# RakuAST in rakupp — design note and implementation plan

**Status: under implementation — P0, P2c, P3 and P1 are in the tree, with
`--rakuast`, the tree oracle and P1-L10N as of 2026-09-12; P4 is open. Part IV
is the log, Part III has the decision, the refreshed evidence, and the order.** Part I (below) is the design note settled 2026-07-31 and
re-verified 2026-08-18 — nothing in it is reopened. Part II (second half of this
document, added 2026-09-01) phases the implementation; the trigger is the
mainstreaming announcement
([dev.to/lizmat/mainstreaming-rakuast](https://dev.to/lizmat/mainstreaming-rakuast-49j8)):
RakuAST becomes Rakudo's **default** frontend at 2026.09 (announced for ~Sept 26,
not yet shipped; the legacy frontend goes behind `RAKUDO_LEGACY=1` and is removed
at 6.e later in 2026), `macro` is removed upstream, and slangs move to new hooks.
Ecosystem modules will now adopt RakuAST for real — and a module that uses it
does not degrade under rakupp, it hard-fails. The 2026-08 eco-sweep already
counts **12 dists (the Intl/L10N family) failing on `.AST`**
([ECOSWEEP-2026-08](../findings/ECOSWEEP-2026-08.md)).

Part I written 2026-07-31 (rakupp v1.5.2+); every measurement and probe below
re-verified 2026-08-18 against **Rakudo 2026.07** — the newest release at the time —
and **rakupp 3.14.0**. Nothing in the design changed; the drift in the numbers
is noted where it happened. (`grep -rn RakuAST src/` was empty then; as of
2026-09-01 it finds two comment lines, Builtins.cpp:6866-6868 as of 2026-09-11, and nothing else.)

# Part I — the design note

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

Upstream status, re-checked 2026-08-18 against 2026.07: unchanged then —
RakuAST opt-in (`RAKUDO_RAKUAST=1`), `$*RAKU.version` `v6.d`, the 2026.07
announcement recording "a *lot* of work on performance optimizations, and
feature parity with the legacy compiler for ecosystem modules", the
bootstrapped build at 1228 of 1345 spectest files at the grant report.
**Superseded 2026-09-11.** Rakudo 2026.08 (release #196) shipped 2026-08-21;
its announcement says most of the release's work went into the RakuAST
frontend — "several dozen fixes, largely found by testing modules from the
ecosystem" — plus code-generation speedups, and names the next release for
2026-09-26. The default did **not** flip in 2026.08: measured on the installed
2026.08, a `macro` (removed under RakuAST) still compiles stock and under
`RAKUDO_LEGACY=1`, and only `RAKUDO_RAKUAST=1` rejects it; `$*RAKU.version` is
still `v6.d`. The flip is the mainstreaming post of 2026-08-31 (cited at the
top): 2026.09 is the first release to use RakuAST by default, the legacy
frontend stays reachable behind `RAKUDO_LEGACY=1` until 6.e, and "if you are on
2026.08 you only need `RAKUDO_RAKUAST=1`" is the migration advice. So
"converging, not arrived" is no longer the state — it arrives on a date, and
this plan runs against that clock, not an open-ended one.

## Why it is worth doing at all — and why it is not urgent

(The urgency half was re-judged 2026-09-11 — Part III, *The decision*. The
weighing below is kept as it was written.)

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
$RAKUAST-LIT7             # instead of  -> $x { #`(Block|…) ... }
```

then EVAL the text in a scope where those names are bound to the real objects.
Identity preserved, closures preserved, still no compiler. Text stays the
bridge, with a value channel running alongside it. (Spelling note, verified
2026-09-01: the name must not put a hyphen before a digit — `$RAKUAST-LIT-7`
parses as subtraction, in rakupp and Rakudo alike. `$RAKUAST-LIT7` is fine.)

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
- <https://dev.to/lizmat/mainstreaming-rakuast-49j8> — the 2026-08 announcement
  that triggered Part II: RakuAST default at 2026.09, legacy removed at 6.e,
  `macro` removed, slangs re-hooked (Slangify assists), some P5xxx modules
  disabled.

---

# Part II — the implementation plan (2026-09-01)

Phases for the option-B view. This part phases the design above; it does not
reopen it. Drafted against the sources at v3.23.0+ (Part III re-anchors every citation to v3.26.0 — the numbers in Part II are the 2026-09-01 ones), with every file:line below
read in the current tree; the plan was then adversarially reviewed on three
axes (speed, coverage, completeness) and the findings folded in — the build
seam, the parallel-mode registry, the two-period oracle, and the refusal spec
all came out of that review.

## The hard constraint, restated

No side effects on speed (perf-guard kernels, startup, binary size, `--slim`,
the WASM bundle) or coverage (Roast pass counts, module battery, ecosystem
green list). Each phase states how neutrality is achieved **by construction**,
and each ends with the full gate battery green:

1. `tools/perf-guard.raku --check` exit 0 on an idle machine, **plus** an
   explicit A/B against the pre-phase commit (same arch, same machine, per the
   6E-PLAN P5 precedent). The A/B is not optional: a uniform small cost — the
   exact shape a stray hook would take — reads INCONCLUSIVE under the
   localization defence, and only the A/B resolves it.
2. Roast: three runs at the **pinned Roast revision** (currently b2cbe8a42,
   2026-06-12), union list archived, `comm -23 vPREV-union vNEXT-union` empty —
   **plus** a per-file pass-counts diff: any file whose pass count *dropped* is
   listed and explained. The union list is blind to erosion inside partial
   files; the counts diff is the tripwire. (Needs a small `run-roast` addition:
   archive per-file counts next to the file list.)
3. `rakupp t/run.raku` green, plus the phase's own new regression cases.
4. `tools/run-optbench.raku` — interpreter, `--exe`, `--exe -O`, Rakudo
   identical output.
5. `t/slim/run.raku` size budgets on **both darwin slices** (arm64 and
   x86_64 — the binding ledge is ~52 KB on x86_64-darwin against the 6.25 MB
   budget; the ~471 KB figure in early drafts was the cross-arch delta, not
   headroom) and `tools/slim-diff.raku` byte-identical.
6. Module battery not regressed (checkout must be re-established first — see
   Open items; gate 6 must actually run, not be waived).
7. Startup (`tools/bench/startup.raku`) and non-slim binary sizes (rakupp CLI,
   plain `--exe` hello) recorded before/after — recorded numbers per the
   BENCHMARKS.md convention, not budgets.
8. **WASM row**: rebuild via `rakujs/build.sh`, run `rakujs/smoke.cjs`, record
   the `rakujs.wasm` byte and gzipped deltas. `build.sh` sweeps all of
   `src/*.cpp` into the bundle and bundle bytes ARE playground startup; no
   other gate watches this.

**Release-train rule**: P0 never ships in a public release without P2. Name
existence alone flips ecosystem feature-detection (`try ::('RakuAST::Node')`,
`.^find_method('DEPARSE')`) from a working fallback path onto a RakuAST path
that dies; existence and a surface that answers must land together.

## Architecture decisions carried into every phase

- **The RakuAST classes live in their own immutable registry, never in
  `classes_`.** Executing ClassDecls would tail-alias bare `Statement` /
  `Expression` into general resolution (Interpreter.cpp:8613-8621) — a
  semantics leak — and seeding `classes_` lazily is a data race: in default
  parallel mode `engageGil` never takes the GIL for compute
  (Interpreter.cpp:3484), so inserting into the map while workers read it
  lock-free is the exact rehash-under-reader crash the `noteSymbolMutation`
  machinery polices. Instead: build the complete map of ClassInfos once and
  publish it via the codebase's own `PublishedOnce` idiom (release-store /
  acquire-load, Ast.h:45-71) — a single atomic publish, safe post-freeze, safe
  in parallel mode, zero cost on every path that never misses. Consult it only
  where `classes_` has already missed: the NameTerm known-check
  (Interpreter.cpp:28459-28496 vicinity), the method-call miss path
  (Builtins.cpp:7075 vicinity), and the handful of `classes_.find` misses that
  RakuAST semantics needs (`~~` RHS resolution, `typeCheckBind` for
  `RakuAST::Node $x` params — enumerate at P0, expected 4-6 sites). Node
  instances carry `shared_ptr<ClassInfo>` in ObjectData directly, so dispatch
  and MRO walks are pointer chases that touch no map. Consequences for free:
  `resolveClassAlias`'s suffix scan never sees these names, so `Foo::Call`
  can never mis-alias to `RakuAST::Call` — no exclusion code needed; and a
  user's own `class RakuAST::Mine {}` (legal today, stays legal) lives in
  `classes_` and shadows naturally.
- **The trigger is a parser-set bool, not a source scan.** A `usesRakuAst`
  flag on `Program` (beside `langRev`, Ast.h:829-832), set when a
  `RakuAST::`-prefixed name or the `:rakuast` adverb is parsed. Zero startup
  work, and it works for EVAL'd units because EVAL parses through the same
  Parser. The module-load path checks it where `langRev_` is already
  save/restored (Interpreter.cpp:5810/5893), so a `require`d module
  materializes (one atomic publish) before its own execution.
- **Do not put the prefix into `isKnownTypeName`** — it is revision-agnostic
  and consulted by class-parent checks, param typing, and the tail-alias
  suppressor; gating happens where `sixE()` and the pragma state are in scope.
- **Build seam, stated because the default is wrong**: CMake globs `src/*.cpp`
  into `rakupp_rt` — the base runtime every `--slim` binary links — and only
  the explicit `RAKUPP_PARSE_SOURCES` list (CMakeLists.txt:139, Lexer+Parser)
  goes to the parse archive. Every new `RakuAst*.cpp` TU must be added to
  `RAKUPP_PARSE_SOURCES` explicitly, and every symbol the runtime side calls
  into them (the materializer, the view/deparse/eval entries) gets a throwing
  double in `src/stubs/stub_eval.cpp`, or `--slim=-eval` links break. The
  story is coherent — `.AST` *is* the parser, so its stub throws the same
  requires-eval error. CMakeLists.txt and the stubs file are on the P0/P1
  file lists. SlimScan learns `.AST`, `.DEPARSE`, `.EVAL`-on-tree, and
  `RakuAST::` names as F_EVAL uses (SlimScan.cpp:44-47); the slim hello binary
  contains none of this. No fifth slim feature unless P2 measurement shows
  `parse.a` growth threatening the darwin budgets — measure first, decide
  then.
- **`.AST` is Cool, not Str.** Verified on Rakudo 2026.08: `42.AST` and
  `<42>.AST` both return `RakuAST::StatementList`. The fast ladder arm sits
  late (after the hot Part3 Str arms, beside the Cool `EVAL` arm,
  MethodCallPart3.cpp:430) guarded on Str **without** an allomorph exclusion;
  the method-miss path handles any other Cool by toStr-then-parse. Buf/Blob
  still refuse.
- **`.DEPARSE` / `.EVAL` / `visit-children` are not ladder arms at all**: they
  are `Callable::builtin` Code values on the `RakuAST::Node` ClassInfo,
  inherited by every node class via the normal parent walk — they exist only
  after materialization, and cost nothing before it.
- **The view builder is a read-only walk.** It follows AstSerial's pattern
  (one case per NK via the EXPR_KINDS/STMT_KINDS X-macros, **throwing**
  default — never AstDump's silent `default:`), writes no
  `DecidedOnce`/`PublishedOnce` cache field, dereferences no `padOwner`,
  clones nothing.
- **Nodes are plain ObjectData** over the registry ClassInfos, built directly
  (the `$*REPO` / `.CREATE` pattern), skipping the construction protocol.
- **The refusal is pinned to measured Rakudo, not styled after a neighbor.**
  Verified on 2026.08: the class is `X::Experimental`, the message verbatim
  `Use of RakuAST is experimental; please 'use experimental :rakuast;'`, and
  `::('RakuAST::IntLiteral')` without the pragma returns a **Failure**, not
  the type. The gate runs BEFORE any registry consultation on both the plain
  and symbolic paths (the symbolic path returns a Failure carrying
  X::Experimental, mirroring the X::NoSuchSymbol pattern). Timing divergence
  recorded, not fought: Rakudo refuses at compile time (===SORRY!===), rakupp
  at first mention during the walk — same divergence class as the evalString
  timing notes.
- **Oracle, two periods.** Until Rakudo 2026.09 ships, the oracle is the
  installed **2026.08**: `.AST`/`.DEPARSE`/tree probes need no env var there
  (verified: `'42'.AST` works stock), but whole-program probes (the four scope
  probes, round-trip output identity) run **twice**, stock and
  `RAKUDO_RAKUAST=1`, to anticipate the default flip. On the 2026.09 release:
  re-pin, drop the env var, re-run the oracle suite as its own recorded step.
  Every published match-percentage names its oracle version.

## Entry criteria (before P0 lands)

- Re-establish the module-battery checkout and a fresh ecosystem source list
  on this machine (both are missing — machine-switch residue). Grep the
  ecosystem sources for `RakuAST::`, `::('RakuAST`, `find_method('DEPARSE'`,
  and `experimental :rakuast`; publish the hit list. This is the population
  the release-train rule protects.
- Build the pre-campaign same-arch binary for the A/B leg (RELEASING.md
  forbids trusting figures from a `-modified` tree; no second checkout exists
  yet).
- **Fudge-shield inventory**: rakupp rides Roast's `#?rakudo todo "fixed in
  RakuAST"` shields today (verified: S02-literals/pairs.t:84 is a genuine
  rakupp fail counted as shielded; S09-typed-arrays/hashes.t's `%h{Int}.of`
  wants Mu, gets Any). Run the eight RakuAST-marked files (13 lines at the pin) once with the todo
  directives stripped and archive which union memberships depend on which
  shields — when the pin advances past 2026.09 and upstream deletes those
  todos, the diff is then pre-explained instead of looking like RakuAST
  fallout. Where an underlying fail is cheap (`.of` ⇒ Mu), schedule it
  independently.
- Record the S32-str/format.t baseline (today: aborts at test 2 of 49, `No
  such method 'Callable' for invocant of type 'Format'`). It is the only Roast
  file that executes a `RakuAST::` name — a free canary: its count must not
  move during P0-P4, and any movement means a phase leaked behavior.
- When Rakudo 2026.09 ships: probe the actual 6.d gating rule before trusting
  the design note's — (a) bare 6.d + `RakuAST::IntLiteral.new(42)`, (b) with
  pragma, (c) `use v6.e.PREVIEW`, (d) `::('RakuAST::…')` in all three. If
  mainstreaming relaxed the pragma requirement, our gate follows measured
  Rakudo, not the note — otherwise the gate hard-refuses exactly the new
  adopter modules this plan exists to serve. Fold the four probes into the
  per-release oracle re-check.

## P0 — pragma gating + the RakuAST:: registry skeleton

**What lands.** `use experimental :rakuast` becomes real, and the ~39 head
class names exist — constructible, `~~ RakuAST::Node` works, MRO walks — with
no `.AST` yet. (Not publicly released before P2; see the release-train rule.)

- Parser: capture the `:rakuast` adverb. **Re-checked 2026-09-11: the spaced form already lands in `UseStmt::importArgs` (Parser.cpp:8749-8757 — the `:tag` capture that `use Prompt :prompt` needed), so what remains is the tight `experimental:rakuast` spelling and the lexical scope tracking.** As drafted — the
  spaced form falls through the arg-capture loop (Parser.cpp:7905-7927), the
  tight form is consumed but only `ver` kept (:7897). Push the pair into
  `UseStmt::importArgs` when the module is `experimental`; lexical parse-time
  state as an `experimentalScopes_` twin of `monkeyScopes_` (Parser.h:244-246,
  push/pop at 5517/5527). The capture stays **pure**: regression cases assert
  `use experimental :cached;` / `:pack;` (spaced and tight) remain silent
  no-ops under both revisions, interpreted and `--exe`, and no consumer of
  `importArgs` fires for module `experimental`.
- Interpreter: a per-compilation-unit flag beside `langRev_`
  (Interpreter.h:1604), set at Interpreter.cpp:4233, save/restored around
  module load where `langRev_` already is (5810/5893) — inside the measured
  ~1% 6.e-campaign machinery, which a pure-6.d program bypasses.
- The gate at the NameTerm known-check and the method-call miss path, per the
  pinned refusal spec above, ordered before registry consultation.
- `RakuAstClasses.cpp` (new TU, in `RAKUPP_PARSE_SOURCES`): the static table
  (name, parent, attrs) for the ~39 head classes + `RakuAST::Node`, the
  one-shot builder, the `PublishedOnce` publish. Stub doubles in
  `src/stubs/stub_eval.cpp`.
- SlimScan: `RakuAST::` names and the pragma count as F_EVAL uses.

**Files**: src/Parser.{cpp,h}, src/Interpreter.{cpp,h}, src/Builtins.cpp,
src/SlimScan.cpp, src/RakuAstClasses.cpp (new), src/stubs/stub_eval.cpp,
CMakeLists.txt.

**Speed neutrality by construction**: the prefix check sits on lookup paths
that today end in a throw; startup does zero new work; nothing enters
`classes_`; no node structs change; slim binaries unchanged. **Coverage**: no
Roast file at the pin uses the pragma or asserts X::Experimental (grep
verified, zero hits), so the gate flips nothing there; the battery grep from
the entry criteria bounds the ecosystem exposure.

**Acceptance**: full battery, plus regression cases — pragma accepted under
6.d; `RakuAST::IntLiteral` visible under `use v6.e.PREVIEW` without pragma;
refused under bare 6.d with the verbatim message, all three shapes (plain
name, `::('RakuAST::…')` answering a Failure, and `class RakuAST::Mine {}`
still working); bare `Node`/`Statement`/`Call` still unresolved after touching
RakuAST; a threaded program `require`-ing a RakuAST-using module; `--exe`
parity throughout.

**Size**: ~300-500 lines.

## P1 — the `.AST` view builder

**What lands.** `'source'.AST` builds the view from our tree, plus the four
parse-time surface facts the oracle needs.

- `RakuAstView.cpp` (new TU, parse archive): `buildExpr`/`buildStmt`, one case
  per NK via the X-macros, throwing default. All 49 NKs handled; kinds with no
  faithful mapping throw a clear "not implemented", never a wrong tree.
  Target: the ~39 classes covering 95% of corpus nodes.
- The `.AST` arm with Cool semantics (see architecture). `.AST` runs
  Lexer+Parser only. **Both recorded divergences from the design note carry
  over**: (1) rakupp surfaces syntax errors but not Rakudo's compile-time
  undeclared-variable errors; (2) **BEGIN executes at `.AST` time in Rakudo**
  (re-verified on 2026.08) and does not in rakupp. The oracle harness filters
  or flags corpus files containing BEGIN/CHECK/INIT so the Rakudo side's
  execution cannot contaminate the match run — same filter for P2's output
  comparison.
- The four surface facts, one constant store each on parse paths already
  taken, batched into **one** `kAstSerialVersion` bump (18→19 as of 2026-09-11 — it was 15 when drafted; one-time cache
  reparse, noted in release notes):
  - `Call::parenned` (sites Parser.cpp:4789/2311 true, 4904 false). The one
    field that cannot hide in padding: sizeof 80→88, nano-malloc bucket
    80→96. Hot offsets unchanged; the byte is never read at eval.
  - `VarExpr::synthTopic` at the bare-`.` site (Parser.cpp:4170) — hole @41,
    sizeof stays 296.
  - `Index::angleKey` (angle sites 1924-1994, 2063-2090, 1697-1715) — hole
    @35, sizeof stays 80.
  - `SubDecl::retTypeSpell` ('o'/'r'/'a') at the trait sites 6435-6438 and
    the sigRetType_ merge at 6278 — hole @191, sizeof stays 400 (re-measured 2026-09-11: the struct grew from 368 since the draft, the hole did not move).
  - Matching `F(io,…)` lines in AstSerial.cpp. While the version bumps anyway,
    verify (and fix if real) the two suspected pre-existing serializer gaps —
    `VarExpr::viaPseudoPkg`/`pseudoPkg` and `Param::userTraits` — so users pay
    one cache invalidation, not two.

**Files**: src/RakuAstView.cpp (new), src/RakuAstClasses.cpp,
src/MethodCallPart3.cpp, src/Ast.h (four fields, stated positions),
src/Parser.cpp (four stores), src/AstSerial.{cpp,h}, src/SlimScan.cpp,
CMakeLists.txt, src/stubs/stub_eval.cpp.

**Speed**: builder read-only; arm late; three of four fields padding-neutral;
Call's +8 B (bucket +16 B) is the one thing that could show, and it is paid at
**parse** time per Call node — so the A/B set is fib/subcall/method **plus**
`tools/bench/startup.raku` as an explicit old-vs-new A/B **plus** one
grammar-heavy parse (tools/bench grammar-json), per-kernel deltas quoted.

**Acceptance**: full battery, plus (a) `'…'.AST ~~ RakuAST::Node` over every
examples/ + showcase/ file that parses; (b) the **tree oracle** — serialize
class name + scalar attributes on both engines, diff per the two-period oracle
rule, reported as a match percentage per corpus file. The oracle number is
published and improved, not pass/fail; P1's pass/fail is the battery plus (a).
Regression cases include `42.AST` and `<42>.AST` (Rakudo-checked) and a Buf
invocant refusing.

**Size**: ~850-1,550 lines.

## P2 — `.DEPARSE` + the property gate

**What lands.** The renderer back to source, and the harness that makes it
trustworthy.

- `.DEPARSE` as a builtin Code method on `RakuAST::Node` (invokeMethod's
  builtin arm), renderer in `RakuAstDeparse.cpp` (parse archive), X-macro
  exhaustiveness discipline.
- Fidelity bounds recorded, not fought: StrLit keeps only the NFC value, so
  DEPARSE emits canonical quoting — which is what Rakudo does too (all four
  quote spellings are one QuotedString upstream), so parity costs nothing.
  Desugar shapes deparse as their lowered form for now.
- The property harness (tools/rakuast-roundtrip.raku): for a corpus,
  `parse → .AST → .DEPARSE → parse` must yield an equivalent tree AND running
  both must produce identical output. Corpora: showcase/, examples/,
  raku-corpus, a Roast slice. Raw counts per corpus; a file that cannot
  round-trip is fixed or listed with its reason.
- **Constructed-tree corpus, not just view-built trees.** The round-trip
  property only ever feeds the renderer fully-populated trees the view builder
  made; real consumers construct with `.new` — optional children unset,
  defaults in play — and mutate. Import a slice of **Rakudo's own
  t/12-rakuast suite** (rakudo repo, not roast: construct-with-`.new` →
  `.DEPARSE`/`.EVAL` with inline expectations, pre-oracled upstream) and run
  it here: at P2, DEPARSE must throw-clearly-or-render on every constructed
  shape, never crash. Raw counts published next to the round-trip numbers.

**Acceptance**: full battery (slim on both darwin slices — this is the phase
where `parse.a` grows most; the WASM delta decides whether the
`RAKUJS_NO_RAKUAST` stub-swap toggle in build.sh is worth building), plus the
round-trip property green on examples/ + showcase/, plus the t/12-rakuast
DEPARSE slice. After this phase `RakuAST::IntLiteral.new(42).DEPARSE` answers
`42` — the exact probe the live 6e matrix runs (gen-6e.raku:48-50), and the
matrix row can flip.

**Size**: ~1,000-1,400 lines.

## P3 — `.EVAL` on a tree + the side table

**What lands.** The bridge back: DEPARSE → EVAL in the caller's scope, with
the side table for unrenderable values.

- `.EVAL` builtin on `RakuAST::Node`: deparse; make a child Env
  (`sc->parent = tctx_.cur`, the phaser-runner pattern); `Env::define` each
  side-table entry as `$RAKUAST-LITn`; RAII-swap `tctx_.cur`; `evalString`;
  restore. The child Env isolates the synthetic names afterward while the
  parent link preserves the proven caller-scope visibility.
- The side table fills during DEPARSE when a Literal holds a value with no
  faithful rendering (closures, live objects — the `from-value` cases); bare
  user-called `.DEPARSE` renders the synthetic name too (same behavior class
  as Rakudo's address comment, but ours round-trips when EVAL'd — the point).
- MONKEY-SEE-NO-EVAL stays the accepted no-op it is (pre-existing, recorded).

**Acceptance**: full battery, plus the four scope probes rewritten as tree
EVALs (expected 42/42/99/21, two-period oracle), a closure/live-object case
proving identity survives (`===` after the trip — the case Rakudo's text
rendering silently loses), and the t/12-rakuast EVAL slice matching its inline
expectations. Confirm `.EVAL`-on-tree spellings are matched by
`nameEvalsCode` (Ast.h:354) so DeclCheck/SlimScan stay consistent.

**Size**: ~150-250 lines; evalString does the heavy work.

## P4 — traversal + widening driven by real modules

**What lands.** `visit-children` (Code callback, children in Rakudo's
documented order), `@*LINEAGE` (a dynamic defined in the callback's frame —
the `$*PACKAGE` define pattern), and vocabulary growth from ~39 toward the
~125 real-code classes, **pulled by an acceptance corpus of 2-3 real
RakuAST-using ecosystem modules** — candidate one is Slangify (named in the
mainstreaming post); the L10N/Intl family (the 12 `.AST`-blocked dists from
the eco-sweep) supplies the rest, refreshed by the entry-criteria grep on
post-2026.09 sources. Classes the corpus never touches stay unimplemented
with clear errors.

**Acceptance**: full battery, the acceptance modules' RakuAST paths
demonstrated (whole suites where unrelated features allow), the tree-oracle
percentage re-published. S32-str/format.t:52 becomes passable in principle,
but the file needs the Format/Formatter surface (it aborts at test 2 today) —
a separate campaign; do not claim the file. The canary discipline from the
entry criteria still applies.

## Mainstreaming consequences, recorded

- **The oracle**: two periods, per the architecture section. The 1:1 match
  rate stays a reported percentage pinned to a Rakudo version, re-checked per
  release (upstream is still moving).
- **`macro` is dead upstream.** We never built it; the METAPROGRAMMING.md
  Phase 5 frontier loses `macro`/`quasi` at the cost of a doc edit. What
  replaces that space is RakuAST itself — this plan.
- **Slangs re-hook** (Slangify assists). Out of scope (Deferred); Slangify's
  own RakuAST usage makes it P4 corpus material.
- **Roast will move.** The pin (b2cbe8a42) predates 2026.09; upstream may add
  real RakuAST tests, and the "fixed in RakuAST" shields we verifiably ride
  will vanish as the legacy frontend dies. The entry-criteria inventory
  pre-explains that diff; advancing the pin is its own decision, made outside
  any phase landing.
- **P5xxx disabling is an upstream baseline change.** P5* dists sit on our
  green list (ECOSWEEP-2026-08). Before the first post-2026.09 eco-sweep,
  snapshot which P5* dists upstream disabled and mark those rows
  environment-moved — the Roast pin-and-record discipline applied to the
  green list — so they are never charged to the engine.

## Ecosystem modules that wait for this plan (decided 2026-09-01)

The module campaign does not chase these. They fail under rakupp *because* the
RakuAST surface is missing, so they are postponed until the phases above land —
P1 answers the `.AST` shape, P2 the rest. Twenty dists, measured against the
2026-08-30 sweep, not assumed:

- **13 × `L10N::*`** (AF, Complete, CY, DE, EN, FR, HU, IT, JA, NL, PT, TLH,
  ZH) — every one on `No such method 'AST' for invocant of type 'Str'`. One
  shape, one fix — **corrected 2026-09-11 (Part III, *The L10N family*)**: only `L10N::EN` is a plain parse; the other twelve are localized-keyword parses. `L10N::EO`'s heredoc bug is fixed and it now fails on `.AST` too, making it 14 sweep rows: 13 language dists plus the `L10N::Complete` bundle.
- **`ASTQuery`** (`lib/ASTQuery/Match.rakumod:36` types on `RakuAST::Node`) and
  **`Acme::Overreact`** behind it.
- **`RakuAST::Utils`**, **`Rakuast::RakuDoc::Render`**, and the two dists that
  depend on the latter (`Air-Plugin-RakuDoc`, `Elucid8::Build`).
- **`FINALIZER`** — subclasses `RakuAST::StatementPrefix::Phaser::Leave`, so it
  needs the registry to be inheritable (Open item 4).

**None of them is in the ecosystem top 100, and none is in its dependency
closure** ([ECOSYSTEM-TOP100.md](../ecosystem/ECOSYSTEM-TOP100.md): 169 dists,
zero carrying `needs-AST`). That is what makes the postponement free rather
than a trade — the campaign loses no reachable green by waiting.

They are marked `needs-AST` on /modules/ecosystem/, generated by
raku.online's `sites/modules/tools/rakuast-fallout.raku` and carried as column 9
of `ecosweep.tsv`. A flag, not a seventh verdict: verdicts are the ladder of the
first rung that failed and are mutually exclusive, while this axis is orthogonal
— `P5tie` is green today *and* flagged. The same tool emits the opposite flag,
`legacy` (27 dists), for what dies WITH the frontend and is therefore never
engine work: 19 × `Slang::*`, `P5tie`, `overload::constant`, `P5__DATA__`,
`Grammar::BNF` (it provides `Slang::ABNF`/`Slang::BNF`), `Asserter` and
`Control::Bail` (both reach QAST), plus `App::Crag`, `TOP` and **`Text::CSV`**
downstream. Text::CSV is the one that costs: `lib/Text/CSV.rakumod:3` is
`use Slang::Tuxic`, so its 7 runtime dependents ride an old grammar hook too,
and it will meet the same wall on stock Rakudo at 2026.09.

The `legacy` set is not static, and the flag should not be read as a death
sentence: **12 of the 19 `Slang::` dists already runtime-require `Slangify`**
(Slang::Tuxic among them), so their authors have begun the move the announcement
asks for. Slangify eases activation, not hook-finding, so they stay flagged until
they land on the new hooks — at which point they become ordinary work and the
tool reclassifies them on the next sweep. `Slangify` itself is never flagged: it
is the migration interface, and its failure here is our own sink-context bug,
shared with `Air` and `Slang::Tuxic`.

### App::Rak (2026-09-11) — the first demand that costs, and the smallest

`rakupp install App::Rak` (18 dists, all lizmat's) now installs 16 of them and
stops at **Needle::Compile**, the needle compiler App::Rak calls for every
pattern; `rak` itself installs and runs. Needle::Compile is RakuAST from the
first line: it BUILDS a tree (99 `RakuAST::` references) and calls `.EVAL` on
it. Unlike the twenty above, this one is reachable and wanted — `rak` is the
tool people install — so it is the first entry here that costs something to
postpone.

What it needs is narrower than the plan: **mostly the construction half.** It
asks `.AST` of existing text only for `{ code }` and `/regex/` needles (Part III splits the three needle kinds), never walks a tree it did not build,
and never deparses one back for comparison. So the 1:1 harness is never
on its path; P2's renderer and P3's `.EVAL` are, over exactly these 28 classes
(counted in `lib/Needle/Compile.rakumod`, 0.0.12):

- names and calls: `Name` (17 uses, incl. `Name.from-identifier`),
  `Call::Method` (7), `Call::Name` (4), `ArgList` (6), `ColonPair::True` (2),
  `Term::TopicCall` (3), `Term::Name` (2)
- values and operators: `StrLiteral` (4), `Var::Lexical` (5), `Infix` (4),
  `ApplyInfix` (4), `ApplyPostfix` (4), `Prefix` (1), `ApplyPrefix` (1),
  `Ternary` (1), `Type::Simple` (1)
- declarations and blocks: `VarDeclaration::Simple` (3), `Initializer::Bind`
  (2), `Signature` (2), `Parameter` (2), `ParameterTarget::Var` (2),
  `PointyBlock` (2), `Block` (2), `Blockoid` (2), `Statement::Expression` (6),
  `StatementList` (4), `CompUnit` (1), and the `Node` role as a type
  constraint (5)

Three facts about how it uses them, read off the code, not assumed:

1. **`.EVAL` is the production path** (`$AST ?? $ast !! $ast.EVAL` at the end
   of `compile-needle`); `.DEPARSE` is printed only under
   `NEEDLE_COMPILE_DEBUG`. Its suite never compares deparsed text, so the
   renderer's spelling is not pinned to Rakudo's — the P2 property gate
   (deparse → parse → same behaviour) is the whole requirement.
2. The suite checks **`$ast.^name eq "RakuAST::PointyBlock"`** (t/01, line
   49): the registry's class names are observable, the rest of the tree is not.
3. Two constructor spellings beyond plain `.new`: `RakuAST::Name.from-identifier($str)`
   and `RakuAST::ColonPair::True.new($name)`. Everything else is keyword
   arguments (`RakuAST::ApplyInfix.new(left => …, infix => …, right => …)`).

Because string needles never read rakupp's own tree, that subset could even be
delivered ahead of P1 as a plain Raku module shipped with the engine (the way
`Data::Native` answers a `use` with nothing installed): 28 classes holding
their children, one `DEPARSE` per class, `EVAL` = `self.DEPARSE.EVAL` in the
caller's scope (P3's side table applies only to spliced runtime values, which
Needle::Compile does not do — its trees are built from strings and names).
Whether that shortcut or the full P0-P3 is taken is a decision, not a finding;
the size either way is bounded by the list above.

The other 17 dists of the chain are engine work that is DONE (commit of
2026-09-11: ~55 fixes, 18 regression files), including Rakudo's private
regex-cursor protocol that String::Utils' `replace` drives — that one was
mistaken for a second RakuAST-class wall until read closely, and shimmed in
~60 lines.

## Deferred, with reasons

- **`CHECK { $*CU }`** — the same deparse/re-parse trick works in principle,
  but it is the most invasive part of the surface and the least used; revisit
  when an acceptance module actually needs it.
- **The vocabulary tail** — 26 of the 125 observed classes appear ≤3 times in
  24k corpus nodes; they throw clear "not implemented", widened on demand.
- **Slang hooks** — upstream just replaced its own mechanism; do not chase a
  moving interface.
- **A fifth surface fact** (`.method: args` vs `.method(args)`) — MethodCall
  has hole room @109-111, but the oracle should show it matters before it
  costs a serializer line.
- **Compile-time timing divergences** — the 6.d refusal fires at first
  mention here vs Rakudo's compile-time SORRY; undeclared variables surface
  at run time; `my` inside EVAL'd text declares into the caller's scope; user
  `class RakuAST::Mine {}` is accepted here where Rakudo SORRYs. All
  pre-existing evalString/walk-order divergences that RakuAST inherits, not
  RakuAST work — recorded so post-2026.09 Roast tests asserting Rakudo's
  timing are diagnosed correctly.

## Doc-sync checklist (each phase syncs what it changed)

- docs/guide/FEATURES.md:38-41 (the 50/51 line and "Deliberately not done:
  RakuAST") and faq/6e.md:475-481 (which says 52/53 — the two already
  disagree; sync both to the figure the live matrix re-measures, not to each
  other). After P2 the matrix row can flip.
- faq/6e.md:139-149 (the 6.d/6.e gating table becomes current behavior at P0)
  and :389 (the `macro` matrix row cites `use experimental :macros`, removed
  upstream at 2026.09).
- The live matrix is measured, not hand-scored: re-run
  raku.online's gen-6e.raku and republish /spec/6e after P2.
- docs/internals/METAPROGRAMMING.md:80/:85/:111 — the "one incidental
  reference" note, the ✗ row, the frontier list, plus the macro-removal note.
- **Five more stale "not there yet" sites**: guide/OVERVIEW.md:88,
  guide/HIGHLIGHTS.md:124, internals/ARCHITECTURE.md:352,
  internals/PARSING.md:536, dev/README.md:79-86 (flip "deferred, not built"
  to in-progress at P0, done at P2/P4).
- 6E-PLAN.md:104-107 ("the matrix will keep showing it red") retires at P2.
- Roast/README/ROAST/COUNTING numbers move only if the figures move; then
  RELEASING.md step 3 applies. BENCHMARKS.md startup row when re-measured;
  release notes carry the one-time AST-cache invalidation at P1.
- The auto-memory tracking this area (raku-pp-metaprog: "Phase 5 =
  macros/RakuAST/slangs frontier"; raku-pp-big-areas) updates at P0 landing.

## Open items

1. Re-establish the module-battery checkout and an ecosystem source list on
   this machine (both missing; machine-switch residue). Gate 6 and the
   feature-detection grep depend on it.
2. Build the pre-campaign same-arch binary for the A/B leg.
3. Rakudo 2026.09, when it ships: the four gating probes, the oracle re-pin,
   and the pragma-rule verification (entry criteria).
4. `.^add_method`/`augment` on RakuAST classes: ClassInfo-backed classes take
   the normal path, which is Rakudo-parity — confirm as the deliberate policy
   at P0 rather than inherit it silently.
5. The two suspected AstSerial gaps (VarExpr::viaPseudoPkg/pseudoPkg,
   Param::userTraits): verify; if real they affect precompiled-module
   coverage independently of RakuAST and belong inside P1's version bump.
6. Advancing the Roast pin past b2cbe8a42: separate decision; P4's claims
   read better against a post-2026.09 Roast, and the shield inventory must
   exist first.
7. Per the perf-kernel convention, no `.AST` kernel is required (nothing
   always-on lands); if any phase ends up adding a hook after all, that phase
   adds the kernel.

## Size summary

| Phase | New/changed code | Notes |
|---|---:|---|
| P0 | ~300-500 lines | capture, gate, 39-class table, stubs, CMake |
| P1 | ~850-1,550 lines | builder ≈2-3× AstSerial's walking body; 4 fields, 1 serializer bump |
| P2 | ~1,000-1,400 lines | renderer + round-trip harness + t/12-rakuast slice |
| P3 | ~150-250 lines | side table + Env plumbing; evalString reused |
| P4 | ~150 + demand-driven | traversal small; widening priced by the corpus |

Total first cut (P0-P3): roughly 2,300-3,700 lines, all in parse-archive TUs
except ~150 lines of gate/capture/arm touches and four bytes of AST fields
(one of which grows Call 80→88 — the P1 A/B owns proving that neutral).

---

# Part III — the 2026-09-11 review: decision, evidence refreshed, order

Part II phased the work and was left scheduled. This part records the decision
to build it, refreshes every number Part II leaned on, re-anchors its file:line
citations to v3.26.0, and — because the tarballs of every blocked dist were
read for the first time — replaces two claims that turned out to be wrong (the
L10N family is not "one shape, one fix", and Needle::Compile does ask `.AST`).
Nothing in Part I's design is reopened; the view-on-demand shape, the DEPARSE
bridge and the side table stand.

## The decision

**Build it.** The user's call, 2026-09-11, with "too many modules depend on it"
as the reason. The measurement that goes next to that reason, so nobody later
mistakes the one for the other:

- By the plan's own ranking (reverse *runtime* dependents over the REA index of
  2026-09-10, 2,544 dists, latest version per dist) **no blocked dist is in the
  top 100** — the boundary sits at 7 dependents and the largest count in the
  blocked closure is **2** (App::Rak, Rakuast::RakuDoc::Render). The top-100
  battery of 2026-09-03 still carries zero `needs-AST` rows; its only RakuAST
  column entries are the two `legacy` ones (Text::CSV, Slang::Tuxic).
- What does hold: `rak` is the one end-user tool whose `rakupp install`
  reached this wall (Part II, *App::Rak*); Rakuast::RakuDoc::Render is in the
  fresh-100 (FRESH100-2026-08-20, row 2026-07-29); the blocked authors are the
  most active cluster in the ecosystem (lizmat: Needle::Compile, RakuAST::Utils,
  FINALIZER, all 13 L10N dists), and 2026.09 makes RakuAST the default frontend
  they write against; and the `needs-AST` flag undercounts by construction —
  the sweep records the *first* error only, so a dist that dies earlier for an
  ordinary reason is never counted (Rakuast::RakuDoc::Render is exactly that
  case, below).

So the case is *reachability of tools and of what is coming*, not present
dependency counts. The release-train rule (P0 never ships without a DEPARSE
that answers) is unchanged.

## Evidence refreshed

**The sweep** (`docs/dev/findings/ecosweep/sweep-2530.tsv`, 2,530 dists):
18 rows whose first error names `.AST` or a `RakuAST::` class —
14 × `L10N::*` (AF, CY, Complete, DE, EN, EO, FR, HU, IT, JA, NL, PT, TLH, ZH;
all `No such method 'AST' for invocant of type 'Str'`), `Intl::Format::Number`
(`Undeclared name 'RakuAST::Infix'`), `FINALIZER` (`cannot inherit from
'RakuAST::StatementPrefix::Phaser::Leave'`), `RakuAST::Utils` (parse error
inside the module), `Rakuast::RakuDoc::Render`. Two changes against Part II:
`Intl::Format::Number` joins, and `L10N::EO` joins (its heredoc bug is fixed;
the next rung is `.AST`). `ASTQuery` (and `Acme::Overreact` behind it) is
source-verified in raku.online's `rakuast-fallout.raku`, not error-fingerprinted:
its first error is `Cannot add tokens` in ASTQuery::Actions — ordinary engine
work sits in front of its RakuAST need.

**The reverse-dependency closure** of the non-L10N seeds, over the same index
(a scratch script; the phase-hash shape below is handled):

| dist | depth | runtime dependents |
|---|---|--:|
| Rakuast::RakuDoc::Render | seed | 2 (Air-Plugin-RakuDoc, Elucid8::Build) |
| Needle::Compile | seed | 1 (App::Rak) |
| ASTQuery | seed | 1 (Acme::Overreact) |
| RakuAST::Utils, FINALIZER, Intl::Format::Number | seed | 0 |
| App::Rak | 1 | 2 (App::Rak::Markdown, App::Rak::Complete) |
| Air-Plugin-RakuDoc, Elucid8::Build, Acme::Overreact | 1 | 0 |
| App::Rak::Markdown | 2 | 1 (IRC::Client::Plugin::Rakkable) |
| App::Rak::Complete | 2 | 0 |
| IRC::Client::Plugin::Rakkable | 3 | 0 |

13 dists, plus the 14 L10N rows (`L10N::Complete` is the bundle of the other
13): **27 dists** wait on this plan. Not "none" — Part II's App::Rak entry was
already the correction — but not the top lists either.

**A blind spot in the ranking tools, found on the way.** Both rankers
(raku.online's `sites/modules/tools/rank-runtime.raku`, which produced
ECOSYSTEM-TOP100.md, and `tools/eco-fresh/rank-deps.raku`) descend a phase
hash as `{"runtime": [...]}` and never see the other spelling,
`{"runtime": {"requires": [...]}}` — the shape every lizmat dist uses. Measured
on the 2026-09-10 index: **94 dists' runtime dependencies are invisible** to
them. Corrected counts move five dists across the top-100 boundary
(String::Utils 4→10, Identity::Utils 3→8, PSGI 5→8, paths 3→7,
Array::Sorted::Util 4→7; JSON::Fast 240→254 at the top) and **none of them is
a RakuAST user** — the decision above does not change. The fix is a
one-line descent into `requires`/`recommends` in each tool, filed separately;
the next ECOSYSTEM-TOP100 refresh should re-rank with it.

## What each blocked dist needs, read from its tarball

Every row below was read from the REA archive tarball (version named), not
inferred from the sweep's first error.

### Needle::Compile 0.0.12 → App::Rak (three needle kinds, three surfaces)

`compile-needle` dispatches on `implicit2explicit`: a plain string becomes
`"equal"`, `/…/` becomes `"regex"`, `{…}` / `*.…` becomes `"code"`; there is
also `"json-path"` (a plain-string `EVAL` of a `use` line — no RakuAST) and
`"file"`. Everything ends in `wrap-in-block` and `.EVAL` (`$AST ?? $ast !!
$ast.EVAL`); t/01-basic.rakutest:49 asserts
`compile-needle("bar", :AST).^name eq "RakuAST::PointyBlock"`.

| needle kind | what it builds or parses | surface |
|---|---|---|
| `"equal"` (the default: `rak foo`) | `Var::Lexical('$_')`, `StrLiteral`, `Infix('eq')`, `ApplyInfix`; `Call::Name(nomark)`/`Term::TopicCall(fc)` under `:ignoremark`/`:ignorecase`; `with-matches` | construction + DEPARSE + EVAL — **P2c/P3 only** |
| `"regex"` (`rak '/ foo /'`) | `"/$i$m $spec /".AST.statements.head.expression` — a **`RakuAST::QuotedRegex`** — then `Ternary`/`ApplyPostfix`/`Call::Method(match)` around it | `.AST` of a regex literal (P1, but a *source-slice* node: its DEPARSE is the literal text, so no regex tree is needed) |
| `"code"` (`rak '{ .contains("x") }'`) | `$spec.AST(:compunit)` → `RakuAST::CompUnit`; `.statement-list.unshift-statement(...)` | `.AST` of a whole program (P1) + the `StatementList` mutation API |
| all kinds | `wrap-in-block`: `StatementList.unshift-statement`, `PointyBlock`/`Signature`/`Parameter`/`ParameterTarget::Var`/`Blockoid` | construction |

Rakudo 2026.08 confirms the spellings: `q[/ foo /].AST.statements.head.expression.^name`
is `RakuAST::QuotedRegex`; `q[say 1].AST(:compunit).statement-list` is a
`RakuAST::StatementList` that `.^can("unshift-statement")` and answers
`.statements`. So **string needles — the common case — go green at P2c+P3;
regex and code needles wait for P1**, and P1 must include `:compunit`, the
three `StatementList` accessors, and a `QuotedRegex` node that carries its
source slice.

### The 13 L10N dists: `.AST($lang)` is a localized-keyword parse

Each dist has one test (`plan 1`), and it is not a plain `.AST`. L10N::DE's:

```raku
my $ast := Q:to/CODE/.AST("DE");
mein $a = 42;
wenn $a == 42 {
    sag "The answer"
}
CODE
$ast.EVAL;
```

`mein`/`wenn`/`sag` are `my`/`if`/`say`. Only `L10N::EN` is the identity
translation; the other twelve need the source translated before our parser
sees it. What a dist ships (generated by Raku/L10N's `update-localization.raku`,
in a fixed "PLEASE DON'T CHANGE" block): a `role L10N::DE` of **tokens whose
bodies are single literal words** — in DE, 18 categories: 41 `infix-`,
18 `phaser-`, 16 `enum-`, 16 `block-`, 13 `stmt-`, 10 `quote-`, 9 `scope-`,
9 `modifier-`, 7 `traitmod-`, 7 `term-`, 6 `routine-`, 5 `use-`, 5 `package-`,
3 `multi-`, 3 `meta-`, 2 `typer-`, 2 `prefix-`, 1 `constraint-` — plus
`core2ast`/`trait-is2ast` methods returning `RakuAST::Name.from-identifier`,
and a `role RakuAST::Deparse::L10N::DE` whose `xsyn` maps `core-say → sag`
(that one serves `DEPARSE(:L10N)`, which no test exercises — out of scope,
recorded). The role **loads under rakupp today**: the sweep's first error is
the test's `.AST`, not the module.

Design, in Part I's spirit (no grammar hook, no slang — the L10N roles touch
none):

1. `Str.AST(Str $lang)`: `require L10N::$lang`, then read the table straight
   out of the role's `ClassInfo::rules` (Value.h:1063 — a token's pattern text
   is stored as a string, `rules{"block-if"}` is ` wenn`, trim it). No source
   scan, no new parser: the role is already parsed by the engine.
2. Invert it into translated→English per category and hand the Lexer a
   translation table. One lookup where `Tok::Ident` is produced, active only
   while a table is installed (null-pointer check; zero cost on every path
   without one). Rakudo's slang tries the localized token first and falls back
   to English, so both spellings parse — mirror that: translate when the map
   has the word, otherwise leave it.
3. Positions that need the map: statement keywords (`block-`, `stmt-prefix-`,
   `modifier-`, `phaser-`), declarators (`scope-`, `package-`, `routine-`,
   `typer-`, `multi-`), `use-`, word infixes/prefixes/metas
   (`infix-`, `prefix-`, `meta-`), traits (`traitmod-`, `trait-is-`),
   `constraint-where`, terms (`term-now/self/rand/time`), `enum-` names,
   `core-` routine names in call position, `named-` argument names, `adverb-`
   names on quotes/regexes/subscripts. Every DE value is a single identifier
   (hyphens allowed: `hack-linieende`), so the mapping is token-for-token.
4. `.EVAL` on the result runs the translated program in the caller's scope —
   P3's path unchanged.

**Gate** (per the probe rule: a probe must be able to fail): `L10N::EN` proves
nothing — it is the identity. The regression case is DE or NL source covering
at least `scope-`, `block-`, `core-`, `infix-` and a phaser, asserting the
program's output; and a deliberately misspelled keyword must **fail** to parse.
Size ≈150-250 lines (the `.AST($lang)` arm, the table build, the lexer hook);
sequenced after P1 because it is P1's `.AST` with a pre-pass.

### Intl::Format::Number 0.2.0

Builds formatter trees at run time (67 `ApplyInfix`, 47 `Statement::Expression`,
46 `Var::Lexical`, 44 `Infix`, 20 `IntLiteral`, 18 `StrLiteral`, …) and runs
them with **the `EVAL` sub form on a node** — `EVAL format-number-rakuast |c`
(lib/Intl/Format/Number.rakumod:184) under `MONKEY-SEE-NO-EVAL`. Verified on
2026.08: `EVAL RakuAST::IntLiteral.new(42)` answers 42. So P3 adds the sub
form (the `EVAL` builtin accepting a `RakuAST::Node` argument routes to the
tree path) beside the method. Vocabulary beyond Needle::Compile's 28:
`Var::Lexical::Constant`, `Initializer::Assign`, `Statement::If`,
`Statement::For`, `StatementModifier::If`, `IntLiteral`, `Prefix`/`ApplyPrefix`,
`Type::Simple` — construction-side, into P2c's table.

### RakuAST::Utils 0.0.3

Signature vocabulary only: `Type::{Simple,Parameterized,Definedness,Coercion,Capture}`,
`Parameter::Slurpy::{Flattened,Unflattened,SingleArgument,Capture}`,
`ParameterTarget::{Var,Term}`, `Trait::Is`, `Literal`, `Signature`,
`Parameter`, `ArgList`, `Name` — 17 classes, no `.AST`/`.EVAL`. P4 widening;
its sweep error is a parse error *inside the module* (line 98), which must be
diagnosed first — it may be ordinary parser work.

### FINALIZER 0.0.10

`class LeavePhaser is RakuAST::StatementPrefix::Phaser::Leave` plus
`RakuAST::Block`. Needs the registry classes to be **inheritable** by user
classes — open item 4 — decided at P0: registry ClassInfos serve as `parent`
exactly like user ClassInfos (the `is` path at Interpreter.cpp:9268 consults
`classes_` then `resolveClassAlias`; the registry is the third lookup, after
both miss).

### ASTQuery 0.0.7 (+ Acme::Overreact)

`.AST` ×6, `.DEPARSE` ×2, `.EVAL`, `visit-children`, `@*LINEAGE` ×5 — P4 —
**and** `CHECK { my $ast = $*CU.AST; …; $*CU.AST = $ast }`
(t/12-doc-examples.rakutest:82-98): the mutable compilation unit. That is the
deferred item, and this is the first module that needs it; it stays deferred
until ASTQuery's own `Cannot add tokens` failure is out of the way, so the
question can be asked against a running suite.

### Rakuast::RakuDoc::Render 1.0.18 (+ Air-Plugin-RakuDoc, Elucid8::Build)

A different subtree: `RakuAST::Doc::{Block,Paragraph,Markup,DeclaratorTarget,LegacyRow}`,
`.rakudoc` on `StatementList`/`Node`, `.paragraphs`/`.set-paragraphs`,
`.DEPARSE` ×17, `.AST` ×8 — pod represented as RakuAST nodes. Part I's
vocabulary count (125 classes over 24k nodes) never saw a `Doc::` class because
the corpus has no pod, so this is unpriced. Its first failure today is
`use-ok "RakuDoc::Numeration"` — a file with no RakuAST in it (one comment) —
so RakuAST is **not this dist's first wall**. Price it as its own phase (P5)
once the ordinary failure is fixed; it is not on the App::Rak or L10N path.

## The order, demand-driven

Part II's phases stand; their order and one split change so that the first
green arrives earliest:

| step | lands | who goes green |
|---|---|---|
| **P0** | pragma gate + registry (with the real ancestor chain, below) | — (never released alone) |
| **P2c** | the renderer over **constructed** trees — the ~40-class union of Needle::Compile, Intl::Format::Number and RakuAST::Utils — plus the t/12-rakuast slice | — |
| **P3** | `.EVAL` method + `EVAL $node` sub form + the side table | **App::Rak (string needles), Intl::Format::Number** |
| **P1** | the `.AST` view, incl. `:compunit`, `statements`/`statement-list`/`unshift-statement`, `QuotedRegex` as a source slice; the round-trip property gate (Part II's P2 harness) joins here, now that there is a view to round-trip | **App::Rak (regex + code needles)** |
| **P1-L10N** | `.AST($lang)` per *The 13 L10N dists* | **13 L10N dists + L10N::Complete** |
| **P4** | `visit-children`, `@*LINEAGE`, widening | RakuAST::Utils, FINALIZER, ASTQuery minus CHECK |
| | *(measured 2026-09-12: `.parent` is NOT the API to build. `RakuAST::Node` does expose `parent(int $generation?)` publicly, but it answers `Nil` for every tree a Raku program can obtain on 2026.08 — plain `.AST`, `.AST(:compunit)`, and after the tree has been compiled and run — there is no `set-parent`, and `origin.nestings` is unset too. Nothing populates it. Reaching an enclosing node is `@*LINEAGE`, which the WALKER maintains: Rakudo does not set it during `visit-children` either. That also happens to be the only option that is free for us — our nodes are refcounted `shared_ptr` with no weak reference in the value model, so a stored parent pointer is a cycle and a leak, which a GC'd Rakudo does not pay. What IS live and we do not build: `.origin`, whose `.from`/`.to` carry real source offsets on every node.)* | |
| P5 | the `Doc::` subtree | Rakuast::RakuDoc::Render and its two dependents — priced when reached |

P2c is Part II's P2 without the round-trip harness (which needs P1's view);
the harness moves to P1. The release-train rule is satisfied at P2c: names
exist together with a DEPARSE that answers.

**The registry must carry Rakudo's ancestor chain, not a flat parent.**
`.^mro` is observable, and 2026.08 answers for `IntLiteral`:
`IntLiteral Literal Term Termish Expression MayCreateBlock Sinkable CheckTime
CaptureSource CompileTimeValue Node Any Mu`. `~~ RakuAST::Term` and
`~~ RakuAST::Expression` are the kind of checks a walker writes, so the
table's `parent` column is the real chain and the intermediate names
(~20 beyond the head 39) exist as classes with no attributes.

**The t/12-rakuast slice** (rakudo `main`, 47 files, listed 2026-09-11 — the
local rakudo checkout is from 2020 and has none of it): the construction-side
files are literals (1.8 KB), name (6.1), operators (15.4), call-name (7.1),
call-method (10.2), var (50.7), block (6.4), sub (17.0), signature (30.0),
statement (45.2), statement-mods (16.0), strings (8.8), terms (8.3),
circumfix (5.8), postfix (22.6), pair (4.9), eval (2.3) — 17 files, ~259 KB.
Run raw pass/fail per file and publish the counts; at P2c the requirement is
throw-clearly-or-render, never crash.

**How they get here — superseded 2026-09-11, do NOT vendor.** This paragraph
used to say "fetch them at a pinned rakudo commit into `t/rakuast/upstream/`
with Rakudo's LICENSE (Artistic-2.0) beside them". That would have been the
first third-party code in the tree, and it cuts against both shapes the repo
already uses for an upstream suite: **Roast is an external checkout** behind
`ROAST=` with zero files tracked, and **RosettaCode programs are fetched** into
the gitignored `rc-cache/` and reused forever (`tools/rc-compare.raku`,
`.gitignore:31`). This repo references upstream suites; it does not copy them.
The slice takes the second shape — `tools/rakuast-t12.raku` holds the pin and
the file list, the counts are published under findings, and the files are never
tracked. Reading the files also corrected the list: `eval.rakutest` is not a
construction test (it drives rakudo's own precompilation harness), so it is
**16 files, not 17**.

**DEPARSE whitespace, for parity where it is free**: 2026.08 renders a
`PointyBlock` as `-> $_ {\n    "bar"\n}` — four-space indent, newline after
the brace. The renderer follows that convention from the start so the
constructed-tree suite's inline expectations compare byte-for-byte where they
can.

## Rakudo 2026.08 facts, re-verified 2026-09-11

Rakudo 2026.09 has not shipped (installed: 2026.08; the two-period oracle
rule applies unchanged). Re-probed today, all as Part II states, plus the new
ones:

- refusal under 6.d without the pragma is `===SORRY!===` with the verbatim
  message; `::('RakuAST::IntLiteral')` answers a `Failure`
- `42.AST`, `<42>.AST` → `RakuAST::StatementList`; `.AST(:compunit)` →
  `RakuAST::CompUnit`
- `RakuAST::Name.from-identifier("foo").DEPARSE` → `foo`;
  `RakuAST::ColonPair::True.new("x").DEPARSE` → `:x`
- `EVAL RakuAST::IntLiteral.new(42)` (sub form) → 42; the method form and a
  constructed `$x + 1` against an outer `my $x = 41` → 42
- `RakuAST::IntLiteral.new(42).raku` → `RakuAST::IntLiteral.new(42)`
- `RakuAST::Literal.from-value([1,2]).DEPARSE` → `[1, 2]` (the lossy case,
  unchanged)

rakupp 3.26.0 today: `use experimental :rakuast` is a silent no-op, `.AST` is
`X::Method::NotFound`, every `RakuAST::` name is `Undeclared name`,
`try ::("RakuAST::Node")` falls through to the fallback branch, and
`class RakuAST::Mine {}` works — the exact starting state P0 assumes.

## Re-anchoring Part II to v3.26.0

Every Part II citation, with where it is now. The structural claims were
re-read at the new sites; where a claim changed, it says so.

| Part II cites | now (2026-09-11) |
|---|---|
| Ast.h:45-71 `PublishedOnce` | Ast.h:46-71, unchanged |
| Ast.h:829-832 `Program::langRev` | Ast.h:928 |
| Ast.h:124 `Binary::simpleOp` | Ast.h:296 |
| Ast.h:354 `nameEvalsCode` | Ast.h:354, unchanged |
| `UseStmt::importArgs` | Ast.h:842 |
| `VarExpr::viaPseudoPkg`/`pseudoPkg` serializer gap | Ast.h:200-201 exist; no `F()` in AstSerial.cpp — **gap confirmed, still open** |
| `Param::userTraits` serializer gap | AstSerial.cpp:191-195 — **closed** since the draft |
| `kAstSerialVersion` 15 | AstSerial.h:23 = **18** (bump to 19) |
| Interpreter.h:1604 `langRev_` | beside `sixE()` at Interpreter.h:1922 |
| Interpreter.cpp:4233 `langRev_ = prog.langRev` | 4529 |
| Interpreter.cpp:5810/5893 module-load save/restore | 6540 (save), 6632 (module's own), 6712/6723/6732 (restore) |
| Interpreter.cpp:3484 `engageGil` | 3772 |
| Interpreter.cpp:8613-8621 `use v6.x` in the UseStmt case | `case NK::UseStmt` 8564; revision 8619-8622 — the `:rakuast` flag is set here, from `importArgs` |
| tail-alias suppressor | 9226-9236 (comment) |
| `classes_.find` + `resolveClassAlias` miss sites | 9053, 9268 (`is` parent), 9295, 9346, 9419, 10217 (`howName`), 15047 — the registry is consulted after each |
| Interpreter.cpp:28459-28496 NameTerm known-check | 32596-32650; the X::NoSuchSymbol Failure at 32597-32601 and the **Format/Formatter 6.e gate at 32606-32608 are the two patterns the RakuAST gate copies** |
| `isKnownTypeName` | 8007 (do not add the prefix there — unchanged rule) |
| `isPragmaName` | 5601 (`experimental` listed); use sites 5954, 7003 |
| Builtins.cpp:7075 method-call miss path | MethodCallPart3.cpp:1111 (`X::Method::NotFound`); the Failure form Builtins.cpp:4967 |
| Builtins.cpp:6225-6227 | 6866-6868 |
| MethodCallPart3.cpp:430 Cool `EVAL` arm | 492 |
| Parser.cpp:7905-7927 / 7897 use-arg capture | 8725-8780; the `:tag` capture at 8749-8757 **already stores the spaced `:rakuast`** |
| Parser.h:244-246 `monkeyScopes_`; push/pop 5517/5527 | Parser.h:272-274; Parser.cpp:5971/5981; set at 8719 |
| experimental-adverb precedent | Parser.cpp:7214 (`will complain`, parse-only) |
| Parser.cpp:4789/2311/4904 `Call` sites for `parenned` | `make_unique<Call>` at 1028, 1227, 1267, 1473, 1547, 1559, 1845, 1934, 2004, 2382, 2391, 2423 — **twelve** sites, each decides parens |
| Parser.cpp:4170 bare-`.` topic synthesis | `VarExpr("$_")` synthesized at 1906, 1949, 1960, 2021, 2300, 2316, 2383 — seven; `synthTopic` goes on the method-call one |
| Parser.cpp:1924-1994 / 2063-2090 / 1697-1715 angle subscripts | `readAngleWords` in subscript context at 1651, 1914, 2168, 2280 (3153, 3321, 3501, 4451 are other contexts) |
| Parser.cpp:6435-6438 trait sites, 6278 merge | `sigRetType_` stores 6272, 6689, 6712, 6913; merge 6919; block forms 4497, 4663 |
| SlimScan.cpp:44-47 | `F_EVAL` enum :46, `Scan::use` :53 |
| CMakeLists.txt:139 `RAKUPP_PARSE_SOURCES` | 197 (list), 212 (archive) |
| `src/stubs/stub_eval.cpp` | four throwing doubles (Lexer ctor/`tokenize`, Parser ctor/`parseProgram`); the RakuAst TUs add theirs beside them |
| `rakujs/build.sh` sweep | :58, `src/*.cpp` minus `main.cpp` and `stubs/` |
| `Callable::builtin` | Value.h:309 (`BuiltinFn`), :372 (the field), `Value::closure` :763-769; `ClassInfo` 1056 (`methods` 1062, `rules` 1063, `isRole` 1068), `ObjectData` 1186 |
| gen-6e.raku:48-50 | sites/spec/tools/gen-6e.raku:48-50, unchanged; `rakuast-fallout.raku` at sites/modules/tools/ |
| Roast pin b2cbe8a42, "ten files" | 13 lines in **8** files; the one real test is S32-str/format.t:52-53 |

Struct sizes, measured with `-fdump-record-layouts` on the v3.26.0 headers
(arm64, libc++): `Call` 80 (no hole — `parenned` grows it to 88, bucket
80→96, as Part II says); `VarExpr` 296 (hole @41-47 after `declare`);
`Index` 80 (hole @35-39 after `semicolonSub`); `SubDecl` **400** (was 368;
hole @191 after `isPrivate` still there); `MethodCall` 112 (`dsize` 109 —
the @109-111 tail room for the deferred fifth fact still there).

## Doc-sync anchors, moved

FEATURES.md:38-41 → :45. faq/6e.md is `docs/guide/faq/6e.md`: the gating table
:139-149 and the `macro` row :389 are where Part II says; the "separate
campaign" line is :481 (Part II: 475-481); :79, :130 and :515 also mention
RakuAST. OVERVIEW.md:88 unchanged; HIGHLIGHTS.md:124 → :129;
ARCHITECTURE.md:352 → :179; PARSING.md no longer mentions RakuAST (drop it
from the list); METAPROGRAMMING.md:80/85/111 unchanged; dev/README.md:79-86 →
:87-94, plus :109 (the top-100 flag line); 6E-PLAN.md:104-107 → :7 and :105.

## Open items, refreshed

1. Module-battery checkout / ecosystem source list: the top-100 battery ran on
   2026-09-03, so a checkout existed then — confirm it is still on this
   machine before P0's gate 6, rather than re-establishing blind.
2. The pre-campaign same-arch binary for the A/B leg — unchanged.
3. Rakudo 2026.09: the four gating probes + oracle re-pin — unchanged (not
   shipped as of today).
4. Inheritable registry — **decided above** (FINALIZER): registry ClassInfos
   are ordinary parents.
5. AstSerial gaps: `userTraits` closed; `pseudoPkg` open — goes into P1's
   18→19 bump.
6. Advancing the Roast pin — unchanged.
7. No `.AST` perf kernel unless a hook lands — unchanged.
8. **New**: the rankers' `requires` blind spot — fix both tools, re-rank, and
   re-read ECOSYSTEM-TOP100's boundary before the next battery.
9. **New**: `.^mro` parity — the registry carries the real chain; regression
   case asserts `IntLiteral.^mro` against the 2026.08 list.
10. **New**: `EVAL $node` sub form (Intl::Format::Number) — P3 scope.
11. **New**: the `Doc::` subtree — a decision when Rakuast::RakuDoc::Render's
    ordinary failure is out of the way; unpriced until then.
12. **New**: `DEPARSE(:L10N)` — no test needs it; recorded, not scheduled.
13. **New (P0)**: `Program::langRev` has the same cache hole `usesRakuAst` had
    — the blob carries statements, not the Program's scalar fields, so a
    module loaded from the precomp cache is hoisted under the IMPORTER's
    revision rather than its own. Predates this campaign and blocks nothing in
    it; the same one-line recovery in `deserializeAst` would close it.

## Size summary, refreshed

| step | new/changed code | notes |
|---|---:|---|
| P0 | ~300-500 lines | as Part II; parser capture already exists, the tight spelling and scope tracking remain; table carries the ancestor chain |
| P2c | ~900-1,300 lines | renderer over ~40 constructed classes + registry table + the 17-file t/12 slice harness |
| P3 | ~170-270 lines | Part II's P3 + the `EVAL $node` sub form |
| P1 | ~950-1,650 lines | Part II's P1 + `:compunit`, the three `StatementList` accessors, `QuotedRegex` as a source slice, and the round-trip harness moved in from P2 |
| P1-L10N | ~150-250 lines | `.AST($lang)`, table from `ClassInfo::rules`, lexer hook |
| P4 | ~150 + demand-driven | unchanged |
| P5 | unpriced | the `Doc::` subtree |

First green (App::Rak string needles, Intl::Format::Number) after
P0+P2c+P3 ≈ 1,400-2,100 lines; the 13 L10N dists after a further
≈1,100-1,900.

## `--rakuast` — the view on the command line (added 2026-09-11)

Not in Parts I/II; the user's ask. `--ast` prints our tree (main.cpp:1608 the
flag table, :2192 `Mode::Ast`, :2618 the usage line, :2749 `dumpAst`;
`--ast-roundtrip` at :2193 and :2761-2787 is the precedent for a mode that
dumps two trees and compares them). **`rakupp --rakuast SRC` is a P1
deliverable**: it prints the RakuAST view of the whole program — the
`:compunit` shape, what `slurp($f).AST(:compunit)` answers on Rakudo — in the
oracle serialization below, and the Rakudo-side dumper prints the same
serialization, so the per-file measurement of Part I's *Could the view be 1:1*
question is a shell `diff`, and polishing the view is: pick a corpus file,
diff, fix the first differing line, repeat.

**The serialization** — one spec, two implementations, each proven against
the other by the diff being empty on files the view already handles:

- one node per line, two spaces of indent per depth, children in the node's
  own `visit-children` order (measured on 2026.08: `Call::Name` visits `Name`
  then `ArgList`; our builder emits in the same order per class);
- the class name without the `RakuAST::` prefix;
- then the node's *syntax-bearing scalar attributes* as ` key=value`, sorted by
  key — strings `.raku`-quoted, Ints plain, Bools `True`/`False`, type objects
  by name. An attribute whose value is a node or a list of nodes is a child
  line, not a value.
- **Which attributes count.** Measured on 2026.08, `.^attributes` on a RakuAST
  node exposes compiler state beside syntax: `IntLiteral` carries `$!value`
  next to `$!origin`, `$!sorries`, `$!worries`, `$!thunks`, `$!sunk`,
  `$!okifnil`; `Call::Name` adds `$!resolution`, `$!parse-performed`,
  `$!begin-performed`, `$!callstatic`, `$!ct-inline-candidate`;
  `VarDeclaration::Simple` has `$!sigil`/`$!twigil` (syntax) beside
  `$!initializer-method`, `$!attribute-package`, `$!accessor` (state). None of
  the state is syntax and a view must not reproduce it. The rule is testable
  rather than a hand-kept list: an attribute is in the dump iff its value is
  a Str/Int/Bool/Num/type object **and it passes the two-position test** —
  the same statement dumped from a different line and column dumps
  identically (which excludes `origin` mechanically, and anything else that
  encodes where the source was). The Rakudo dumper applies the rule; the
  per-class attribute list it yields is written down once as the spec and
  re-derived per oracle version.
- the number: `1 − changed-lines ÷ oracle-lines` from a line diff, per file,
  published as raw counts per corpus (files, oracle lines, matching lines),
  pinned to the oracle version.

**Files**: main.cpp (flag, mode, usage), `RakuAstView.cpp` (the builder,
P1), a new `RakuAstDump.cpp` beside `AstDump.cpp` (the serializer, parse
archive), `tools/rakuast-oracle.raku` (the Rakudo side — installed 2026-09-11, first
version: a child process per file because `.AST`
runs `BEGIN` and `use`; no per-file cap yet — the accepted corpus has one BEGIN block, and Proc::Async's broken promise escaped `try` twice, so the seed uses synchronous `run` — and a `visit-children` walk),
`tools/rakuast-diff.raku` (runs both sides, computes the percentages, writes
the TSV under docs/dev/findings/), and the `--ast` row at
docs/guide/CLI.md:379 gains a sibling. `--slim` is not involved: the flag
lives in the CLI binary, which always carries the parser. Optional at P2:
`--rakuast=deparse` printing the DEPARSE text, so the round-trip gate is
`rakupp --rakuast=deparse f | rakupp -` on the shell — decide when the
renderer exists. Size ≈150-250 lines across the four pieces; the builder
itself is P1's.

### The corpus, measured (2026-09-11)

The question behind `--rakuast` was whether raku-corpus plus every program we
can run gives a big enough target to polish the view against. Measured
before any rakupp side exists — the full table, the failure list and the
caveats are in
[findings/rakuast/README.md](../findings/rakuast/README.md):

- raku-corpus (4bede39): **1,870 programs**, of which Rakudo 2026.08 produces
  a tree for **1,858** (99.4%) — 162,992 nodes, 213 distinct classes,
  44 covering 95% of nodes, 51 appearing three times or fewer. Against Part I's
  44-file measurement (24,442 nodes, 125 classes, 39 for 95%): 6.7× the nodes
  and a slightly wider head.
- The 12 without a tree are not the corpus's fault: 4 crash inside the
  2026.08 RakuAST frontend (`NQPMu`; a user-defined `postfix:<!>` is one
  trigger), 7 are book examples of deliberately wrong calls that `.AST`'s
  compile-time check refuses — the trap Part I records, now with a count —
  and 1 is pod-table strictness.
- Two more synthesized nodes for the 1:1 list: every `CompUnit` carries
  `VarDeclaration::Implicit::Doc::{Data,Finish,Pod,Rakudoc}` with no source
  token. And `Call::Name::WithoutParentheses` occurs in 1,571 of the
  programs, so P1's `parenned` bit is exercised almost everywhere.
- What "executable" means here: the oracle set is *what the pinned Rakudo
  compiles*, not what rakupp runs — `BEGIN` executes at `.AST` time (four
  corpus programs print during it), `use` resolves against the child's
  include path (21 programs need `-I` for a sibling module), and compile-time
  checks apply. The archived per-file TSV is the 2026.08 baseline; re-run
  and re-pin when 2026.09 ships.
- The corpus polishes the **view** (P1) and the **deparser** (P2's
  round-trip); trees built with `.new` — the App::Rak / Intl::Format::Number
  side — are covered by the t/12-rakuast slice and the modules themselves,
  not by any corpus of source.

---

# Part IV — the implementation log

One section per step as it lands: what is in the tree, where it deviates from
the plan above and why, and what the work found that the plan did not know.

## P0 — pragma gating + the RakuAST:: registry (landed 2026-09-11)

`use experimental :rakuast` is real, and the `RakuAST::` classes exist:
constructible, carrying Rakudo's own ancestry, so `~~ RakuAST::Expression`,
`.^mro`, `.^parents`, `.isa` and `.does` all answer as they do there. No `.AST`,
no `.DEPARSE`, no `.EVAL` — per the release-train rule this does not ship alone.

**Files.** New: `src/RakuAstClasses.{h,cpp}` (the registry), the generated
`src/rakuast-classes.inc`, `tools/rakuast-class-table.raku` (the generator),
`t/regression/rakuast-registry.raku` (30 checks). Touched: Parser.{cpp,h},
Interpreter.{cpp,h}, Builtins.cpp, MethodCallPart2.cpp, Codegen.cpp,
AstSerial.cpp, Ast.h, Value.h, SlimScan.cpp, stubs/stub_eval.cpp,
CMakeLists.txt. ≈540 engine lines against the ~300-500 estimate; the overrun is
the ancestor closure (below) and the per-routine pragma (below).

### The table is 113 classes, not ~59

The plan budgeted the 39-class head plus ~20 intermediates. Built from the
measured head instead — the 44 classes covering 95% of raku-corpus's nodes,
plus every class the blocked dists name, plus **the ancestor closure of both** —
it comes to 113. The closure is the whole of the overrun and it is not
optional: `~~ RakuAST::Expression` is what a walker writes, and `Expression` is
only ever an ancestor.

**The chains cannot be derived, and that was measured, not assumed.** For 44 of
the 113 classes the linearization is *not* its first parent's chain with a head
in front — Rakudo's are C3 over roles that each class composes for itself.
`RakuAST::Literal`'s first parent is `Termish`, while `RakuAST::IntLiteral`,
which inherits from `Literal`, has `Term` before `Termish`. So the table stores
each class's whole list, generated from the installed Rakudo, and rakupp
reproduces rather than recomputes. `.^mro` is the row plus `Any` and `Mu`;
`.^parents` is the row minus the class itself — both verified byte-for-byte
against 2026.08 (`MayCreateBlock` and `Node` answer the empty parent list on
both engines, and `ApplyInfix` keeps `Node` in the *middle* of its mro, ahead of
`WhateverApplicable`).

**One hook carries all of it.** `typeAncestry()` (Builtins.cpp) already
delegates the `X::` tree to a generated table for exactly this reason; RakuAST::
is a second delegation on the same line, after the map miss, so nothing else
pays for it and `.isa`, `.does`, `~~` on type objects and `lubType` are all
right without a second table to keep in step. `parent` is the first ancestor and
`extraParents` the rest, so `findMethod` reaches `RakuAST::Node` (where P2c's
`.DEPARSE` will live) and an instance type-check finds any ancestor in one
level. `.does` was the one path that climbed only `parent` and needed the
ancestry explicitly.

### The pragma is a fact about a ROUTINE, not only about a unit

Part II put a flag beside `langRev_` and save/restored it around module load.
That is necessary and not sufficient: **a module's subs are hoisted before its
mainline runs**, so by the time the `use experimental :rakuast;` statement
executes, the routines it was written to govern already exist — and every
`RakuAST::` name inside them then refused when the importer called them. Which
is the entire case the namespace exists to serve (App::Rak calls
Needle::Compile's `compile-needle`; the RakuAST names are inside *its* body).

So the pragma rides `langRev`'s three existing seams, in all three places:

- `Program::usesRakuAst`, set by the parser — this is Part II's flag, and this
  is its real reason. Read before `hoistSubs`, at `exec` and at module load.
- `Callable::rakuAst`, stamped at the four sites that stamp `Callable::langRev`.
  It lives in the padding after that `int`: `sizeof(Callable)` is 568 before and
  after, measured.
- the `anyRevSwitch_`-guarded swap in `callCallableRaw` and `invokeMethod`,
  which now restores both. The pragma arms `anyRevSwitch_`, so a process that
  uses neither 6.e nor RakuAST never takes the branch at all.

### The precomp cache silently dropped it — first run green, second run red

`serializeAst` writes a Program's **statements** and none of its scalar fields.
A cached module therefore came back with `usesRakuAst` false, hoisted its own
subs as if the pragma were absent, and refused its own names — on the second run
and every run after, while the first run was green. `deserializeAst` now
recovers the flag from the statements (the pragma is an ordinary `use`), so a
deserialized Program answers exactly as a freshly parsed one and no format bump
was needed.

**The same shape is still open for `Program::langRev`**: a cached module is
hoisted under the *importer's* revision rather than its own. Nothing in this
campaign depends on it and it is not a RakuAST regression — it predates this
work — but it is the same trap one field over. Filed as open item 13.

### Two divergences, pinned by the regression case rather than fixed

- **`--exe` does not refuse an un-pragma'd name.** The native codegen runs no
  undeclared-name check at all: `say Nonexistent` prints `(Nonexistent)` from a
  compiled binary today. RakuAST inherits that leniency and nothing about this
  step should change a general property of that backend. The case asserts both
  halves, so the day the native backend grows the check it says so instead of
  going quietly green. What a module's feature probe actually asks — `try
  ::('RakuAST::Node')` — *is* identical in both, because codegen does not
  compile `::()` and the unit falls back to the interpreter.
- **`class RakuAST::Mine {}` keeps working under bare 6.d**, where Rakudo
  refuses to let a program so much as mention the name. Deliberate, and it is
  what makes the registry-not-`classes_` decision pay: a user class is found
  before the gate is ever reached, and shadows a registry class of the same name.

### Smaller findings

- `rtUse` never received a `use` statement's `:tag` arguments — codegen dropped
  them on the floor, so a compiled program's runtime pragma state could not
  match the interpreter's. It takes them now.
- No `experimentalScopes_` in the parser. Nothing at P0 gates at parse time, and
  unused lexical state is worse than state added when its consumer arrives;
  it belongs with `.AST`.
- Four threads each `require`-ing the same module race inside rakupp's
  module-load publish, RakuAST or no RakuAST. The acceptance case is therefore
  one worker doing the `require`, plus — for what this step actually needs to
  prove — eight threads racing the lazy materialization under 6.e, where
  nothing publishes the registry up front.
- **Oracle quirk, for the generator:** `::("RakuAST::WhateverCode::Argument")`
  answers a `Failure` on 2026.08 while the literal name resolves. Nested names
  do not come out of the symbolic lookup; the generator walks the package stash
  (`::("RakuAST::WhateverCode").WHO<Argument>`) for anything qualified.
- SlimScan counts a `RakuAST::` name and the pragma as `eval` uses. Measured:
  `--slim=auto` on hello still cuts all four features; the same on a
  two-line RakuAST program cuts three and keeps the parser, and the binary runs.

### The gates, and what the perf leg actually measured

`t/run.raku` 857/857, `t/slim/run.raku` all green (arm64; the x86_64 slice and
the WASM row are deferred to the end of P2c by decision, since P0 never ships
alone), `tools/run-optbench.raku` — all nine rows agreeing across interp,
`--exe`, `--exe -O` and Rakudo. Startup is unchanged (80 interleaved runs:
best 2.39 → 2.30 ms, median 2.65 → 2.50 ms — the new build measures *faster*,
which is the size of the noise). Sizes: the CLI 14,788,408 → 14,808,360 bytes
(+19,952, +0.13%), a non-slim `--exe` hello 10,295,040 → 10,295,472 (+432).

**`perf-guard --check` fails — and fails identically on unmodified HEAD.** On
this machine today (load 3.4 of 8 cores, the user's own applications) it reports
`loopsum` and `hash` over tolerance for the P0 build *and* for the clean
baseline binary built from 95be351. The recorded baseline is `2026-09-01
(v3.24.0)` — v3.27.0 shipped without re-recording it, and shipped
[#79](https://github.com/ash/rakupp/issues/79), the unlocated ~10% call-path
regression, beside it. So the gate's own answer here is standing debt, not an
answer about this change; the A/B is what resolves it.

**The A/B, and its controls.** `tools/perf-guard.raku` timings, nine interleaved
rounds per pair (a scratch driver alternates the two binaries inside each round,
so drift lands on both sides), medians compared:

| pair | mean | worst kernel |
|---|--:|--:|
| a binary against ITSELF (the floor) | +0.07% | ±1.5% |
| baseline + one unused byte in `Callable` (pure recompile) | +0.23% | +1.5% |
| baseline + the registry TU linked and one never-taken call (**no behaviour**) | **+0.62%** | +1.7% |
| baseline vs P0 | **+1.24%** | +2 to +5%, varying by sitting |

So about half of P0's measured cost is carried by a control that runs no new
code at all; the residual is ~0.6% spread across every kernel, with no kernel
carrying it consistently, against a harness whose floor on the mean is ±0.2%.
(Read the +0.62% row against what #79's own investigation found — displacing
107 lines of never-called code WITHIN a translation unit moved `fib` by 0.4%.
That control moved code inside a TU; this one adds a TU to the link, which is
a different perturbation, and neither says the effect is imaginary.)
That is the 6E campaign's result over again ("about 1% uniform, at the noise
floor"), and it is as far as this machine can resolve it.

**Accepted as noise for now — the user's call, 2026-09-11.** The re-measure is
not waived, it is moved: `--check` and the A/B run again on a quiet machine
before the phase that ships this, and the baseline binary and the interleaved
driver are kept at `rc-work/rakuast-p0/` (`rakupp-95be351`, `ab.raku`) so it is
a re-run rather than a rebuild.

**Three localized hypotheses, each killed by measurement** — worth recording
because each was plausible and each was wrong:

1. *The widened `RevGuard`* (it now restores the pragma beside the revision, so
   it is two words bigger and is constructed on every call). Isolated A/B of the
   two builds that differ only in it: **−0.14% mean**, signs both ways. Free.
2. *A `shared_ptr` local in the type-invocant method arm.* Rewritten to a raw
   pointer into the registry (which is the better code and was kept): the mean
   did not move. Not it either.
3. *The new `Interpreter` field.* This one was half right — declared after
   `int langRev_` it did not fit the padding and pushed every later member of a
   2,336-byte object along by eight, and `sizeof(Interpreter)` said so. Moved
   into the three bytes after `anyRevSwitch_`, the size is identical to baseline
   again and `objnew` came back from +4.2% to +2.1%. The declaration reads worse
   where it now sits, which is why the comment there says why.

### Doc sync, and what was deliberately NOT flipped

Synced: METAPROGRAMMING.md (the `RakuAST::…` row is now ◑ and says which half
is in), ARCHITECTURE.md (the grammar-mutating-layer sentence names the four
operations rather than the whole namespace), dev/README.md (the plan's entry
says "under construction", with the order), faq/6e.md (the `RakuAST` section
says what rakupp answers today, and the "deliberately not done" bullet says the
campaign is under way and why the live matrix row has not moved).

**Not flipped, on purpose:** guide/OVERVIEW.md and guide/HIGHLIGHTS.md keep
`RakuAST` in their "not there yet" lists. Those lists answer *what can a reader
do*, and at P0 the answer is still nothing: no `.AST`, no `.DEPARSE`, no
`.EVAL`. They move when the renderer answers. Same for the live 6.e matrix,
whose probe is `RakuAST::IntLiteral.new(42).DEPARSE`.

### Roast, three runs at the pin

`b2cbe8a42`, `--workers=2` (what the v3.27.0 lists were taken at), 1,464 files:
**670 / 670 / 669** fully passing. Runs 1 and 2 produced byte-identical file
lists; run 3 lost `S17-scheduler/basic.t`, which the union keeps.

- `comm -23 v3.27.0-union p0-union` — **empty**, and so is the other direction.
  The union is the same 670 files v3.27.0's four runs produced.
- Per-file pass counts against the best of v3.27.0's four archived runs
  (`rc-work/release-3.27/roast-run*.txt`): **zero files dropped**. One file is
  in the older runs and not in these — `S17-lowlevel/cas.t`, which timed out in
  two of those four runs as well.
- The canary holds: `S32-str/format.t` still reads `1/1` — it aborts at test 2,
  exactly as the entry criteria recorded, so no phase has leaked behaviour into
  the one Roast file that executes a `RakuAST::` name.

Lists and per-file output are under `rc-work/rakuast-p0/` (scratch, not a
release measurement — `docs/status/roast-lists/` is one file per release).

## P2c — the renderer over constructed trees (landed 2026-09-11)

`.DEPARSE` answers, and `RakuAST::IntLiteral.new(42).DEPARSE` is `42` — the
probe the live 6.e matrix runs. **57 of 57 spec cases render byte-for-byte as
Rakudo 2026.08 does**, including the whole tree App::Rak's commonest invocation
builds.

**Files.** New: `src/RakuAstDeparse.cpp` (construction + renderer),
`tools/rakuast-deparse-spec.raku`, `tools/rakuast-t12.raku`,
`t/regression/rakuast-deparse.raku`, and two findings TSVs. Touched:
`RakuAstClasses.{h,cpp}` (the builtin methods), the generated table, the stubs,
CMakeLists.

### One spec file, two engines, and the diff is the gate

`tools/rakuast-deparse-spec.raku` is engine-neutral. Run under Rakudo it
produced `docs/dev/findings/rakuast/deparse-2026.08.tsv`; the regression case
runs the same file here and compares row by row, both directions. Two copies of
57 constructions would drift, and the gate would then be comparing this engine
against a memory of the oracle rather than the oracle — the same shape Part III
specifies for `--rakuast`.

### Reading the tarballs found 17 classes the plan's summary did not

The vocabulary is a grep of Needle::Compile 0.0.12, Intl::Format::Number 0.2.0
and RakuAST::Utils 0.0.3 — **52 distinct classes, of which the P0 registry
carried 35**. The table is now 136. Noted on the way: the REA index on this
machine tops out at Needle::Compile **0.0.9** while Part III cites 0.0.12, which
fetched fine by URL — the local index is behind fez, which matters for any
future "read it from the tarball" step.

### Three conventions, measured rather than assumed

- four-space indent, and no trailing newline after a closing brace;
- a statement list `;`-separates all but the last statement and newline-
  terminates every one — one statement is `42\n`, two are `42;\n"foo"\n`;
- **an explicit `$_` invocant elides**: `ApplyPostfix(Var::Lexical('$_'),
  Call::Method('fc'))` renders `.fc`, the same text `Term::TopicCall` gives.
  `.fc` *means* `$_.fc`, so nothing is lost — but a renderer that keeps the `$_`
  hands back a differently shaped tree, which is the thing P1's round trip is
  about to measure. (This was briefly mistaken here for an upstream bug; `"A".fc`
  and `(1,2).elems` keep their invocants, which is what settles it.)

An uncovered class throws `X::NYI` naming itself. The renderer's contract is
correct Raku first and Rakudo's spelling second, because P3's `.EVAL` runs over
the text it produces.

### The t/12 slice: 16 files, zero crashes

P2c's requirement over `.new`-built trees is met. **5 of 328 assertions pass**,
which is the honest baseline: each `ast-ok` checks four things per case and only
`.DEPARSE` is in scope — P3 moves two more, and `.raku` on a node is on no
step's list. The `first-error` column is P4's widening list read off real
upstream tests: `Var::Compiler::File`, `Statement::Empty`, `Class`,
`Postfix::Power` (classes), `QuotedString`, `FatArrow` (renderer cases),
`Name.is-identifier` (a method), `Circumfix::Parentheses.new` (a positional
constructor).

### Gates

`t/run.raku` 857 of 858 — the one failure is `example: echo-server`, which
cannot bind 127.0.0.1:8099 because a concurrent session's `rakupp
api-server.raku` holds it; the pre-P2c binary fails identically, so it is a
collision, not a regression. `t/slim/run.raku` green, and `--slim=auto` on hello
still cuts all four features. Perf and Roast were not re-run for this step: the
perf re-measure is already parked for a quiet machine, and the renderer adds no
path a Roast file reaches (no Roast file at the pin constructs a `RakuAST::`
node — the one that names a class asserts `~~ RakuAST::Node`, which P0 settled).

## P3 — `.EVAL` on a tree, and the side table (landed 2026-09-11)

The bridge closes. A constructed tree runs, in the caller's lexical scope, by
both spellings — the `.EVAL` method on a node and the `EVAL $node` sub form
Intl::Format::Number writes — and a live value the text cannot carry survives
the trip with its identity intact.

**Files**: `src/RakuAstDeparse.cpp` (the side table and `rakuAstEval`),
`RakuAstClasses.{h,cpp}`, `src/Builtins.cpp` (the sub form), the stubs,
`tools/rakuast-eval-spec.raku` and `t/regression/rakuast-eval.raku`.
≈120 lines against the 170-270 estimate — the small figure is the point: the
text bridge means there is no compiler here, only a renderer and `evalString`.

### The nine cases, and what they actually prove

`tools/rakuast-eval-spec.raku` is engine-neutral like the deparse spec, and
**both engines produce the same nine rows**. The first four are Part I's scope
probes rewritten from `EVAL q[…]` to `.EVAL` on a tree — 42 / 42 / 99 / 21 —
and each answer is reachable only if the fragment saw the enclosing scope.
Case 3 is the one that carries it: the `say` runs OUTSIDE the EVAL and still
sees 99, so the fragment wrote to the real container rather than a copy. Case 5
is the other half — a live object comes back `===` itself on both engines,
which is free for Rakudo (it compiles the tree) and is the side table here.

### The side table, and the one place this beats the oracle

A `Literal` holding a closure or an object has no source behind it. Rakudo
renders those as an address comment and the value is gone. Here they render as
`$RAKUAST-LITn` and `.EVAL` binds the name back to the value in a CHILD scope —
the parent link keeps every name the caller had reachable, and the synthetic
names are gone the moment it returns. The counter runs whether or not anyone is
collecting, so a bare user-called `.DEPARSE` and the one `.EVAL` makes produce
the same text.

`nameEvalsCode` needed no change: SlimScan already sees `.EVAL` by name, and a
program that calls it on a node cuts nothing and runs compiled (verified).

### What the t/12 slice caught, which nothing else would have

Re-running it after P3 turned one file from `threw` to **CRASH** —
`statement.rakutest`, SIGSEGV. Two defects, both mine, both in the P2c
renderer and neither reachable from the 57-case spec:

1. **An unguarded dereference.** `Statement::If` did `render(*attr(node,
   "then"), …)` where every other case uses the null-safe `opt`. A node built
   by `.new` with a required child unset is a real shape — the `.raku`
   round-trip leg of upstream's own helper constructs exactly that — and the
   miss segfaulted. There was exactly one `*attr(` in the file; there are none
   now.
2. **A silently wrong rendering, which is worse.** `Statement::If` dropped its
   `elsifs`, so a three-branch statement came back as `if … else …` — valid
   Raku that means something else, the one thing this renderer must never
   produce. The chain renders now, byte-identical to upstream's expected text,
   and the case is pinned in the spec (58 cases, still all matching).

That is the argument for the slice in one paragraph: a 57-case spec written by
the same person who wrote the renderer tests the shapes that person thought of.

**After P3**: 16 files, no crashes, **7 of 328 assertions** (from 5). The
remaining gap is `.raku` on a node — upstream's helper checks four things per
case and that leg is on no step's list.

### Gates

`t/run.raku` **859 of 859** — and the `example: echo-server` failure reported
under P2c is gone with the port, confirming it was a collision with a
concurrent session and not a regression. `t/slim/run.raku` green.

**Roast: one run, not three, and here is exactly what that bought.** 668
fully-passing against P0's 670/670/669. The two files the P0 union has and this
run does not are `S17-scheduler/basic.t` and `S32-io/io-special.t`, and both are
`[TIME]`, not a failure — each has timed out in some earlier run of unmodified
code and passed in others, and run alone right afterwards they pass **34/34**
and **48/48**. The machine carried 17 timeouts this run against 13-16 before,
which is the difference. One run cannot do what three plus a union diff do, so
this is recorded as a sanity check: the only path a Roast file can reach in this
step is one guarded branch in the `EVAL` builtin, inert unless its argument is a
RakuAST node, and no Roast file at the pin constructs one. The three-run gate
runs with the phase that ships this.

## P1 — the `.AST` view (landed 2026-09-11)

`'source'.AST` builds the RakuAST view over our own parse, and `parse → view →
DEPARSE` returns byte-identical text to Rakudo on the shapes both cover.
`.AST(:compunit)`, the three `StatementList` accessors and `QuotedRegex` as a
source slice are in, which is App::Rak's regex and code needles.

**Files**: `src/RakuAstView.cpp` (new), the four surface facts in Ast.h + Parser,
AstSerial (**bumped 18 → 19**), MethodCallPart3 (the `.AST` arm),
MethodCallPart2 (node attribute accessors), the registry and its generator,
`tools/rakuast-roundtrip.raku`, `t/regression/rakuast-view.raku`.

### The four surface facts cost exactly what the plan predicted

Measured with the same headers before and after: `Call` **80 → 88** (the one
that cannot hide, nano-malloc bucket 80→96, paid once per Call at PARSE time and
never read at eval), and `VarExpr` 296, `Index` 80, `SubDecl` 400 all
**unchanged** — `synthTopic`, `angleKey` and `retTypeSpell` each landed in an
existing hole. The bump also closes the `VarExpr::viaPseudoPkg`/`pseudoPkg`
serializer gap, which was never written at all: a cached unit came back with
`$::($n)` reading as an ordinary lexical. One invalidation, not two.

`retTypeSpell` has no reader yet. It is recorded now because the alternative is
a second cache invalidation when the signature view wants it.

### The round-trip harness is the gate, and it found four real bugs

`tools/rakuast-roundtrip.raku` renders every examples/ and showcase/ program
through the view and **parses the result again**. Three outcomes, and only one
of them is a failure: `view` and `deparse` are named MISSES, while `reparse` —
both ran and the text will not parse — is something WRONG. The tool fails on
`reparse` and nothing else.

It went 7 reparse failures → **0**, and each was a rendering that was valid-
looking and incorrect:

1. **A bare `;` on its own line after every closing brace.** A block statement
   already ends in a newline and takes neither a `;` nor a second one.
2. **`say [+]@a`** — a reduction metaop needs the space the parser demands of
   it, and four corpus programs use one.
3. **`multi` dropped from the declarator**, so two candidates rendered as two
   `sub`s of one name — a redeclaration error rather than a program. The slurpy
   marker went the same way: `($p, *@rest)` rendered `($p, @rest)`.
4. **`Statement::Unless` read the wrong attribute**, so the body vanished and
   the `if` inside it became a separate statement.

And one defect that was not a rendering at all: **a parse error inside `.AST`
escaped as a top-level `===SORRY!===`**, past every `try` and `CATCH`, taking
the program with it. It is a Raku exception now. The harness found it because
re-parsing rendered text is precisely the call most likely to meet bad source.

**Where it stands**: 59 files, **27 round-trip completely**, 32 stop at a named
`view` miss (package declarations 12, `given`/`when`, the loop controls, `s///`),
0 reparse. Those 32 are the widening list, read off real programs.

### Two measured spellings that no amount of reasoning would have given

- **A CompUnit terminates every statement; a bare StatementList omits the last.**
  `q[say 1].AST.DEPARSE` is `say 1\n` and `.AST(:compunit).DEPARSE` is `say 1;\n`
  — structural, so no per-statement state reproduces it.
- **`RakuAST::CompUnit` has no `.statements`**, only `.statement-list`. Reaching
  through would have been a leniency a module's `.^can` probe could read as a
  different API, so it does not.

### Still open in P1

`--rakuast` (the CLI flag, the dump serialization, `tools/rakuast-diff.raku`)
and the published tree-oracle match percentage. The view has to cover more of
the corpus before that number means anything — the 32 `view` misses would
dominate it — so the widening comes first and the number after.

### Gates

`t/run.raku` **860 of 860**, `t/slim/run.raku` green, the three earlier RakuAST
regression cases still green, and `ast-cache-publication` green across the
version bump. Roast and the perf A/B are owed for this step and are deferred
with the ones P3 named: the perf leg wants a quiet machine and this step adds
8 bytes to `Call`, which the plan says is the one thing that could show.

### P1 widening, round one (2026-09-11)

**27 → 45 of 59 programs round-trip completely**, 13 named `view` misses, 0
reparse. The registry is 175 classes. Added: package declarations
(`class`/`role`/`grammar`/`module` — four classes upstream, told apart by the
declarator), `given`/`when`/`default`, the C-style `loop`, and the loop
controls, which are CALLS upstream rather than statements of their own
(measured: `for … { last }` puts a `Call::Name::WithoutParentheses` in the body).

The harness caught four more renderings that were valid-looking and wrong — the
same shape of find as the first round, and the reason the reparse leg is the
gate rather than the counts:

1. **A named parameter lost its colon.** `(*@a, :$solar)` rendered
   `(*@a, $solar)`, which does not merely read differently — it moves the
   parameter into the positional list, and the parser refuses it outright
   ("required parameter after variadic"). Two corpus programs.
2. **A sigilless binding lost its backslash.** `my \NULL = …` rendered
   `my NULL = …`, a different declaration that does not parse.
3. **The contextualizers leaked an internal name.** `@($x)` rendered `ctx@ $x` —
   our own spelling of the op, in what is supposed to be Raku. They are
   `Contextualizer::{List,Hash,Item}` upstream, not prefix operators.
4. `is copy` and the other parameter traits are still dropped; that one only
   reads wrong, so it is a miss rather than a failure.

**The 13 that remain**: phaser blocks (7), `s///` (3), a pair with a computed
key (3), and one `Call::MetaMethod` the renderer has no case for.

One test moved with the code: `t/regression/rakuast-view.raku` asserted that
`class C { }` throws X::NYI, which was true when it was written and is not now.
The assertion is the FRONTIER, not that particular construct, so it points at a
phaser block instead — and it will move again.

## `--rakuast` and the tree oracle (landed 2026-09-12)

**`rakupp --rakuast FILE` prints the view as an indented class-name tree**, the
sibling of `--ast`. `tools/rakuast-oracle-dump.raku` prints the same
serialization from Rakudo, so comparing the two engines is a `diff`, and
`tools/rakuast-diff.raku` sweeps a corpus and scores it.

**The published number: 70.1% — 17,162 of Rakudo's 24,499 nodes, over the 39 of
59 corpus programs both engines can tree** (8 Rakudo itself refuses, 12 the view
refuses by name). The view also builds 5,565 nodes Rakudo's tree has not; that
count is published beside the percentage because the plan's formula is recall
and recall alone can be gamed by adding nodes.

(It read 72.0% over 36 files when this section was first written. P1-L10N's
widening admitted three more programs and grew the denominator by 9,090 nodes,
and the three are harder than the average of the thirty-six — so the coverage
went up and the percentage went very slightly down. Both halves are the
measurement; neither is the measurement on its own, which is why the file count
is published with the number.)

**Files**: `src/RakuAstDump.cpp` (new), `--rakuast` in main.cpp + CLI.md,
`tools/rakuast-oracle-dump.raku` and `tools/rakuast-diff.raku` (new), the
measured per-file table in `docs/dev/findings/rakuast/oracle-shape-2026.08.tsv`,
and the view/deparse/registry work the oracle drove.

### The plan's two-position test does not separate syntax from compiler state

The plan's rule for deciding which scalar attributes are syntax-bearing is the
TWO-POSITION TEST: parse the same statement from a different line and column,
and keep only the attributes that agree. It excludes `origin` mechanically,
which is what it was designed for, and it does not do the wider job. Measured on
2026.08, a single `my $x = True`:

```
VarDeclaration::Simple begin-performed=1 bind-targeted=0 forced-dynamic=False
  hoisted-to-outer=0 initialized=True is-bindable=True is-parameter=False
  lowered-array-init=0 lowered-to-local=0 meta-object-produced=True okifnil=0
  parse-performed=1 scope="my" sigil="$" sunk=1 twigil="" unused-slurpy=0
```

`scope`, `sigil` and `twigil` are syntax; the other fourteen are compiler state,
and every one of them is POSITION-INVARIANT, so the test keeps them. An
attribute-level percentage would have been dominated by state a view is not
supposed to have.

So both dumps are **shape only** — class name and nesting — and attributes are
behind `--rakuast=attrs` / `--attrs` for anyone who wants to look. That is a
narrowing of what the plan specified, recorded here rather than quietly done.

### A RakuAST node is not a tree node, and the first dump walked the graph

The plan said `visit-children`. The first version of both dumps walked the
attributes instead, sorted by name, on the reasoning that name-anchored order is
deterministic on both engines without either side reproducing the other's
traversal. That reasoning was wrong in a way only measurement showed: a RakuAST
node links UPWARD and sideways — a declaration knows its containing block, a
statement list knows its comp unit, a resolver hangs off the unit — so an
attribute walk is a graph walk. A twelve-line program dumped 45 nodes, 19 of
them the same handful of blocks reached from underneath, and one 40-line program
ran for twenty minutes before a `.WHICH` visited set bounded it.

`visit-children` is the syntactic children in source order. Switching to it cut
that 45 to 26 and made the two dumps comparable at all. The visited set went
away with the graph walk; the per-file `alarm 120` cap stayed.

### The first score was not a diff

`1 − changed ÷ oracle-lines` was implemented as line N against line N. That is
not a diff: one extra node near the top shifts every line under it, so a tree
that was right everywhere but one statement scored 1%. The number measured the
alignment. It is `diff(1)` now — normal format, counting the oracle lines
reported as deleted — and the same corpus that read 1.2% read **41.6%** with no
change to either engine.

### What the oracle found, in the order it found it

Each of these was a real divergence, and each was invisible to the round-trip
harness because the wrong shape rendered to text that parses:

1. **`ApplyInfix` keeps its operands in an `ArgList`**, not in `left`/`right`
   slots. Measured: 2026.08's node carries exactly `$!infix` and `$!args`, and
   `.left`/`.right` read through the list. The view invented two slots — two
   nodes Rakudo's tree has not, one it does. `.new(:left, :infix, :right)` is
   still the constructor every dist writes, so `rakuAstNew` folds it the way
   Rakudo's own constructor does.
2. **`=` is not an `Infix`.** It is `RakuAST::Assignment`; `+=` and friends are
   `MetaInfix::Assign` wrapping the plain infix, not an operator spelled `+=`;
   and `:=` *is* a plain infix. Three spellings, all measured.
3. **A declaration owns its initializer.** `my $x = 1` reaching the view through
   the expression path built `ApplyInfix` over a declaration; Rakudo has no such
   shape — `VarDeclaration::Simple` with an `Initializer::Assign` child, and the
   `=` never becomes an operator at all. The statement-level path already did
   this; only the expression path did not.
4. **Statement modifiers are modifiers.** Our parser desugars all eight into the
   block form and sets `modifier`; the view rebuilt none of them, so
   `$total += $_ for 1..10` presented as `for 1..10 { $total += $_ }`. Same
   program, different syntax, and a walker asking whether a statement has a
   modifier got No from a tree that had one. `given` turns out to be a LOOP
   modifier and `with`/`without` CONDITION modifiers — measured, and not what
   the names suggest.
5. **A class body was empty.** This is the find that justifies the tool. Our
   parser lifts attributes, methods and grammar rules out of a `ClassDecl`'s
   statement list into their own vectors, and the view rendered only what was
   left — so a class with twenty methods produced a tree saying it had none.
   Not a gap: a WRONG tree, the one thing the view is not allowed to produce,
   and it round-tripped green because an empty class body is valid Raku.
6. `$!x` is a `Var::Attribute`, not a `Var::Lexical`.
7. **`has $.x` deparsed as `has $x`** — the twigil dropped. A public attribute
   with an accessor became a private one without, and because `has $x` is
   perfectly good Raku the round trip reparsed it and called it stable. The
   class-body work is what put a `has` in front of the renderer at all.
8. `True`/`False` are `Term::Enum`. A view cannot resolve a name against the
   setting, but the lexer already knows it read a Bool, so these two are the one
   case it can spell without resolution. `Less`, `SeekFromBeginning` and the
   rest stay `Term::Name`, because knowing THOSE are enum values is exactly the
   resolution a view does not do.

The corpus went **41.6% → 69.1% → 72.0%** across those, and the extra-node count
**6,384 → 3,507**.

### The oracle refused eighteen programs, and the oracle was the bug

Eighteen of the fifty-nine died with `Redeclaration of symbol 'JSON'`, which
read exactly like Rakudo refusing the program. It was the tool refusing itself:
`.AST` compiles in the caller's scope the way EVAL does, so a program that
declares a package declares it in the dumper — and the two-position test parses
the source twice in one process. The second parse now only happens under
`--attrs`, and ten of the eighteen came back.

### The round-trip count went DOWN, on purpose

**45 → 41 of 59 round-trip, and that is the fix working.** Eight programs whose
grammars had been rendering as empty bodies now refuse by name — `a grammar rule
(the regex tree)` — because the whole `Regex::*` subtree has no view yet. A
named refusal replacing a silently wrong answer costs four from a count that was
measuring the wrong thing.

### What is left, measured rather than guessed

From `--tally` over the corpus, the largest remaining blocks:

* **the regex tree** — `RuleDeclaration`, `TokenDeclaration`, `Regex::Sequence`,
  `Regex::WithWhitespace`, `Regex::Assertion::*` and the rest. This is the one
  big piece, and it is what holds the grammar-heavy programs out of the corpus;
* **derived nodes a view does not build**: `ParameterTarget::Var` carries the
  lexical declaration the parameter implies, a method's `Signature` carries an
  explicit invocant and the implicit `*%_`, and an attribute default is lowered
  into a `Method::Initializer` with a `Trait::WillBuild`. Adding these would
  raise the number without the view knowing anything more, which is what the
  `extra` column exists to make visible;
* **two spelling differences that are not errors**: Rakudo deparses a routine
  as `sub f ($x!)` — a space before the signature, and every non-optional
  positional marked required — where we render `sub f($x)`. Same program, and
  `RakuAST::Parameter` has no `required` slot upstream at all (it carries
  `$!optional` as a three-state `Bool`, and the `!` comes from that being
  explicitly `False`), so matching it means reproducing that three-state rather
  than adding a flag. Recorded, not done;
* **one traversal artifact, not a divergence**: `VarDeclaration::Simple` stores
  its `desigilname` as a `RakuAST::Name` on both engines, and Rakudo's
  `visit-children` does not visit it while our mechanical walk does — roughly
  one `extra` line per declaration. Not fixed, because the fix is a per-class
  exception table in the dump, and a dump tuned until the number improves is
  worth less than the number.

### Gates

`t/run.raku` 862/862, `t/slim/run.raku` all checks, the four RakuAST regression
cases (which run the deparse and eval spec files under both engines and diff),
and the round-trip harness: **0 files rendered unparseable text** — every gap is
a named `view` or `deparse` miss.

## P1-L10N — `.AST($lang)`, the localized parse (landed 2026-09-12)

`'mein $a = 42; wenn … { sag … }'.AST("DE")` parses German keywords and answers
the tree the English source would. **12 of the 14 L10N rows' own test suites are
green under rakupp** (AF, CY, DE, EN, EO, FR, HU, IT, JA, NL, PT, TLH); ZH and
the `L10N::Complete` bundle that depends on it are held by two things that are
not L10N — see *What holds the last two* below.

**Files**: `TokenXform` in Token.h, the seam in `rakuAstView`,
`Interpreter::l10nTokenXform` beside the `.AST` arm in MethodCallPart3.cpp, and
`t/regression/rakuast-l10n.raku`.

### No slang, and no second parser: it is a pass over the token stream

Part I rules out a grammar hook, and the L10N roles need none. Our lexer returns
**every keyword as a plain `Tok::Ident` and lets the parser decide** — that one
property is what makes a whole localized language a rewrite of some token texts
and nothing else. `TokenXform` is a `std::function<void(std::vector<Token>&)>`
applied between `lexer.tokenize()` and the `Parser` constructor; without a
language it is null and no path changes.

### The translation is the dist's, read two ways because it is stored two ways

A dist ships one generated role — ~185 `token <category>-<english> { <localized> }`
declarations plus a few `core2ast`/`trait-is2ast` methods.

* **The tokens** are already in the role's `ClassInfo::rules` once the module
  loads, so the map falls out of what the engine parsed. The English keyword is
  the name's **last** hyphen-separated segment: measured over all 185 names, and
  the only three-segment ones (`stmt-prefix-*`, `quote-lang-*`) put the keyword
  last too, so no table of categories has to be kept in step. DE and NL declare
  an identical name set, which is what makes the rule safe to generalize.
* **The routine names** (`sag` → `say`) are not tokens. They sit in a
  `constant %mapping` inside `core2ast`, which nothing outside the method can
  enumerate — so they are looked up by **calling it**, one word at a time and
  cached, with a **Match** as the invocant. That is exactly what the method is
  written for (`self.ast` is Nil, `self.Str` is the word) and how the slang calls
  it, so it cannot drift from what the dist ships.

The module is loaded with **no import**: the dist's `sub EXPORT` installs the
slang into `$*LANG`, which is Rakudo's frontend and not ours, and what it would
have done is the thing being replaced.

### Three filters, two of them wrong, both silently

Deciding which token bodies are keywords went wrong twice in opposite
directions, and each time the result was a language that looked translated:

1. An **ASCII identifier test** (`rakuIdentStart`, which is ASCII by design)
   dropped every German keyword with an umlaut — `füralle`, i.e. `for`. The
   program still parsed, because `füralle @list -> $x { }` is a call.
2. **Skipping every quoted body** then dropped Italian's `my`: the generator
   quotes anything that is not a *simple* bare word, which is two different
   things at once — operator spellings (`"^ff"`, `"(enthält)"`) and ordinary
   keywords containing a hyphen (`scope-my` is `"il-mio"`).

There is no filter now. Unquote and insert: a spelling our lexer can never
produce as one `Tok::Ident` is a key nothing will ever look up — eight entries of
dead weight, and it cannot be wrong. Deciding otherwise means re-deriving the
lexer's own rule beside it, which is what both bugs were.

The one deliberate skip is `meta-` (three entries: `meta-Z` is `R`). A
metaoperator reaches our lexer as a single operator token, never as an
identifier, so mapping the bare letters would rename a *variable* instead.

### `.AST` is not behind the pragma, and gating it failed every dist

Measured on 2026.08: `Q[say 1].AST` answers a StatementList with no `use
experimental :rakuast` in sight, while `RakuAST::IntLiteral.new(1)` is a
compile-time refusal. Rakudo gates the **names**, not the method. We gated both —
stricter than the thing being matched, and every L10N dist's own test opens with
`Q:to/CODE/.AST("DE")` and no pragma, so all fourteen failed on the wrong
grounds. The name gate (Interpreter.cpp) is unchanged; the method gate is gone.

### What the L10N suites found in the VIEW, which is most of this change

L10N::ZH ships ten test files where every other dist ships one, and it is the
best gate in the family. What it and the German probe turned up:

1. **`whenever $chan -> $v { … }` rendered `whenever $chan, -> $v { … }`** —
   a two-argument call instead of a handler, so the block never ran. It is
   `Statement::Whenever` upstream, and now here.
2. **`with`/`without` were built as `Statement::Given`** — three classes upstream,
   and the two defined-guards are shaped like the `if` family
   (`condition`/`then`), not like `given` (`source`/`body`). Worse, the `else`
   was dropped, so `with $x { } orwith $y { }` presented as a `given` with one
   branch and no alternative.
3. **`repeat` had no view** — `Statement::Loop::RepeatWhile` / `RepeatUntil`.
4. **Phaser blocks had no view**, the largest single gap left in P1:
   `StatementPrefix::Phaser::<Keyword title-cased>` over a `blorst`, measured
   over all seventeen keywords. This alone took the round trip from 41 to 44.
5. **A topic-less `for` was built as a pointy block with an empty signature**,
   rendering `for 1, 2 -> { … }` — which takes no parameter, so the body saw the
   OUTER `$_`. Upstream it is a plain `Block`.
6. **The string escaper existed twice**, each copy missing what the other had:
   `quoted()` escaped the newline and the tab but not the brace, and the
   QuotedString arm's own inline loop escaped the brace but not the newline. A
   literal segment holding a `\n` came back as an actual line break inside a
   string — legal Raku on its own, which is why it survived so long, and not
   legal the moment the same string also held a `{`. One escaper now.
7. **The statement-list separator was answered per class.** Measured on lone
   constructed nodes: Rakudo's `Statement::If` renders its own trailing newline
   and `Statement::For` does not, yet a list separates them identically — and so
   does it for `my $c = -> { 1 }`, which is no kind of block statement. The rule
   is on the LIST: a statement whose text ends in `}` gets a bare newline, never
   a `;`. Writing it per class gets one of those three wrong whichever way it is
   written.

Items 1, 2, 5 and 6 were **wrong renderings** — valid Raku meaning a different
program — so the round-trip harness could not see any of them.

### The gate is a fixture language, and it can fail

`t/regression/rakuast-l10n.raku` builds its own `role L10N::ZZ` in a temp
directory rather than depending on an installed dist: offline, and able to fail.
It carries the three shapes that were got wrong — a bare word, a quoted
hyphenated body (`"il-mio"`), and a body outside ASCII (`füralle`) — plus a
`core2ast`, and it asserts the translated program's OUTPUT, that the program's
own names (`Punkt`, `zeige`) survive untranslated, that a misspelled keyword is
left alone, and that without the language argument the source is *not* that
program. Verified to fail three separate ways when the fixture is broken.

### What holds the last two

* **`import Foo;` is not implemented in the engine** (`Undefined routine
  'import'`; Rakudo runs it). Nothing to do with RakuAST — L10N::ZH's
  `11-use-import` is simply the only test in the corpus that writes it.
* **`enum` has no view.** Our parser normalises `<red green blue>` to an array of
  string literals, losing the word-list spelling — and Rakudo *refuses*
  `enum Colour ["red", "green", "blue"]`, so rendering what our tree holds would
  produce text upstream cannot parse. The honest fix is to keep the `qw` spelling
  at parse time (a surface fact in Ast.h and a serializer bump, the same shape as
  P1's four), not to reconstruct it in the renderer.

### Gates

`t/run.raku`, `t/slim/run.raku`, the seven RakuAST regression cases, the
round-trip harness (**44 of 59, 0 files rendering unparseable text**), and the
fourteen L10N dists' own suites through `rakupp test`.
