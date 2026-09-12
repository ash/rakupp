# The 1000-module line

The v4 line in `VERSIONS.md` is 1,000 ecosystem distributions passing their own
test suites. This plan is the revision of 2026-09-12 and the order the rest of
the work is taken in.

## Where the count stands

The last whole-ecosystem sweep is `findings/ecosweep/sweep-2530.tsv`
(2026-09-05): **824 of 2,530**. 193 commits landed after it — RakuAST and its
L10N work, the App::Rak chain, Red, the module tickets — so the snapshot could
no longer be planned from.

A **282-dist probe** re-measured a targeted slice on 2026-09-12
(`findings/ecosweep/probe-282-2026-09-12.tsv`, two shards, 120 s budget,
engine `v3.28.0-5-gb63f50e`). Two disjoint halves:

| cohort | dists | converted | note |
|---|---|---|---|
| named (RakuAST/L10N, the App::Rak chain, the top-25 dep-fail blockers and their dependents) | 132 | 29 | measured, not estimated |
| random sample of the rest of the failing cohort | 150 | 5 | the half that extrapolates |
| **total** | **282** | **34** | **0 regressions** |

The random half converts at 5/150 = 3.3% (Wilson 95%: 0.8–6.9%). Over the
1,560 failing dists nobody re-measured that is 51 conversions, range 13–108.

    measured green today          860   (824 + 34 probed, + JSON::JWT and Hash::int measured green in the head list)
    estimated unmeasured gain    + 51   (range 13–108)
    ------------------------------------------------
    estimate                     ~911   (range 873–968)  of 2,535

The index has grown to 2,535 dists. **The gap to 1,000 is about 90**, and the
honest range on that gap is 33–130. Only a full sweep settles it; this estimate
is what the plan is ordered by, not a number to publish.

## Where the 1,000 is

`tools/eco-fresh/dep-leverage.raku` scores every dist by how many *not-yet-green*
dists sit transitively downstream of it. Run over the merged board:

    rakupp tools/eco-fresh/dep-leverage.raku \
        --rea=<store>/rakupp-install/rea-meta.json \
        --sweep=docs/dev/findings/ecosweep/merged-2026-09-12.tsv

Eight blockers at the head of that board have **168 distinct not-green dists**
behind them — the network/async/Cro constellation. That one cluster is larger
than the entire gap to 1,000, and nothing else on the board comes close.

Measured directly on 2026-09-12 (`findings/ecosweep/head-cluster-2026-09-12.tsv`):

| blocker | downstream | state |
|---|---|---|
| IO::Socket::Async::SSL | 101 | `The socket was closed during negotiation` |
| CBOR::Simple | 98 | hang FIXED 2026-09-12; 3 assertions left, both causes named |
| Log::Timeline | 95 | hangs still, for its own reason |
| JSON::JWT | 94 | **already green** — the snapshot was stale, now off the board |
| IO::Path::ChildSecure | 93 | `code returned a Failure` |
| Cro::HTTP | 92 | hangs still, for its own reason |
| IO::Capture::Simple | 70 | `OH HAI! in captured string` |
| HTTP::UserAgent | 63 | same failure as IO::Capture::Simple |

Below the constellation, four blockers are single named gaps with real reach:

| blocker | downstream | the gap |
|---|---|---|
| NativeHelpers::Blob | 47 | `No such method 'shape'` on a native array |
| Testo | 33 | `No such method 'cache'` on a Regex |
| LibraryCheck | 30 | a bogus library check answers ok |
| Method::Protected | 27 | `No such method 'add_attribute'` on a metaclass |

Hash::int (22 downstream) is **already green** too; two of the top twenty rows
being stale is the measure of how far the 2026-09-05 snapshot had drifted.

## The first fix: reading past the end of a Blob

CBOR::Simple times out because `cbor-decode` never terminates, and it never
terminates because **`nqp::readuint` past the end of a Blob answers 0 here and
throws upstream**. The dist depends on the throw by name:

```raku
CATCH {
    when /^ 'MVMArray: read_buf out of bounds' / {
        fail-malformed "Early end of input";
    }
    default { .rethrow }
}
```

`9f81ff` — an indefinite array holding a definite array that hits a break — runs
off the end of a 3-byte blob at `pos=3`, and rakupp hands the decoder an endless
supply of `Int`s: 278,159 loop iterations in five seconds, where Rakudo makes
exactly one. Probe (`scratchpad`, seven independent rows, each with its own
fixture and marker):

| row | rakupp | Rakudo |
|---|---|---|
| `nqp::readuint(Blob[uint8].new(0x41,0x42,0x43), 1, $ne8)` | 66 | 66 |
| `nqp::readuint(Blob[uint8].new(0x51,0x52,0x53), 3, $ne8)` | **0** | throws `MVMArray: read_buf out of bounds offset 3 start 0 elems 3 count 1` |
| the same at offset 99 | **0** | throws |
| a 16-bit read straddling the end | **29440** | throws |
| an empty Blob at offset 0 | **0** | throws |
| `nqp::atpos_i` past the end | 0 | 0 |
| `Blob[uint8].new(0x91,0x92)[7]` | **(empty)** | throws `Index out of range. Is: 7, should be in 0..1` |

Two engine bugs, and one control row (`nqp::atpos_i`) where both engines agree
that reading past the end answers 0 — so the probe can tell the cases apart.
The message text is asserted by the dist, so it has to be reproduced verbatim;
that is the exception to the usual rule about not copying Rakudo's prose.

**Log::Timeline and Cro::HTTP both sit downstream of CBOR::Simple**, and both
hang. That they hang for THIS reason was the obvious guess, and it was wrong:
both still time out at 200 s with the fix in. Their hangs are separate work.

## What the first session against this plan changed (2026-09-12)

**Needle::Compile is green, and so is App::Rak** — the dist RakuAST was built
for, and the flagship behind it. Six engine bugs stood between them, none of
them RakuAST: a Str-ish mixin's `method Str { self ~ "" }` recursing (string
context must take such an operand's VALUE, which also fixed `$m eq "mm"`
answering **False**); multi narrowness being SUMMED rather than compared per
parameter; a `$node but Role` breaking deparse by name; two missing RakuAST
mutators; and `but` on a BUILT-IN type object answering an instance, so
`my constant StrType = Str but Type` was `.defined` and nothing could bind it.
That last one generalised: a parameter typed by ANY `constant` alias did not
resolve. Roast: union of two runs **670, the identical file set to v3.28.0**,
zero regressions. Also converted: P5chdir.

**Nine engine fixes in total, all Roast-gated (four runs, union 670 — the
identical file set to v3.28.0, zero regressions).** The last three came out of
CBOR::Simple: `nqp::readuint` past the end of a Blob, `nqp::istype` answering
FALSE for a type object against its own type (`nqp::istype(Str, Str)`), and
`Num.Str` printing 17 digits where Rakudo prints 16 — a value whose decimal
expansion ends in an exact tie (every power of two) has printf break the tie to
even and hand back the one neighbour that does not round-trip.

**Every run showed one or two missing files and not one was a regression**: each
was verified passing standalone, and the repeat offender
(`integration/99problems-51-to-60.t`) scored `[TIME]` — a harness timeout — while
running in **0 s standalone on both the new binary and the pre-change
snapshot**. The cause was my own concurrent builds. Snapshot and codesign a
binary before starting a gate (`run-roast.raku` runs `build-arm64/rakupp`
itself), and judge a gate by the file DIFF, never the count.

**CBOR::Simple's hang is fixed; three assertions remain and they are OURS.**
`nqp::readuint` past the end of a Blob answered 0 where Rakudo throws, so
`cbor-decode` never terminated. Fixed; the verdict went from a **200 s timeout
to a 1.1 s self-fail**.

**A correction, and the reason it is written down.** This plan briefly recorded
that the three remaining assertions fail on Rakudo too, so the dist could not go
green on either engine. That was measured through `/usr/local/bin/raku`, which a
concurrent session on this machine repointed to `build-arm64/rakupp` at 21:00 —
so the "oracle" was rakupp. Against the real Rakudo
(`/opt/homebrew/bin/raku`, v2026.08 on MoarVM) **CBOR::Simple passes in full**:
94/94, 75/75, 39/39. The 98 dists behind it are still live, and the two bugs
left are named below. Pin the oracle by PATH, never by name.

**The other hangs are NOT this bug.** Log::Timeline, Cro::HTTP and LWP::Simple
still time out at 200 s with the fix in. They sit downstream of CBOR::Simple and
the shared-cause guess was wrong; their hangs are their own, and the async/Supply
side is where to look next.

## Order of work

1. ~~`nqp::readuint` past the end~~ — **done**, and two more with it, leaving
   **CBOR::Simple one assertion from green**: `nqp::istype` answered FALSE for a
   TYPE OBJECT against its own type (`nqp::istype(Str, Str)` was False), which is
   how the dist tells a string-keyed map from an object-keyed one; and `Num.Str`
   printed 17 digits where Rakudo prints 16, because a value whose decimal
   expansion ends in an exact tie (every power of two) has printf break the tie to
   even and hand back the one neighbour that does not round-trip.

   The last assertion needs a representation change and is NOT started — and it
   is the SAME blocker Log::Timeline now stops on, so it is the highest-value
   single fix on this board: an **object hash gives `.keys` back STRINGIFIED**. `my %h{Mu}; my $k = [7,8,9];
   %h{$k} = "v"` stores and fetches correctly, but `%h.keys.head` is `"7 8 9"`
   where Rakudo answers `[7, 8, 9]`, so the dist's
   `keys.first(* eqv [1,2,3])` finds nothing. rakupp's hash is `std::string →
   Value` by construction, so the original key needs somewhere to live. The
   mechanism already exists and is the place to start: `hashEntryKey()` returns
   `stored.pairKey()` when the stored value carries one, which is how Set/Bag/Mix
   recover their elements' original types. What is missing is the STORE side
   recording it for an object-keyed hash — the lvalue path (`Interpreter.cpp`,
   the `hashSubKey(eval(idx->index.get()), base)` site) hands back a slot
   POINTER that the caller then overwrites, so the stamp cannot simply go there. Also still open: `Blob[n]` out of range answers empty where Rakudo
   throws `Index out of range` (probe row R7).
2. **CBOR::Simple is GREEN; Log::Timeline is one thread from it; Cro is a
   campaign.** Five more engine fixes landed 2026-09-12 (`b4675bc`, `6105460`),
   each Roast-gated at 670 with zero regressions.

   **CBOR::Simple — converted.** All seven files, 342 assertions, the same counts
   Rakudo gets; the sweep verdict went from a **200 s timeout to a 1.0 s pass**.
   That is the largest single blocker on this board cleared: **98 not-yet-green
   dists sit transitively downstream of it.** Four bugs did it — `nqp::readuint`
   past a Blob's end, `nqp::istype` of a type object against its own type,
   `Num.Str` printing 17 digits where Rakudo prints 16, and an **object-keyed
   hash handing `.keys` back stringified**.

   **Log::Timeline — four of five files green**, and `output-socket` went from
   hanging while writing a stringified Channel onto the wire to **35 assertions**
   and a 41.9 s self-fail (was a 180 s timeout). Three async bugs got it there:
   `whenever $chan` in a `supply {}` bound the CHANNEL instead of its values;
   closing a TAP shut down the SOCKET; and a react never closed the tap it made,
   so its reader outlived it and ate bytes meant for the next tap. What is left
   is one thread: the server answers a bad handshake and closes, and `done`
   inside the client's `LAST` does not end its react — the done has to cross the
   `.lines` transform first. `done` in a LAST now works for a plain supply
   (that fix is in); the transform chain is the remaining layer.

   **Cro::HTTP is NOT one bug.** Measured over all 31 test files: **12 green,
   4 hang, 906 assertions passing to 102 failing** (the request parser alone is
   307/36). It needs its own batch, worked the way any dist is. LWP::Simple,
   Cro::WebSocket, Cro::SSL and HTTP::Supply still time out and have not been
   bisected. **IO::Socket::Async::SSL is the one to look at first** — 101
   downstream, the top of the whole leverage board, and it fails FAST (3.2 s,
   `The socket was closed during negotiation`) rather than hanging.

3. **The four named gaps** — `.shape`, `Regex.cache`, LibraryCheck,
   `add_attribute`. 137 downstream between them, and each is a small,
   self-contained piece of work.
4. **The remaining 46 timeouts.** Each costs a sweep 120 s and most are in this
   same constellation; a timeout is usually a hang, and a hang is usually an
   engine bug with a wide blast radius.
5. **The full sweep**, to settle the number rather than estimate it (~8 h, two
   shards — see the runbook in `findings/ECOSWEEP-2026-08.md`).

## Two things this revision cost, so they are not paid twice

- **`rakupp` binds no named argument after the first positional.** A script
  invoked `sweep-fresh.raku LIST --store=…` printed its usage and exited;
  `--store=… LIST` runs. Rakudo accepts both orders. Not yet filed.
- **Both reverse-dependency rankers miss `{"runtime":{"requires":[…]}}`** — a
  phase whose value is a hash rather than a list. `dep-leverage.raku` handles
  it; `rank-deps.raku` still does not.
