# Roast: what Rakudo passes on this machine — the ceiling, measured

Every Roast figure this project publishes is a count against 1,464 files, and
until now nothing measured what the reference engine itself passes on the same
machine through the same harness. The ecosystem got that measurement on
2026-09-16 ([ecosweep/RAKUDO-BASELINE-2026-09-16.md](ecosweep/RAKUDO-BASELINE-2026-09-16.md));
this is the Roast equivalent, and it answers the question "can Roast reach 100%"
with the only definition of 100% that is real.

## Method

- `tools/run-roast.raku` scores whatever engine runs it (`$*EXECUTABLE`), so the
  run was `raku tools/run-roast.raku` — **Rakudo 2026.08** as the engine under
  test, Roast **b2cbe8a42**, the same 1,464 files, the same counting rules.
- Rakudo does not apply `#?rakudo` fudge directives itself; its spectest runs
  Roast's `fudge` first. Raku++ applies the same directives in its lexer. So
  the run used a detached git worktree of the Roast checkout with Roast's own
  tool applied in place — `fudgeall --keep-exit-code --version=v6.d rakudo.moar`
  rewrote 280 files — which puts both engines on the same fudged bar.
- `ROAST_TIMEOUT=60` and `-j=3`, so a file Rakudo passes slowly still counts
  toward the ceiling (Raku++ is measured at 10 s). Wall time 8 min 16 s.
- Raku++ 4.0.1 was swept the same hour on the same checkout: 676, the v4.0.1
  release figure, file for file.

The list is archived as
[`docs/status/roast-lists/rakudo-2026.08.list`](../../status/roast-lists/rakudo-2026.08.list)
with a `.meta` sidecar, beside the per-release lists.

## The numbers

| | files |
|---|---:|
| Rakudo passes | **1,433 of 1,464** (97.9%) |
| Raku++ passes | 676 |
| Raku++ passes where Rakudo does not | 7 |
| **reachable on this machine** (676 + queue) | **1,440** |
| **the queue**: Rakudo passes, Raku++ does not | **764** |
| fail under both engines | 24 |

So the honest figure is **676 of 1,440 = 46.9%**, where 676 of 1,464 reads
46.2%. The ceiling barely moves the percentage — unlike the ecosystem, where it
moved it from 39% to 56% — because Roast is Rakudo's own suite: almost
everything in it is reachable. Rakudo's assertion figure on the same run was
218,748 of 218,765.

### The 31 files Rakudo does not pass here

| Rakudo outcome | files |
|---|---|
| no TAP (24) | `S02-literals/format.t`, `S02-literals/version.t`, `S04-statements/goto.t`, `S05-capture/hash.t`, `S05-nonstrings/basic.t`, `S06-advanced/caller.t`, `S06-advanced/return_function.t`, `S06-signature/multiple-signatures.t`, `S06-signature/slurpy-blocks.t`, `S11-modules/re-export.t`, `S12-attributes/trusts.t`, `S12-class/open_closed.t`, `S12-traits/basic.t`, `S12-traits/parameterized.t`, `S13-overloading/fallbacks-deep.t`, `S13-overloading/multiple-signatures.t`, `S19-command-line-options/01-dash-uppercase-i.t`, `S24-testing/11-plan-skip-all.t`, `S32-str/gb18030-encode-decode.t`, `S32-str/gb2312-encode-decode.t`, `S32-str/shiftjis-encode-decode.t`, `S32-temporal/time.t`, `t/fudge.t`, `t/fudgeandrun.t` |
| partial (4) | `S04-phasers/exit-in-check.t` 1/1 (plan mismatch), `S10-packages/require-and-use--dead-file.t` 2/16, `S14-roles/generic-subtyping.t` 0/1, `S29-os/system.t` 39/41 |
| timeout at 60 s (3) | `S17-supply/batch.t`, `S17-supply/watch-path.t`, `S32-str/sprintf-b.t` |

Seven of these Raku++ passes (`format.t`, `exit-in-check.t`, `re-export.t`,
`fallbacks-deep.t`, `multiple-signatures.t` in S13, `11-plan-skip-all.t`,
`sprintf-b.t`); they are in the reachable set because a pass is a pass. The
rest are outside any engine's reach on this box — most are files Rakudo's own
`spectest.data` does not run — and no time should go into them.

## The queue, by how Raku++ fails

[`roast-queue-2026-09-17.tsv`](roast-queue-2026-09-17.tsv) is the 764: path,
class, assertions lost, the first failing test's description with its
expected/got, and the first error on stderr. Every file in it is *provably*
reachable. The engine's own output for each file was captured with a 10-second
timeout from a scratch working directory, the way the harness runs it.

| class | files | what it means |
|---|---:|---|
| `parse` | 65 | no TAP: a parse error before any test runs |
| `dies` | 28 | no TAP: dies at run time before the first test |
| `part-0` | 62 | emits some passing tests, then dies before the plan is met |
| `part-1` | 104 | one assertion lost |
| `part-2` | 102 | two lost |
| `part-3-10` | 273 | three to ten lost |
| `part-11+` | 118 | more than ten lost |
| `timeout` | 12 | did not finish in 10 s |

By synopsis: S32 132, S02 91, S03 76, S06 66, S12 62, S05 57, S17 51, S04 46,
integration 43, S16 22, S26 20, S09 19, S14 17, S11 13, the rest under ten each.

**It does not cluster.** The 90 files that die (`dies` + `part-0`) die on
**41 distinct missing methods, one file each** (`Supply.zip`, `Match.chunks`,
`Proc::Async.bind-stdout`, `Pair.hash`, `Metamodel::Primitives.create_type`, …)
plus a long tail of one-off runtime errors; the 65 parse errors are 20
different messages, the biggest being `expected )` at 20 files, each on a
different construct. The one-assertion files repeat a description at most three
times (`Cool.{starts-with,ends-with,index} with wrong args does not hang`). This
is the same shape the ecosystem queue had (365 plain assertions, near-unique):
each fix is one file, occasionally two or three.

## What that means for "100%"

The reachable 100% is **1,440**, and Raku++ is **764 files** short of it. At
the best sustained rate this project has recorded for Roast — a dozen to twenty
files in a sitting when the sitting is spent on one-assertion files — that is
weeks of sittings, not one, and the per-file cost rises as the cheap end is
used up (the 104 one-lost files first, then 102 two-lost, then 273 that lose
three to ten). The board above is the work list; take it from the cheap end
and re-measure the ceiling only when Rakudo or Roast moves.
