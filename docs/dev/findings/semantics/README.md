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
| [Supply.md](Supply.md) | Supply, Supply-factories, Supply-coercers, Supplier, Rakudo/Supply | 69 | 38 | 4 | implemented 2026-09-18 (Roast S17-supply 27→55 of 58) |
| [Nil-Any.md](Nil-Any.md) | Nil, Any, Any-iterable-methods | 50 | 16 | 20 | implemented 2026-09-18, extended 2026-09-21 (sheet items 6→30 of 50) |
| [Str.md](Str.md) | Str, Stringy, Cool (string half), allomorphs | 66 | 12 | 54 | implementing (PARTIAL), 2026-09-18 and 2026-09-21 (sheet items 6→12 of 66; `val.t` 913 failing → 0) |
| [List-Array.md](List-Array.md) | List, Array, Seq, Slip | 36 | 4 | 14 | implemented 2026-09-19, extended 2026-09-21 (sheet items 21→22 of 36) |
| [Hash-Map-Pair.md](Hash-Map-Pair.md) | Hash, Map, Hash/Object, Pair | 20 | 2 | 4 | implemented 2026-09-20 (Roast 698→705 files, 204,801→205,080 assertions; S32-hash/adverbs 1,012→1,067, S09-hashes/objecthash 21→27) |
| [IO.md](IO.md) | IO/Path, IO/Handle, io_operators, IO/Spec/Unix, IO/Special, IO/Pipe, IO/CatHandle, IO/Path/Parts | 26 | 7 | 14 | implementing 2026-09-20/21 (sheet items 2→12 of 26; Roast 704→707 files, S16-io/lines 3→111, S32-io/io-path 2→30, chdir 9→33, indir 0→24) |
| [Range.md](Range.md) | Range, the `..` family and `prefix:<^>`, the Range candidates of `+ - * / cmp eqv` | 33 | 12 | 27 | implemented 2026-09-22 (S02-types/range.t 216/259 → 256/259, S07-iterators/range-iterator.t 83/103 → 103/103) |
| [Int-Num-Rat.md](Int-Num-Rat.md) | Int, Num, Rat, Rational, Real, Numeric, Cool (numeric half), Rakudo/Internals coercions, Order, Polymod | 28 | 3 | 26 | implemented 2026-09-22 (S32-num 21 → 26 files, 3,424 → 3,465 assertions) |
| [Sequence.md](Sequence.md) | Rakudo/SEQUENCE, the `...` candidates in operators | 29 | 13 | 28 | extracted 2026-09-23 |
| [Promise.md](Promise.md) | Promise, Lock, Lock/Async, Lock/Soft, Semaphore, Awaiter, core.d/await, Scheduler, the `sleep` family | 36 | 12 | 36 | extracted 2026-09-23 |
| [Date-Time.md](Date-Time.md) | Instant, Duration, Dateish, Date, DateTime, the leap-second table in Rakudo/Internals | 33 | 4 | 31 | extracted 2026-09-23 |
| [Proc-Async.md](Proc-Async.md) | Proc, Proc/Async, the `run` and `shell` subs | 37 | 9 | 36 | extracted 2026-09-23 |
| [Code-Introspection.md](Code-Introspection.md) | Code, Block, Routine, Signature, Parameter, Attribute, WhateverCode, ForeignCode, traits | 37 | 24 | 37 | extracted 2026-09-23 |
| [Exception-Backtrace.md](Exception-Backtrace.md) | Exception, Backtrace, Failure, control (die/warn/fail/CX), the X:: attribute surface | 36 | 15 | 35 | extracted 2026-09-23 |
| [Set-Bag-Mix.md](Set-Bag-Mix.md) | QuantHash, Setty/Set/SetHash, Baggy/Bag/BagHash, Mixy/Mix/MixHash, the set_* operator files | 44 | 17 | 44 | extracted 2026-09-23 |
| [Blob-Buf.md](Blob-Buf.md) | Buf (Blob and Buf), Encoding and its registry, encoders and decoders | 37 | 21 | 37 | extracted 2026-09-23 |
| [Grammar-Match.md](Grammar-Match.md) | Grammar, Match, Cursor, Regex, the Regex-taking Str routines | 41 | 23 | 39 | extracted 2026-09-23 |
| [Compiler-Side.md](Compiler-Side.md) | Perl6/Grammar.nqp (precedence table, quote rules), Perl6/Actions.nqp (sink), Raku/Grammar.nqp compared | 34 | 8 | 32 | extracted 2026-09-23 |

The `rakupp differs` column is the CURRENT count: for an implemented sheet it
is what is still open, and each item's own line says what.

**An item is all-or-nothing, and that flatters nothing.** A probe line holds
dozens of fields; an item counts as differing when ONE of them does, and a
single field that dies early takes the rest of its line with it. The Range and
Int-Num-Rat sheets of 2026-09-22 moved fourteen and eleven items' worth of
behaviour while the item counts fell by two and none — the Roast columns beside
them are the honest measure of what changed.

**Not every item can be counted.** Re-running the first six sheets against the
Rakudo binary on 2026-09-21 reproduced 244 of their 267 recorded outputs; the
other 23 did not, and they are not rakupp gaps in either direction. Three
causes: a hash's iteration order is per-process (HM-14, HM-15), nine Supply
items bucket on the wall clock, and a handful depend on the machine. A sheet
row's `rakupp differs` count therefore overstates the work by however many of
its items are in that set — measure with both engines before believing a
number. The probe harness for this lives in the session scratchpad, not the
repo: it is a few dozen lines that parse the item format above.

Every candidate from the method-surface probe of 2026-09-17 (described in
the memory of that day and in the Supply sheet's method section) now has a
sheet. Further sheets come from new evidence: a Roast area that stays red
after step two, or a module batch that fails on one type.

## Provenance

Sources: the `rakudo/rakudo` repository at tag `2026.08` (commit
`24e6e5312f28`), fetched into the local checkout with `git fetch --depth=1
origin refs/tags/2026.08:refs/tags/2026.08` and read with `git show
2026.08:<path>`; the working tree of that checkout was not moved. Oracle: the
Homebrew Rakudo `v2026.08` on MoarVM 2026.08. The release tag is read, never
`main`, so that the source and the oracle binary agree.
