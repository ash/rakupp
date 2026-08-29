# Plan: v3.23.0 — the re-baseline

*Written 2026-08-29, the day after v3.22.0 shipped. The third and last of the
consolidation releases in
[VERSIONS.md](VERSIONS.md#v3210--v3230--the-consolidation-arc-planned-2026-08-28).
It follows [GATES-PLAN.md](GATES-PLAN.md), and starts from
[findings/TOOLS-3.23.md](../findings/TOOLS-3.23.md) — a review of the
instruments themselves, done before any re-measuring.*

VERSIONS.md wrote this release before the arc began, and left it open on
purpose: *"the choice below is the author's reading of what the arc needs, and
can be traded for something better before the code starts."* Two sittings have
happened since. This is the traded-in version.

## The number

**Every figure this project measures itself by, re-measured from one run on the
machine of record, with the baselines re-recorded and each one naming what
produced it.**

That last clause is new, and it is the whole difference between this plan and
the one VERSIONS.md sketched.

## Why the scope grew a step

The arc's premise is that a gate whose baseline predates the review is not a
gate. v3.22.0 fixed seven instruments and proved eight plants go red. The
obvious next move was to re-measure everything through them.

**A review of the instruments first found twenty-five defects** — written up
with repros and before/after numbers in
[findings/TOOLS-3.23.md](../findings/TOOLS-3.23.md). Three matter enough to
restate here, because each would have corrupted the re-measurement this release
exists to perform:

- **Nothing recorded what was being measured.** Gate commands are written
  `rakupp tools/…`, a PATH lookup; three rakupp binaries answer to that name on
  the machine of record (v3.22.0, v1.0.0, v0.5.1) and the right one is first by
  PATH ordering alone. Measured on one Roast file, v1.0.0 reports `0 / 2` fully
  passing where v3.22.0 reports `1 / 2` — a plausible number with no tell. The
  Roast checkout was unrecorded too, and gate 1 is a *diff against the previous
  release's list*: if Roast moves, the diff blames the engine.
- **The docs sweep ran on a two-release-old engine.** `doc-examples-diff.raku`
  defaulted to `build/rakupp`, which on this box is a v3.20.1 **x86_64** build
  that runs fine under Rosetta. (It sweeps *this repo's* `docs/` — 223 of 299
  blocks matching. README's "official documentation examples" figure is a
  different corpus entirely, produced by gate 7's `typerun.raku`; attributing it
  to this tool was an error made and corrected during the review.)
- **`perf-guard`'s standing-debt note named one kernel of five**, from a
  hardcoded list of the four kernels that existed before 2026-08-09 — so it
  reported `fib +6.5%` and stayed silent about `rats +37.4%`, which is the
  debt RELEASING.md calls the honest headline and the open question the whole
  arc carries.

All twenty-five are fixed. **Parts A and B below are therefore already done**,
and this plan starts at Part C — the measuring itself.

---

## Part A — the instruments, again *(done, 2026-08-29)*

Twenty-five defects fixed, four planted-defect checks added and proved,
one engine bug found and fixed, and one release-gate claim corrected. Full write-up and measurements in
[findings/TOOLS-3.23.md](../findings/TOOLS-3.23.md). Summary of what changed:

| tool | what it does now that it did not |
|---|---|
| `run-roast.raku` | names the rakupp and the Roast revision it measured; `--list=` writes a `.meta` sidecar |
| `perf-guard.raku` | reports the debt for **all nine** kernels, worst first, on both success paths; `--record --for=vX.Y.Z` stamps and verifies provenance; refuses if `%kernels`/`@KERNELS` drift; a FAILED verdict now names the machine load that produced it |
| `run-optbench.raku` | refuses if `tools/optbench/` and `@benches` disagree; PID-scoped scratch |
| `doc-examples-diff.raku` | picks by architecture, refuses cross-arch, names the binary and version |
| `eco-sweep.raku` | reports **measured vs skipped**, shouts when nothing was measured; usage line now runnable |
| `check-figures.raku` | a flag it cannot verify is a failure; checks bucket arithmetic across all snapshots |
| `blame-parse`, `triage-dists`, `ast-opportunity` | no hardcoded developer paths; runnable usage lines |
| **`tools/lib/Gate.rakumod`** | new — the binary choice, the architecture check and the provenance line, in one place instead of six copies |
| **`CMakeLists.txt`** | defaults to the HOST architecture; the documented build command no longer produces a translated binary |
| `prove-gates.raku` | a `figures` plant, and a standing note naming the release check that has no red path |

The engine bug: **a `:g` match did not carry its subject**, so `.orig`,
`.prematch` and `.postmatch` answered the matched text and `.from`/`.to` were
**byte** offsets — correct by accident on ASCII, wrong on any non-ASCII subject.
Fixed in `substSelect`; `t/regression/global-match-carries-its-subject.raku`
passes unmodified under Rakudo.

---

## Part B — close what the review left open *(done, 2026-08-29)*

All five closed. Full detail in
[findings/TOOLS-3.23.md](../findings/TOOLS-3.23.md) Part 2; the three that
changed the picture:

- **O3 had a root cause nobody had looked for.** `build/` came back x86_64 from
  a clean rebuild, because **cmake itself is x86_64** here — an Intel-Homebrew
  binary at `/usr/local/bin/cmake`, ahead of `/opt/homebrew` on PATH. CMake
  infers the target from its own process, so **README's documented build command
  produced a translated binary on the machine of record**, and had all along.
  That single fact is upstream of three separate gate defects. Fixed in
  `CMakeLists.txt` by asking the kernel instead of cmake.
- **O1 is not "nobody planted gate 7" — gate 7 cannot fail.** None of its four
  tools has a red path: `matrix`, `conformance` and `divergences` contain no
  `exit`, and `typerun` exits only on a timeout or a missing command. It is a
  report a human reads. `prove-gates --list` now says so.
- **O5's answer was that load average is the wrong instrument.** Two measured
  signals replaced the threshold question: the gate's own run-to-run spread, and
  whether the slowdown is *localized* — because contention moves every kernel
  together while a regression moves the ones it touched.

Plus one thing the plan had not asked for: **RELEASING.md's post-release site
instructions were split by cadence.** What carries the version or the timeline
(the WASM engine, the hand-written figures, the roast map, snapshot →
dashboard) runs at **every** release; what carries a measurement (`/spec`,
`/spec/rules`, `/modules`, the content sites) runs **on demand** — and **all of
it, with the sweeps re-run first, on a major version**. Writing it down found
three wrong instructions, including that `rakujs/build.sh` copies nothing and
that three files, not two, have to be moved by hand.

**Two things remain, and both are somebody's call rather than a fix:**

### B1. Give conformance a real pass/fail criterion *(the wording is done)*

**Decided 2026-08-29: reword now, criterion later.** README, CHANGELOG.md and
findings/GATES-3.22.md now say *"every gate that can fail detects a planted
defect — 8 plants across the 7 gates with a red path"*, and the CHANGELOG entry
carries a correction note rather than silently rewriting a shipped release's
claim.

What remains is the substantive half, and it lives in the **raku.online** repo:
give conformance a red path. The shape is already available — `history.jsonl`
records a divergence count per release, and RELEASING.md documents the ±5 flap
band — so a checker can compare the new count against the last line and exit
non-zero outside the band. Plant it in `prove-gates` when it exists, and the
claim becomes "every gate" without a qualifier.

### B2. Clear the Roast checkout

28 untracked files, 27 of them residue from 14–16 July, before `run-roast`'s
per-run scratch directory landed. The harness now reports them; removing them
was blocked by a permission classifier as a destructive operation outside the
project.

```bash
git -C /Users/ash/roast clean -f
```

Do this **before** Part C: the provenance line names a Roast revision, and a
revision does not describe a checkout with 28 files in it that the revision
never had.

## Part C — the re-baseline itself

The release's number. **One sitting, one machine, one set of inputs**, with
gate 0 of RELEASING.md run first so that every figure below can name what
produced it.

1. **Roast**, three runs at `--workers=4`, the repeating profile quoted, both
   lists and the `.meta` sidecar archived and committed, and the diff taken
   against v3.22.0's **union**. Record the Roast revision in the CHANGELOG — if
   it differs from `b2cbe8a42` (2026-06-12), say so, and do not read the list
   diff as an engine change until the moved files are accounted for.
2. **The local suite**, `t/run.raku`. 580 at the time of writing.
3. **Perf**, re-recorded with `--record --for=v3.23.0`, and the reasons in the
   CHANGELOG. Quote `vs best`, not `delta` — at v3.22.0's numbers that column
   reads `rats +36.2%`.
4. **optbench** and **the slim pair**, both legs.
5. **The 59-dist battery**, against the committed baseline.
6. **Conformance**, both halves, and the ±5 flap band stated rather than read
   as progress.
7. **Both example sweeps, which are different corpora and must not be quoted for
   each other.** `tools/doc-examples-diff.raku` covers this repo's own `docs/`
   (299 blocks); README's *"Official documentation examples byte-identical"* row
   is gate 7's `typerun.raku` over the official Raku docs (~1,495 `=begin code`
   blocks), recorded as `examples.ok` in raku.online's `history.jsonl`. Note
   that the README figure therefore comes from the one release check with **no
   red path** — see B1.
8. **The ecosystem**, re-swept **from scratch**: a fresh `--out`, not the
   previous verdict file. `eco-sweep` now says `N measured, M skipped` so this
   is checkable rather than assumed.

Then the figure refresh, `check-figures` with all three flags, and the
after-the-tag list in RELEASING.md — which now carries its own checklist of the
steps that have historically been skipped.

**What this buys the campaign after it:** the 1000-of-2,524 target gets a
starting line that was measured, not inherited.

---

## Part D — the review, still 80 files of 83

v3.22.0's Part D pulled one thread — `isList` lost at a binding boundary — and
found **three instances of one defect in one function across two releases**, the
first of which shipped a silently wrong cryptographic hash. It states plainly:
*"Not yet reviewed: everything else."*

That carries forward. Two concrete leads are already written down and are the
natural entry points, because both are places a gate is known not to look:

- **`--exe` cannot compile a sub-signature destructure.**
  `sub k([$b, @c]) { @c.elems }` emits C++ referencing an undeclared identifier.
  Pre-existing, narrowed by elimination (plain `@c`, named `:@c` and
  `@a [$x, *@y]` all compile). **No gate was looking**: `slim-diff` files such
  programs under "does not compile at all: not this gate's business", and
  `run-optbench` only ever compiles its own nine kernels — so the code generator
  can fail on a valid construct with every gate green, which is the exact failure
  mode RELEASING.md says gate 4 exists to prevent.
- **A `Range` bound to `@c` answers `Array`**, where Rakudo keeps `Range` — the
  one form of 68 still diverging after A1.

Part D is open-ended by nature. The honest done-when is the one v3.22.0 used:
whatever is reviewed is written up, and what is not reviewed is *said* to be
not reviewed.

---

## Starting cold

1. Read [findings/TOOLS-3.23.md](../findings/TOOLS-3.23.md) — the twenty-five
   fixed defects with their repros, and Part 2 for the three root causes that
   closing the open items exposed.
2. Run **gate 0** of [RELEASING.md](../RELEASING.md) before anything else.
   Everything in Part C depends on knowing what is being measured.
3. Part B is done. Its two leftovers are B1 (a decision, in another repo) and
   B2 (`git clean` in the Roast checkout) — do B2 first, since Part C's
   provenance depends on it.
4. Part C is one sitting on purpose. A figure measured in a different sitting
   from its neighbours is the thing this arc exists to stop.
5. Standing gates apply to every batch, as always: zero Roast regressions, the
   local suite, `perf-guard --check`, methodology in
   [COUNTING.md](../../status/COUNTING.md).

## The trap that is still live

If the machine returns to the performance regime it was in before 2026-08-27,
**gate 3 will read ~25% faster than baseline on `rats` and say nothing at all**.
A large negative delta on the five movers is not a win to announce; it is
[GATES-3.22.md](../findings/GATES-3.22.md) Part B resolving itself, and it
should be investigated rather than recorded. `best` keeps every earlier figure
so `vs best` carries the debt in the open — and as of v3.23.0 the gate's summary
line actually names all five kernels carrying it.
