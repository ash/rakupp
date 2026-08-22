# The engine studies — series index

Nine implementations of dynamic languages, read against this codebase, one
findings doc each. The program started 2026-08-21 with a concrete question —
perl's interpreter was beating ours on `hashfill`
([BENCHMARKS.md](../../../status/BENCHMARKS.md) "vs Perl 5") — and grew into a
survey: each doc pairs an engine's primary sources (papers, internals posts,
repo docs, fetched and dated) with the measured state of our own structures,
and ranks what transfers. Applied results live inside each doc ("Applied so
far" sections) and in the plan docs they feed.

## The docs

| doc | engine | what it contributes here |
|---|---|---|
| [PERL5-TECHNIQUES.md](PERL5-TECHNIQUES.md) | Perl 5 | The constants playbook, largely **applied**: pads, TARG result slots, the stored-hash `ValueHash`, the cold-block/payload-slot `Value` shrink (344 → 128 bytes). The remaining frame: head/body endgame, args-on-one-stack, threaded loop. |
| [PHP7-TECHNIQUES.md](PHP7-TECHNIQUES.md) | PHP 7 (phpng) | The same family's playbook run as one rewrite, ~2× with no JIT. New levers: per-callsite dispatch caches, attribute slot tables; `zend_reference` as the production precedent for container cells; the 16-byte zval as endgame confirmation. |
| [PYTHON3-TECHNIQUES.md](PYTHON3-TECHNIQUES.md) | CPython 3.11–3.14 | The control layer: adaptive specialization (PEP 659), the type version serial for cache invalidation, the shared-keys→inline-values attribute journey, lazy frames under full introspection, and PEP 703 as the shipped form of PLAN-gil-removal's Option 2. |
| [RUBY-TECHNIQUES.md](RUBY-TECHNIQUES.md) | Ruby (shapes, YJIT) | Cross-class shape keys (roles make it relevant); the global-serial invalidation cautionary tale; lazy BBV as first-observation specialization (the `DecidedOnce` philosophy, named); MJIT's failure locating `--exe` correctly; Ractors as the isolation road not taken. |
| [MOARVM-TECHNIQUES.md](MOARVM-TECHNIQUES.md) | MoarVM | The same language's tax map. Interned callsites + binding plans (the most actionable item of the late series); new-disp's all-dispatch-is-one-guard-list design; spesh's guard census confirming containers > types > dispatch; NFG/strands as the conformance reference; startup as the anti-model. |
| [RAKUDO-TECHNIQUES.md](RAKUDO-TECHNIQUES.md) | Rakudo (compiler level) | The only readable catalog of *Raku-legal* static optimizations (magical elimination, block flattening, smartmatch/junction reductions, lexical lowering with its blocking list); the dispatcher inventory as a priced checklist of semantic sites; RakuAST as the metaprogramming compatibility target. Also the record of the clean-room stance's dated evolution: read designs, never port code. |
| [LUA-TECHNIQUES.md](LUA-TECHNIQUES.md) | Lua 5.0 + LuaJIT | Upvalues — per-variable capture cells, the closure half of the container refactor; the register-VM spec (RK operands, register-window calls, fused test+jump) filed for the threaded-loop design doc; the 12–16-byte TValue; LuaJIT's dispatch residue. |
| [JSC-TECHNIQUES.md](JSC-TECHNIQUES.md) | JavaScriptCore (+ Bun) | The economics of speculation with constants (p≈1, exit costs, tier spread); watchpoints completing the invalidation menu; the LLInt frame-layout lesson; the two-engine ropes verdict; Bun as market validation of startup + native surface + compatibility. |
| [V8-LAZY-PARSING.md](V8-LAZY-PARSING.md) | V8 (front end only) | Don't compile what you don't run: preparse/parse-on-first-call, the at-most-once invariant that fixed superlinear reparsing, the lazy-bodies × per-variable-capture interlock, and the cache-warming model for `AstSerial`/module precomp. Scoped when-the-time-comes. |

## What was already here before the studies

A reader of the table above could take away that every fast mechanism in
this codebase traces to some other engine. The record says otherwise, and
this index is the place to say it plainly: **several of the series'
findings were implemented in Raku++ before the corresponding engine's code
or write-ups were read** — independent rediscovery that the studies then
named and corroborated, not borrowing. The docs mark these case by case
("already ours", "banked", "confirmation", "no action"); collected:

- **COW strings with cached scan properties.** `CowStr`'s promoted shared
  bodies with memoized ASCII-ness, grapheme counts and offset tables came
  out of [STRING-SCAN-QUADRATICS.md](../STRING-SCAN-QUADRATICS.md), before
  the perl study — which then met `SvIsCOW` and downgraded its own COW
  bullet to "already exists here". `zend_string`'s and CPython's cached
  string properties are the same instinct, encountered later.
- **In-place `~=` append** — `strcat` O(n) in every mode, predating the
  two-engine ropes discussion it now answers.
- **First-observation specialization.** The decide-once-at-first-execution
  pattern — `hoistNeed` and the Callable scan flags, the hot-AST-node
  specialization that pre-dated pads — was in place before the
  specialization literature (PEP 659, BBV/YJIT, spesh) was read; the
  studies supplied names and policy refinements (counters, cold
  thresholds, retirement), not the idea.
- **Compare-and-jump fusion** — the conds-answer-as-bool lane landed from
  this project's own profiles (the TARG slice) before the PHP and Lua
  studies found the same fusion named "smart branch" in both.
- **Lazy materialization of the rare case** — `Env::ex`/`EnvExtras`
  predates the series and is the pattern PHP's lazy guards table and
  CPython's lazy frames institutionalize.
- **Pay-on-throw exception handling** — `catchScan`/`phaserScan` scan a
  body once and make entering a `try`-shaped construct free, the shape
  CPython 3.11 shipped as "zero-cost exceptions".
- **Packed arrays by construction** — the Array/Hash split plus
  `vector<Value>` storage; three engines' packed-array machinery solves a
  problem this design never had.
- **Precompiled-chunk loading** — `AstSerial` and `--bundle` predate the
  Lua deletable-compiler and V8 code-cache items that now frame them.
- **The frame pool and the regex-compilation cache** — landed from local
  profiles just before or alongside the perl study; perl's arenas and
  every engine's pattern caches are the corroboration.
- **The strategy itself** — fast startup, a native runtime surface, one
  binary, compatibility as the bar — is the project's founding premise
  (2–3 ms startup was a headline before any study existed), which the Bun
  section reads back as market validation, not as a source.

The distinction matters beyond credit. A mechanism reached independently
here *and* by engines under different constraints is stronger evidence
than one merely copied — much of the list below is agreement this project
is **one of the independent witnesses to**, not a syllabus it received.

For contrast, the honestly-imported column — what this codebase did take
from the studies, with measured results in the docs: `ValueHash` (perl
item 6), lexical pads and flat-loop scopes (perl item 2), TARG result
slots (perl item 4), and the census-guided `Value` shrink batches (perl
item 1a/1b). Everything else in the series is, as of this writing, design
input — open items, not adopted code.

## What the engines agree on

Convergences witnessed independently by two or more engines — in several
cases with this codebase among the independent witnesses (previous
section) — recorded once:

- **The value head is small and the body is behind a pointer.** 8 bytes
  (JSC, LuaJIT), 12–16 (Lua), 16 (PHP 7), 24 (perl's SV head). With
  containers, allomorphs and the numeric tower, our endgame band is 16–24.
- **Arguments land in the callee's frame.** Lua's register windows (2003),
  PHP 7's SEND (2014), MoarVM's callsite buffers — no separate argument
  object anywhere.
- **Compare-and-jump fuse.** PHP's smart branch, Lua's test-skips-jump
  executed as one dispatch, our conds-as-bool lane.
- **Strings intern once and carry their hash.** perl's HEKs, `zend_string`,
  Python's `str`, Lua, Ruby — five engines; our `IStr` covers the closed
  vocabularies, the general case waits on the head/body split.
- **Specialize under one of two policies.** Counters-then-specialize
  (PEP 659, JSC tiers) or first-observation (BBV/YJIT, spesh's statistics,
  our `DecidedOnce` family) — and every one of them counts failures and
  retires losing specializations.
- **Invalidation comes in three modes.** Check-per-use serials (CPython),
  subscribe-and-jettison watchpoints (JSC), key-miss (Ruby shapes) — the
  `ClassInfo` serial should be designed to serve the first two.
- **Ropes are a last resort.** MoarVM strands and JSC ropes both grew
  flattening heuristics because reads suffered; uniquely-owned in-place
  append stays our answer until profiles object.
- **Resumable control flow forbids a C-stack-recursive runloop.** Lua and
  MoarVM, independently. Filed against any future first-class
  `gather`/coroutine work.
- **Which variables escape is *the* analysis.** V8's preparser tracks
  references even while skipping bodies, Lua's compiler derives upvalue
  lists from them, Rakudo's optimizer lowers lexicals to locals only when
  it proves their absence. Three engines, one escape analysis — the core
  static input to the container/closure refactor.
- **Layout before dispatch before compilation machinery.** phpng, Faster
  CPython, Ruby (shapes shipped before YJIT exploited them), JSC's
  profiling tiers — and the JITs bolted on late (PHP 8, CPython 3.13)
  moved real workloads least. The order this project has been following is
  the order that ships.

## Where the items flow

The actionable items feed, in rough order:
[REPRESENTATION-PLAN.md](../../plans/REPRESENTATION-PLAN.md) and the
container/binding refactor (head/body endgame, reference/upvalue cells,
intrusive refcounts, biased RC, immortal bits), the dispatch-cache program
(ClassInfo serial → per-callsite guard-list caches → adaptive nodes),
[PARALLEL-PLAN.md](../../plans/PARALLEL-PLAN.md) (PEP 703's mechanisms), and
two future design docs — the threaded execution loop (PERL5 item 3, spec
material in the Lua doc) and lazy bodies (the V8 doc, sequenced with
per-variable capture).
