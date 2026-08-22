# What Ruby (YARV, shapes, YJIT) teaches Raku++

Fourth in the series after [PERL5-](PERL5-TECHNIQUES.md),
[PHP7-](PHP7-TECHNIQUES.md) and [PYTHON3-TECHNIQUES.md](PYTHON3-TECHNIQUES.md).
Sources, read 2026-08-22: the Object Shapes feature proposal
(bugs.ruby-lang.org #18776, Issroff/Patterson), Shopify's YJIT announcement
and the Ruby 3.3 YJIT retrospective (railsatscale.com), and the lazy basic
block versioning paper (Chevalier-Boisvert & Feeley, arXiv:1411.0352). The
method-cache history, MJIT story and Ractors are stated from general
knowledge — the primary posts were not fetchable at time of writing.

Ruby is the closest *language* sibling in the series: open classes (`augment`
with the safety off), blocks everywhere, everything-is-an-object, runtime
mixins, `method_missing` where we have `FALLBACK`. Its cost centers are
therefore our cost centers — attribute access, method dispatch under
mutation, block-heavy call patterns. Like Python it has reference semantics,
so its value layout teaches little; what it uniquely offers is a shipped
answer to three questions the earlier docs left open: what shape a *global*
attribute-cache key should have, how cache invalidation goes wrong at scale,
and what the approachable retrofit JIT looks like.

## 1. Object shapes: the cache key that works across classes

The design (from the feature ticket): a **shape** is a node in a global tree
recording which instance variables an object has set, in what order, plus its
frozen state. Setting `@a` then `@b` walks two transition edges; every object
that took the same path — *regardless of its class* — carries the same shape
ID. An ivar read becomes: compare cached shape ID, load at cached index. The
scheme it replaced needed a class-serial check, a frozen check and an
embedded/extended flag check per access — "one comparison and one memory
read versus three comparisons and four reads" is the ticket's own YJIT
summary. Microbenchmarks moved 1.02–2.15×; Railsbench was ~neutral in the
interpreter but YJIT's guard code shrank across the board. Practical scale
data: shape IDs got 32 bits and a GC after Shopify's monolith hit ~40k
shapes against the original 16-bit/65k budget; frozen-ness was folded into
the tree as a terminal transition rather than a parallel flag.

What this adds to the slots plan (PHP7 item 2, confirmed again by Python's
journey): per-class slot tables make *storage* fast, but the **cache key**
does not have to be the class. Raku makes the cross-class case real in a way
even Ruby's is not: roles. Every class that composes `role Point { has $.x;
has $.y }` lays out `$!x`,`$!y` identically, and a shape-keyed cache hits
across all of them where a `ClassInfo*`-keyed cache goes polymorphic. The
honest sequencing: per-class slots + ClassInfo-keyed caches (already planned)
are the 90% answer for an interpreter; a global shape space earns its
complexity only when profiles show role-typed callsites going megamorphic —
note it as the upgrade path, adopt the two operational lessons now (budget
the ID space; fold frozen/readonly-like state into the key rather than
alongside it).

## 2. The method-cache invalidation journey: a cautionary tale, then the fix

Ruby ran for years on a **global** method-cache serial: any `def`, `extend`,
`include` or singleton-class touch anywhere invalidated every method cache in
the process. James Golick's classic write-up documented Rails apps living in
permanent cache-miss because some gem defined methods at runtime. Ruby 3.0
removed the global serial in favor of per-class(-subtree) invalidation, and
later releases kept narrowing the blast radius.

For the `ClassInfo` version serial planned in PYTHON3 item 2, this is the
"how it goes wrong" datum: the serial must be **per class and propagate down
the inheritance/role graph only**, never a process-wide epoch — and the
mutation paths that bump it (`augment`, `.wrap`/`.unwrap`, instance `does`,
`.HOW` edits, EVAL-time declarations) should be audited for "does user code
hit this in steady state" the way Ruby eventually audited `extend`. Raku
culture mutates classes far less than Ruby's, which makes the
watchpoint-style variant (JSC doc, item 2 there) even more attractive:
mutation is rare enough to pay the invalidation walk instead of a per-call
compare.

## 3. YJIT and lazy basic block versioning: type feedback without counters

The BBV idea (from the paper): compile **basic-block versions** specialized
to the type context in which they are reached — and discover that context
not by profiling counters but by *lazy compilation itself*. A branch is
initially a stub; when execution first reaches it, the compiler is invoked
with the types that are actually in hand, generates the version for exactly
that context, and patches the branch. Type knowledge propagates block to
block; redundant checks disappear (71% of type tests eliminated, up to 50%
speedup in the research VM, outperforming a tracing JIT on several
benchmarks). YJIT is this design grown up inside CRuby: near-instant warmup
(close to peak after a single iteration), side exits back to the
interpreter, and by 3.3: liquid rendering 2.5× the interpreter, Railsbench
+65%, optcarrot 3.3× with 99.1% of instructions in JIT code, production
memory overhead held under 8%, plus a "cold threshold" that stopped
compiling rarely-run code (−20% generated code).

Two readings for us. The distant one: if rakupp ever grows a JIT, BBV is the
approachable architecture — no IR, no profiling infrastructure, block-level
granularity, written *against* an existing interpreter — and YJIT is the
proof it retrofits into a large messy C runtime. The near one is more
interesting: BBV's "specialize on first observation, no counters" and PEP
659's "counters, then specialize" are the two policies for the adaptive-node
work (PYTHON3 item 1), and **this codebase already leans BBV**: every
`DecidedOnce`/`PublishedOnce` annotation decides on first execution and
never revisits. The synthesis the two docs point at: first-observation
specialization for properties that cannot regress (arity shape, catch scan),
counter-based with cheap fallback for properties that can (observed operand
types) — and YJIT's cold-threshold lesson on top: don't specialize what
barely runs.

## 4. MJIT: the failed sibling that locates our `--exe` correctly

Ruby's first JIT (2.6–3.2) wrote each hot method out as C source and spawned
the system compiler at runtime. It won on numeric benchmarks (optcarrot) and
never on Rails: compile latency, process spawning, icache pressure from
per-method .so loading. It was demoted and removed once YJIT matured. The
lesson is a boundary, not a refutation: **compile-via-C-compiler cannot be a
runtime tier** — but as an ahead-of-time, whole-program step it is exactly
rakupp's `--exe`, which pays the C++ compiler once, offline, and ships a
static binary. MJIT failing where `--exe` works is evidence the
interpreter + AOT-transpile pairing sits on the right side of that line, and
a reason not to drift toward "JIT individual hot subs through the C++
compiler at runtime" if it ever gets tempting.

## 5. Ractors: the road we (and PEP 703) did not take

Ruby 3.0's parallelism answer is share-nothing actors: objects are confined
to one Ractor unless deeply frozen/shareable, each Ractor has its own lock,
communication is by copy or move. Five years on, adoption remains thin —
most of the gem ecosystem is not Ractor-safe, and the sharing rules fight
the language's own idioms. Meanwhile Raku's concurrency *spec* promises
shared closures across `start` blocks, shared channels, shared arrays under
`race` — the shared-memory model is not optional for us. Ruby's experience
is therefore a datum *for* the harder road already chosen
([PLAN-gil-removal.md](../../plans/PLAN-gil-removal.md) Option 2, PEP 703's
road): isolation models are simpler to build and cheaper to make safe, but
they only pay when the language's idioms cooperate, and Raku's don't.

## What deliberately does not transfer

- **The heap/GC model** — 40-byte RVALUE slots, generational + incremental
  compacting GC: reference-semantics machinery, no analog here.
- **YARV bytecode specifics** — a stack machine; the register-shaped
  material in this series comes from Lua ([LUA-TECHNIQUES.md](LUA-TECHNIQUES.md)).
- **ZJIT** (the method-based successor being developed alongside YJIT for
  Ruby 3.5) — noted as direction, too young to mine.

## The process note

Shapes shipped in the *interpreter* first (3.2), with modest interpreter
wins and neutral Railsbench — and were then exploited by the JIT, where the
shortened guard sequences compounded. Same order as every engine in this
series: semantic/layout groundwork first, compiler exploitation after, each
step gated on the language's own suite and production metrics
(yjit-bench, Shopify's fleet). The Shopify-funded standing team mirrors
Faster CPython's structure; the takeaway for a one-person project is
unchanged from the Python doc: the *order* is portable even where the
staffing is not.

## Suggested order

| # | change | payoff | cost | depends on |
|---|---|---|---|---|
| 2 | invalidation scoping rules for the ClassInfo serial (per-subtree, audited mutation paths) | correctness of the whole cache program | low | design input to PYTHON3 item 2 |
| 3 | adopt the two-policy rule for adaptive nodes (first-observation vs counted) + a cold threshold | medium | low | PYTHON3 item 1a stats first |
| 1 | shape-keyed caches for role-heavy code | medium, conditional | medium-high | only if ClassInfo-keyed caches measurably go megamorphic |
| 4, 5 | boundary notes (no runtime C-compiler tier; shared-memory stays the plan) | — | — | recorded |
