# What CPython 3.x teaches Raku++

Third in the series after [PERL5-TECHNIQUES.md](PERL5-TECHNIQUES.md) and
[PHP7-TECHNIQUES.md](PHP7-TECHNIQUES.md). Sources: PEP 659 (specializing
adaptive interpreter), the Python 3.11 "Faster CPython" release notes, PEP 412
(key-sharing dictionaries), PEP 703 (making the GIL optional) — read
2026-08-22 — plus established facts about the 3.12–3.14 releases (immortal
objects, the copy-and-patch JIT, the tail-calling interpreter build) stated
from general knowledge, not fetched documents.

CPython earns a study for a different reason than perl and PHP did. It is
**not** a cost-model sibling: Python has reference semantics, and every value
— including every integer — is a heap `PyObject` with a refcount and a type
pointer. That base design is the one our 128-byte pass-by-value `Value`
deliberately avoided, so Python's *layout* has little to teach here. What it
uniquely offers is everything CPython built to compensate, under compatibility
constraints stricter than ours, with numbers published per change:

- the best-documented **adaptive** optimization program ever run inside a
  shipping interpreter (3.11's 1.25× average came from runtime type feedback,
  not layout),
- the canonical **cache invalidation** design (type version tags),
- a twenty-year attribute-storage migration that ends where PHP and Rakudo
  point (slots), with the fallback story worked out,
- the proof that **lazy frames** survive full introspection (their
  `sys._getframe` is our `CALLER::`),
- and the only production design for **removing a GIL from a refcounted
  interpreter** (PEP 703) — which reads as a direct commentary on
  [../plans/PLAN-gil-removal.md](../../plans/PLAN-gil-removal.md) and the
  in-flight [../plans/PARALLEL-PLAN.md](../../plans/PARALLEL-PLAN.md).

The honest frame, as always: Python's compensations exist because its base
representation is expensive, and several of its constraints (a frozen C API,
16-bit bytecode format, thirty years of debugger/tracing contracts) are drags
we do not carry. Where a technique below looks smaller than its Python payoff,
that is usually why — and occasionally the asymmetry runs our way.

## 1. Adaptive specialization: the systematic form of our hand-built lanes

PEP 659's machinery, concretely: every optimizable instruction ships as an
**adaptive** form carrying a small saturating warmup counter in its first
inline-cache slot. After enough executions it attempts to specialize —
`LOAD_ATTR` becomes `LOAD_ATTR_INSTANCE_VALUE` / `LOAD_ATTR_SLOT` /
`LOAD_ATTR_MODULE`, `BINARY_OP` becomes the int/float/str fast paths — by
**rewriting itself in place**, with the cached data (a type version, an index)
in 16-bit cache entries laid out immediately after the instruction. Every
specialized form starts with a guard; on guard failure a counter decrements,
and at zero the instruction swaps back to the adaptive form. De-optimization
is that cheap — an opcode store. Measured in 3.11: binary ops +10%, subscript
+10–25%, calls +20%, method load +10–20%, the whole suite 1.25× average
(10–60% range).

Us: the TARG lanes, the simple-assign lane, the native lanes and
"conds answer as bool" are the **static** half of this — specialization
decided from syntax and declaration shape at first execution
(`DecidedOnce`/`PublishedOnce`). What we do not have is the **adaptive** half:
specialization from *observed runtime types*, with a guard and a cheap way
back. For an AST walker the rendition is node self-rewriting (Truffle's
design; PEP 659 is the same idea for bytecode): an infix node that has seen
Int⊕Int N times replaces itself with an int-lane node that guards and falls
back. Two implementation lessons from their write-up worth stealing verbatim:

- **Counters, not booleans.** A site that fails its guard occasionally should
  not thrash between forms; saturating counters with a re-attempt period give
  megamorphic sites a cheap steady state (stay generic, stop trying).
- **Instrument before building.** The priority order of their specializations
  came from `--enable-pystats` hit/miss statistics, not intuition. Before we
  grow more lanes, a debug-flag counter per lane (attempted / hit / guard-fail)
  would tell us which AST shapes actually run hot generic — the same
  measure-first discipline the pointer census applied to layout.

Sequencing: this builds *on top of* the per-callsite caches from
[PHP7-TECHNIQUES.md](PHP7-TECHNIQUES.md) item 1 — a cache is the degenerate
(monomorphic, no counter) form. Land the caches first; grow them adaptive
where the stats say so.

## 2. Type version tags: the invalidation half we have not designed yet

Both this doc's item 1 and the PHP doc's dispatch caches share a prerequisite
this codebase currently lacks: a way to know a cached resolution went stale.
CPython's answer has run in production for two decades: `tp_version_tag`, a
32-bit serial per type, bumped whenever the type or anything in its MRO
mutates. Every cached lookup — the pre-3.11 global method cache keyed on
(version, name), and every 3.11+ inline cache — stores the tag it resolved
under and validates with one integer compare. A mutation doesn't hunt down its
dependents; stale caches discover themselves by missing.

Us today: `ClassInfo` (Value.h:835) has no serial of any kind, and
`findMethodForCall` re-walks `methods` and the parent/role chain on every
call. The moment a resolution is cached at a callsite, `augment`, `.wrap` /
`.unwrap`, runtime role mixins (`does`), and `.HOW` mutation all become
invalidation events. The Python design translates directly: a `uint32_t`
serial on `ClassInfo`, bumped by every mutating path, stored alongside every
cached target, compared on use. Inheritance needs one decision: either a
cache stores the serial of *every* class on its resolution path, or a base
mutation walks known subclasses bumping theirs — CPython does the latter
lazily (invalidate on next check). Cheap either way, thread-friendly
(monotonic store, acquire read), and the kind of thing that is much easier to
thread through `invokeMethodChain` *before* the caches exist than after.

## 3. The attribute-storage journey: shared keys → inline values → slots

Python spent twelve years walking instances from "one dict per object" to
"values inline in the object", shipping each step:

- **PEP 412 (3.3), key-sharing dicts:** instances of a class share one keys
  table cached on the type; each instance holds only a dense values array.
  An instance whose attribute set diverges falls back to an ordinary combined
  dict — measured rare. 10–20% memory reduction on OO programs; ~10% speedup
  on the object-churn benchmark.
- **3.11, inline values:** the values array moved into the object allocation
  itself, and `__dict__` became a *lazily materialized view* — most instances
  never create one, but `obj.__dict__` still answers, built on demand over
  the inline storage.

The endpoint is the same compile-time-slots design as PHP 7's
`properties_table` and Rakudo's P6opaque — PHP7-TECHNIQUES item 2 already
carries it for us. What the Python journey adds to that item's design:

- **The fallback is load-bearing, and it works.** Their split→combined
  demotion is exactly the spillover map our item sketched for runtime `does`
  on an instance; Python's data says the demotion path is cold enough that
  nobody notices it. Ship slots for composed attributes, demote the odd
  mutant, stop worrying.
- **Introspection wants a view, not a store.** `.^attributes` /
  `.raku`-style walkers can be served by a materialized-on-demand view over
  slots, the way `__dict__` is — the MOP surface does not force the storage.
- **A hash column can retire.** 3.11 dicts stopped storing per-entry hashes
  when every key is a unicode string, because `str` caches its own hash —
  entry 352 → 272 bytes for a 7-key dict. Our `ValueHash` entry carries an
  8-byte `h` precisely because our keys don't cache theirs. When the
  head/body endgame gives keys a hash-caching string body (PHP7 item 5), the
  column becomes redundant — one more 8-byte return on that refactor, noted
  here so the endgame design remembers it.

## 4. Frames: lazy materialization survives full introspection

The 3.11 frame work, in numbers: internal frames became minimal C structs
allocated from contiguous chunks (no per-call malloc), the heavyweight
`PyFrameObject` is created **only when a debugger or `sys._getframe()` asks**
— most calls never make one — worth 3–7% across the suite; Python-to-Python
calls were inlined into the interpreter loop so they consume no C stack
(recursive fib 1.7× faster, recursion depth freed from the C stack); and
exceptions became **zero-cost**: a static exception table consulted on raise
replaced per-`try` block-stack pushes, so entering a `try` costs nothing.

The transferable part is the *proof*, because the obvious objection here is
"but Raku has `CALLER::`, `OUTER::`, `Backtrace`, `temp`" — and Python's
introspection surface is just as demanding, and they made frames lazy anyway.
The mechanism: keep the common activation record minimal and pool-allocated,
and materialize the introspectable object on demand, keeping it coherent with
the minimal frame. Us: a call allocates a pooled `shared_ptr<Env>` whose
fast half is already the pad; the map (`vars`) and `EnvExtras` exist for the
slow paths. `EnvExtras` is *already* this pattern at field granularity —
lazily allocated, shared-empty for readers (Interpreter.h:205). The endgame
extends it one level: an activation is pad + parent, and the name-keyed view
materializes only for `EVAL`, `CALLER::`, dynamic lookup and friends. That is
the same direction PERL5 item 2's tail points; Python is the evidence the
introspection bill still gets paid correctly.

Two smaller alignments, recorded: our CATCH/phaser handling already scans
once per body and pays on throw (`catchScan`/`phaserScan`, Value.h:247) —
that is the zero-cost-exceptions shape, no action. And our tree-walk recurses
the C stack per Raku call, which their call-inlining removed for bytecode;
for us that folds into the threaded-loop design doc (PERL5 item 3), where it
belongs.

## 5. PEP 703 read against PLAN-gil-removal

This is the item the study is worth doing for. PLAN-gil-removal chose
Option 2 — "harden the runtime, not every user structure" — and
PARALLEL-PLAN is executing it (P0/P1 landed, TSan CI, the stress matrix with
a known-bad ratchet). PEP 703 **is** Option 2, shipped: the same decision,
with five mechanisms and an overhead table. Reading them side by side:

- **Biased reference counting** — each object carries an owning thread id, a
  non-atomic local count for the owner, and an atomic shared count for
  everyone else; the owner's incref/decref (the overwhelming majority) stays
  plain. Here the asymmetry runs **our way**: Python adopted BRC to avoid
  *adding* atomic cost when the GIL stopped serializing; we already pay
  atomic `shared_ptr` counts on every copy, GIL or no GIL. For us BRC is not
  a tax to accept but a **claw-back** — and it only becomes implementable
  with intrusive refcounts, so it lands in the same head/body endgame bucket
  as PHP7 items 3/8. Write it into that design as the third refcount
  requirement: intrusive, immortal-capable, owner-biased.
- **Immortalization** — local count saturated (`UINT32_MAX`) makes
  incref/decref no-ops on `None`, small ints, interned strings. Third
  independent appearance of this technique in the series (PHP's interned
  flags, PEP 683, now nogil, where it also kills *contention*). For a
  parallel-by-default rakupp, the immortal bit on type objects, literal
  bodies and the interner is worth more than it was worth to either
  single-threaded engine.
- **Per-object locks with critical sections** — a mutex per container,
  acquired around mutation, *released whenever the thread would block* (the
  critical-section discipline that prevents nested-lock deadlocks), with
  address-ordered acquisition for two-container ops. This is a concrete,
  proven middle ground for PARALLEL-PLAN's container phase — simpler than
  lock-free structures, honest about semantics. Their spec's own words:
  per-object locking provides "weaker protections than the GIL" and cannot
  make concurrent operations atomic — the same line PARALLEL-PLAN's
  known-bad ratchet draws (and our `ub-hash-write` stress datum, where
  Rakudo itself dies, says the reference sets no higher bar).
- **Optimistic lock-free reads** — dict/list lookups try a conditional
  incref without the lock and validate after, falling back to the locked
  path; freed pages are kept type-stable briefly (sequence counters gate
  mimalloc page reuse) so the optimistic probe never dereferences garbage.
  If ValueHash/ValueList reads ever show up in parallel profiles, this is
  the pattern — note it needs allocator cooperation, which is exactly why
  they swapped in mimalloc.
- **Stop-the-world, two-pause cycle GC; specialize once, not per-thread** —
  their specialization state is written by one thread only, which is our
  `PublishedOnce` rule already; alignment, no action.

The price they accepted: 5–6% single-threaded overhead (their Skylake/Zen 3
table) in the no-GIL build. Our equivalent budget question — how much the
GIL-mode interpreter may slow down to make the parallel mode default — is
PARALLEL-PLAN's gate to set, but the shape of their answer (single-digit,
bought back over releases) is a usable benchmark.

## 6. Startup: freeze what boot always parses

3.11 froze the core startup modules — statically allocated code objects
compiled into the binary, skipping read-unmarshal-allocate at boot — for
10–15% faster startup. Us: startup is already the headline (2–3 ms), but the
technique is the answer to a future we are steering toward: the more of the
prelude/CORE that gets written in Raku rather than C++, the more boot-time
parsing appears — and the fix is to embed the `AstSerial` form of the prelude
in the binary at build time, Rakudo's CORE.setting precompilation done
AOT-and-embedded. A marker for MODULES/ABI planning, not work for now.

## 7. Reference cycles: the leak we have chosen, and its eventual bill

CPython pairs refcounting with a cycle collector over container objects,
because pure RC leaks cycles — and its nogil variant needed the two-pause
stop-the-world design to keep collecting safely. Us: `shared_ptr` cycles
leak, and today we break the known ones by hand (`breakSelfClosures` for the
closure-captures-its-own-frame edge, with `noCycleBreak_` suspensions where
the env must outlive the frame — Interpreter.h:953). That is a defensible
CLI-scale position (perl leaks cycles without `weaken`; PHP only added its
collector in 5.3), recorded here as *chosen*, not overlooked. The trigger to
revisit is long-running processes — the Rakus server is the canary — and the
eventual shape is Python's: a mark pass over the container graph (our `PK`
tags make "is a container" a one-byte test) triggered by allocation growth,
stop-the-world first, clever later.

## What deliberately does not transfer

- **The object model itself.** Everything-boxed, refcount-per-integer is the
  base cost Python compensates for; our value representation already made
  the other choice. None of items 1–5 depend on their layout.
- **Value-semantics lessons.** Python has none to give — assignment binds,
  never copies. COW, separation, value-copy costs: that is the perl/PHP lane.
- **The C-API and bytecode-format compat drags.** Their 16-bit instruction
  format, frozen ABI and debugger contracts shaped (and slowed) every design
  above; our ABI surface is our own (ABI-PLAN.md). Where their write-ups say
  "we could not do X", we usually can.
- **The copy-and-patch JIT (3.13, off by default; modest gains so far)** — a
  data point, not a technique to import: together with PHP 8's JIT it makes
  two independent engines whose JITs, bolted on after the interpreter work,
  moved real workloads far less than the layout-and-specialization years did.
  That is direct support for this project's strategy (interpreter constants
  plus native transpile, no JIT) — and equally a marker that the 3.14
  **tail-calling interpreter** (clang's `preserve_none`, reported single-digit
  dispatch wins) is the modern dispatch option to evaluate if PERL5 item 3's
  threaded-loop design doc ever gets written.

## The process, third data point

phpng was a big-bang rewrite by three people, gated on real apps. Faster
CPython is the opposite pole: a standing funded team, a public multi-year
plan (specialize → micro-ops → JIT), a public ideas repo, per-release
compounding gains (1.25× in 3.11, smaller since), everything gated on
pyperformance and full compatibility. Both shipped. The common factors —
measure first, gate on the language's own suite, data layout and dispatch
before compilation machinery — are the same two we keep re-learning; the
team-structure difference mostly set the *granularity* of the steps, not
their order. This project is running the CPython cadence (incremental,
benchmark-gated, plan docs per campaign) with phpng's layout-first
sequencing, which the evidence from both says is the right hybrid.

## Suggested order

Meshed with the two earlier docs' tables (PERL5 items 1/3/7 and PHP7 items
1–3 remain the frame):

| # | change | payoff | cost | depends on |
|---|---|---|---|---|
| 2 | `ClassInfo` version serial (bump on augment/.wrap/does/HOW) | enabling — invalidation for every cache | low | none; do **before** PHP7 item 1's caches |
| 1a | per-lane hit/miss counters behind a debug flag | enabling — decides where adaptive pays | low | none |
| 1b | adaptive node-rewriting on the shapes the stats pick | medium-high | medium | PHP7 item 1 caches + 1a |
| 3 | slots design notes (fallback demotion, introspection view, retiring `h`) | — folds into PHP7 item 2 / head-body endgame | — | write into those designs |
| 4 | Env endgame: pad-only activation, name view on demand | medium | large | with the container/binding refactor |
| 5 | PEP 703 mechanisms into PARALLEL-PLAN (critical sections now; BRC + immortal bit into the intrusive-refcount design) | high for v3.0.0 | varies | container phase; head/body endgame |
| 6, 7 | markers (frozen prelude; cycle collector) | future | — | revisit at Rakus-scale / prelude-in-Raku |

Series read as a whole: perl taught the constants (pads, TARG, stored
hashes), PHP taught the layout endgame (small heads, slots, cells,
per-site caches), and Python teaches the *control* layer — feedback,
invalidation, laziness under introspection, and how to take the GIL out of a
refcounted engine. The three barely overlap, which is the strongest sign the
reading program is pulling from the right shelf.
