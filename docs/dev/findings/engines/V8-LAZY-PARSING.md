# V8's lazy parsing, read against Raku++'s front end

Eighth in the series, and the narrow one by design: V8's *runtime*
techniques share ancestry with JavaScriptCore's and are covered once, in
[JSC-TECHNIQUES.md](JSC-TECHNIQUES.md); this doc takes V8's genuinely
distinct contribution — the industrialized lazy front end — because it
answers the question that started the whole reading program ("why is the
compiler so fast") at the opposite end of the scale from perl's answer.
Perl's answer was: make one pass and keep it simple. V8's answer is: **don't
compile what you don't run.** Sources, read 2026-08-22: the V8 team's
"Blazingly fast parsing, part 2: lazy parsing" and "Code caching for
JavaScript developers" (v8.dev).

The scale that forced it: a browser parses megabytes of JavaScript per
page, most of whose functions never run, on the critical path to first
paint. Raku++ is not there — startup is 2–3 ms with parsing included
([BENCHMARKS.md](../../../status/BENCHMARKS.md)) — so this doc is explicitly a
**when-the-time-comes** study: the mechanisms, the traps V8 documented so
we don't rediscover them, and an honest account of when the time actually
comes.

## 1. The mechanism: preparse everything, parse on first call

V8 runs two front ends. The **preparser** checks syntax and collects
minimal scope information but builds no AST — it exists so the real parser
can *skip function bodies entirely*. The **parser** builds the full AST and
bytecode, and runs for a function only when it is first called. Heuristics
patch the obvious hole: source patterns that signal
call-during-startup — `(function(){…})`, `!function(){…}()` and other
PIFE ("possibly-invoked function expression") shapes that bundlers emit —
are compiled eagerly, skipping the wasted preparse.

The economics only work because the preparser is much cheaper than the
parser *and* because most functions never graduate: for cold functions the
preparse is the whole cost, for hot ones it is a small prefix.

## 2. The trap they documented: superlinear reparsing

The part worth importing verbatim is the failure mode. Before Chrome 63,
lazy-compiling an outer function re-preparsed every inner function; with
nesting, inner bodies were preparsed again and again — **nonlinear parse
cost in nesting depth**, discovered in production. The fix: the preparser
now performs full scope resolution once and serializes its variable
metadata (a dense flag array per function); when a function is later
lazily parsed, that metadata is applied instead of re-visiting inner
bodies. Invariant achieved, in their words: any function is at most
preparsed once and fully parsed once.

The general law under it: **lazy compilation composes only if the skipped
unit's summary is saved**. Skipping without a summary means re-deriving;
re-deriving nests. Any lazy-body design for Raku++ starts from that
invariant, not from the naive skip.

## 3. Why the preparser tracks variables — and the sequencing insight for us

V8's preparser cannot just bracket-match past a body: JavaScript must
decide which outer variables are captured by inner functions, because
captured variables are heap-allocated in contexts while the rest live on
the stack. So even the skipping pass tracks declarations and references —
the scope analysis is the irreducible part; only AST-building is skipped.

This lands on Raku++ with unusual timing implications. **Today, skipping a
sub body is semantically cheap for us**: closures capture the whole frame
(`Callable.closure` is a `shared_ptr<Env>`), so no per-variable capture
analysis is needed — a skipped body hides only its own lexicals, which are
invisible outside anyway. But [LUA-TECHNIQUES.md](LUA-TECHNIQUES.md)
item 3 argues (and the container/binding refactor will likely adopt) Lua's
per-variable capture cells — after which a skipped body **must** still
report its outer references, which is exactly the analysis V8's preparser
exists to do while skipping. So the two features interlock: implement lazy
bodies before per-variable capture and the skim is trivial; implement
capture first and the skim must grow V8's reference tracking. Either order
works — but the design docs must be written together, and this is the
non-obvious dependency this study exists to record.

The Raku-specific constraints on skipping, for that design doc:

- The skim must bracket-match through Raku's quoting universe — q-langs,
  regexes, heredocs, embedded comments — which the Lexer already
  tokenizes; a "skim mode" is a lexer feature, not a new parser.
- Compile-time effects force eagerness: a body containing `BEGIN`, `use`,
  `constant`, or an operator/slang declaration cannot be skipped blind.
  The skim watches for those tokens and bails to the full parser — the
  moral twin of V8's PIFE heuristic, in reverse.
- Multi/proto declarations, `is export` and signatures stay eager — only
  the *body* is deferred, matching V8 (function headers are always
  parsed).

## 4. Code caching: the other half, which we already half-have

V8's second front-end weapon: after a script runs, its bytecode is
serialized and keyed by source — an in-memory cache (80% real-world hit
rate) plus a disk cache managed through the HTTP cache; on a "hot" load,
compilation is skipped entirely. The subtlety their developer guidance
keeps repeating: **lazily-compiled functions are not in the cache** —
only what was compiled by the time the script finished — so real-world
advice bends toward forcing eager compilation (IIFE-wrapping, bundling)
of what the next run will need. Laziness and caching pull against each
other, and the cache wins for startup-critical code.

Us: the artifacts already exist — `AstSerial`, `--bundle`/`--aot`, and
the module-precomp direction in
[MODULES-PLAN.md](../../plans/MODULES-PLAN.md) — what V8 adds is the
*warming model*: decide explicitly what the cached artifact contains. If
lazy bodies land, a module's serialized form should store bodies
**post-first-parse** (the cache warms as the module gets used), or
provide a "compile fully for the cache" mode — the `--bundle` lane —
mirroring V8's cold/warm/hot progression rather than fighting it.

## 5. Streaming and off-thread parsing: the parallel dividend

V8 parses scripts on background threads while they download. Our analog
is nearer than it looks: module parsing is embarrassingly parallel across
a `use` graph, the worker threads already exist
([PARALLEL-PLAN.md](../../plans/PARALLEL-PLAN.md)), and the parser's shared
state (interner side-tables) is exactly the kind of surface that plan is
hardening anyway. A `use`-heavy application could overlap module parsing
with mainline execution up to first-reference. Marker, not work: profitable
only past the module-count threshold where item 6 says any of this
matters.

## 6. When the time comes (an honest sizing)

Parse time is not on today's critical path: 2–3 ms startup including
parse, and the benchmark kernels are runtime-bound. The thresholds that
flip it, in likely order of arrival:

1. **Prelude-in-Raku growth** — every setting sub written in Raku is
   parsed at every boot; the embedded-serialized-prelude answer (PYTHON3
   item 6, Lua's deletable compiler) beats lazy parsing here because boot
   code *does* run — cache, don't defer.
2. **Wide-API modules** — a `use`d module with hundreds of subs of which
   a script calls five is lazy parsing's home turf; that is the
   Cro-shaped future, and the win compounds with per-module caching
   (item 4).
3. **Batch corpora** (Roast, the course) — thousands of small files that
   run *everything* they declare: little for laziness, everything for
   process startup, which is already won.

So the standing order is: measure parse share on a wide-API module
workload before building any of this; when it crosses a few percent,
items 2–4 above are the plan, in that order, with the item 3 sequencing
constraint honored.

## What deliberately does not transfer

- **PIFE-style source heuristics as a public contract** — V8's eager
  patterns became folklore that bundlers target; our equivalent knob, if
  ever needed, should be explicit (`--parse-eager`, a pragma), not a
  source idiom to cargo-cult.
- **The scanner micro-work** (part 1 of their series — UTF-8 windows,
  perfect-hash keywords): our lexer is not a measured bottleneck;
  revisit only with profiles.
- **Compile-on-demand for the top level** — V8 defers *functions*; the
  Raku mainline runs once and interleaves with compile-time effects;
  only declaration bodies are deferral candidates.

## Suggested order

| # | change | payoff | cost | depends on |
|---|---|---|---|---|
| 4 | module cache warming model (store post-parse bodies; `--bundle` = fully warm) | medium, arrives with modules | low-medium | MODULES-PLAN precomp |
| 3 | write lazy-bodies and per-variable-capture design docs **together** | prevents a rediscovered V8 trap | design-time only | LUA item 3, container refactor |
| 1+2 | the skim itself (lexer skim mode + saved summaries, at-most-once invariant) | high at module scale, nil today | medium | triggered by parse-share measurement |
| 5 | parallel module parsing | small-medium | low once safe | PARALLEL-PLAN container phase |
