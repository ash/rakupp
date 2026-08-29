# Archived Roast file lists, one per release

RELEASING.md gate 1 calls the **file list** the gate — not the pass count, which
moves with machine load and the timeout profile. Diffing that list against the
previous release is what shows whether anything actually regressed.

For that diff to exist, the previous release's list has to still exist. Twice it
did not: v3.20.1 archived nothing, so v3.21.0's diff fell back to a development
run found in `rc-work/` (which is `.gitignore`d scratch, and was never the
release's own measurement). This directory is the fix — the list is small,
textual, and diffs cleanly, so git is the right place for it.

## What is in here

One file per release, `vX.Y.Z.list`: the paths of every fully-passing Roast file,
one per line, sorted, relative to the Roast checkout root. Written directly by
the harness:

```bash
ROAST=/path/to/roast rakupp tools/run-roast.raku --workers=4 \
    --list=docs/status/roast-lists/vX.Y.Z.list | tee roast.txt
```

`--list=` writes the list as **data**, collected as each file is judged. It is
not `grep '[PASS]' | awk '{print $NF}'` over the human-readable output, and that
distinction is the point: at `--workers=4` the children's TAP diagnostics used to
be spliced into the parent's status lines — consistently four a run — so four
files per run silently lost their path and read as regressions. (Fixed in
v3.22.0 by tapping the children's stderr; the list exists so the gate no longer
depends on that output's framing at all.)

## Two files per release, and which one to diff

- `vX.Y.Z.list` — **one run**, the one whose file count is the repeating profile.
  This is the release's measurement, and it is what the CHANGELOG's figure comes
  from.
- `vX.Y.Z-union.list` — every file that fully passed in **any** of the release's
  runs.

**Diff against the UNION.** A handful of concurrency and scheduler tests
(`S17-*`, mostly) sit near the 10-second per-file timeout and flap between runs
under `--workers=4`: v3.22.0's three runs gave 642 / 642 / 644, and every file
that differed was `[TIME]` in the run that lost it and passed when run alone.
Diffing single runs reports those as regressions, which is noise, and noise is
how a gate stops being read.

```bash
cd docs/status/roast-lists
comm -23 vPREV-union.list vNEXT-union.list   # regressed — must be empty
comm -13 vPREV-union.list vNEXT-union.list   # gained
```

Both files are written sorted, so `comm` needs no `sort`. Where the previous
release has no union file, diff its single list against the next union — that is
the same criterion v3.21.0's CHANGELOG used in prose ("every file passing in the
most recent full baseline passes in at least one of the four runs"), and for
v3.22.0 it is empty.

## Where the history starts

**v3.22.0 is the first release with a list here.** Nothing was archived before
it, so its own diff has no predecessor — that is the debt this directory closes,
and it closes it going forward rather than backwards. `v3.21.0-devrun.list` is
included alongside for reference and is explicitly **not** a release
measurement: it is recovered from `rc-work/roast-issue41.txt`, a development run
of 2026-08-28, and four of its paths had to be repaired by hand from the
diagnostics that corrupted them. Read it as context, never as a baseline.
