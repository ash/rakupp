# Finding engine faults by running the ecosystem

Real distributions are the best bug-finder this project has: they use the
language the way people actually write it, and every batch so far has turned up
faults no test suite of ours would have thought to write. The method below is
what that activity looks like when it is run deliberately rather than by
rummaging. Each stage exists because the stage above it could not have found
what it finds, and the costs are measured, not estimated.

## The four stages

Each stage is roughly an order of magnitude more expensive than the one before
it, and an order of magnitude more informative about *semantics*.

| # | stage | cost | finds |
|---|---|---|---|
| 0 | fetch + `install --no-test` into a shared store | ~45 min, once | nothing — it is the setup every later stage reuses |
| 1 | `rakupp -c` over every module file | ~3 ms per module | parse faults |
| 2 | `rakupp -I inst#<store> -e 'use Mod'` | ~1 s per dist | load faults (BEGIN, mainline, missing methods) |
| 3 | `rakupp test Mod`, TAP diffed against Rakudo | ~3.2 s per dist | **wrong values** — the faults that raise nothing |

Stage 0 is the expensive part and the whole reason to do it once: seed a store
with the most-depended-on modules (`rank-deps.raku` → `seed-store.raku`, 460 of
480 install, 192 → 576 dists) and every later stage runs against it. Without a
seeded store, per-dist cost is about ten times higher and it is all network.

Stages 1 and 2 are cheap enough to run over the **whole** ecosystem after every
engine change. Stage 3 is the 2.3-hour sweep you run when you want a number.

## Why all four, and not fewer

**Stage 1 cannot see a load fault.** `rakupp -c` on
`Math::Libgsl::Constants` says `Syntax OK`. That module locates its library in a
`sub LIB` that runs at load time and dies there. Compiling is not loading.

**Stage 2 cannot see a wrong value.** Of the twelve faults fixed in the
2026-09-16 random-fifty batch, four raised nothing at all — they answered
something, and the answer was wrong: `floor($x)` disagreeing with `$x.floor` on
a wide rational, `do {1} \` + newline + `+ 2` silently dropping the addition and
answering 1, `=~=` with no zero rule, and `.can` on a built-in type answering
False for a method the type plainly has. There is no error text to grep for.
Only running the suite finds them, which is what stage 3 is for.

**Stage 1 is worth keeping even though stage 2 subsumes it.** It needs no
dependencies resolved and no store, so it still reports a parse fault in a
module whose dependencies are themselves broken — and parse faults are the
cheapest class to fix and the most likely to be shared between distributions.

## The oracle filter — run it FIRST, not last

Every failure from stages 1–3 is re-run under Rakudo **on the same machine**.
If Rakudo fails the same way, it is not our bug and it leaves the queue.

This is the highest-value filter available and it is mechanical. The 2026-09-16
batch lost real time to the Math::Libgsl family: eighteen distributions, one
identical error (`Cannot resolve caller comb(Any:U)`), which read as the find of
the day. They locate their library with `run('/sbin/ldconfig', '-p')`, macOS has
no `/sbin/ldconfig`, and the identical chain yields `Any` under Rakudo too.
Font::QueryInfo shells out to `fc-query`, absent for the same reason. Neither is
convertible on this machine at all.

## Index by FAULT, not by distribution

The board is per-distribution; the work is per-fault. Normalise each failure to
a signature and invert it into `fault → distributions`. That is what makes the
payoff of a fix knowable *before* it is written — "this blocks seven" against
"this blocks one" — and it is where the shared wins come from: of the
twenty-five distinct parse faults in that batch, five were shared by two
distributions each.

Rank the queue by blocked-distributions × how close each blocked distribution
is to green. A distribution failing one assertion is the cheapest conversion in
the ecosystem.

## Traps, each of which cost time once

**`use-ok` swallows the exception.** Test's `use-ok` catches whatever the load
threw and reports a bare pass/fail, so the board records "a test failed" and
discards the one thing worth knowing. Seventy-four distributions in that batch
failed on nothing but a `use-ok`; installing each `--no-test` and loading it by
hand named the real fault for all seventy-four in one pass, and every fix in the
second half of the day came off that list. **This is the argument for stage 2
existing at all**: do not read a verdict where you can read a cause.

**`-c` takes a FILE, not a module name.** `rakupp -c JSON::Fast` answers
`Cannot open file: JSON::Fast`. Compile the unpacked sources, or load the
installed module with `-e 'use …'`.

**A store is reached with `-I inst#<prefix>`, never a bare path.** An
installation store keeps content-hashed sources (`sources/D13368C0…`) and finds
modules through its `short/` index, which is also what carries `ver`/`auth`/
`api`. A bare `-I <store>` is a *filesystem* repository and looks for
`Foo/Bar.rakumod`, which is not there. This is how `tools/install.raku` runs
every suite: `-I <dist>/lib -I inst#<prefix>`.

**One `Looks like you failed N of M` per log is not the dist's score.** A suite
with several test files can fail more of them further down. Parse per file, or
a three-file failure reads as a one-assertion near-miss (Sway::Config did).

**A verdict shifts with the store, and that is not a regression.** Measured
against a lean store, a distribution whose dependency fails its own suite moves
`self-fail` → `dep-fail`: the dependency now gets installed *and* tested. Read
the first-error column, which names the fault whichever distribution owns it.
See [findings/ecosweep/](findings/ecosweep/) for which store each board used.

## What this does not replace

Roast and `t/run.raku`. The ecosystem finds faults; the gates prove a fix did
not cost something else. Every fault found this way lands with a file in
`t/regression/` that passes under Rakudo byte-for-byte and fails on the binary
the batch started from — otherwise there is no evidence the fixture can fail.
