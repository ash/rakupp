# Cutting a release

The gates below are the whole checklist. They exist because each one has caught
something real at least once; the notes say what.

## The gates

Run these in order. Every one must pass **before** the version is bumped.

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
WindowServer, and once four `pandoc` jobs at 50% each. A failing kernel is
re-measured before the gate believes it, and if the load average is above 60% of
the core count it reports **inconclusive and exits 2** rather than accusing the
code. Treat exit 2 as "run it again on an idle machine", never as a pass.

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
  the whole time, which is why nobody noticed. It now reads
  `2026-08-11 (v3.14.0)`. Re-record deliberately at a release (and say so in the
  CHANGELOG), or the "no regression since last release" claim quietly stops
  meaning that — and check the recorded date before repeating this note, which
  itself went stale and was quoted as fact during the v3.5.0 gates.
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
   rakupp tools/check-figures.raku --expect=NNN     # NNN = this run's fully-passing count
   ```

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
6. **Republish the site data** — the graphs and listings under
   <https://raku.online/spec/> and <https://raku.online/spec/rules/>, *and* the
   hand-written figures on the front page (see below).

Step 6 is the one that gets forgotten, because the release itself is already
out by then and everything looks finished. It isn't: gate 7 rewrote the
conformance data in the raku.online checkout and left it sitting there, so until
this runs, the site shows the *previous* release's divergences, coverage meters
and Roast map while announcing the new version.

**After the tag, not before.** `gen-dashboard.raku` mines the rakupp repo's `v*`
tags and reads `docs/status/ROAST.md` / `docs/status/BENCHMARKS.md` *as committed at each one*.
Run it before tagging and the new release is simply absent from the timeline —
and since it only collects, never measures, nothing warns you.

```bash
# in the raku.online checkout, sites/spec
rakupp tools/gen-roast-map.raku /path/to/roast.txt $(date +%F)  # gate 1's output
rakupp tools/snapshot.raku --rakupp=/path/to/rakupp --oracle=raku   # BEFORE the dashboard
rakupp tools/gen-dashboard.raku --rakupp-repo=/path/to/raku++ --battery=/path/to/raku-module-battery
../../../raku++/rakujs/build.sh    # the playground engine — see the notes below
./verify.sh                                    # both sites, every example, publishes nothing
cd ../.. && ./build.sh spec                    # sites/spec -> www/spec
```

Then commit `www/` *together with* `sites/spec/src/data/`, and push. The Pages
workflow publishes `www/` **verbatim** — there is no build step in CI, so
anything not built and committed locally does not go live, and the regenerated
data files alone change nothing that a visitor sees.

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

Two more collectors go stale silently — neither fails, they just stop:

- **The dashboard's modules series** is mined from the *battery repo's commit
  subjects* (`Tier-2: N/50` for the old probe bar, `battery: N/59` for the
  zef bar). Commit the battery repo with a subject in that shape before
  running `gen-dashboard`, or the graph flatlines at the last such commit
  while the README says something better.
- **The playground engine** (`/play/` and every embedded run button) is the
  Raku.js WASM bundle, built separately by `rakujs/build.sh` from the tagged
  source and copied into the site's `www/` (`rakujs.js` + `rakujs.wasm`).
  Nothing regenerates it: skip this and the playground banner keeps
  announcing the previous release — v2.0.0 shipped while `/play/` still said
  1.7.0, and v3.0.0 shipped while it still said 2.0.0. Rebuild it, copy the
  pair over, and let `build.sh` re-stamp the cache-busting hashes.

  **Then check the bundle you just copied, rather than trusting the build.**
  The banner is not in the page — `worker.js` calls `rakupp_version` on the
  engine at runtime — so the only thing that fixes it is the `.wasm`:

  ```bash
  strings www/rakujs.wasm | grep -oE '^[0-9]+\.[0-9]+\.[0-9]+$'   # the new version
  ```

  The re-stamped `?v=` tag matters as much as the file: without it a returning
  visitor keeps running the cached old engine.

- **The front page and the install page carry hand-written figures.**
  `www/index.html` and `www/install/index.html` are edited directly — `sites/`
  holds only faq, spec and tour, so no generator touches them and no check
  compares them to anything. They said `Version 2.0.0` with v2.0.0's Roast and
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
coherent run. `inventory.raku` and `typedoc.raku` need a Rakudo doc checkout and
only change when the *documentation* does, not when Raku++ does; they are not
per-release work.
