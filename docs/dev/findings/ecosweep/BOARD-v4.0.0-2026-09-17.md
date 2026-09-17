# The ecosystem board at v4.0.0

**1,006 of 2,529 distributions pass their own test suites** — 40% of the
catalogue, and **56% of the 1,791 that any engine can reach on this machine**.
The second number is the one to read: 738 distributions cannot pass here under
Rakudo either, for want of libgsl, fontconfig, `/sbin/ldconfig` or a network,
and a rate against the whole catalogue charges this engine for libraries the box
does not have.

## How this was measured

Not a fresh whole-ecosystem sweep — those take about eight hours and one did not
run this cycle. This is `board-2026-09-15-late.tsv` (989 passing) with **296
distributions re-measured on the release engine** and merged over it, newest
winning:

- **196 whose verdict had moved** since that board, taken from the intermediate
  runs of the preceding sittings. Those files needed filtering first: some were
  **Rakudo-oracle** runs and some were **cold-store** runs, and merging either
  into a warm-store rakupp board would have written another engine's or another
  method's verdicts into this one. Excluding them changed the moved set from 178
  to 196.
- **100 board-green distributions chosen at random** as a regression probe.
  Re-measuring only what is expected to have improved can produce nothing but
  good news, which is not a measurement.

Warm store, as the 989 was: one shared store per shard, so the tenth dist
needing `JSON::Fast` finds it installed. That answers *"does this pass once its
dependencies are present"*. The cold-store question — *"can a reader starting
from nothing install and test this"* — is a different instrument worth roughly
200 fewer, and the two must not be mixed.

Two shards under `nice`, 120 s per distribution, `OPENSSL_PREFIX` exported from
the runner, the engine copied and re-signed (macOS SIGKILLs an unsigned copy
with no message), `Test::Selector` held out to be run last and alone because its
suite re-invokes itself unboundedly. Zero `fetch-fail` rows, which is the marker
for a machine too sick to believe.

## What moved

**21 gained, 4 lost, and none of the four is an engine regression.** Only
`self-fail` can be one — `dep-fail`, `build-fail`, `timeout` and `fetch-fail`
are the closure or the machine — and both self-fails were run down:

| distribution | verdict | what it actually was |
|---|---|---|
| two dists | `dep-fail` | a dependency failed before their own tests ran |
| `PerlMongers::Hannover` | `self-fail` | `sh: /usr/local/bin/perl6: Bad CPU type in executable` — the dead x86_64 binary. This box lost Rosetta on 2026-09-16, after the base board was taken. The machine, not the engine. |
| `JobQueue` | `self-fail` | **flaky**: re-run three times it gave self-fail / pass / self-fail on `'freed slot refills'`, a timing-dependent queue test. |

So the honest reading is 1,006 with one loss owed to the machine and one to a
flake — 1,007 on the box as it stood a day earlier.

## Reading this file

`self-fail` is the only verdict that can be a regression of the engine. Before
calling any other one a regression, look the *dependency* up in this same file:
a `dep-fail` above an already-failing dist is the closure resolving, not
something new. A `timeout` at a 120 s budget is not a verdict either.
