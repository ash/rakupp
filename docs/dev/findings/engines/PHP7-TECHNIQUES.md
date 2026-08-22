# What PHP 7's engine rewrite teaches Raku++

A study of the PHP 5 → PHP 7 engine rewrite ("phpng", 2014–2015), read against
Raku++'s interpreter the way [PERL5-TECHNIQUES.md](PERL5-TECHNIQUES.md) read
perl. Sources: Nikita Popov's internals write-ups — *Internal value
representation in PHP 7* parts 1–2 (npopov.com, 2015), *PHP's new hashtable
implementation* (2014), *The PHP 7 virtual machine* (2017) — read 2026-08-22.
The `sizeof` numbers for our side were measured the same day on this machine
(clang/libc++, arm64).

Why PHP is worth a study of its own: it shares perl's cost model —
copy-on-assign value semantics, refcounting, hashes as the workhorse — but
unlike perl, whose constants were shaved over thirty years, PHP rewrote its
core structures **in one deliberate pass** and documented what moved the
needle. The team's reported result was roughly 2× throughput on real
applications (WordPress was the metric) with memory cut by more than half —
**with no JIT**; the JIT arrived five years later in PHP 8 and mattered far
less for their workloads than the layout work. phpng is, in effect, a
completed run of the items still open in PERL5-TECHNIQUES.md, plus answers in
three areas where perl has nothing to say: objects, references, and
per-callsite caches.

The honest frame, as before: PHP's life is easier than ours in ways that
inflate its numbers. The engine is single-threaded per request (plain
refcounts, no atomics), the allocator can bulk-free at request end, values
have no methods, and there are no containers, no laziness, no gradual typing,
no NFG, no numeric tower. The transferable part is the layout decisions and
the *observed order* of what mattered — not the multiplier.

Items ranked by what this study adds for us, most actionable first. Where an
item is really a design input to work the perl study already named (the
head/body endgame, container/binding), it says so rather than pretending to be
new.

## 1. Per-callsite run-time caches for dispatch

PHP 7 op arrays are immutable and shared, so caches live beside them: every
function carries a `run_time_cache`, an array of pointer slots, and oplines
carry indices into it. `INIT_FCALL` caches the resolved function pointer the
first time and reads it thereafter. Property and method ops use a
**polymorphic two-slot cache**: slot 0 holds the class entry last seen, slot 1
the resolved datum (property offset, method pointer); if the next access is on
the same class — which it almost always is — the lookup is one pointer compare.

This is the one mechanism in this study that neither perl nor our current
design has systematically, and it lands exactly on our named cost.
`methodCallInner` re-resolves every method call through the dispatch chain;
MName (MethodName.h) made each name comparison cheap, but the *chain* still
runs per call, and the object-construction profile puts dispatch in the top
two remaining costs (the interning story,
[../../book/ch/10-interning.md](../../../book/ch/10-interning.md)).

**Lever:** a per-callsite cache on the method-call AST node — `{ClassInfo*
seen, resolved target}` — checked before the chain, filled after it, exactly
the shape of the `DecidedOnce`/`PublishedOnce` annotations `Callable` already
carries (Value.h:236). PHP's placement lesson applies: their caches live
*outside* the shared op array because the op array must stay immutable; our
AST is likewise shared (module cache, `.assuming`, parallel threads), so the
cache must be publication-safe — the `PublishedOnce` pattern, or an
interpreter-owned side table indexed by node, which is literally PHP's
layout. Misses (mixins, `augment`, a second class at the same site) fall
through to the chain and can either re-fill (monomorphic, PHP-style) or
disable the slot. `augment`/`.wrap` need an invalidation story — a per-class
serial bumped on mutation, checked with the cache — which Ruby's global→
per-class method-cache history argues for getting right on day one.

## 2. Objects: attribute slots assigned at class-composition time

PHP 5 stored every object property in a hashtable; a minimal object with one
property cost 136 bytes across four allocations, and a property read was four
pointer dereferences. PHP 7's `zend_object` embeds a `properties_table[]`
directly in the object allocation: **each declared property gets a
compile-time slot index from its class**, a read is `obj->properties_table[n]`
— 40 bytes base + 16 per declared property, one indirection. The hashtable
still exists but only materializes for *dynamic* (undeclared) properties, and
the `__get` guards table is created lazily in slot 0.

Us today (`ObjectData`, Value.h:942): `sizeof(ObjectData)` is **256 bytes**
before a single attribute, and every attribute lives in a `ValueMap` — so each
one costs a ~168-byte entry (24-byte `std::string` key + 128-byte Value + hash
+ flags) reached through a string-hash lookup, on every `$!x` read. Two
separate levers here:

- **The cheap one:** `ObjectData` embeds a full 128-byte `boxed` Value that is
  only meaningful for `5 but Role`-style mixins (`hasBoxed` is false on
  essentially every real object), plus a 16-byte monitor-lock pointer used
  only by `monitor` classes. Both belong behind one rare-case pointer — the
  same census-justified move as `ValueExt`. That alone takes the base object
  from 256 toward ~120 bytes.
- **The real one:** assign each declared attribute a slot index when the class
  is composed (roles flattened, inheritance walked — the walk already exists
  for attribute collection), store attributes in a `std::vector<Value>`
  indexed by slot, and keep name→index on `ClassInfo` for the late paths.
  This is the pads lesson (PERL5 item 2) applied to `self`, which is exactly
  what PHP did. Runtime additions (`does` on an instance) get PHP's answer:
  a spillover map that only materializes when someone actually mixes in.
  Grammars are the workload that would feel it most after plain OO code —
  Match objects and actions are attribute-heavy.

Rakudo does the same thing (P6opaque assigns attribute offsets at composition
time), so this is doubly confirmed — it is not a PHP-ism, it is what every
engine that got objects fast did.

## 3. The 16-byte zval: the head/body endgame, run to completion

The numbers first. A PHP 5 zval was a 24-byte struct, heap-allocated per
living value (~48 bytes with allocator overhead, 32 with the GC wrapper),
refcounted **even for integers**, up to four dereferences deep. PHP 7's zval
is **16 bytes, never individually allocated**: an 8-byte value union (int and
double inline; everything else a pointer), a 4-byte type-info word, and a
4-byte spare (`u2`). Refcounts moved into the pointed-to bodies (a common
`zend_refcounted` header), and integers/floats/booleans are simply not
refcounted at all — assignment is a 16-byte copy, full stop.

Us: `sizeof(Value)` is **128** after the 2026-08 batches (344 → 208 → 128;
REPRESENTATION-PLAN batches 2 and 4). What the remaining 128 hold: `i`+`n`
both inline (16), the 40-byte `CowStr`, `hashKind` (8), a 16-byte flag/tag
region, the payload and cold-block shared_ptrs (32), `enumName`/`enumType`
(16). PHP's endgame says the head wants to be *one payload word plus one
tag/flags word*, with the string behind the pointer like everything else.
That is PERL5 item 1's endgame, already coupled to the container/binding
refactor; what PHP adds to its design are three specifics:

- **The `u2` trick:** the spare 4 bytes in the head are deliberately
  *context-dependent* — hash buckets use them as the collision-chain next
  index, oplines as cache-slot indices. A one-word head with a reusable
  scratch field is how their buckets stay at 32 bytes with no side
  allocations. Worth remembering when sizing our head: a field does not need
  a single meaning to earn its place.
- **Intrusive refcounts, not control blocks.** Our payload handles are
  `shared_ptr` — 16 bytes each, plus a control block. A `zend_refcounted`-style
  header in the body makes the handle 8 bytes and removes the separate count
  object. Two shared_ptrs → two intrusive pointers saves 16 bytes of head and
  one indirection per refcount touch. The cost is hand-rolled counting —
  reasonable to take only as part of the endgame redesign, not before.
- **Immediates unrefcounted:** already true here (an Int/Num Value carries
  null pointers) — banked, no action.

## 4. `zend_reference`: PHP already ran the container experiment

This one is about the container/binding refactor
(the roadmap's #2), and it is the closest thing to a production A/B of the
design question in front of us.

PHP 5 marked references with an `is_ref` flag *on the value*, which fused two
orthogonal things — "is shared for COW" and "is a reference" — into one
refcount. The consequence was "separation": a value could not be shared
between a reference user and a plain user, so the engine defensively copied
arrays at boundaries, which is where a decade of "adding `&` makes PHP
slower" folklore came from. PHP 7 made the reference an **explicit heap
object**: `zend_reference` = refcounted header + one embedded zval; every
variable in the reference set points to it; `IS_REFERENCE` is a first-class
type tag, and defined ops deref it at defined points. Result: plain values
stopped paying anything for the existence of references, sharing works across
ref/non-ref users, and a whole class of engine invariants collapsed into one
rule.

The mapping writes itself: `zend_reference` *is* a Scalar container — a
refcounted cell holding one value, pointed to by slots, dereferenced at
operation boundaries. Raku's wrinkle is that semantically **every** `my $x`
is a container, so the naive translation (every variable slot points to a
heap cell) would tax everything — that is the PHP 5 shape, not the PHP 7 one.
The PHP 7 lesson, restated for us: **the plain-value path must carry zero
container cost, and a real cell should exist only where binding demands one**
(`is rw` params — the `rwLinks` machinery this would replace, `.VAR`, `:=`
aliasing, `is default` cells). Rakudo arrives at the same place dynamically
(spesh deoptimizes containers away); an AST interpreter can arrive at it
statically, since the parser already knows which slots are ever bound or
passed rw — the `PadLayout.simple` classification (Interpreter.h:239) is the
seed of exactly that analysis.

## 5. The hashtable: convergent evolution, two deltas left

PHP 7's HashTable and our `ValueHash` (built from PERL5 item 6) are the same
architecture, arrived at independently: dense entry storage in insertion
order, a separate power-of-two index, tombstoned deletes. Theirs: 32-byte
buckets `{zval, h, key*}`, collision chain through the zval's `u2` (zero
extra bytes), index co-allocated with the buckets, compaction when the used
count hits table size. Their measured result: 144 → 36 bytes per element
(13.97 MiB → 4.00 MiB for 100k integer keys). Ours already delivered its
step-change (hashfill −19…−24%, BENCHMARKS "vs Perl 5"). The remaining deltas:

- **Entry weight.** Our entry is ~168 bytes: a 128-byte Value and a 24-byte
  inline `std::string` key. Theirs is 32: a 16-byte zval and a *pointer* to a
  shared, interned, hash-caching `zend_string`. Both deltas are item 3 — when
  the head/body split lands, the Value shrinks and the key becomes a pointer
  to a refcounted string body. No standalone action.
- **The probe key's hash is recomputed per lookup** (`hashKey`,
  ValueHash.h:65). PHP hashes a string once *per string value* — the hash
  rides in `zend_string.h` — so a key used across many operations never
  rehashes. Our `StrBody` already banks exactly this kind of property
  (`allAscii`, `nGraphemes`); a cached hash is the missing sibling, but today
  it would only reach promoted (≥64-byte) strings, and real keys are short.
  Honest sequencing: becomes worth doing the day string bodies exist for all
  lengths — fold into item 3, note it in the endgame design.
- **Tombstones:** PHP compacts; we never do, because `Value&` stability
  across inserts is a contract (`rtIndexRef`, autoviv lvalues). Recorded as a
  known, paid-for divergence — not a bug to fix.
- **Packed arrays — nothing to copy.** PHP's one array type serves both list
  and dict duty, so "packed" (ascending int keys → drop the index, subscript
  `arData` directly) was a major win *for them*. Raku separates Array from
  Hash at the language level and our `ValueList` is a plain vector — we get
  packed for free, structurally. Recording this so nobody imports the merged
  design to get a feature we already have.

## 6. `SEND` writes arguments into the callee's frame

PHP 7 calls are INIT → SEND… → DO_FCALL. INIT bump-allocates the whole frame
— header plus slots for args, locals and temporaries — from 256 KiB VM-stack
pages; each SEND evaluates one argument **directly into the callee frame's
next slot**; DO_FCALL just starts execution. There is no argument list as a
separate object anywhere: the args land in what already *is* the callee's
variable area (perl's item 7, but stronger — perl still pushes onto a shared
stack and the callee moves things around).

Us: every call builds a `ValueList` (heap vector), then the binder moves
values into the frame pad. The `-O` codegen lane already proved the shape
natively — fixed-arity positional subs get direct `Value` parameters,
skipping the ValueList (Codegen.h) — and the interpreter now has the two
prerequisites it lacked when PERL5 item 7 was written: real pads with slot
indices, and the TARG-era arity classification (`arityShape`, `hadSig`) that
identifies the simple-signature lane statically. **Lever:** for that lane,
evaluate arguments straight into the callee frame's pad slots and skip the
ValueList and the binder's general path. The blockers are the honest ones
from item 7 — aliasing (`is rw`, slurpies, nameds fall back to the general
path) — but as a *lane* rather than a rewrite, this is now medium rather
than large. Design it with item 4's container analysis; they touch the same
binder.

## 7. Specialized handlers and smart branch: confirmation, plus a scaling lesson

PHP generates its opcode handlers (`zend_vm_gen.php`) specialized two ways:
by operand *kind* (CONST/TMPVAR/CV — compile-time), and by runtime *type*
guards on the hottest ops (int+int add straight-lines; the general handler is
the fallback). Comparisons implement **smart branch**: `IS_EQUAL` checks
whether the next op is a conditional jump and performs the jump itself,
eliminating the materialized boolean and the second dispatch.

We are already on this road, hand-rolled: the TARG lanes, the simple-assign
lane, natives staying in-lane, and "conds answer as bool" (commit 3bef8e9)
*is* smart branch for an AST walker. What PHP's experience adds is about
scale, not mechanism: they specialize **only the top ~25 opcodes** — the tail
never pays for its variants — and when the variant matrix outgrew hand
maintenance they moved to a generator. If our lane count keeps growing,
the same fork appears: cap the set by profile, and consider generating the
lane bodies from templates before they diverge by hand. No action now;
a marker for when the Interpreter's lane code next feels repetitive.

## 8. Immutable values skip refcounting entirely

Every PHP source literal becomes an **interned** string: deduplicated,
process-lifetime, flagged non-refcounted — copying one is a pointer copy with
no count traffic, and opcache keeps them in shared memory across workers.
Constant arrays get the same immutable flag. (CPython arrived at the same
place in 2023 as "immortal objects".)

Us: `IStr` already interns the closed-vocabulary tags, and short literals are
cheap inline copies. The interesting part is what this does to *atomics*: our
refcounts are `shared_ptr` atomics because parallelism is load-bearing here
(PARALLEL-PLAN, the TSan legs), so every copy of a shared body pays a locked
op — a cost PHP never had (plain ints, single-threaded requests). An
immortal-bit check (`refcount == sentinel` → skip the atomic entirely) is the
version of this technique that survives our threading model, and it applies
to string-literal bodies, type objects, and the interned tables. Only
becomes implementable with intrusive refcounts — park it inside item 3's
design, where it makes the atomics question part of the head/body endgame
rather than an afterthought.

## What deliberately does not transfer

- **The request-scoped allocator.** Zend MM's big wins lean on bulk-free at
  request end; our processes are long-running, and malloc is already ~9% of
  the worst kernel (PERL5 doc, item 8 note). Arena thinking stays
  opportunistic.
- **Plain (non-atomic) refcounts.** PHP's single-threaded-per-request model
  licenses them; our parallelism is real and tested. The salvageable part is
  the immortal bit (item 8), not the plain counters.
- **One array type for list+dict.** Their packed/hash duality solves a
  problem our type system doesn't have (item 5).
- **Opcache shared memory.** Their deployment shape (many identical workers),
  not ours. The nearest analog we'd ever want — a compiled-module cache over
  `AstSerial` — is a startup story, separate from this study.

## The process, which was the actual question

The phpng history is worth as much as the techniques. It was a three-person
experiment branch (Dmitry Stogov, Xinchen Hui, Nikita Popov), measured from
day one on a **real application** (WordPress requests/second), not
microbenchmarks, and merged by RFC only after the numbers were undeniable.
The order of work is the striking part: they rewrote the two core data
structures first — zval, then HashTable, then objects — and only then tuned
dispatch (specialized handlers, smart branch, caches). Their retrospective
consistently credits the *layout* work, not the dispatch work, with the bulk
of the 2×; and the JIT, added five years later, moved typical web workloads
comparatively little. All of it shipped with full language compatibility,
gated on the language's own test suite and real apps.

Two things carry over directly. First, the order — representation before
dispatch — is the order our own profiles keep choosing (the 344→128 Value and
ValueHash landed before the TARG/dispatch slice, each unlocking the next),
and PHP is evidence that the sequence holds all the way to the end of the
road. Second, the discipline is one we already run — Roast as the
compatibility gate, BENCHMARKS.md A/B on the same machine, the pointer census
before layout changes — so the meta-lesson is mostly confirmation: the
census-measure-gate loop is how a rewrite of every core structure ships
without breaking the language.

## Suggested order

Meshed with PERL5-TECHNIQUES.md's remaining table (its items 1/3/7 stay the
frame; TARG/item 4 landed 2026-08-22, after that table was last re-ranked):

| # | change | payoff | cost | depends on |
|---|---|---|---|---|
| 1 | per-callsite dispatch caches (+ per-class serial for augment/.wrap) | high — dispatch is a named top-two cost | medium | nothing; `PublishedOnce` pattern exists |
| 2a | `ObjectData` diet: `boxed`/monitor behind a rare-case pointer | small-medium, cheap RSS win | low | a census pass to confirm rarity |
| 2b | attribute slot tables per composed class | medium-high for OO and grammars | medium | pairs with 1; Rakudo-confirmed design |
| 6 | args-into-pad lane for simple signatures | medium | medium | pads + arityShape (landed); design with the binder work |
| 3, 4, 5, 8 | design inputs to the head/body + container refactor: one-word head with `u2`-style scratch, intrusive refcounts + immortal bit, key-as-string-body, container cells only where bound | largest | large | the refactor's design doc — write these in |
| 7 | specialization scaling (top-N only; generate lanes) | — | — | marker only, revisit when lanes multiply |

Read together with the perl study, the two docs now bracket the design space:
perl shows the techniques aging thirty years in place; PHP shows the same
family of techniques executed as one coherent rewrite, with before/after
numbers per structure. Where both agree — head/body values, interned
hash-cached keys, slot-indexed frames, dispatch caches — the evidence is as
strong as interpreter engineering evidence gets.
