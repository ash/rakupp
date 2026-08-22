# raku-eye — the standing watch on fresh Raku code

[FRESHNESS-PLAN.md](FRESHNESS-PLAN.md) designed the weekly sweep and deliberately
left it hand-started: the scarce resource was the fix session, and measurement
was cheap enough to begin by hand. raku-eye moves the measurement half out of
human hands entirely. Every Monday, unattended, it pulls what the ecosystem
published since the last run, measures rakupp against it (with Rakudo as the
control and as the performance reference), and publishes the result. The fix
session stays exactly where it was — a person and a terminal — but it now
starts from a finished report instead of an empty one.

The split is the point. FRESHNESS-PLAN's phases 0–2 (refresh, sweep, triage
clustering) are mechanical and move to the machine; phase 3 (one gated fix
batch) stays in sessions. raku-eye never edits the compiler, never retries a
bad number into a good one, and has no AI anywhere in it. It produces two
things only:

1. **lists of things to improve** — mismatch clusters ranked by files affected;
2. **the record of how good we are over time** — append-only ledgers and the
   charts drawn from them.

## Where it lives and runs

A separate public repo, **github.com/ash/raku-eye**, sibling to rakupp. Reasons
recorded when this was decided: weekly bot commits and accumulating data stay
out of rakupp's hand-curated history; a red monitoring run does not sit on the
compiler's checks; and the measure/fix boundary becomes physical — the thing
that measures cannot edit the thing it measures.

Execution is a scheduled GitHub Actions workflow in that repo. Both repos are
public, so this costs nothing (public repos get unlimited Actions minutes on
4-vCPU runners), needs no tokens or deploy keys, and every run happens in a
disposable VM — the right container for freshly published third-party code,
and immune to the macOS unkillable-child problem the manual sweeps documented.

- **Schedule:** `cron: '17 5 * * 1'` (Monday 05:17 UTC — off the congested
  top-of-hour, report ready by European morning), plus `workflow_dispatch` for
  manual re-runs, plus a `concurrency` group so runs never overlap.
- **Actions cron is best-effort** — runs fire late sometimes and occasionally
  not at all. This is harmless by construction: every delta is computed from
  recorded state (last swept club commit, previous REA index), not from "the
  last 7 days", so a missed Monday just means the next run covers two weeks.
- Roughly one hour per run; the job cap is six.

## The five legs

### 1. Weekly Challenge

Exactly the PWC leg of FRESHNESS-PLAN, run by the machine: the file-level git
delta since the last recorded commit (catches late merges into old
directories), plus the open mismatch set re-run, plus a rotating 10% slice of
the pass set. Skip rules unchanged: a file counts only if Rakudo runs it
headlessly and reproduces its own output across two runs. Comparison is
stdout + exit status, byte-for-byte.

A **regression** — pass last week, mismatch now — is printed as its own ledger
column and flagged red on the dashboard. It is the one thing the Eye exists to
catch on the weeks nobody is looking.

The club repo is fetched fresh each run as a partial clone
(`--filter=blob:none` + sparse checkout of the `raku/` directories) — the full
repo is six years × dozens of languages and we need almost none of it.

### 2. Ecosystem releases

The eco leg of FRESHNESS-PLAN: diff today's REA `META.json` against the stored
snapshot, vendor the new and bumped dists (checksummed tarballs, symlink-free,
path-checked — the battery rules), then the three legs cheapest-first: parse
(`rakupp -c` every file), suite under rakupp, Rakudo control for what fails.
Standing policy holds: a dist that fails identically under Rakudo is
upstream-broken — recorded, never worked on. The control runs fresh every
week; it has been wrong before when looked up.

The pinned top-200 battery is out of scope here and stays in
`raku-module-battery` (private, deliberate pins). The Eye only ever touches
the fresh delta, using rakupp's public `tools/eco-fresh/` scripts.

### 3. raku-corpus — the curated own-code battery

[ash/raku-corpus](https://github.com/ash/raku-corpus) (public) is 1,886
runnable programs from the books, the course, challenge solutions, snippets,
and larger programs — curated as a self-contained golden-output suite:
committed reference outputs in `expected/` (generated under a pinned Rakudo,
v2026.06 at the time of writing), a `rejected/` set already excluded, a
committed `run-status.tsv`, and its own harness including `diff-rakupp.sh`.

That makes this the cheapest leg per file: **one execution under rakupp,
byte-compared against the committed reference** — no oracle runs, no
reproducibility double-runs; the corpus's curation already did that work. The
Eye clones it fresh, drives the corpus's own harness (cwd is each program's
directory — the advent solutions read their `input.txt` in place), and counts
matches. Same execution hygiene as everything else: network unshared,
per-child timeout, process-group kill.

Why it earns a weekly slot despite changing rarely: it is the author's own
code, so **every mismatch is actionable** — no upstream-broken policy, no
author-template noise — and it is stable, which makes its series the purest
regression tripwire on the dashboard: a line that should hold or climb, where
any dip names the exact file that broke. The ledger records the corpus commit
and the goldens' Rakudo version; the chart marks the rare weeks either one
steps. ~1,800 short programs at one run each, in parallel, is minutes.

### 4. Benchmarks — rakupp vs Rakudo, continuously

New relative to FRESHNESS-PLAN. rakupp moves weekly; Rakudo releases roughly
monthly; the question "where do we stand against Rakudo, and which way is it
moving" deserves a chart nobody has to remember to update.

The harness already exists: `tools/run-bench.raku` measures every kernel under
**interp**, **native** (`--exe`), and **rakudo** (and Perl 5 where a twin
exists). The Eye runs it weekly and appends the numbers.

Methodology, because CI hardware is noisy and heterogeneous:

- **Ratios are the series, absolutes are the footnote.** Each kernel is
  measured back-to-back under both engines on the same runner within the same
  minutes, interleaved repetitions, median taken. The chart shows
  interp/rakudo and native/rakudo per kernel. Ratios survive runner-to-runner
  CPU variation; absolute milliseconds from a shared VM do not, and are stored
  in the JSONL but never headlined.
- **Every row records** the rakupp commit, the Rakudo version, and the runner
  CPU model. The chart draws a vertical marker whenever the Rakudo version
  changes, so a ratio jump is attributable to the correct side.
- **Rakudo policy:** the latest prebuilt release (linux-x86_64 archive from
  the rakudo/rakudo releases), cached by version. No compiling Rakudo, no
  tracking its main.
- **[BENCHMARKS.md](../status/BENCHMARKS.md) is unaffected.** It remains the
  curated, on-one-Mac document with its own numbers. The Eye's series is a
  different machine and says so; the two are not expected to agree in absolute
  terms, and the ratios are the bridge.

### 5. Publishing

Every run ends by regenerating a static dashboard and appending the ledgers:

- **Trend charts** — PWC match % of counted, eco parse-clean/suite-green
  counts, the raku-corpus match line, benchmark ratios per kernel — as static
  SVG generated by the report
  tool. No chart libraries, no CDN, nothing to rot.
- **The improve-list** — current mismatch clusters, ranked by files affected,
  with a sample file and the normalized first-divergence signature each. This
  is triage already done; a fix session starts by reading it.
- **Regressions** in their own red box, newest first.
- **Latest-week tables** and links down to the raw JSONL for every number on
  the page. Raw numbers, no adjectives.

## raku.online

Verified setup: `raku.online` is the custom domain of the `ash/raku.online`
*project* site (the user site `ash.github.io` carries no domain), so project
pages do not cascade under it. The account's existing pattern is one repo per
subdomain (`duo.raku.online`), and the Eye follows it:

- **eye.raku.online** — a DNS CNAME record `eye → ash.github.io` plus the
  custom-domain setting on the raku-eye repo's Pages. That is the entire
  integration; the dashboard deploys there on every run via the Pages actions
  (site built from data each time, derived output not committed).
- **A card on raku.online itself** — the Eye publishes `data/latest.json`
  (current match %, benchmark headline ratio, regression count, date). GitHub
  Pages serves everything with `Access-Control-Allow-Origin: *`, so the
  raku.online homepage fetches it client-side and renders a small stats card
  linking to the full dashboard. No build coupling between the two sites; if
  the fetch fails the card simply doesn't render.

## Repo layout

```
raku-eye/
  .github/workflows/weekly.yml   # the Monday run
  tools/                         # driver, report generator, sandbox wrapper — Raku, run under the just-built rakupp
  state/                         # last swept club commit; per-file PWC verdicts; previous REA index
  data/                          # append-only: history TSVs per leg; weekly JSONL detail; latest.json
  site/                          # templates/static assets; build output goes to Pages, not to git
  README.md                      # what this is, one screen, linking here
```

The workflow, in order: checkout raku-eye → checkout rakupp `main` → restore
caches (ccache, Rakudo-by-version) → build rakupp (Release, Ninja) → install
Rakudo → fetch the corpus deltas and the raku-corpus clone → run the four
measurement legs →
cluster, append ledgers, write `latest.json` → build the site → commit
`state/` + `data/` and push → deploy Pages.

The tools are Raku and run under the rakupp binary built minutes earlier —
the tools-are-also-tests rule carries over. The accepted consequence: a week
where rakupp's `main` cannot run the driver is a loudly red Monday run, which
is that week's most important finding; fix, then `workflow_dispatch`.

## Rules for the unattended version

Carried over from FRESHNESS-PLAN, plus what unattended adds:

- **The row is the number that came out.** No retry-until-green, no dropping
  a bad week. Nondeterminism is handled where it always was — the two-run
  Rakudo reproducibility rule — not by re-rolling.
- **Fetch and execute are separate steps.** Everything fetches first; then
  the execution legs run with the network unshared (`unshare -n` wrapper —
  the Linux replacement for the battery's `sandbox-exec` profile), per-child
  timeouts, process-group kill. The VM is disposable on top of all of it.
- **The Eye never edits the compiler.** It has no write access to rakupp and
  no fixing logic. Improvement flows one way: dashboard → fix session →
  rakupp commit → next Monday's numbers.
- **Upstream-broken stays out of scope**, verified fresh weekly.
- **A module that compiles is not a module that passes** — parse-clean,
  suite-green, and upstream-broken are separate columns everywhere.
- Actions auto-disables cron on 60 days of repo inactivity; the weekly ledger
  commit resets that clock as a side effect.

## What has to be built

In rakupp (these also serve the manual sessions, so they come first):

1. **`tools/pwc-sweep.raku`: `--files=LIST`, `--state=FILE`, `--sample=N%`,
   regressions as their own output line** — items (1) of FRESHNESS-PLAN's
   build list, unchanged, still unbuilt (the tool has only `--from/--to`
   today).
2. **`tools/run-bench.raku`: a machine-readable output mode** (`--tsv` or
   `--json`) alongside the human table, emitting per-kernel medians and the
   engine/version/CPU metadata the ledger row needs.

In raku-eye:

3. **The driver** — `tools/eye-run.raku`: orchestrates the five legs, owns
   state files, writes ledgers and `latest.json`. The clustering is the
   existing mechanical method (normalize first divergence / error signature,
   bucket, rank) as code, not prose.
4. **The report generator** — `tools/eye-report.raku`: data in, static
   HTML + SVG out.
5. **The workflow** — `weekly.yml` as laid out above, with the caches.
6. **The domain** — DNS CNAME for `eye.raku.online`, Pages custom domain,
   and the latest.json card on raku.online's homepage.

Order: 1–2 land in rakupp first (a manual session can use them immediately);
3–5 make the first automated Monday; 6 is cosmetic and can trail by a week.
The first run's baseline: seed `state/` from the current PWC ledger (the 23
open mismatches of challenges 371–387 and the pass set) rather than
re-sweeping history — round one and round two stay documented in
[PWC-DIVERGENCES.md](../findings/PWC-DIVERGENCES.md); the Eye's series starts
at its own week zero.

## Open decisions

- **Whether the dashboard shows the round-one backlog** (the 2,707-mismatch
  history) as static context, or starts clean at the automated series.
- **REA yanked/removed dists** — same open question as FRESHNESS-PLAN.
- **Bench cadence for `--exe`** — native compiles every kernel through the
  C++ toolchain each run; if runner minutes ever matter it could go
  fortnightly, but at current sizes there is no reason.
