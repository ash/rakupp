# Cutting a release

The gates below are the whole checklist. They exist because each one has caught
something real at least once; the notes say what.

## The gates

Run these in order. Every one must pass **before** the version is bumped.

### 0. Say what is being measured, before measuring it

Two inputs decide every number below, and until v3.23.0 neither was recorded.

**The binary.** Every gate command here is written `rakupp tools/…`, which is a
PATH lookup, and gates 1 and 2 test `$*EXECUTABLE` — whichever binary ran the
harness. On the machine of record three answer to that name:

```bash
which -a rakupp
```

```
/Users/ash/raku++/build-arm64/rakupp     # v3.22.0
/usr/local/bin/rakupp                    # v1.0.0   (Homebrew Cellar, 22 July)
/opt/homebrew/bin/rakupp                 # v0.5.1
```

The right one is first **by PATH ordering alone**. Measured on a single Roast
file, v1.0.0 reports `0 / 2` fully passing where v3.22.0 reports `1 / 2` — a
plausible number, from a two-year-old engine, with nothing in the output to say
so. `run-roast.raku` and the perf/optbench/doc-example tools now open by naming
the binary and its version; **read that line** rather than trusting the shell.

**The Roast checkout.** Gate 1 is a *diff against the previous release's file
list*. Roast is upstream and moves, so if it changed between two releases, files
appear and disappear and the diff charges every one of them to the engine. Note
the revision before starting, and put it in the CHANGELOG entry:

```bash
git -C "$ROAST" rev-parse --short HEAD && git -C "$ROAST" log -1 --format=%ci
```

`run-roast.raku` records it too — in its banner, and in the `.meta` sidecar
written beside `--list=`. A release whose Roast revision differs from its
predecessor's must say so, and must not read the list diff as an engine change.

**Start from a clean checkout.** The banner also counts untracked files, because
a revision does not describe a tree that has files in it the revision never had.
The per-run scratch directory catches tests writing *relative* paths, but not one
writing beside its own `.t` file — `S16-io/lines.t` does
`$*PROGRAM.sibling('lines.testing')`, an absolute path in an upstream tree we do
not patch — so the harness reports what it left instead:

```bash
git -C "$ROAST" clean -n      # look first
git -C "$ROAST" clean -f
```

### 1. Roast — no regression

```bash
ROAST=/path/to/roast rakupp tools/run-roast.raku --workers=4 \
    --list=docs/status/roast-lists/vX.Y.Z.list | tee roast.txt
```

Compare against the previous release's figure in [CHANGELOG.md](../../CHANGELOG.md).

`--list=` writes the fully-passing file paths as **data** — collected as each
file is judged, not reconstructed from the printed output. **Commit that file**:
it is this release's entry in [`docs/status/roast-lists/`](../status/roast-lists/),
and it is the baseline the NEXT release diffs against. v3.20.1 archived nothing,
so v3.21.0's diff fell back to a development run found in `rc-work/` — which is
`.gitignore`d scratch and was never that release's own measurement.

Keep `roast.txt` too — the per-file `[PASS] 3/3 …` lines are what step 6 below
feeds to `gen-roast-map.raku`, and re-running the suite just to get them back
costs an hour.

`--list=` also writes `vX.Y.Z.list.meta` beside it: the rakupp version and path,
the Roast revision and path, the file count and the worker count. **Commit that
too.** It is the only record of what produced the list, and gate 0 says why.

Read the **denominators**, not just the pass count. A file that dies removes its
tests from *both* sides of "tests that ran", so a real regression can leave the
percentage untouched — a falling denominator means a file stopped emitting TAP.
See [COUNTING.md](../status/COUNTING.md), which works through exactly that case.

The suite flaps by a file — 624↔625 fully passing and 10↔11 timeouts on the same
build — worth about 20 assertions either way. Take three runs and use the
repeating profile, not the best one seen.

**If the three runs have no repeating file count, take a fourth.** v3.0.1's
first three gave 595 / 593 / 594; a fourth broke the tie at 594, and quoting
the 595 would have been exactly the thing this rule exists to prevent. The
v3.0.0 notes published the *top* of their band (197,191 of 197,186–197,191)
and it did not reproduce on any later machine — which is how a release ends up
looking like a regression when the software improved.

**The gate is the file LIST, not the total.** Diff the fully-passing files
against the previous release; the assertion count moves with machine load and
the timeout profile, so it cannot tell you whether anything broke:

```bash
cd docs/status/roast-lists
comm -23 vPREV-union.list vNEXT-union.list    # regressed — must be empty
comm -13 vPREV-union.list vNEXT-union.list    # gained
```

Archive TWO files: `vX.Y.Z.list` from the run whose count is the repeating
profile, and `vX.Y.Z-union.list` holding every file that passed in ANY run.
**Diff the unions.** The `S17-*` concurrency tests sit near the 10s timeout and
flap under `--workers=4` — v3.22.0's three runs gave 642 / 642 / 644, and every
file that moved was `[TIME]` in the run that lost it and passed when run alone.
Diffing single runs turns that flap into reported regressions.

Both files are written sorted, so `comm` needs no `sort`. **Do not rebuild these
lists with `grep '[PASS]' | awk '{print $NF}'`** — that is how the gate used to
work and it was not reliable. At `--workers=4` the children's TAP diagnostics
were spliced into the parent's status lines, consistently four a run:

```
  [PASS]    4/4  S03-smart# Failed test 'smartmatch with list RHS …' at …
```

The tallies survived it (they are computed from captured output), but the LIST
did not: `$NF` on such a line is not a path, so four files a run silently lost
their identity and read as regressions — and the site's roast map inherited the
undercount, writing 639 where the run had measured 643. The cause was an
untapped `Proc::Async` stderr, which is INHERITED and so wrote straight into the
parent's own stream; fixed in v3.22.0. The `--list=` file exists so that the
gate no longer depends on that output's framing at all.

At v3.0.1 that showed zero regressions and three gains against a headline
figure 111 assertions *lower* than v3.0.0's published number. Without the file
diff there was no way to tell the two apart.

### 2. The local suite

```bash
rakupp t/run.raku
```

All checks, including every `t/regression/` case. A regression file passes iff it
exits 0 with `PASS` as its last line — it is not TAP.

**A case that needs an installed module must declare `#?requires Foo::Bar`.**
This suite runs on your machine, where zef has installed things a CI runner has
never seen, so a missing declaration means the case passes here and fails
everywhere else. `t/regression/module-compat-cluster.raku` shipped in v3.0.0
that way and turned the Linux and macOS jobs red. Probing from inside the file
is not an option when the module supplies a trait — the import has to happen at
compile time. The runner already understands the declaration; use it.

### 3. Performance — no regression

```bash
rakupp tools/perf-guard.raku --check      # exits non-zero on a regression
```

**A release must not ship a performance regression.** This gate compares the
build against [`tools/perf-baseline.raku`](../../tools/perf-baseline.raku), which
holds the last release's measured times, and fails if any kernel is more than
`tolerance-pct` (5%) slower.

Two numbers are tracked per kernel:

- `baseline` — the last release. This is what the gate enforces.
- `best` — the fastest ever measured, and when. Not enforced, but reported, so
  that a regression which was once accepted does not quietly become the new
  normal.

The gate has three outcomes, not two:

| exit | meaning |
|---|---|
| 0 | no kernel is more than `tolerance-pct` slower |
| 1 | a regression, confirmed by re-measuring the failing kernels |
| **2** | **inconclusive** — the machine is loaded, so the timings say nothing |

That third state exists because every false alarm this gate has raised was a
background process, not the build: Spotlight indexing, `ecosystemanalyticsd`,
WindowServer, and once four `pandoc` jobs at 50% each. Treat exit 2 as "run it
again on an idle machine", never as a pass.

Three things have to be true before the gate accuses the code, and as of v3.23.0
two of them are **measured** rather than inferred:

1. **The failing kernel is re-measured.** A real regression reproduces.
2. **Its runs must not span more than the tolerance.** `measure()` reports the
   spread of its own runs beside the minimum, and the table shows it. If a
   kernel's three runs differ by more than the 5% it is enforcing, the reading
   cannot support a verdict either way — the difference being blamed on the
   build is smaller than the difference between two runs of the same build.
3. **Fewer than half the gated kernels are over tolerance.** A code regression
   is localized; contention is not. Six of nine kernels failing at once, across
   arithmetic, calls, strings, hashing and regex — which share no code path — is
   the machine's signature, not a change's.

Signals 2 and 3 were added because the load check alone was not enough, and the
gap was measured rather than argued: at load 4.33 on 8 cores (54%, *under* the
60% cut) an **unmodified** binary failed this gate with five kernels 7.6–10.3%
slower, and the re-measure confirmed rather than cleared it. Both older defences
were built against a transient — a daemon waking up — and a sustained load is
not transient, so it reproduces straight through them. A FAILED verdict now also
prints the load that produced it, which only the inconclusive path used to do.

If the gate genuinely fails, the options are to fix it, or — when the cost is
understood and deliberate — to re-record the baseline and **say why in the
CHANGELOG**:

```bash
rakupp tools/perf-guard.raku --record
```

Re-recording without an explanation is how the debt gets lost. It has happened
twice: an 8–22% interpreter regression slipped through the v0.7.1→v0.9.0 cycle
(and is why `loopsum`/`hash` were added to the guard at all), and an ~11.6% `fib`
regression accreted across v1.2.x and was only found by re-measuring for a
release five days later. Both were gradual, spread across the dispatch path, with
no single hotspot — which is exactly the shape a per-change eyeball misses and a
recorded baseline catches.

Absolute times are machine-specific. The gate compares two builds on the *same*
machine; on a new one, re-record before trusting a failure.

The command above needs no `RAKUPP=` on the machine of record. It used to: the
default was `build/rakupp`, which on that box is an **x86_64** build sitting
beside a native `build-arm64/`, so the gate as documented reported
`INCONCLUSIVE — … is x86_64 on a arm64 host` instead of measuring, and every run
of the v3.21.0 sitting needed `RAKUPP=build-arm64/rakupp` to get a number at
all. Since v3.22.0 the default searches `build/`, `build-arm64/` and `./rakupp`
and picks the first built for THIS architecture, saying so in its banner when
that is not the first candidate. An explicitly set `RAKUPP` is still honoured
exactly as given — that is what the A/B usage depends on.

**What this gate does and does not mean, as of v3.22.0.** The baseline moved
between 2026-08-27 and 2026-08-29 — five of nine kernels up 8–26%, four
unchanged — and after two sittings of work **the cause is not known**. Ruled out
by measurement: the code, load, OS and toolchain, power and thermal state, the
kernels themselves, a different machine, build nondeterminism (two builds of one
commit are byte-identical), binary layout (deliberate perturbation buys ≤3.5%),
the macOS nano allocator, and the min-of-3 metric (twenty runs span 5%). The
write-up is in [findings/GATES-3.22.md](../findings/GATES-3.22.md).

Two consequences for reading this gate:

- **The `vs best` column is the honest headline, not `delta`.** `baseline` is
  the re-recorded number, so `delta` reads ≈+1% while `rats` is 36% behind the
  fastest ever measured on this machine. Quote `vs best` when reporting.

  Until v3.23.0 the gate's own summary line could not tell you that. It listed
  the standing debt from a hardcoded `<fib asg loopsum hash>` — the four kernels
  that existed before the string, call and Rat kernels were added — so it named
  `fib +6.5%` and stayed silent about `rats +37.4%`. It now reports every kernel,
  worst first. It also used to skip the note entirely when a kernel read over
  tolerance and then re-measured clean, which with a 5% tolerance over a 1.7%
  run-to-run and 3.5% layout floor is a common path, not a rare one.
- **The gate is blind in one direction.** If the machine returns to its old
  form, `rats` will read ~25% FASTER than baseline and the gate will say
  nothing. A large negative delta on the movers is not a win to announce; it is
  this open question resolving itself, and it should be investigated, not
  recorded.

The same measurements put a floor under the tolerance: ~1.7% run-to-run on an
identical binary, plus up to ~3.5% from layout alone. A 5% tolerance is not much
above that, so a 6% reading is a re-measure, not a finding.

Two known blind spots, both live as of v3.0.1:

- **The baseline drifts out of date silently.** It went four releases without
  being re-recorded (`2026-07-29 (v1.5.1)` while v3.0.0 shipped), and it passes
  the whole time, which is why nobody noticed. Re-record deliberately at a
  release (and say so in the CHANGELOG), or the "no regression since last
  release" claim quietly stops meaning that.

  **Do not quote the recorded date from this file — read it from the output.**
  This note has gone stale twice: it was quoted as fact during the v3.5.0 gates,
  and it then sat at `2026-08-11 (v3.14.0)` while the baseline said
  `2026-08-29 (v3.21.0)`. `--check` prints the real one in its `gate:` line.

  The mechanism behind all of that is fixed as of v3.23.0: `--record` used to
  leave the `recorded` field alone while `--check` printed it, so the provenance
  was hand-maintained and nothing moved when it was forgotten. It now stamps the
  field, verifies it reads back, and names the binary it measured. Pass the
  release it is being recorded for — gates run *before* the version bump, so
  without it the stamp names the previous release:

  ```bash
  rakupp tools/perf-guard.raku --record --for=vX.Y.Z
  ```
- **There is no string kernel.** `fib`/`asg`/`loopsum`/`hash` are arithmetic,
  hashing and control flow. v3.0.1 replaced the entire string representation
  in `Value` and the gate could not have seen a regression either way. If a
  change touches strings, measure it separately and put the numbers in the
  CHANGELOG — and consider adding the kernel.

### 4. The compiler agrees with the interpreter

```bash
rakupp tools/run-optbench.raku            # exits non-zero if any mode disagrees
```

Checks that the interpreter, `--exe`, `--exe -O` and Rakudo produce *identical*
output on every kernel before reporting a timing — so it catches an optimisation
that changed an answer, not just one that got slower.

The code generator is a second implementation of the language, so a bug can live
there and nowhere else: [#8](https://github.com/ash/rakupp/issues/8) and the
declared-type loss found while fixing
[#9](https://github.com/ash/rakupp/issues/9) were both compiler-only.

### 4b. Slim binaries behave identically (v3.14+)

```bash
rakupp t/slim/run.raku                    # the negative suite: cuts throw, never lie
rakupp tools/slim-diff.raku               # the differential: --slim vs full,
                                          # byte-identical out/err/exit per program
```

The differential builds every program in `t/regression` and `examples` twice
and compares observable behaviour; a cut that changes anything fails the
release (SLIM-PLAN, defence 5). Programs that disagree with THEMSELVES
(`rand`-seeded, timing-dependent) are reported as nondeterministic rather
than judged — read that list, it should be short and unsurprising. The
module-battery leg runs at the release too:

```bash
rakupp tools/slim-diff.raku <battery program paths>
```

### 5. A second toolchain

```bash
cmake --build build-gcc16 -j8            # or whatever GCC is to hand
```

Gates 1–4 all run one compiler. v3.0.0 shipped C++ that Clang and GCC accepted
and MSVC would not link: a block-scope `extern` inside `namespace rakupp` names
`rakupp::…` under Clang and GCC but `::…` under MSVC, so the Windows job failed
while the MinGW job (GCC) stayed green. Building GCC locally costs minutes and
catches the divergences that are not MSVC-specific; MSVC itself only exists in
CI, which is why the pre-tag CI check in step 4 below is not optional.

### 6. The distribution bar

```bash
RAKUPP=/path/to/rakupp raku tier2/run-dist-tests.raku    # in raku-module-battery
```

The battery is real third-party code, and it catches what the suites above
cannot. At v3.0.1 it was the *only* thing that noticed `Supply.wait` had never
blocked — returning `True` immediately for every Supply, 0 ms against Rakudo's
317 ms — because Log::Async uses it as a barrier and nothing in Roast or
`t/` did.

**Run it alone.** At v3.25.0 the battery ran beside the slim differential and
the conformance sweep (load 5–6 on 8 cores) and reported Digest PASS → DIFF on
`t/ripemd.t` twice; alone, on a quiet box, the same commit passed it in 22 of 22
runs under every condition that could be recreated, including a `-j8` rebuild
at load 9–11. The runner discards its children's output, so a verdict taken
under concurrent gates cannot be diagnosed afterwards — only re-run. Gates 1, 4b
and 6 all spawn per-file children with wall-clock caps; run them one at a time.

**Run this after any performance work, not just before a release.** A change
that makes the interpreter faster is a change to every race in the system: the
same fault had been latent for releases and only became visible when the main
thread got fast enough to lose the race it used to win. The symptom was a test
reporting `expected: ["one", "three"]` against `got: ["one", "three"]`, which
reads as an equality bug and is not one.

Commit the battery repo with `battery: N/59` in the **subject** — the dashboard
mines that string and nothing warns when it goes stale.

### 7. Conformance — only before a release

```bash
# in the raku.online checkout, sites/spec
rakupp tools/typerun.raku --rakupp=/path/to/rakupp --oracle=raku   # types
rakupp tools/matrix.raku  --rakupp=/path/to/rakupp --oracle=raku   # operators
rakupp tools/conformance.raku && rakupp tools/divergences.raku     # reports
```

Both halves feed <https://raku.online/spec/rules/divergences/>. This is slow
(~25 min for the types sweep), which is why it runs per release rather than per
batch.

**README's documentation-example figure comes from here.** The row *"Official
documentation examples byte-identical on both engines"* is `typerun.raku`'s
`examples.ok`, swept over the official Raku docs (~1,495 `=begin code` blocks)
and recorded in raku.online's `src/data/history.jsonl`. It is NOT
`tools/doc-examples-diff.raku`, which sweeps this repo's own `docs/` — a
different, much smaller corpus (223 of 299 blocks matching, measured
2026-08-29). Do not quote one for the other; that mistake was made during the
v3.23.0 review and is written up in
[findings/TOOLS-3.23.md](../findings/TOOLS-3.23.md).

**This is the one entry on this list that cannot fail** — which means that
headline figure is produced by the only release check with no red path. None of the four tools
has a red path: `matrix.raku`, `conformance.raku` and `divergences.raku` contain
no `exit` at all, and `typerun.raku` exits non-zero only on a timeout (124) or a
missing command (127) — infrastructure, not conformance. Measured 2026-08-29:
`conformance.raku` and `divergences.raku` both exit 0.

So read it as a **report**, not a gate. The pass/fail judgement is yours: compare
the divergence count against the previous release's, allow the ±5 flap band
below, and account for anything outside it before tagging. `prove-gates` lists
this under "Release checks with NO plant, and why" rather than pretending to
cover it, and until conformance gets a committed baseline and a real criterion
(the tooling for which lives in the raku.online repo), the honest form of
v3.22.0's claim is "every gate **that can fail** detects a planted defect".

This leaves `src/data/typerun.raku` and `src/data/matrix.raku` rewritten but
**unpublished** — step 6 in the next section is what turns them into pages.

The count has a **±5 flap band**: the `Set`/`Bag`/`Mix`/`Map` examples move in
both directions between runs of identical code, because Rakudo randomizes hash
iteration order per process. Do not read a ±5 move as progress.

## Picking the number

**Plain, monotonically increasing versions. No cute numbers.**

`v3.14.0` was a pi joke, tagged 2026-08-11 between `v3.1.0` and `v3.5.0`. It
cost more than it was worth, in three separate places, and every one of them
was found after the fact:

- **Homebrew** compares versions component by component as integers, so it
  ranks `3.14.0` **above** `3.7.0`. The tap was bumped to 3.7.0 at that
  release and it is correct for a fresh `brew install` — but anyone who
  installed while the formula pinned 3.14.0 is never offered the upgrade,
  because brew believes they already have something newer. There is no
  formula-level fix: Homebrew has no epoch, and `revision` only breaks ties
  within one version.
- **The dashboard** sorted `git tag --list v*` as STRINGS, which put `v3.14.0`
  between `v3.1.0` and `v3.5.0` — the right slot, by luck, since that is where
  it belongs by date. A `v3.20.0` would have string-sorted *before* `v3.5.0`
  and drawn the newest release three points from the end of every chart.
  Fixed at v3.7.0 (`gen-dashboard.raku` now orders by the ref's commit
  timestamp, with a numeric version tiebreak for two tags on one commit), but
  the trap is generic: anything sorting version strings has it.
- **Reading the repo.** `v3.14.0` looks like the newest v3 tag in every
  listing that sorts lexically or numerically, and it is four releases old.

So:

1. **The next release is at least `3.20.0`** — it has to clear `3.14.0`
   numerically before Homebrew will offer an upgrade to anyone sitting on that
   build. `3.8.0` would be correct by every other measure and still leave
   those users stranded.
2. After that, increment normally. A minor bump per release, a patch for a
   fix-only release.
3. If you ever need to know whether one tag is older than another, read the
   **tag date** against [CHANGELOG.md](../../CHANGELOG.md). Do not read the
   number.

## Then

1. Bump `project(RakuPP VERSION …)` in [CMakeLists.txt](../../CMakeLists.txt),
   the matching `(version "X.Y.Z-git")` in
   [.guix/modules/rakupp-package.scm](../../.guix/modules/rakupp-package.scm)
   (the Guix package, PR #6), and `version = "X.Y.Z"` in
   [flake.nix](../../flake.nix) (the Nix package, issue #5).

   Then **rebuild and check it**:

   ```bash
   cmake -S . -B build-arm64 && cmake --build build-arm64 -j8
   ./build-arm64/rakupp --version        # must say the new version
   ```

   v3.0.0 skipped this step entirely and nothing noticed: every gate passes on
   a binary that reports the wrong version, the tag was cut, and the release
   shipped announcing `2.0.0`. One command would have caught it.

   Read the whole output, not just the first line. The `Build` line carries
   `git describe`, so **it must not end in `-modified`** — that suffix means
   the binary was built from a tree with uncommitted changes, and nobody can
   ever reconstruct what it actually contains. Commit or stash first and
   rebuild. A released binary must name a commit that exists:

   ```bash
   ./build-arm64/rakupp --version | grep -q -- -modified && echo 'REBUILD: uncommitted changes are in this binary'
   ```
2. Write the CHANGELOG entry — measured numbers, not projected, with the
   methodology link. Note anything deliberately left open.
3. Refresh the figures in README, `docs/status/ROAST.md`, `docs/status/COUNTING.md`,
   `docs/guide/FEATURES.md`, `docs/guide/GUIDE.md`, `docs/guide/HIGHLIGHTS.md`, `docs/guide/OVERVIEW.md`
   and `docs/status/ROADMAP.md` — all from **one** run, so they agree with each other.
   Add the release's row (and phase note) to `docs/status/MILESTONES.md` — its own
   footer asks for this on every tag, and it sat four releases stale (v2.0.0 →
   v3.14.0) before anyone noticed, because nothing in this list said so.
   `docs/status/BENCHMARKS.md` too if the benchmarks were re-run.

   **Re-measure BENCHMARKS.md BEFORE the tag, not after.** raku.online's
   `gen-dashboard.raku` mines this file *as committed at each ref*, so a tag
   draws whatever it said when it was cut and no later edit can move that point.
   v3.24.0 shipped with tables taken at `v3.23.0-45-gb6905bf` — before the
   eight-carry-chain multiply — so the published `bigint` series read ~31 ms for
   an engine that does it in 5.8, and the release's own point still carries the
   pre-fix prose. Only `main` moved when the file was corrected afterwards.

   Two more traps behind that one, both found the same day:

   - **`bench-backfill.tsv` also carries `main` rows.** They OVERRIDE the mined
     numbers, so correcting the doc alone is not enough: leave them and the
     chart drops at the release and jumps back at `main`, drawing a regression
     that never happened. Update the tag's rows *and* `main`'s.
   - **The tag's rows come from the released ARTIFACT**, not a local build —
     that is what makes releases comparable with each other. Download
     `rakupp-macos-universal.tar.gz`, check it against its `.sha256`, and
     measure that. `main`'s rows come from the local build at the tag.

   Then **prove** they agree, because a half-landed refresh looks exactly like
   a finished one — at v3.0.0 two files carried the new numbers and six kept
   the old, and the timeout count read 5 in one file and 10 in another:

   ```bash
   grep -rhoE '19[0-9],[0-9]{3}|2[01][0-9],[0-9]{3}|[0-9]{3} / 1,462' \
     README.md docs/status/ROAST.md docs/status/COUNTING.md docs/guide/*.md \
     docs/status/ROADMAP.md | sort | uniq -c | sort -rn
   ```

   The current figure and its three denominators should dominate the tally —
   at v3.0.1, `197,080` ×21 over `218,613` / `215,794` / `203,502`, and
   `594 / 1,462` ×8. Everything with a low count then has to be *accounted
   for* rather than assumed wrong: the other runs of the release band and the
   previous release's numbers legitimately appear once or twice in the prose
   that documents them. What must not appear is a fourth value in the same
   role as the headline — that is a file the refresh missed.

   That grep has a **blind spot**, and it cost v3.21.0 two stale tables: a count
   written as a bare cell carries no denominator, so the pattern never reaches
   it. Run the checker too, which reads the headline figures structurally:

   ```bash
   rakupp tools/check-figures.raku --expect=NNN --examples=NNN --version=X.Y.Z
   ```

   Pass all three. `--expect` alone checks one cell of a row family, and that is
   how v3.22.0's refresh reported "all headline figures agree at 642" while
   README.md still called v3.21.0 the current release, still labelled its
   comparison column `v3.21.0`, and still carried the previous release's
   documentation-example count — in the same table it had just checked. A table
   labelled with one release holding another's numbers is worse than a stale one.

   It compares the standing `| **Fully passing** | … |` cells in
   `docs/status/ROAST.md` and `docs/guide/GUIDE.md` against README's comparison
   row, and fails if they disagree with each other or with `--expect`. Those two
   cells are exactly what the grep missed: ROAST.md sat at **638** while the tag
   read 643 — and that row is the one raku-spec's `gen-dashboard.raku` parses, so
   the dashboard published the stale number. GUIDE.md's table was **v1.x-era**
   (528 fully passing, 238 no-TAP, 12 timeouts), sitting directly beneath
   assertion figures the refresh had just updated.

   The cells must stay bare: `gen-dashboard.raku` strips every non-digit from
   that cell, so writing `643 / 1,464` there to make the grep reach it would make
   the dashboard read `6431464`. That is why this is a checker and not a
   reformat.

   Two things it learned in v3.23.0, both from being wrong first:

   - **A flag whose figure it cannot find is now a failure, not a pass.** It used
     to check only what its rules happened to reach, so
     `--examples=950 --version=3.23.0` exited 0 against a tree stating neither —
     the same false comfort as the grep it replaced, one layer up.
   - **It checks the file-bucket arithmetic**: fully + partial + no-TAP + timeout
     must equal the denominator. Unlike everything else in that tool this applies
     to historical snapshots too, because it is internal consistency rather than
     currency — a v3.21.0 paragraph may carry v3.21.0's numbers, but they still
     have to add up to 1,464. It was written because two figures survived the
     v3.22.0 refresh that nothing else could see: `docs/guide/FEATURES.md`
     carried v3.21.0's `685 partial` beside v3.22.0's `642` (summing to 1,463),
     and `docs/status/ROAST.md`'s v3.21.0 snapshot opened with `642` in a
     paragraph whose next sentence said the count repeats at 643.
4. **Check CI is green on the exact commit you are about to tag**, then tag,
   then publish.

   ```bash
   gh run list --limit 3            # the push build for this commit
   ```

   v3.0.0 was tagged onto a commit whose `windows-x64`, `linux-x86_64` and
   `macos-universal` jobs all failed, and the tag was *moved* after the release
   had already been published. The three failing jobs never re-uploaded, so the
   release served binaries from two different commits — a Linux, macOS and MSVC
   build from *before* the last batch, next to an OpenBSD and MinGW build from
   after it. Nothing about the release page showed this; the asset timestamps
   were the only tell.

   **Never move a tag that has already been published.** Cut a new patch
   version instead.
5. **Bump the Homebrew tap** —
   [ash/homebrew-rakupp](https://github.com/ash/homebrew-rakupp), a separate
   repository that nothing here and nothing in CI touches.

   ```bash
   # after the release assets exist
   curl -sL https://github.com/ash/rakupp/releases/download/vX.Y.Z/rakupp-macos-universal.tar.gz.sha256
   curl -sL -o /tmp/src.tgz https://github.com/ash/rakupp/archive/refs/tags/vX.Y.Z.tar.gz
   shasum -a 256 /tmp/src.tgz
   # edit Formula/rakupp.rb: both urls, both sha256s, and version "X.Y.Z"
   brew fetch --formula ash/rakupp/rakupp     # must print ✔︎ Formula rakupp (X.Y.Z)
   ```

   `brew install rakupp` is the **first** install route the README and
   raku.online offer for macOS, and the tap sat on 1.1.0 until v3.0.1 — eleven
   releases, roughly three weeks, handing every `brew` user a build from 24
   July. Nothing failed; the formula simply kept pointing at an old tag.

   Two traps. The formula carries the version in **three** places (the source
   `url`, the `on_macos` `url`/`sha256`, and `version`) and a partial edit
   installs a mismatched binary rather than erroring. And the checkout under
   `/usr/local/Homebrew/Library/Taps/ash/homebrew-rakupp` is a **separate
   clone** from your working copy — `brew audit`/`brew fetch` read *that* one,
   so testing your edit means copying the file in (then `git checkout --` to
   leave it clean) or pushing first.
6. **Republish raku.online.** This is the step that gets forgotten, because the
   release is already out by then and everything looks finished. It isn't —
   until this runs, the site announces the new version while showing the
   previous release's engine, graphs and measurements.

### What always runs, and what runs on demand

Split them by what the thing actually carries. Anything carrying the **version
number or the release timeline** moves at every release, because the release is
what changed it. Anything carrying a **measurement** only moves when the sweep
behind it was re-run — regenerating it from stale inputs writes a new date onto
an old number, which is worse than leaving it.

**On a major version (`X.0.0`), all of it runs**, and the sweeps that feed the
second group are re-run first. A major is the release people actually go and
look at, and it is the wrong one to have a two-release-old ecosystem table on.

| | when | why |
|---|---|---|
| **The Raku.js WASM engine** | **always** | it *is* the engine; `/play/` and every run button on the site execute it |
| **Front page + install page figures** | **always** | hand-written, no generator, no check |
| **`snapshot.raku` → `gen-dashboard.raku`** | **always** | the timeline gains a point per tag; skip one and it is missing forever |
| **`gen-roast-map.raku`** | **always** | gate 1 produced a new `roast.txt`; it is the release's own measurement |
| **`spec` + `spec/rules`** | on demand — **always on a major** | only meaningful if gate 7's sweep was actually run this cycle |
| **`modules` / ecosystem** | on demand — **always on a major** | only meaningful after a fresh `eco-sweep`; the listing is 2,524 verdicts, not a version string |
| **`tour`, `faq`, `book`, `examples`, `showcase`, `grid`** | on demand — **always on a major** | content-driven: they change when their own source changes, not when the engine ships |

#### Always — after the tag, never before

`gen-dashboard.raku` mines the rakupp repo's `v*` tags and reads
`docs/status/ROAST.md` / `docs/status/BENCHMARKS.md` *as committed at each one*. Run it
before tagging and the new release is simply absent from the timeline — and
since it only collects, never measures, nothing warns you.

```bash
# in the raku.online checkout
../raku++/rakujs/build.sh                       # 1. the playground engine…
#    …which builds into raku++/rakujs/playground/ and copies NOTHING. Do that:
cp ../raku++/rakujs/playground/rakujs.js   www/
cp ../raku++/rakujs/playground/rakujs.wasm www/
cp ../raku++/rakujs/playground/examples.js www/play/
#    (worker.js is NOT copied — the site keeps its own variant.)
#    Then check the bundle you copied, not the build — see below:
strings www/rakujs.wasm | grep -oE '^[0-9]+\.[0-9]+\.[0-9]+$'

cd sites/spec
rakupp tools/gen-roast-map.raku /path/to/roast.txt $(date +%F)      # 2. gate 1's output
rakupp tools/snapshot.raku   --rakupp=/path/to/rakupp --oracle=raku # 3. BEFORE the dashboard
rakupp tools/gen-dashboard.raku --rakupp-repo=/path/to/raku++ \
                               --battery=/path/to/raku-module-battery   # 4.
./verify.sh                                     # every example, publishes nothing
cd ../.. && ./build.sh spec                     # sites/spec -> www/spec
```

Then edit the **hand-written version figures** (below), commit `www/`
*together with* `sites/spec/src/data/`, and push.

#### On demand — and unconditionally on a major

Run these only when the sweep behind them was part of this cycle. Each needs its
data regenerated **before** `build.sh` renders it, or you publish the same page
with a newer date:

```bash
cd sites/spec                                   # gate 7 must have run this cycle
rakupp tools/typerun.raku --rakupp=/path/to/rakupp --oracle=raku
rakupp tools/matrix.raku  --rakupp=/path/to/rakupp --oracle=raku
rakupp tools/conformance.raku && rakupp tools/divergences.raku
cd ../.. && ./build.sh spec                     # /spec and /spec/rules

# the ecosystem listing — needs a fresh eco-sweep, not the last verdict file
./build.sh modules                              # /modules and /modules/ecosystem

./build.sh all                                  # on a major: everything, in one go
```

`build.sh` takes `theme`, `tour`, `spec`, `grid`, `faq`, `book`, `modules`,
`examples`, `showcase`, or `all`.

**On a major, do it in this order**: re-run the sweeps → regenerate the data →
`./build.sh all` → `sites/spec/verify.sh` (run it from that directory) → then
the always-list above (the WASM engine,
the snapshot and the dashboard), so the dashboard's last line describes the data
that is actually on the site.

The Pages workflow publishes `www/` **verbatim** — there is no build step in CI,
so anything not built and committed locally does not go live, and the
regenerated data files alone change nothing that a visitor sees.

`snapshot.raku` sits between two ordering hazards, one on each side — the
v2.0.0 release hit the second one and shipped a dashboard whose conformance
trend ended at the previous release:

- **After gate 7's sweep, never before.** It appends one line to
  `src/data/history.jsonl` describing whatever the other data files say *at
  that moment*, and that file is what the trend chart on
  [/spec/rules/divergences/](https://raku.online/spec/rules/divergences/)
  draws. Snapshot before regenerating and the release's line permanently
  records the previous release's numbers: the file is append-only and nothing
  ever rewrites an earlier line.
- **Before `gen-dashboard`, never after.** The dashboard's conformance series
  is mined *from* `history.jsonl`, so a dashboard generated before the
  snapshot lacks the release's own point. If the order slips, re-running
  `gen-dashboard` is safe (it only collects); re-running `snapshot` is NOT
  (it would append a duplicate line).

Three more things go stale silently — none of them fails, they just stop:

- **The dashboard's modules series** is mined from the *battery repo's commit
  subjects* (`Tier-2: N/50` for the old probe bar, `battery: N/59` for the
  zef bar). Commit the battery repo with a subject in that shape before
  running `gen-dashboard`, or the graph flatlines at the last such commit
  while the README says something better.
- **The playground engine** (`/play/` and every embedded run button) is the
  Raku.js WASM bundle, built separately by `rakujs/build.sh` from the tagged
  source. Nothing regenerates it from the site side: skip this and the
  playground banner keeps announcing the previous release — v2.0.0 shipped
  while `/play/` still said 1.7.0, and v3.0.0 shipped while it still said
  2.0.0.

  **The build copies nothing.** It writes into `raku++/rakujs/playground/`, and
  three files have to be moved by hand: `rakujs.js` and `rakujs.wasm` into
  `www/`, and `examples.js` into `www/play/`. (`worker.js` is *not* copied — the
  site keeps its own variant, so overwriting it would be a regression.) Then let
  `build.sh` re-stamp the cache-busting hashes.

  `build.sh` regenerates `examples.js` with a rakupp it chooses itself, and
  since v3.22.0 it checks that binary is native rather than taking the first
  candidate — the same trap as perf-guard and optbench. It prints which one it
  used (`==> generating examples.js with …`); read that line, because it filters
  by architecture but not by *age*, and the examples every visitor runs are
  generated by whatever it picked.

  **Then check the bundle you just copied, rather than trusting the build.**
  The banner is not in the page — `worker.js` calls `rakupp_version` on the
  engine at runtime — so the only thing that fixes it is the `.wasm`:

  ```bash
  strings www/rakujs.wasm | grep -oE '^[0-9]+\.[0-9]+\.[0-9]+$'   # the new version
  ```

  The re-stamped `?v=` tag matters as much as the file: without it a returning
  visitor keeps running the cached old engine.

- **The front page and the install page carry hand-written figures.**
  `www/index.html` and `www/install/index.html` are edited directly. `sites/`
  holds `6e`, `book`, `examples`, `faq`, `grid`, `modules`, `showcase`, `spec`
  and `tour` — and no source for either of these two, so nothing generates them
  and no check compares them to anything. (`build.sh` *touches* `www/index.html`,
  but only to re-stamp its `?v=` cache tag; it never writes the figures.) They said `Version 2.0.0` with v2.0.0's Roast and
  distribution numbers for **two** releases, while the CHANGELOG moved twice.
  Update, in both files:

  - the version in the hero / eyebrow line,
  - `Where it stands, at X.Y.Z` and the table under it — Roast tests and
    denominator, files fully passing, distributions, doc examples,
  - the `sr-stats` tiles near the top, which repeat the distribution count,
  - the sub/method counts, against `docs/guide/REFERENCE.md`.

  `www/slides/index.html` is a *talk*, not a status page: it labels its own
  numbers with the release they were measured on, and its source of truth is
  `presentation/index.html` in the rakupp repo. Leave it unless the deck is
  being re-cut, and edit it there rather than here.

If a sweep was skipped, skip its regeneration too rather than re-running an old
tool against new data — the point of the history is that each line is one
coherent run. That is the whole reason for the on-demand column above: a
regenerated page with a new date and last release's numbers is worse than an
untouched one, because the date is the only thing a reader has to judge it by.

**A major version is the exception, and it is not a licence to regenerate from
stale inputs** — it is an instruction to *re-run the sweeps first*, then
regenerate everything. If a sweep genuinely cannot be run for a major, say so in
the CHANGELOG rather than publishing its page with a fresh timestamp. `inventory.raku` and `typedoc.raku` need a Rakudo doc checkout and
only change when the *documentation* does, not when Raku++ does; they are not
per-release work.

---

## The things that get forgotten

Everything below is written out in full above. This is the short list, because
every one of them has actually been missed at least once, and none of them
fails loudly — a forgotten step here looks exactly like a finished release.
The cost is named so the list stays a record rather than a ritual.

**Before the gates**

- [ ] **Pass `RAKUPP=` explicitly to every measuring tool.** `pick-rakupp`
      searches `build/` before `build-arm64/` and takes the first NATIVE one, so
      a stale-but-arm64 `build/` wins and the tool measures an engine dozens of
      commits old while its banner says only `rakupp 3.x.y`. It did this to
      `perf-guard`, `run-optbench` and `run-bench` (twice) during the v3.24.0
      release, with `build/` 28 commits behind — `run-bench` reported `bigint`
      at 31.3 ms where the release does 5.8, and recording that would have put a
      6x-wrong number in the file every later release is gated against.
- [ ] **Name the binary.** `which -a rakupp` — three answer on the machine of
      record, and the right one is first by PATH ordering alone. Read the
      provenance line each tool now prints. *(v1.0.0 scores a Roast subset 0/2
      where v3.22.0 scores 1/2, with nothing in the output to say which ran.)*
- [ ] **Note the Roast revision**, and put it in the CHANGELOG. *(Gate 1 is a
      diff against the last release's list; if Roast moved, the diff blames the
      engine for it.)*
- [ ] **Check `build/` is native**: `file -b build/rakupp`. Fixed at the root in
      v3.23.0 — `CMakeLists.txt` now defaults to the host architecture — but the
      cause is still on the machine: `/usr/local/bin/cmake` is an **x86_64**
      binary ahead of `/opt/homebrew` on PATH, and CMake infers the target from
      its own process, so the documented `cmake -S . -B build` produced a
      *translated* binary for as long as anyone had been running it. *(Three
      gate failures downstream of that one fact: perf-guard, optbench,
      doc-examples-diff.)* A configure now prints
      `-- rakupp: building for the host architecture arm64`.

**Around the gates**

- [ ] **Three Roast runs, not one**, and quote the repeating profile. Diff the
      **unions**. *(v3.0.0 published the top of its band and it never
      reproduced.)*
- [ ] **Archive `vX.Y.Z.list`, `vX.Y.Z-union.list` and the `.meta` sidecar**, and
      commit them. *(v3.20.1 archived nothing, so v3.21.0 diffed against
      `.gitignore`d scratch.)*
- [ ] **Re-record the perf baseline deliberately** — `--record --for=vX.Y.Z` —
      and say why in the CHANGELOG. *(Went four releases stale, passing the whole
      time.)*
- [ ] **Run the battery after performance work**, not only before a release.
      *(`Supply.wait` had never blocked; only the battery noticed.)*
- [ ] **Commit the battery repo with `battery: N/59` in the subject.** *(The
      dashboard mines that string; nothing warns when it goes stale.)*

**Writing the release**

- [ ] **`--version` must name the new version and must not end in `-modified`.**
      *(v3.0.0 shipped announcing 2.0.0.)*
- [ ] **Bump all three package files** — `CMakeLists.txt`, `.guix/…/rakupp-package.scm`,
      `flake.nix`.
- [ ] **Add the release's row to `docs/status/MILESTONES.md`.** *(Sat four
      releases stale, v2.0.0 → v3.14.0.)*
- [ ] **`check-figures --expect=N --examples=N --version=X.Y.Z` — all three
      flags.** *(With one flag it reported agreement while README still called
      the previous release current, in the table it had just checked. A flag
      whose figure it cannot find is now a failure, not a pass, and
      `prove-gates --gate=figures` proves it goes red.)*
- [ ] **CI green on the exact commit you are about to tag**, and never move a
      published tag. *(v3.0.0 served binaries built from two different commits.)*

**After the tag — the half that is easiest to skip, because it already looks done**

- [ ] **Bump the Homebrew tap** (a separate repo; three places in the formula).
      *(Sat on 1.1.0 for eleven releases.)*

*raku.online, every release — these carry the version or the timeline:*

- [ ] **Rebuild the Raku.js WASM engine, copy all three files, and check the
      bundle you copied** — `rakujs.js`/`rakujs.wasm` into `www/`, `examples.js`
      into `www/play/`; the build copies nothing itself. Verify with
      `strings www/rakujs.wasm | grep -oE '^[0-9]+\.[0-9]+\.[0-9]+$'`. *(v2.0.0
      shipped with `/play/` announcing 1.7.0; v3.0.0 with 2.0.0; v3.22.0's was
      built by a two-release-old compiler because "native" was never checked.)*
- [ ] **`gen-roast-map.raku`** with this release's `roast.txt`.
- [ ] **`snapshot.raku` after gate 7's sweep and before `gen-dashboard`.**
      *(Append-only: snapshot early and the release permanently records the
      previous one's numbers. v2.0.0 hit this.)*
- [ ] **`gen-dashboard.raku`** — after the tag, or the release is missing from
      every graph and nothing warns you.
- [ ] **Edit the hand-written figures on `www/index.html` and
      `www/install/index.html`.** Nothing generates them and no check compares
      them. *(Two releases stale.)*
- [ ] **Commit `www/` together with `sites/spec/src/data/` and push.** Pages
      publishes `www/` verbatim — there is no build step in CI.

*raku.online, on demand — these carry measurements, so only if the sweep ran:*

- [ ] **`/spec` and `/spec/rules`** — only if gate 7's sweep ran this cycle.
- [ ] **`/modules` and `/modules/ecosystem`** — only after a fresh `eco-sweep`,
      not from the last verdict file.
- [ ] **`tour`, `faq`, `book`, `examples`, `showcase`, `grid`** — only if their
      own source changed.

*On a MAJOR version (`X.0.0`), none of the above is optional:*

- [ ] **Re-run the sweeps first**, then regenerate, then `./build.sh all`, then
      `sites/spec/verify.sh` — in that order, so the dashboard's last line describes the
      data actually on the site. A major is the release people go and look at,
      and the wrong one to leave a two-release-old ecosystem table on. If a sweep
      genuinely cannot be run, say so in the CHANGELOG rather than republishing
      its page with a fresh timestamp.
