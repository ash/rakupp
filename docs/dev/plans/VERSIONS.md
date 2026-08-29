# Versions — what each major release set out to do

One section per major version: the goal it was *named* for, where the plan
lived, and (for shipped versions) what actually landed. This is the
forward-looking companion to [MILESTONES.md](../../status/MILESTONES.md) —
that file records what happened and when; this one records what each version
was *for*, so the next campaign is planned the same way the last ones were.

The pattern every shipped major has followed: **one number a stranger can
re-measure**, a written plan before the code, and the standing gates on every
batch (zero Roast regressions, the local suite, `perf-guard --check`,
methodology in [COUNTING.md](../../status/COUNTING.md)).

## v1.0.0 — "it's Raku" (shipped 2026-07-22)

- **The number:** 90% of declared Roast assertions, no architecture changes,
  no performance regressions. Shipped at **194,496 / 216,066 (90.0%)**,
  ~39% of files fully passing.
- **The plan:** the campaign ran off
  [findings/ROAST-GAPS.md](../findings/ROAST-GAPS.md) (the systematic gap
  classification) with [100.md](100.md) as the ceiling analysis — 100% does
  not exist; ~97% is the real ceiling, and the walls past ~92% are projects,
  not tasks. An independent pre-1.0 review
  ([findings/REVIEW-1.0.md](../findings/REVIEW-1.0.md)) and five fix waves
  closed the cycle.

## v1.1.0 — 100% Unicode (shipped 2026-07-24)

- **The number:** all of S15 — **91,752 / 91,752 assertions**. Full UCD case
  tables, NFG-aware regex, complete `uniprop`. A deliberate single-synopsis
  campaign between the 1.0 and ecosystem pushes.

## v1.5.x — conformance measured, properties become gates (shipped 2026-07-29/31)

- **The number (v1.5.0):** the measured divergence gap with Rakudo, halved —
  **202 → 152** across the documentation sweep and the operator behaviour
  matrix. The release's other half was structural: properties we care about
  became **gates that fail a release** (performance among them —
  [RELEASING.md](../RELEASING.md)) instead of numbers someone remembers to
  check.
- **v1.5.1:** speed with zero behaviour change (fib −17.6%), Roast
  byte-identical before and after — the release that proved the perf gate.
- **v1.5.2:** changed a standard rather than a number: a module counts as
  working only when **its own test suite passes** — the bar v2.0.0 was then
  measured on.

## v2.0.0 — other people's code (shipped 2026-08-07)

- **The number:** **50 / 59** top-50 distributions passing their own `zef`
  install-time suites (from 11 when the bar was set), counted honestly —
  the Pair-form subtest fix re-measured the whole suite on the way.
- **The plan:** [ecosystem/V2-MODULES-PLAN.md](../ecosystem/V2-MODULES-PLAN.md),
  with the batch-by-batch record in
  [ecosystem/MODULE-FINDINGS.md](../ecosystem/MODULE-FINDINGS.md) and the
  pre-2.0 source review in [findings/REVIEW-2.0.md](../findings/REVIEW-2.0.md).
  Almost none of the work was module-specific: the modules were the *finder*
  for general interpreter bugs.

## v3.0.0 — the compiler grows up (LANDED 2026-08-09; planned 2026-08-07)

Not framed as Rakudo parity — Rakudo stays the oracle every divergence is
judged against, but the headline items are capabilities, each with its plan
written before any code:

1. **A real command line, with a first profiler**
   ([CLI-PLAN.md](CLI-PLAN.md)) — **DONE 2026-08-07**, the same day the
   campaign was planned: one option parser replacing the position-sensitive
   mode cascade (goldens written against the old binary first); the
   completed Perl one-liner family with `-i` in-place editing, `-a`/`-F`
   autosplit and `-0777` (four live perl differentials in the suite); flags
   borrowed from other compilers (`-M`, `-v`, `--target`); `--profile` —
   routine-level instrumented profiling whose disabled hooks measured at
   zero cost; MAIN usage byte-identical to Rakudo (issue #17); and
   [guide/CLI.md](../../guide/CLI.md). Building `-i` found and fixed a
   general interpreter bug (`my $*OUT = $handle` did not reroute
   say/print/put).
2. **Real multicore parallelism, on by default**
   ([PARALLEL-PLAN.md](PARALLEL-PLAN.md)) — execute the GIL-removal design
   ([PLAN-gil-removal.md](PLAN-gil-removal.md), Option 2): a written memory
   model, a TSan-gated stress suite, runtime hardening so a user data race
   can never crash the interpreter (today it SIGABRTs), a worker pool, then
   flip `RAKUPP_PARALLEL` from opt-in to default with `RAKUPP_GIL=1` as the
   escape hatch.
3. **True Longest-Token Matching** ([LTM-PLAN.md](LTM-PLAN.md)) — replace
   the regex engine's probe-and-rank approximation with a side-effect-free
   declarative-prefix NFA ranking `|` alternations and protoregex dispatch,
   fixing the oracle-verified divergences (ranking by greedy end instead of
   declarative-prefix end; user code running during candidate selection)
   without giving back the engine's measured speed advantage.

**The numbers a stranger can re-measure** at the tag: the unguarded-race
family runs without a native crash and an embarrassingly-parallel benchmark
scales ≥3× on 8 cores at unchanged single-thread speed; the LTM divergences
are gone from the oracle sweep with `longest-alternative.t` at the
fudged-Rakudo score; and the one-liner cookbook runs byte-identical to Perl's
`-i`/`-a`/`-F` behaviour. As with 1.x → 2.0.0, finished work lands in v2.x
minor releases along the way; **v3.0.0 tags when all three pillars hold
their gates at once**.

**All three pillars hold their gates (2026-08-09).** CLI: complete since
2026-08-07. LTM: true NFA ranking is the default (`RAKUPP_LTM=0` = the
legacy probe, one release); Roast/battery/spec-site gates all green in
both settings. Parallel: on by default (`RAKUPP_GIL=1` = the escape
hatch); three consecutive quiet parity runs at 197,186–197,190 with an
identical 5-file timeout list against the GIL's 11, and the shipping
default measures **197,191 / 218,772 declared (90.1%)** — the highest
total recorded on this codebase. Two ledgered post-flip exceptions
(PARALLEL-PLAN.md): nonblocking-await.t and bug-coverage-stress.t.

## v3.5.0 — the 6.e language revision (shipped 2026-08-20)

Numbered below v3.14.0 deliberately: the release is named for the language
revision it carries, and 4.0.0 is reserved for the modules and embedding
milestone. Package managers order 3.5.0 as the older version — a known,
accepted consequence.

- **The number:** the 6.e support matrix, run four ways (both engines × both
  revisions) — **50 of 51 full, 0 divergent, 0 partial**, against 23/1/19/8
  when the plan was written. RakuAST is the one not implemented, deliberately;
  it is [its own campaign](RAKUAST-PLAN.md).
- **The plan:** [6E-PLAN.md](6E-PLAN.md), written before the code, off a
  measured matrix rather than a feature list. Its three decisions — the
  revision is a property of the CODE not the process, under 6.d do the 6.d
  thing always, and the additions are gated too — are what made "a program's
  meaning must not depend on the engine" affordable.
- **What it shipped besides:** the Weekly Challenge round-two corpus from
  74.8% to 90.9% byte-identical over thirteen gated batches, the first
  ecosystem freshness sweep (the 100 newest distributions) and the parse
  cluster it proved ours, and `require ::($name)` — broken for a week by the
  v4 arc and found by the release's own battery gate.

## v3.14.0 — only what the program needs (planned 2026-08-09)

A single-subject minor in the shape of v1.1.0, starting after v3.0.1 ships:
a compiled program should stop carrying the parts of Raku it cannot reach.

- **The number:** `say "Hello"` compiled with `--exe --slim` is **≤ 5.5 MB**,
  down from **9,830,680 bytes** today — while every program in `t/`,
  `examples/` and the module battery produces byte-identical stdout, stderr
  and exit status built slim and built full. The size is only half the
  claim; the differential is the other half.
- **The plan:** [SLIM-PLAN.md](SLIM-PLAN.md), written before any code, off a
  measured breakdown of where the 9.8 MB goes. Today the size is a *constant*
  — 24 examples span 0.8% — because the runtime is one archive in which
  everything is genuinely reachable from `Interpreter::Interpreter()`.
  `-dead_strip` buys 200 KB and LTO measures *worse* than `-O2`, so the cut
  has to be made in the source: Unicode data tables and the parser move
  behind accessors with stub counterparts, and `--exe` links the real archive
  or the stub per feature. Measured on hand-pruned builds that run correctly:
  **−45%** for the Unicode tables, **−51%** with the parser as well.
- **One key, and a plan to make it unnecessary:** the whole surface is
  `--slim[=SPEC]`, where SPEC is a level (`none` / `safe` / `auto` / `max`),
  `±feature` overrides, and directives (`help`, `list`, `why:`, `verify`) —
  the `-fsanitize=`/`-march=native+crypto` idiom rather than six new flags
  on a command line v3.0.0 had just tidied. `safe` (the free 16%: dead-strip
  and symbols, no feature removed) is on with no flag; bare `--slim` is the
  sound automatic level and the only thing most users type. Because `auto`
  is sound by construction, the end state is that it becomes the default
  with `--slim=safe` as the escape — the shape `RAKUPP_PARALLEL` and
  `RAKUPP_LTM` took in v3.0.0. That flip is a phase of its own, explicitly
  outside the 3.14 tag, gated on the differential suite holding across
  several consecutive releases rather than one clean run.
- **Why it is a campaign and not a patch:** the risk is not size, it is
  cutting something a program needs. The plan's centre is the six defences
  that make that impossible-or-loud — prove-unused rather than guess-unused,
  a force-full trigger list, stubs that throw a named exception naming the
  rebuild flag instead of returning empty tables, a manifest in the binary,
  and the differential and negative suites as release gates. Per-builtin code
  pruning is a stated non-goal: cutting only *data* keeps the entire failure
  surface at one function.

## v3.21.0 → v3.23.0 — the consolidation arc (planned 2026-08-28)

Three releases in a row that add no campaign. Language work has the property
Larry Wall kept pointing at — push the design in one place and something pops
out in another — and after v3.20.1 that is where this engine is: a lot of
correct individual changes, and no single sitting in which all of them were
measured together. The arc exists to get back to a known state before the next
chase starts, and each release is allowed to be boring.

### v3.21.0 — the state after the changes

- **The number:** the full gate set in [RELEASING.md](../RELEASING.md), green,
  on one machine in one sitting — Roast file list with no regressions, the
  local suite, `perf-guard --check`, optbench, the slim pair, a second
  toolchain, and the 59-dist battery back to its v3.7.0 parity figure.
- **What it carries:** what has accumulated on main since v3.20.1 and cannot
  reach anyone until there is a tag — issues [#38](https://github.com/ash/rakupp/issues/38),
  [#39](https://github.com/ash/rakupp/issues/39), [#40](https://github.com/ash/rakupp/issues/40),
  [#41](https://github.com/ash/rakupp/issues/41), [#42](https://github.com/ash/rakupp/issues/42)
  (HTTP::Tiny, broken in the only published release), TAP::Harness going green,
  exact Bag/Mix counts, bound lists refusing the resizing mutators, `--lsp`, the
  installer's uninstall and version-floor fixes, and a build stamp that names
  the release it came from.
- **Not in it:** anything new. A release whose job is to make the next two
  start from a state a stranger can reproduce.

### v3.22.0 — instruments first, then the review

*Planned in [GATES-PLAN.md](GATES-PLAN.md), written the day v3.21.0 shipped.
The order below is not the order this section was first written in: the v3.21.0
sitting put the instruments ahead of the review, because that release passed all
seven gates and shipped a silent wrong answer anyway, and six of the seven
turned out to have a defect of their own.*

- **The number:** every gate detects a planted defect — for each of the seven,
  inject a known break, run the gate unmodified, confirm it goes red. Then the
  review's finding list closed, and a Roast file list re-derived from a clean
  full run rather than carried forward from deltas.
- **The shape:** a complete source review in the form the last two took
  ([findings/REVIEW-1.0.md](../findings/REVIEW-1.0.md),
  [findings/REVIEW-3.7.md](../findings/REVIEW-3.7.md)) — batched, each batch
  gated, every finding either fixed or written down as deliberate. Plus the
  runs that get skipped between releases because they are slow: the whole
  Roast suite, the whole 2,524-distribution sweep, the conformance matrix.
- **It starts with a list it did not have to find.** v3.21.0's own gates left
  seven findings, written up with repros in
  [findings/OPEN-3.21.md](../findings/OPEN-3.21.md). The first is an engine bug
  that release SHIPS — `flat` over a BOUND array stops spreading (`0ae9387`),
  so `Digest`'s RIPEMD hashes wrong, silently, and only the 59-dist battery
  saw it. The second is the perf baseline moving for a reason still unknown.
  The other four are gates that cannot be read or that pass while leaving
  damage. Fixing the first two is this release's floor, not its ceiling.
- **Why it is its own release:** a review that lands inside a feature release
  is a review nobody can bisect against.

### v3.23.0 — the re-baseline

*Planned in [REBASELINE-PLAN.md](REBASELINE-PLAN.md). This section was left open
when the arc was written — "can be traded for something better before the code
starts" — and it was: the plan is the traded-in version, and it grew one step at
the front.*

- **The number:** every figure this project measures itself by, re-measured
  from **one** run on the machine of record, the baselines re-recorded, and
  **each one naming what produced it**.
- **Why this and not more fixes:** the problem the arc is answering is as much
  a measurement problem as a code problem. `perf-baseline.raku` has drifted
  before and passes silently the whole time — RELEASING.md documents four
  releases where it did. The conformance sweep, the doc-example count, the
  Roast map and the ecosystem verdicts are each carried forward between
  releases and re-derived rarely. After a review that touches everything, a
  gate whose baseline predates the review is not a gate. So: re-record the
  perf baseline with the reasons in the CHANGELOG, re-run the conformance
  halves, regenerate the Roast map, and re-sweep the ecosystem from scratch
  rather than from the last verdict file.
- **The step at the front, which the original scope did not have.** Before
  re-measuring anything, the instruments were reviewed —
  [findings/TOOLS-3.23.md](../findings/TOOLS-3.23.md), **twenty-five defects, all
  fixed**. Three of them would have corrupted this release's own numbers:
  nothing recorded *which* rakupp or *which* Roast a run measured (three rakupp
  binaries answer on PATH here, and the right one is first by ordering alone);
  the documentation-example figure was swept with a v3.20.1 x86_64 build; and
  `perf-guard`'s debt note named one kernel of five, silent about the `rats
  +37.4%` this arc exists to carry. One of them was an engine bug the review
  tripped over — a `:g` match did not carry its subject, so `.from`/`.to` were
  byte offsets on any non-ASCII string.
- **One claim corrected.** v3.22.0 said "every gate detects a planted defect".
  Gate 7 (conformance) **cannot** detect one: none of its four tools has a red
  path — `matrix`, `conformance` and `divergences` contain no `exit`, and
  `typerun` exits only on a timeout or a missing command. It is a report a human
  reads, not a gate that can fail. The figure checker, which *can* fail, is now
  planted and caught.
- **What carries forward:** v3.22.0's source review is 3 files of 83, and its
  two named leads are both places a gate is known not to look — `--exe` failing
  to compile a sub-signature destructure, and a `Range` bound to `@c`.
- **What it buys:** the 1000-module campaign below gets a starting line that
  was measured, not inherited.

### Then: 1000 of 2,524

The standing target after the arc — distributions of the Raku ecosystem
passing **their own** test suites under rakupp, from **637** at the last sweep
to **1000**. The lever is the blocked cohort, not the failing one: 421
distributions never ran their own tests at all because a dependency failed
first, so one fixed dependency releases many dependents at once. A handful of
those dependencies are ours, which makes the first stretch cheaper than the
ratio suggests. Findings drive the work, as they have since v2.0.0 —
[findings/ECOSWEEP-2026-08.md](../findings/ECOSWEEP-2026-08.md), and
[raku.online/modules/ecosystem](https://raku.online/modules/ecosystem/) for
what each distribution did.

## v4.0.0 — Raku that travels (forming, 2026-08-08)

Not yet a settled campaign: the pillars below are decided and written, and the
rest of the list is open. They are about the same thing from different
directions — **Raku++ working somewhere other than a developer's own shell**,
which is the one capability the earlier majors never targeted.

1. **Modules that travel** ([MODULES-PLAN.md](MODULES-PLAN.md)) — `rakupp
   install`, so getting a module no longer requires installing Rakudo and zef;
   and binaries from the `--exe`/`--aot`/`--bundle` family that carry their
   modules with a *guarantee* rather than by luck. The compile modes already
   embed the module graph as serialized ASTs, and it already works — what is
   missing is that a module which cannot be embedded is skipped **silently**,
   so a binary that needs the disk at run time builds without complaint. The
   installer is a Raku program shipped with the release, dispatched by `rakupp
   install`, deliberately *not* C++ in the binary — an `--exe` output and an
   embedded rakupp must carry no HTTP client, index parser or tar reader.
2. **rakupp as a library, embedded in other languages**
   ([EMBED-PLAN.md](EMBED-PLAN.md)) — one C API (`rakupp.h`), then thin
   bindings: Python, Node/TypeScript, Rust, and the existing WebAssembly build
   folded onto the same surface. An embedding already ships — the 95-line
   `extern "C"` shim behind raku.online — which is both the proof the idea
   works and the evidence for what a real API has to add: calling a Raku sub,
   getting a value back, and host functions callable from Raku. Zero `exit()`
   calls in the runtime means the hardest precondition already holds; what
   blocks it is a 1 GiB stack thread, a process-wide SIGPIPE change and an
   owned stdout, all of which must become opt-in.

   **The substrate turned out to be its own plan** ([ABI-PLAN.md](ABI-PLAN.md),
   2026-08-09): the native extension ABI that shipped a day after EMBED-PLAN was
   written is the *harder half* of an embedding API, so the two directions share
   one value vocabulary rather than growing two. Phases A0 (a shared
   `librakupp`), A1 (`rk_call` and rooted handles) and A2 (`rakupp.h` —
   lifecycle, eval, output capture, and Raku.js ported onto it) landed on
   2026-08-10; the bindings are A3.

3. **Raku grammars as a service for other languages**
   ([GRAMMAR-PLAN.md](GRAMMAR-PLAN.md)) — the *reason* someone outside this
   project cares about the two pillars above. "Embed Raku in Python" is
   abstract; "use Raku grammars from Python" is a capability the host language
   has no equivalent of — a regex library gives one pattern, not a composable
   grammar with named rules, inheritance and longest-token dispatch, and the
   ANTLR-shaped alternatives want a code-generation step in your build. The
   grammar stays a `.raku` file and any host-language class is a *generator*
   over that text, never a parallel path — the one decision that keeps the API
   from owing a maintenance debt that grows with Raku itself. It needs **no ABI
   change**: ABI 2's `rk_call` and `rk_root` already cover it, which is why it
   can go first and act as evidence about the ABI rather than waiting on it.

   > **G0 landed 2026-08-11** — and the no-ABI-change claim held exactly: the
   > shim plus the Python binding (`bindings/python/`, ctypes, pure source)
   > shipped without touching `src/` at all. Byte-identical host-vs-rakupp
   > outputs gate in CI (`tools/grammar-smoke.raku`); factors documented at
   > parse 1.0× / tree 1.4× / selective ~31×. Outcome details in
   > [GRAMMAR-PLAN.md](GRAMMAR-PLAN.md). This is also A3's first binding.

**The numbers a stranger can re-measure** will be written here when the
campaign is settled. The candidates today: a program using an ecosystem
module, compiled and run on a machine with no Raku and no module store; Raku
called from a stock CPython, with the module installed by rakupp alone; and a
log-parsing grammar driven from Python, against the same grammar run by
`rakupp` directly.

> **Measured so far (2026-08-12), pillars 2+3:** the log-parsing grammar
> from a stock CPython lands at parse 1.0× / eager tree 1.4× / two-fields-
> per-line ~30× of plain `rakupp` (bindings/python/README.md carries the
> table), and the same corpus is byte-identical from five hosts — Python,
> C++, JS (bun), Go, Rust — in `tools/grammar-smoke.raku`, run in CI. "Raku
> called from a stock CPython" needs no rakupp install at all now: the
> release ships a platform wheel with librakupp bundled
> (tools/build-wheel.sh), verified in a clean venv. G1 diagnostics landed
> (line/column/rule in every host's exception type); G3/G4 stay
> evidence-gated; E5 stays a documented limit.
>
> **Pillar 1 landed 2026-08-12 (same day):** "a program using an ecosystem
> module, compiled and run on a machine with no Raku and no module store"
> is now a STANDING CI GATE, not a claim — t/standalone builds binaries and
> runs them with the store hidden, `--standalone` refuses to build a binary
> that would need the disk, and every compile mode reports what it embedded
> and what it could not, with reasons. "The module installed by rakupp
> alone": `rakupp install` resolves the fez index, verifies checksums,
> runs each distribution's own tests, and writes the shared CURI store —
> a 7-dist graph installed by rakupp loads under Rakudo (727 SPDX licenses
> enumerated by both engines from one store). Outcomes in
> [MODULES-PLAN.md](MODULES-PLAN.md). M6 landed the same day — the checker
> first, then uninstall as gated mark-and-sweep under a real flock, with
> shared-blob survival, reverse-dependency and provenance refusals all
> pinned by the 22-check installer gate. **Open before tagging v4.0.0:**
> B3 resources (measure first — now measurable), and the
> release-procedure sweep.

---

*Keeping this current: when a campaign is decided, add its section here
before the code starts — goal, number, plan links. When it ships, fold in
the outcome and add the row to MILESTONES.md.*
