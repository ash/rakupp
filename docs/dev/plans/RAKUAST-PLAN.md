# RakuAST in rakupp — design note and implementation plan

**Status: scheduled.** Part I (below) is the design note settled 2026-07-31 and
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
2026-09-01 it finds two comment lines, Builtins.cpp:6225-6227, and nothing else.)

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
reopen it. Drafted against the sources at v3.23.0+, with every file:line below
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
  wants Mu, gets Any). Run the ten RakuAST-marked files once with the todo
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

- Parser: capture the `:rakuast` adverb. Today it vanishes both ways — the
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
  taken, batched into **one** `kAstSerialVersion` bump (15→16, one-time cache
  reparse, noted in release notes):
  - `Call::parenned` (sites Parser.cpp:4789/2311 true, 4904 false). The one
    field that cannot hide in padding: sizeof 80→88, nano-malloc bucket
    80→96. Hot offsets unchanged; the byte is never read at eval.
  - `VarExpr::synthTopic` at the bare-`.` site (Parser.cpp:4170) — hole @41,
    sizeof stays 296.
  - `Index::angleKey` (angle sites 1924-1994, 2063-2090, 1697-1715) — hole
    @35, sizeof stays 80.
  - `SubDecl::retTypeSpell` ('o'/'r'/'a') at the trait sites 6435-6438 and
    the sigRetType_ merge at 6278 — hole @191, sizeof stays 368.
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
