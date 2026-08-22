# What Rakudo teaches Raku++

Ninth in the series, and the one that required a decision before it could be
written. Until 2026-08-22 this project's working rule was that Rakudo's code
stayed unopened — the clean-room stance recorded in
[JOURNEY.md](../../JOURNEY.md) and [LONGREAD.md](../../../../LONGREAD.md):
correctness came from Roast and docs.raku.org, never from "what Rakudo's
source does". The [MoarVM study](MOARVM-TECHNIQUES.md) stayed within it by
reading the VM's repo docs and public talks. This doc records the stance's
deliberate evolution, not its abandonment: with the engine past 90% of
declared Roast tests, reading the reference *compiler* at the design level
risks nothing the clean-room protected — NQP is a different language over a
different VM, so porting lines was never even possible — and the correctness
oracle remains Roast. The rule going forward, now also noted in the claim
sites themselves: **read designs, never port code.** (The stance had one
prior, library-level exception on record:
[LIBFFI-PLAN.md](../../plans/LIBFFI-PLAN.md) answered a NativeCall semantics
question from `lib/NativeCall.rakumod` — Raku code, not engine code.)

Sources, fetched 2026-08-22 from the rakudo repository:
`src/Perl6/Optimizer.nqp` (the static optimizer) and `docs/dispatchers.md`
(the Raku-level dispatcher inventory). The setting-structure and RakuAST
material is partly from general knowledge of the project.

What Rakudo uniquely offers this series: it is the only *compiler for this
exact language* whose static-optimization decisions can be read — every
transformation in its optimizer is one the language's semantics provably
permit, pre-litigated by the people who defined them. That makes its
optimizer catalog something none of the other eight engines could give us: a
list of Raku-legal shortcuts.

## 1. The optimizer catalog: eighteen Raku-legal transformations

`Perl6::Optimizer` runs over QAST at compile time (level 2 by default).
Sorted against what this engine already does:

**Already ours, statically** — constant folding; single-candidate operator
devirtualization (our TARG lanes and `-O` pass 1's direct-arity calls plus
37 devirtualized builtins, [ch/27](../../../book/ch/27-optimizer.md));
range-loop lowering when bounds are compile-time integers (the loop lanes);
`%_` auto-slurpy elimination when unused (our `usesArgs` flag); compile-time
typed complaints (our [Lint](../../../../src/Lint.cpp) is the analog of
their worry/sorry machinery).

**New levers, ranked by expected payoff here:**

- **Unused magical elimination.** Rakudo skips setting up `$_`, `$/`, `$!`,
  `$¢` in blocks with zero usages of them (blocked only when the block
  makes calls that could reach them contextually). Our pads work met this
  cost dynamically — the per-iteration topic re-insert that flat loops
  removed — but every routine frame still provisions the magicals whether
  or not the body names them. The static version is a parse-time bit per
  body ("names `$/`: no") and a frame setup that honors it. Cheap to
  detect, pays on every call of every small sub — the profile shape the
  `subcall` kernel already watches.
- **Immediate block flattening.** Zero-arity bare blocks (`if`/`for`/bare
  `{ }` bodies) are inlined into the enclosing frame when their lexicals
  lowered and no exit handlers exist — no Block invocation at all. Our
  statically-flat loop bodies (PERL5 item 2's second half) did exactly
  this for loops; Rakudo's version generalizes it to every immediate
  block, with the disqualifier list (parameters, handlers, capture)
  already worked out. Natural next slice on the pads/flat-scope lane.
- **Smartmatch reductions.** `~~ Type` becomes `istype`; literal `~~`
  literal folds at compile time; `~~ 5` / `~~ "x"` become `==` / `eq`;
  `Pair ~~ Pair` compares directly. Our `~~` presently re-derives the
  match kind per evaluation; the AST node can classify RHS shape once at
  parse (type object / literal / regex / code) and pick the lane the way
  TARG classified assignments.
- **Junction short-circuiting.** `$x == 1 | 2` in boolean context becomes
  `$x == 1 || $x == 2` when the operator is a chain op that cannot be
  overridden for `Any`. We autothread junctions generically; the folded
  form skips junction construction entirely on the hot literal cases.
- **Lexical-to-local lowering, with its blocking list.** Their strongest
  pass: a lexical with no nested-block references, no `lexicalref` use and
  no dynamic marker stops being a pad entry at all and becomes a VM
  local. The transformation itself is their shape (we have no "below the
  pad" tier — yet; TARG result slots are the embryo). The transferable
  part today is the **blocking-condition list**, which is precisely the
  escape analysis three other docs keep circling: V8's preparser tracks
  it while skipping ([V8-LAZY-PARSING.md](V8-LAZY-PARSING.md) item 3),
  Lua's compiler derives upvalue lists from it
  ([LUA-TECHNIQUES.md](LUA-TECHNIQUES.md) item 3), and Rakudo lowers only
  when it proves absence. The container/closure refactor's core static
  analysis now has three independent specifications to check itself
  against — one of them for this exact language.
- **Sink-context analysis** — statically marking which expressions are in
  sink context (enabling elision and honest warnings). How much our
  interpreter re-derives sink dynamically is not yet measured; flagged
  for a look rather than claimed as a lever.

## 2. The dispatcher inventory: a priced list of semantic sites

`docs/dispatchers.md` enumerates the Raku-level dispatchers new-disp runs
on. The instructive part is not the mechanism (the MoarVM doc covers it)
but the *membership*: alongside `raku-meth-call` and `raku-call` sit
`raku-assign`, `raku-rv-decont`, `raku-boolify`, `raku-sink`,
`raku-rv-typecheck`, `raku-coercion`, `raku-capture-lex` — in the reference
implementation, **assignment, decontainerization, boolification, sinking,
return-typechecking and coercion are each a guarded dispatch** that
specialization must then flatten. That list is a checklist with prices
attached, and it maps one-to-one onto slices this project has been landing
statically: `raku-assign` ↔ the TARG simple-assign lane, `raku-boolify` ↔
conds-answer-as-bool, `raku-rv-decont` ↔ the container refactor's decont
discipline, `raku-rv-typecheck` ↔ the binder's typed-return checks. The
remaining rows are the to-do list: `raku-sink` (item 1's sink question),
`raku-coercion` (coercion types in signatures), `raku-capture-lex` (the
closure-capture redesign).

Two mechanism details worth lifting anyway: the **resumption primitives**
(`dispatcher-next-resumption`, saved resume state) document exactly what
`callsame`/`nextsame`/`lastcall` require of our wrapper/candidate machinery
— a specification to test our deferral behavior against; and the
**megamorphic fallback** (`dispatcher-index-lookup-table` appended to a
dispatch program when polymorphism exceeds the inline cache) is the
missing last stage of the unified callsite cache design (MOARVM item 3):
guard-list first, hash table after N shapes, never unbounded guard chains.

## 3. RakuAST: the reference design for metaprogramming's endgame

Rakudo's new front end (`src/Raku/ast/`) exists because the original
grammar-to-QAST pipeline had no user-exposable representation — macros,
custom slangs and `will`-style deferred compilation all stalled on it, and
retrofitting an AST behind a shipping compiler has taken years. Two
readings for us. First, the strategic one: this engine has had a real,
serializable AST from day one — the thing Rakudo is retrofitting is our
native format, which is an asset to protect (keep the AST clean enough to
expose). Second, the practical one: when
[RAKUAST-PLAN.md](../../plans/RAKUAST-PLAN.md) (metaprogramming phase 5 —
macros, slangs) becomes active, RakuAST's node vocabulary and deparse
surface are the *compatibility target* — user code will be written against
their class names and behaviors, so our exposed AST should map onto that
vocabulary rather than invent one, the same way Roast disciplines the rest
of the language.

## 4. Setting layers: the revision model, minus the startup bill

Rakudo compiles CORE.setting as nested lexical layers per language
revision — 6.c, then 6.d and 6.e as outer-to-inner overlays — so a
routine's revision determines which setting scope it resolves against.
Our analog is per-routine (`Callable.langRev` gating semantics
divergences), grown ad hoc through the 6.e work
([6E-PLAN.md](../../plans/6E-PLAN.md)). The layered-scope model is the
principled version: revision-divergent builtins live in a revision layer
consulted by resolution, not in `if (langRev >= 2)` branches scattered
through C++. Worth adopting *as an organizing principle* for the builtin
tables the day the per-revision divergences outgrow the current handful —
while noting the anti-model half stays true (their settings are also why
startup costs what it costs; ours stay C++ tables either way).

## What deliberately does not transfer

- **NQP and the bootstrap** — the meta-circular pipeline is the startup
  anti-model already recorded (MOARVM item 5); nothing to port by
  definition of the stance.
- **QAST/MAST machinery** — their IR exists to feed MoarVM; our AST *is*
  the execution format.
- **The optimizer's implementation** — NQP code walking QAST; what
  transfers is the catalog (item 1), each entry re-derived against our
  AST and gated on Roast like everything else.

## The stance, restated for the record

Built clean-room; still no ported code. What changed on 2026-08-22 is that
Rakudo joined the read-at-design-level shelf beside the other eight
engines, with its findings held to the same test: every adopted idea lands
as our own implementation, measured in
[BENCHMARKS.md](../../../status/BENCHMARKS.md) and gated on Roast. The
claim sites ([JOURNEY.md](../../JOURNEY.md),
[LONGREAD.md](../../../../LONGREAD.md),
[OVERVIEW.md](../../../guide/OVERVIEW.md)) now carry the dated note.

## Suggested order

| # | change | payoff | cost | depends on |
|---|---|---|---|---|
| 1a | unused-magical elimination (parse-time bit, honored at frame setup) | medium — every small-sub call | low | pads (landed) |
| 1b | immediate-block flattening beyond loops | medium | medium | flat-scope machinery (landed) |
| 1c | smartmatch RHS classification at parse | medium on `~~`-heavy code | low-medium | none |
| 1d | junction → short-circuit folding | small-medium | low | none |
| 2 | megamorphic lookup-table stage + resumption spec for the callsite cache | design input | — | MOARVM item 1 / PHP7 item 1 |
| 1e | lexical-lowering blocking list into the capture/container analysis | design input | — | LUA item 3, V8 item 3 |
| 3 | RakuAST vocabulary as the phase-5 compatibility target | future | — | RAKUAST-PLAN |
| 4 | revision layers for builtin divergences | future | — | when 6.e divergences multiply |
