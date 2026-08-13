# Plan: grammar speed via a real workload — the pure-Raku JSON grammar

**Status: started 2026-08-13 (user-set).** Method, set by the user: (1) write
a brand-new full-scale JSON parser as a pure Raku grammar — not the native
codec, not a port; (2) profile it on mid/high-difficulty documents; (3) make
the most of the speed, fixing BOTH the engine and the grammar, each finding
gated the standard way. The grammar is the workload we optimize against, and
every divergence or slowness it exposes is campaign material. This replaces
the abstract "compile grammars" question (measured and discarded the same
day — see REPRESENTATION-PLAN.md batch 3 for the surviving teardown work).

## The workload

[tools/bench/grammar-json/jg.raku](../../../tools/bench/grammar-json/jg.raku):
full RFC 8259 — every escape, `\uXXXX` with surrogate pairs, strict numbers
(no leading zeros/plus), unescaped control characters rejected — written
idiomatically (proto tokens for `value` dispatch, actions building real Raku
values: Hash/Array/Str/Int/Rat/Num/Bool). Deliberately NOT micro-tuned; it
must stay the shape a Raku programmer would write. 38-case self-test
(`jg.raku check`), canonical serializer for byte-exact cross-engine
comparison (`jg.raku canon FILE`), best-of-N timing (`jg.raku parse FILE N`).

Corpus: [gen-corpus.raku](../../../tools/bench/grammar-json/gen-corpus.raku),
deterministic (byte-identical on regeneration), four profiles — api.json
(203 KB realistic nested records), deep.json (161 KB, 48-deep
object/array chains), strings.json (243 KB escape-heavy incl. surrogate
emoji), numbers.json (146 KB int/decimal/exponent mix). All four validate
against the native codec, and `canon` output is **byte-identical between
rakupp and Rakudo on all four** — the correctness gate for every batch.

## Baseline — 2026-08-13, build-arm64 (post REPRESENTATION batch 3), Rakudo 2026.07

best-of-5 ms per parse:

| file | rakupp | Rakudo | ratio |
|---|---:|---:|---:|
| api.json (203 KB) | 323 | 520 | 1.61× |
| deep.json (161 KB) | 413 | 621 | 1.50× |
| strings.json (243 KB) | 214 | 470 | 2.20× |
| numbers.json (146 KB) | 187 | 285 | 1.52× |

~0.6 MB/s absolute. The native codec parses these same files in single-digit
ms — the gap IS the campaign.

## First profile (api.json, sample 8 s, main thread 6798 samples)

- **~44%** — the ParseNode→Match conversion walk WITH the action methods
  firing inside it: `invokeMethod` → `exec` per matched rule, `make`/`made`
  plumbing, Hash/Array construction in user code.
- **~38%** — matching (`GrammarMatcher::parse` → the matchNode CPS engine).
- ~18% — harness (outer sub call, assignment, timing) and misc.
- Memo teardown ~0 on the main thread — batch 3's reaper carries it.

So on a REAL grammar with actions, the top bucket is not the matcher: it is
Match-object construction plus per-action interpreter dispatch.

## Findings ledger (each = a fix candidate, gate per batch loop)

1. **`Rat.base(10)` unimplemented** (rakupp dies, Rakudo fine) — found by
   the canonical serializer's first run; worked around in jg.raku
   (`rat-dec`). Engine fix candidate.
2. **Quadratic positional string ops on non-ASCII strings — FIXED
   2026-08-13** (STRING-SCAN-QUADRATICS.md §7 has the full story): one
   `é` anywhere made every positional op re-decode the whole string per
   call. Fix = two byte-offset tables (per codepoint / per grapheme)
   cached on StrBody, CAS-installed once; seven nqp ops + method substr
   converted, indexing semantics preserved by construction. Micro-probe
   4041 → 22 ms; JSON::Fast api.json **52,820 → 335 ms** (158×),
   strings.json 9,355 → 425. Gates: clustering-content oracle probes
   byte-identical to Rakudo, suite 442/442, Roast **197,565** (day's
   best; S15 100% of run assertions, movers = known flappers), canon
   identical. perf-guard: no pre/post separation in interleave; the
   day-long machine noise wants one quiet-window confirmation.
3. **`--exe`: a class-scoped `my` used from a method fails to compile —
   FIXED 2026-08-13.** Codegen dropped class-BODY statements entirely, so
   the method referenced an undeclared C++ identifier. Now such a `my` is
   hoisted into the same globals table top-level `my` uses, with the
   body's initialisers run at class-registration time; C++ scoping then
   gives the Raku answer for anything a method declares or binds itself
   (a local or parameter of the same name shadows it). The one thing that
   table cannot express is TWO live bindings of one name — a class-body
   `my $n` beside a top-level `my $n`, or the same name in two class
   bodies — which would collapse into one global and let the later
   initialiser silently win inside the method. That case is now REFUSED
   (CodegenError → AOT bundling: slower binary, never a wrong answer),
   verified by a probe where the fallback binary matches Rakudo exactly.
   jg.raku's `%esc` is back inside its action class, and compiles natively.
4. **`--exe`: `now` returned 0 and `time` was unrecognized — FIXED
   2026-08-13.** Both fell through to "unknown bareword is a type object",
   so `now.Num` was 0 and every timing/timeout/timestamp in a compiled
   program silently read zero. Root cause was DUPLICATION: the bareword
   constants existed only in the interpreter's NameTerm eval, never in
   `rtNameTerm` (the codegen entry). Now one shared `nameTermConstant()`
   serves both, which also fixed `i` (the imaginary unit) in compiled
   code. Regression: `t/fixtures/native-terms.raku` + three checks in
   t/run.raku that diff interpreted against compiled output.
   Still open (pre-existing, unrelated to the fix): a user's own
   `sub now {…}` never wins in the INTERPRETER, and wins everywhere in
   `--exe` (sub hoisting), where Rakudo switches at the declaration
   point. Also, class-body statements other than `my` declarations
   (nested classes, `use`, lexical regexes) are still dropped by `--exe`.

## The four-way picture (api.json, 203 KB — who parses JSON fastest where)

| parser | rakupp | Rakudo |
|---|---:|---:|
| jg.raku grammar (this campaign) | 323 ms | 520 ms |
| JSON::Fast (hand-written pure Raku) | **335 ms** (was 52,820 before finding #2) | 44 ms |
| native codec (`from-json` builtin) | ~2 ms | — |

The grammar path is rakupp's BEST pure-Raku option today because the
matcher runs in C++; hot hand-written scanner code is where the
interpreter gap bites. Under Rakudo the relation inverts: MoarVM's JIT
makes the hand-written scanner 7.6× faster than the grammar. Both gaps
are campaign material. See the 18 MB section above for the same table at
90× the size, where the grammar's lead over JSON::Fast opens to 2×.

## Batch 1 — action-path quick wins + the decomposition that redirects the campaign (2026-08-13)

Three contained fixes (Interpreter.cpp grammarParse, Regex.h/.cpp onRule):
a per-parse **action-method cache** (build() resolved the method through the
class chain — with guillemet string surgery — twice per node, every node);
the per-node `getenv("RAKUPP_DEBUG_MAKE")` became a static (profiled at
~1.5%); the completion log's ParseNode is now **moved** end-to-end instead
of deep-copied twice per action-bearing completion. Interleaved A/B under
identical load: **only −1-2%** — these were real but small.

The measurement that matters came from the new `--raw` harness switch
(parse WITHOUT the actions object; the Match tree still builds):

| file | full parse | raw parse | action layer share |
|---|---:|---:|---:|
| api.json | 319 ms | 143 ms | **55%** |
| deep.json | 435 ms | 175 ms | **60%** |

So the split is ~55-60% action layer (per-rule `invokeMethod`: Env
creation, `($/)` binding, dispatch, cooperative-return plumbing — PLUS the
interpreted action bodies), ~25% matcher, ~15-20% Match-tree build. At
~35k action calls per api.json parse that is ~5 µs per action call.
**The campaign's next battle is the per-call cost of an interpreted
method invocation** — REPRESENTATION-PLAN phase 3 territory (Env
~103 ns/call is only the floor; binding + dispatch + return handling is
the rest), measured here at grammar scale.

## The 18 MB real-world test — the zef/REA index (2026-08-13)

`~/.zef/store/rea/rea.json` (17.55 MB, one top-level array of 14,884 dist
objects; not committable — regenerate/refresh via zef). Single-rep,
`/usr/bin/time -l`:

The four-way, re-measured 2026-08-13 after the string-index fix
(finding #2), 1 rep, `/usr/bin/time -l`:

| parser | rakupp | peak RSS | Rakudo | peak RSS |
|---|---:|---:|---:|---:|
| jg.raku grammar | **16.2 s** | 6.2 GB | 25.9 s | 1.9 GB |
| JSON::Fast | 32.6 s | 0.72 GB | **2.05 s** | 0.50 GB |
| native codec | **0.21 s** | 0.33 GB | — | — |

(Grammar `--raw`, no actions: 7.6 s / 5.6 GB. Earlier full-parse figure
14.8 s was pre-string-fix; the 16.2 s here is a different day-state, not
a regression — see the perf note in finding #2.)

What it says:

1. **The quadratic is gone at scale, on real data.** The file holds 3,638
   non-ASCII bytes scattered through 18 MB, so the whole-document string
   is non-ASCII and every positional op used to re-decode all of it.
   JSON::Fast/rakupp scaled **97× for 90× more input** (335 ms on
   api.json → 32.6 s here) — linear, the shape the fix was for. The
   pre-fix binary on the same file **did not finish in 240 s** (killed,
   no output), against 32.6 s after: this document was not parseable by
   that path in practice, and the quadratic extrapolation from the
   203 KB measurement puts it in the days.
2. **The grammar is rakupp's best pure-Raku path, and the lead grows with
   size**: a tie at 203 KB (323 vs 335 ms), a **2× win** at 18 MB (16.2
   vs 32.6 s) — the matcher is C++, so it scales where an interpreted
   scanner loop does not. Under Rakudo the relation stays inverted
   (JIT'd scanner 12.6× faster than its grammar).
3. **Memory is our standing weakness**: 6.2 GB for the grammar path,
   3.2× Rakudo, ~350× the input. Rakudo's Match carries orig+from/to; we
   copy a substring per node into a ~344-byte Value. Lazy Match
   materialization (target 2) is a MEMORY fix as much as a speed fix.
   Note JSON::Fast/rakupp needs only 0.72 GB for the same result data —
   the 5.5 GB delta is Match trees, not user data.
4. Robustness holds: the ~15k-element CPS continuation chain survives on
   the big stack, and canonical output is **md5-identical to Rakudo's**
   at this scale.
5. The native codec is **77× faster than the grammar** and 158× faster
   than JSON::Fast-on-rakupp — the ceiling both pure-Raku paths are
   measured against.

## `--exe` and grammar-shape experiments (2026-08-13)

**Compiled (`--exe`) is ~20% faster, for free.** Per-parse cost by the
slope method (reps=6 minus reps=1, cancels startup — needed because the
harness cannot self-time in a binary, see finding #4):

| | api.json per parse | 18 MB wall | 18 MB peak RSS |
|---|---:|---:|---:|
| interpreted | 380-384 ms | 18.4 s | 6.17 GB |
| `--exe` | **288-318 ms** | **14.9 s** | **5.37 GB** |

Compilation takes 1.8 s and yields a 7.9 MB binary. The win is the
action bodies running as C++ instead of AST-walking; the matcher was
already C++, which is why it is 20% and not 2×.

**Grammar-shape experiments** (scratchpad `jgvar.raku`: three parsers,
same input, canon-compared — all "same"):

| variant | api | strings | numbers | deep |
|---|---:|---:|---:|---:|
| A baseline (proto dispatch, string sub-rules) | 307 | 231 | 179 | 381 |
| B string folded into one token, decoded in the action | **272** | 256 | 189 | 394 |
| C = B but plain `\|` alternation instead of the proto | 322 | 338 | 252 | 464 |

Two rules for writing fast grammars on rakupp fall out, both the
opposite of what one might guess:

1. **Keep protos.** Replacing proto dispatch with a plain `|`
   alternation over subrules cost **18-46%** everywhere. Cause is
   measured, not guessed: for the `|` alternation the NFA ranker runs
   (RANKDUMP confirms it is NOT falling back to the probe) and costs
   **+19% vs `RAKUPP_LTM=0`** on that shape, while for proto dispatch
   NFA and probe are within noise (±3%) — the union NFA is cached on
   `GrammarRuleMeta` and its scan is short, whereas the Alt path rescans
   long subrule-expanded prefixes at every position. **Engine target:
   make Alt-over-subrules as cheap as proto dispatch** — most real
   grammars use `|`, so this is worth more than it looks here.
2. **Do not move matcher work into action code.** Folding the string
   sub-rules into one token wins 11% only where strings are
   escape-free (api.json, where the action's `unless .contains('\\')`
   fast path skips everything) and LOSES 6-32% wherever the action must
   actually scan (strings.json escape-heavy). The C++ matcher is
   cheaper than interpreted scanning; keep work in the grammar.

Net: the baseline grammar is already close to its best idiomatic shape.
Grammar-side tuning is worth ~10% and is workload-dependent; the engine
targets below are worth multiples.

## Target list, in measured order (updated after batch 1)

1. **Per-call cost of an interpreted method invocation** (the 55-60%):
   profile ONE action call end-to-end (methodCall → bind → execBlock →
   return) at grammar scale; candidates: Env pooling for fixed `($/)`
   signatures, skipping the multi/trait machinery for plain methods,
   cooperative-return fast path. Gate on perf-guard's subcall kernel too —
   this path is shared with all method calls.
2. **Lazy Match materialization** (most of the raw parse's non-matcher
   half): actions mostly read `$<child>.made` and `~$/` — the full Match
   Value tree (with an `input.substr` COPY per node, O(text × depth) on
   containers) is built even when nothing reads it. Rakudo stores
   orig+from/to and computes .Str on demand; we eagerly copy.
3. **The matcher's ~25%**: memo construction interleaved with matching
   (grammar-split.raku decomposition applies), NFA-vs-probe balance
   (+18/+12/−4% at 100/400/1600 records — amortizes on large docs).
4. **Grammar-side tuning LAST**, and only idiomatic changes (e.g. `<.ws>`
   discipline, ratchet correctness) — the workload must stay honest.

## Gates

Every batch: jg.raku check on BOTH engines, canon byte-identical on the
four corpus files on BOTH engines, then the standard batch loop (suite,
full Roast band, perf-guard).
