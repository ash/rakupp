# Semantics sheets — knowledge extracted from Rakudo, for Raku++ to implement

Rakudo is the de-facto definition of Raku. docs.raku.org and Roast declare
most of it, but not all: edge rules, event ordering, what an empty input
yields, which exception type a misuse throws. These sheets record that
missing knowledge so it can be implemented here.

The campaign runs in **two separated steps**, decided 2026-09-17:

1. **Extract.** A session reads the Rakudo sources of one release and writes a
   sheet: behaviour statements, each with an executable probe whose expected
   output was produced by the Rakudo binary of that same release.
2. **Implement.** A later session implements from the sheet alone, with the
   Rakudo binary as oracle and Roast as the gate. That session does not open
   Rakudo's sources.

The split is the firewall that keeps the clean-room stance intact. The stance
itself, since 2026-08-22, is *read designs, never port code* (see
[JOURNEY.md](../../JOURNEY.md) and
[RAKUDO-TECHNIQUES.md](../engines/RAKUDO-TECHNIQUES.md)).

## What a sheet records — and what it must not

A sheet item states **observable behaviour**: inputs to outputs, which
exception type with which attributes, lazy or eager, which argument types are
accepted, how adverbs combine, and the edge cases: zero, negative, `*`, `Inf`,
empty, undefined. When the source shows a mechanism, the sheet records only
the guarantee that mechanism produces ("an emit issued from inside a tap's
handler reaches that tap after the handler returns"), never the mechanism.

A sheet never contains: Rakudo's iterator or state classes, its locks,
counters, nqp ops, scheduler plumbing, or its error prose verbatim. Exception
*types* and their attributes are semantics; the message text is not, unless
Roast asserts it. We do not reimplement Rakudo's internals: no MoarVM, NQP or
RakuAST in their sense. Each behaviour is implemented our own way, against
our own Value and execution model, and measured; where we can be more
efficient, we are.

## Item format

```
### S-nn  short title                                   D:yes|no  R:yes|no|?  V:spec|quirk|bug
Statement of the behaviour in plain words.
    probe one-liner
    # rakudo 2026.08: exact output
rakupp <version>: matches | differs: what it does instead
```

- **D** — documented on docs.raku.org (checked in the local `doc` checkout).
- **R** — asserted by Roast (checked by reading `S*/` files; `?` = not looked
  for). Items with D:no and R:no are the payload of the campaign.
- **V** — verdict: `spec` (matches the language's intent), `quirk` (Rakudo
  behaviour that is surprising but harmless), `bug` (Rakudo behaviour that
  contradicts its own intent; step two decides, Roast and the IEEE-over-Rakudo precedent in
  [REVIEW-GRAND.md](../REVIEW-GRAND.md) already govern such cases). A `bug`
  item is recorded so that nobody imitates it by accident.

Probes were run through the Rakudo binary named in the sheet's provenance
line, with `alarm 10` around each run; a probe that depends on wall-clock
buckets or thread timing says so.

## Sheets

| sheet | source files read (tag 2026.08) | items | not D:yes, not R:yes | rakupp differs | status |
|---|---|---|---|---|---|
| [Supply.md](Supply.md) | Supply, Supply-factories, Supply-coercers, Supplier, Rakudo/Supply | 69 | 38 | 7 | implemented 2026-09-18 (Roast S17-supply 27→55 of 58) |
| [Nil-Any.md](Nil-Any.md) | Nil, Any, Any-iterable-methods | 50 | 16 | 22 | implemented 2026-09-18 (Roast 725→729 files; sheet items 6→28 of 50) |
| [Str.md](Str.md) | Str, Stringy, Cool (string half), allomorphs | 66 | 12 | 56 | implementing 2026-09-18 (Roast 729→735 files; `val.t` 913 failing → 0) |

The `rakupp differs` column is the CURRENT count: for an implemented sheet it
is what is still open, and each item's own line says what.

Candidates, in the order the method-surface probe of 2026-09-17 ranked them
(the probe is described in the memory of that day and in the Supply sheet's
method section): List and Array; Hash
and Map; Range; Int, Num, Rat; IO::Path; Proc::Async; Promise; Routine, Code,
Parameter, Block, Attribute introspection; Instant, Duration, Date, DateTime;
Set, Bag, Mix; Blob and Buf; Exception and Backtrace; Grammar and Match; then
the compiler side: the precedence table, quote adverbs, sink context.

## Provenance

Sources: the `rakudo/rakudo` repository at tag `2026.08` (commit
`24e6e5312f28`), fetched into the local checkout with `git fetch --depth=1
origin refs/tags/2026.08:refs/tags/2026.08` and read with `git show
2026.08:<path>`; the working tree of that checkout was not moved. Oracle: the
Homebrew Rakudo `v2026.08` on MoarVM 2026.08. The release tag is read, never
`main`, so that the source and the oracle binary agree.
