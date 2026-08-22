# What JavaScriptCore (and Bun) teach Raku++

Seventh in the series ([PERL5-](PERL5-TECHNIQUES.md), [PHP7-](PHP7-TECHNIQUES.md),
[PYTHON3-](PYTHON3-TECHNIQUES.md), [RUBY-](RUBY-TECHNIQUES.md),
[MOARVM-](MOARVM-TECHNIQUES.md), [LUA-TECHNIQUES.md](LUA-TECHNIQUES.md)).
Primary source, read 2026-08-22: Filip Pizlo, *Speculation in
JavaScriptCore* (webkit.org, 2020) — the most quantified account of
speculative optimization in print. Bun facts are from bun.sh as of the same
date. V8 is deliberately absent here except by contrast: its runtime
techniques (hidden classes, ICs, tiers) share ancestry with JSC's, and its
one distinctive front-end idea has its own doc —
[V8-LAZY-PARSING.md](V8-LAZY-PARSING.md).

JSC is the deep end of the pool: four execution tiers and three JITs in the
service of a language whose dynamism resembles Raku's. Nothing below argues
for building that stack. The study's value is that Pizlo publishes the
**numbers behind the judgment calls** — what a tier buys, what a wrong
speculation costs, when checking beats subscribing — and those numbers
calibrate decisions this project faces at one-tier scale.

## 1. The economics of speculation, with the constants filled in

The published figures. Per-bytecode execution cost on JetStream 2: LLInt
(interpreter) 3.97 ns, Baseline JIT 1.71 ns, DFG 0.349 ns, FTL 0.225 ns —
an 18× spread interpreter-to-peak. Tier-up is earned by counters (500
points for Baseline, 1000 for DFG, 100k for FTL; functions get 15/call,
loops 1/iteration, scaled by function size and recompile history). A wrong
speculation (OSR exit) costs ~2,500 ns from DFG and ~10,000 ns from FTL
against a per-instruction benefit of ≤1.48 ns — four orders of magnitude —
which forces the design rule Pizlo states outright: speculate only when
the success probability is ~1, and profile to find **counterexamples**,
not distributions. Repeated exits jettison the code (100 exits, doubled
per recompilation).

Three calibrations for us:

- **The spread bounds the middle tier.** Their interpreter-to-peak is 18×;
  our interp-to-`--exe` runs 3–14× per kernel
  ([BENCHMARKS.md](../../../status/BENCHMARKS.md)). A hypothetical middle tier
  (baseline JIT, threaded code) lives inside our smaller gap — worth
  remembering when weighing PERL5 item 3's cost against its ceiling.
- **p≈1 is the rule we already run** — `DecidedOnce` decides once and never
  guards again precisely because its properties cannot regress; spesh
  (MOARVM item 2) states the same rule statistically. The adaptive-node
  work (PYTHON3 item 1) inherits it: specialize on what cannot fail, or
  guard with a *counted* fallback.
- **Count the exits.** JSC jettisons code that keeps exiting; PEP 659
  de-specializes on counter exhaustion; YJIT stops compiling cold code.
  Three engines agree: every specialized lane needs a failure counter and a
  retirement path, or megamorphic sites bleed.

## 2. Structures, inline caches, watchpoints — and the third invalidation mode

JSC objects carry a pointer to a hash-consed **Structure** (shape); "has
this object shape S" is one pointer compare. Property storage is inline
slots plus the **butterfly** — one allocation addressed in two directions,
named properties growing one way and array elements the other from a
single pointer (a layout worth remembering for any "object with both slots
and indexed storage" need). The Baseline JIT reserves patchable slabs per
access site: first execution patches in a structure check plus direct
load; polymorphic sites accumulate cases (PICs). All of that is the mature
form of what the Ruby/Python docs already carry.

The genuinely new mechanism is the **watchpoint**: instead of emitting a
check, the compiler *subscribes* to "this structure never gains a
property" / "this global never changes"; if the fact is violated, every
subscriber is jettisoned immediately. That is a third invalidation mode
alongside the series' other two, and the three now form a complete menu:

1. **check-per-use** (version serial compare — PYTHON3 item 2),
2. **subscribe-and-jettison** (watchpoint: zero per-use cost, expensive
   mutation),
3. **discover-by-missing** (cache key simply stops matching — Ruby shapes).

The selection rule falls out of mutation frequency: Raku's `augment`/
`.wrap`/instance-`does` are rare enough that watchpoint-style — mutation
walks registered AST cache sites and flips them back to generic — beats
paying a serial compare on every call. Design the `ClassInfo` serial so
both modes hang off it: the serial *is* the subscription version; hot
sites register for the walk, cold sites compare. This refines PYTHON3
item 2 and RUBY item 2 into the final shape.

## 3. The LLInt lesson: the interpreter is load-bearing at every scale

With three JITs above it, JSC still ships an interpreter — and keeps it
sharing the **same frame layout** as JIT'd code, so tiering up mid-loop
(OSR entry) and bailing out (OSR exit) move between tiers without
translating frames. Every tier also profiles for the tier above (value
profiles in LLInt/Baseline feed DFG's speculation). The transferable rule:
whatever executes first should *produce the information and the layout*
the next tier consumes. Our pads already put frame state in slot form —
JIT-compatible by accident — and the adaptive-node stats (PYTHON3 item 1a)
are the value-profile analog. If a baseline tier is ever built, this is
why it starts cheap: the interpreter's layout was designed for it.

## 4. Ropes: the second witness, same verdict

JSC strings become **ropes** on concatenation, flattened lazily. Identical
finding to MoarVM's strands (MOARVM item 4): mature engines rope, then
grow flattening heuristics because read paths suffer. Our uniquely-owned
in-place `~=` append already makes building O(n) with flat reads — strcat
runs 18.7× ahead of Rakudo interpreted. Two-engine verdict recorded once:
ropes are the tool for concat-heavy *shared* strings, adopt only on
profile evidence, and expect to pay in read-path branches.

## 5. NaN-boxing: the last value-representation datum

JSValue packs double/int32/cell-pointer/booleans into 8 bytes via
high-bit-pattern encoding (doubles offset so pointers and immediates
occupy distinct 16-bit-prefix ranges). Series tally complete: 8 (JSC/
LuaJIT) — 12/16 (Lua) — 16 (PHP) — 24 (perl head). Raku's containers,
allomorphs and numeric tower keep our endgame in the 16–24 band (PERL5
item 1); the 8-byte encodings matter to us only as proof of how much a
*small closed* value universe buys — the TARG int lanes already harvest
that inside expression spines without changing `Value`.

## 6. Bun: the runtime shell as the product

Bun is not a faster JavaScript engine — it **embeds JSC** — and that is
what makes it interesting here. Its pitch is the shell around the engine:
runtime, package manager, test runner and bundler in one binary, hot APIs
implemented natively rather than in JS, fewer syscalls, no build step, and
JSC chosen explicitly for startup and memory over V8's peak-throughput
profile. Its published numbers are shell numbers: installs up to 30×
npm, HTTPS serving ~1.9× Node, WebSockets ~34× Node's messages/sec,
`bun test` starting in milliseconds. Originally written in Zig; as of Bun
1.4 the core is **rewritten in Rust** (~1M lines, per bun.sh, benchmarks
held and the binary ~20% smaller).

The mapping to this project is almost embarrassing in its directness:
choose the fast-startup engine (a 2–3 ms C++ interpreter over a VM),
implement the runtime surface natively (our C++ builtins; the Rakus
server), one binary with no build step (`rakupp script.raku`,
`--exe`/`--bundle`), compatibility as the adoption wedge (Roast as the
bar). Bun is market evidence for a thesis this repo bet on before Bun
existed: **startup latency plus a native runtime surface plus drop-in
compatibility wins users before peak throughput does** — Node still has
the bigger JIT; Bun took the mindshare anyway. Worth one footnote of
symmetry: rakujs already benchmarks under Bun
([rakujs/README.md](../../../../rakujs/README.md)), so the two projects
literally share a stack in one direction.

## What deliberately does not transfer

- **The tier stack itself** — four tiers, three JITs, concurrent
  compilation threads: the engineering budget of a browser vendor, aimed
  at peak throughput this project deliberately trades away.
- **Riptide** (concurrent copying GC) — reference-semantics machinery; our
  memory thread runs through PEP 703 (PYTHON3 item 5).
- **V8-style runtime machinery** — same family, covered here once; V8's
  distinct contribution is the front end, in its own doc.

## Suggested order

| # | change | payoff | cost | depends on |
|---|---|---|---|---|
| 2 | dual-mode invalidation on the ClassInfo serial (compare for cold sites, subscribe-and-flip for hot) | completes the cache design | low-medium | PYTHON3 item 2 + RUBY item 2; before PHP7 item 1 lands |
| 1 | exit counters + retirement on every specialized lane | keeps lanes honest | low | with PYTHON3 item 1a stats |
| 3 | keep frame layout tier-compatible (pads already are) | free insurance | — | recorded |
| 4, 5 | verdicts recorded (no ropes; head stays 16–24 B) | — | — | — |
| 6 | Bun as strategy validation — startup + native surface + compat | conviction, not code | — | — |
