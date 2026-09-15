# The handbook's next two hundred pages

The ecosystem handbook at raku.online/modules had 300 pages on 2026-09-15. This
is the work board for the next two hundred, and the measurement behind it.

## How the candidates were chosen

Start from the evening board of 2026-09-15 (`board-2026-09-15-late.tsv`): 989 of
2,529 distributions pass their own test suites under Raku++. Subtract the 299
already covered by a page. That leaves **687 passing distributions with no page**.

Of those, **195 have at least one other distribution depending on them** at run
time. Those 195 are the whole remaining leverage tail, so they are the batch;
five zero-dependent dists round it to 200. When this batch is written, every
passing distribution the ecosystem actually runs will have a page.

That is a different mix from the existing 300, where 46% of the pages cover
dists nothing depends on. The book has been curated for interest as much as for
leverage, and should go on being so — but the leverage tail is finite, and this
is the batch that finishes it.

## Why the board was re-measured

A page states what was measured, so the board it is written from has to be the
truth about the engine as it stands, not as it stood yesterday. All 200 were
re-run with `rakupp test` against a store cloned fresh, on the binary built from
9c59677 plus the `nqp::p6definite` fix below.

**165 of the 200 pass. 35 do not**, and would have been bad pages:

| verdict | count | what it means |
|---|---|---|
| pass | 165 | the distribution's own suite is green here |
| dep-fail | 30 | a dependency's suite fails, so the dist never gets tested |
| self-fail | 3 | the suite runs and fails |
| timeout | 2 | still running after 300s |

The dep-fail count is the interesting one. These dists passed on the board
because the shared sweep store already had their dependencies installed from an
earlier dist; against a store that has to install those dependencies from
scratch, the dependency's own suite is what fails. Both measurements are honest
about different questions — "does this dist pass once its deps are present" and
"can this dist be installed and tested from nothing" — and a handbook page needs
the second one, because a reader starts from nothing.

The 35 are kept in the TSV with their verdict rather than deleted: each is a
lead, and a dep-fail fixed upstream turns into a page.

## Topping the batch back up to 200

487 zero-dependent passing candidates remain untouched. Take 35 from there, run
them through the same fresh-store check, and keep the ones that survive it.
Selection among them should be by what makes a good page — a module that teaches
something, or gets something wrong in an instructive way — not by leverage,
which they do not have.

## The board

`handbook-next-200-2026-09-16.tsv`, sorted passing-first then by dependents.
Columns: `deps name version auth verdict seconds first-error`.
