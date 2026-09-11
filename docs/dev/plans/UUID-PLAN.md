# Plan: UUIDs — every RFC 9562 version from one portable distribution

**Status: DRAFT 2026-09-11. Nothing is built; the distribution's name is not
final.** Probes in this file ran on `build-arm64/rakupp`
(v3.26.0-67-g9b4cb96) and Rakudo v2026.08 on this box, 2026-09-11. The
prototype they were measured with is not in either repository. Answers the
`B7` row of [MODULE-WISHLIST.md](../ecosystem/MODULE-WISHLIST.md).

## TL;DR

- **One new distribution** in `/Users/ash/raku-modules`, pure Raku, tested
  on both engines like everything there. It makes UUIDs of versions 1, 3, 4,
  5, 6, 7 and 8, the Nil and Max UUIDs, parses and validates any spelling,
  compares and sorts, and converts to and from ULID. It exports subs
  (`uuid-v4`, `uuid-v7`, …) **and a class named `UUID`** whose interface is a
  superset of the two `UUID` classes already in the ecosystem, so any of the
  25 distributions that depend on the three existing modules can switch by
  changing one `use` line.
- **No `UUID::Native` distribution and no new engine primitive in the first
  version.** Measured: on Raku++ a v4 costs 13 µs, of which entropy is 1.3
  and the clock 1.2 — the rest is turning 16 bytes into a dashed hex string.
  Everything the module needs natively — `rakupp-crypt_random_buf`,
  `rakupp-md5`, `rakupp-sha1` — is already in the engine, and the module
  finds it by probing, the way `Prompt::Hidden` does. A C primitive would
  bring the 13 µs to about 1; that is recorded as an opt-in last phase for a
  workload that asks for it, and the probe hook it would plug into is in the
  module from day one.
- **The name cannot be `UUID` or `UUID::V4`** — both are taken. The shortlist
  and a recommendation are in [Name](#name). The class inside is `UUID`
  whatever the distribution is called (`LibUUID` set that precedent in 2018).
- Three engine bugs found on the way, none blocking; listed at the end.

## The ecosystem today

Both indexes were read on 2026-09-11 (`360.zef.pm` for fez, `Raku/REA`'s
`META.json` for the archive). Nothing on either makes a version 7 UUID, or
1, 5 or 6. RFC 9562 (May 2024) replaced RFC 4122 and added versions 6, 7 and
8; every other ecosystem shipped v7 within the year.

| distribution | auth, version, date | where | what it does | fez dependents | on Rakudo | on Raku++ |
|---|---|---|---|---|---|---|
| `UUID` | github:retupmoca 1.0.0, 2018-04-30 | REA only | class `UUID`; v4 only | 10 — Red, BSON, Jupyter::Kernel, Jupyter::Chatbook, CRDT, FHIR, Stomp, TOP, Selkie::UI, LLM::RetrievalAugmentedGeneration | pass | pass |
| `UUID::V4` | zef:masukomi 1.0.0, 2022-10-01 | fez | `uuid-v4()`, `is-uuid-v4()` | 14 — Air, Humming-Bird, Date::Event, DateTime::US, JobQueue, LLM::Agent, LLM::Chat, … | pass | pass |
| `LibUUID` | github:CurtTilmes 0.5, 2019-04-05 | REA only | class `UUID` over libuuid via NativeCall | 1 (Marrow); DB::Pg's UUID converter, so Red on Pg | **fails** — no `libuuid.dylib` on macOS | pass — the engine maps `uuid` to libSystem |
| `ULID` | cpan:HANENKAMP 0.1.0, 2019-02-12 | REA only | `ulid()`, Crockford base32 | 1 (Humming-Bird) | pass | **fails** — the `state ($c, $b)` bug below |

What each one gets wrong is the reason to write a new one rather than send
patches:

- **`UUID` draws its 16 bytes with `(0..255).roll(16)`** — the ordinary
  PRNG, not a CSPRNG. A v4 is only as unique as its randomness.
- **`UUID::V4` has about 58 random bits per ID, not 122.** It hex-formats
  the 16 random bytes with `%x`, `.encode`s that text and `unpack`s the
  first 16 *ASCII* bytes of it, so every output byte is one of the sixteen
  characters `0-9a-f`. Every ID it has ever made looks like
  `38336265-3633-4665-b562-343263306334`. It also costs 768 µs a call on
  Rakudo. Fourteen distributions use it.
- **`LibUUID` needs a shared library** that macOS does not ship as a file.
- **`ULID` uses `rand`**, and its own suite finds the engine bug below.

## What the distribution provides

### Subs, exported by default

| sub | makes | notes |
|---|---|---|
| `uuid-v4()` | random | the same spelling as masukomi's, so `use` is the only line that changes |
| `uuid-v7()` | Unix-ms time + counter + random | strictly increasing within a process, see below |
| `uuid-v1()`, `uuid-v6()` | Gregorian 100-ns time + clock sequence + node | v6 is v1 with the time fields reordered so it sorts |
| `uuid-v3($ns, $name)`, `uuid-v5($ns, $name)` | MD5 / SHA-1 of namespace + name | `$ns` is a `UUID`, a Str, or one of the words `dns url oid x500` |
| `uuid-v8(Blob $bits)` | the caller's 122 bits | version and variant bits overwritten |
| `uuid-nil()`, `uuid-max()` | `00000000-…`, `ffffffff-…` | RFC 9562 §5.9, §5.10 |
| `is-uuid($s)`, `is-uuid-v4($s)` | validation | the second is masukomi's name, kept |
| `ulid()` | a ULID | same 48-bit millisecond prefix as v7 |

Every generator also accepts its inputs explicitly — `uuid-v7(:ms(…),
:rand-a(…), :rand-b(…))`, `uuid-v1(:time(…), :clock-seq(…), :node(…))`,
`uuid-v4(:bytes(…))` — for one reason: **the RFC's test vectors for the
random versions can only be asserted by feeding the RFC's inputs**, and a
test that cannot fail is not one.
That is a design decision, not a test hook: it is what makes the module
deterministic under test on both engines.

### The class

`class UUID`, a value type. Everything the two existing `UUID` classes
offer, with the same names, plus what they lack:

| method | from | behaviour |
|---|---|---|
| `new()` | retupmoca, LibUUID | a v4 |
| `new(:version(4))` | retupmoca | 1, 4, 6, 7 accepted; 3 and 5 need `:ns`/`:name`; 8 needs `:bits` |
| `new(Str)`, `new(Blob)` | LibUUID | any spelling below; exactly 16 bytes |
| `new(Int)` | new | 128-bit integer |
| `.Str` | both | **lower-case, dashed, always** — RFC 9562 §4 says output lower case, accept either |
| `.Blob`, `.bytes` | both | 16 bytes; `bytes` is retupmoca's attribute name, here a method returning the same |
| `.version`, `.variant` | retupmoca has `version` | read from the bits, not remembered |
| `.Int`, `.hex`, `.urn` | new | `:256[…]`, 32 hex digits, `urn:uuid:…` |
| `.time` | new | a `DateTime` for versions 1, 6 and 7; `Nil` for the rest |
| `.ULID` | new | Crockford base32 of the same 16 bytes |
| `==`, `eqv`, `cmp`, `<`, `.WHICH` | new | by bytes, so v7s sort by time and a `UUID` can key a hash or sit in a Set |
| `ACCEPTS(Str)` | new | `'…' ~~ $uuid` |

Parsing accepts: dashed, undashed 32 hex digits, `{…}` braces, a
`urn:uuid:` prefix, either case. Nothing else. A bad string `fail`s (LibUUID
did), it does not `die`.

### v7 in detail — the one people will actually use

Layout per RFC 9562 §5.7: 48 bits of Unix milliseconds, version, 12 bits
`rand_a`, variant, 62 bits `rand_b`. The 12 bits of `rand_a` are the §6.2
**method 1 counter**: seeded from entropy on every new millisecond,
incremented within one, and if 4096 IDs arrive inside one millisecond the
millisecond is advanced, which the RFC allows. One `Lock` guards the pair
(last-ms, counter). If the clock goes backwards the last millisecond is kept
and the counter carries on — IDs never go backwards even when the clock
does.

Measured with the prototype: 20,000 in one process — all unique, all in
order; two threads × 5,000 — unique across both, each thread's sequence in
order. On both engines.

**The time source is `now.to-posix[0]`, never `now`.** An `Instant` is not
POSIX time: on Rakudo `now - now.to-posix[0]` is 37 (the leap-second
count); on Raku++ it is **10**, which is the engine bug below. `to-posix`
agrees on both engines to the millisecond. It returns a `Rat` on Rakudo and a
`Num` on Raku++; both carry milliseconds exactly.

### Entropy

- **Raku++**: `try &::('rakupp-crypt_random_buf')`, the engine's `random`
  tag primitive — `getentropy(2)`/`getrandom(2)`, refusing rather than
  degrading. Nothing loads. The probe lives in a sub, not at file scope
  (a module-scope `try` leaves `$!` set and breaks precompilation of any
  importer on Rakudo — every module in the monorepo learnt this once).
- **Everything else**: `require Crypt::Random <&crypt_random_buf>` in a
  module-scope `if`, the shape `Prompt::Hidden` uses for its Windows half.
  Verified on Rakudo: it loads there and not on Raku++. Cost 9.9 µs a call
  (it opens `/dev/urandom` each time). `Crypt::Random` is the only hard
  dependency.
- **No entropy pool.** A pool amortises the open but is duplicated across a
  `fork`, and two processes handing out the same "random" UUIDs is a worse
  failure than 10 µs. `DataRandom.cpp` made the same call for the same
  reason.
- **Never `rand`, `roll` or `pick`.** That is the defect being replaced.

### v3 and v5

MD5 and SHA-1 of the namespace bytes followed by the name's UTF-8, version
and variant bits overwritten, the SHA-1's last four bytes discarded. On
Raku++ `rakupp-md5` / `rakupp-sha1` — the prototype reproduces the RFC's v3
vector through them. Elsewhere, **`Digest` loaded lazily on the first v3/v5
call**: `my &m = try { require Digest <&md5>; &md5 }` inside the sub, verified
on Rakudo. `use Digest` costs about 70 ms of load there and its md5/sha1 are
13 and 10 µs a call, which only v3/v5 callers should pay. `Digest` is a
`recommends`, not a `depends`, and the error when it is missing says what to
install.

### v1 and v6

Time is 100-nanosecond intervals since 1582-10-15, i.e.
`(posix + 12_219_292_800) × 10⁷`. The node is **48 random bits with the
multicast bit set**, drawn once per process — RFC 9562 §6.10 recommends
that over a MAC address, and the module never reads a MAC. The 14-bit clock
sequence is random per process and increments under the same Lock when the
100-ns timestamp repeats. v6 is v1 with the time fields reordered
(§5.6), so it sorts lexically; the RFC's own vectors for the two share every
input, which is how both get asserted from one fixture.

### ULID

Same 48-bit millisecond prefix as v7 and 80 random bits, Crockford base32
(26 characters, no I, L, O, U), monotonic by the same counter. `.ULID` on a
`UUID` and `UUID.new(:ulid($s))` convert both ways as a byte copy — the ULID
spec says its binary form is a UUID's. The existing `ULID` module is the
oracle for the encoding (`ulid-time`, `ulid-random` on fixed inputs), on
Rakudo until the `state` bug is fixed here.

### Export shape

The shape every module in the monorepo arrived at, for reasons recorded in
`notes/Prompt-Hidden.md` and `notes/Data-Native.md`:

- `sub EXPORT` at **file scope**, no `unit module` above it — inside a
  package declaration Rakudo never runs it and reports nothing.
- The class is declared as `class UUID` in the file and exported by name.
- `uuid-backend` — `core` or `Crypt::Random` — is spelled in full
  (`<Dist>::uuid-backend`) inside a `module` block, not exported.
- An import list selects subs; it is refused with a message if it names
  something that does not exist, never swallowed.

## Native or Raku — measured, not argued

The prototype was written first and measured on both engines, 10,000 calls
each, one run, before the question was answered.

| µs per call | Raku++ | Rakudo |
|---|---|---|
| prototype `uuid-v4` | 13.2 | 29.3 |
| prototype `uuid-v7` | 17.6 | 40.7 |
| prototype `uuid-v7`, monotonic under a Lock | 20.2 | 41.3 |
| `UUID.new.Str` (retupmoca) | 22.2 | 93.7 |
| `uuid-v4` (masukomi) | 20.9 | 768.0 |
| — 16 random bytes alone | 1.3 | 9.9 |
| — `now.to-posix` to milliseconds alone | 1.2 | 7.4 |
| — format A: hex table, five `join`s | 12.6 | 23.8 |
| — format B: hex table, one `join`, `substr` slices | 13.0 | 15.4 |
| — format C: `sprintf` over six `:256[…]` | 5.1 | 242.5 |
| — format D: `:256[…].fmt('%032x')`, slices | 9.5 | 51.8 |
| — format E: `.list.fmt('%02x', '')`, slices | 4.6 | 620.7 |

Three things fall out:

1. **The cost is formatting, and the idiom is engine-dependent.** Format E
   is the fastest thing on Raku++ and 135× slower than format B on Rakudo;
   format C is the mirror image. Format B is the one shape that is
   reasonable on both, and it is what the module uses. Picking E when the
   engine is Raku++ would make a v4 about 5 µs there instead of 13 — that is
   a phase-4 item, taken only with a workload in hand, because a branch on
   the engine's name is a dialect and the rule is that the engine gets the
   capability and the module gets the spelling.
2. **A C primitive is worth about 12 µs per ID on Raku++** — 13 down to
   roughly 1. Nobody has a workload where that shows: a UUID is made once per
   row, message or request, each of which costs a hundred times more. So no
   primitive now. The module probes `rakupp-uuid-v4` / `rakupp-uuid-v7`
   anyway — a `Nil` today — so that adding them later is an engine change
   with no module release.
3. **No `UUID::Native` distribution.** In this project `::Native` means C
   in the distribution, compiled against `rakupp_ext.h` at install, fastest
   on Raku++ with a fallback elsewhere
   ([NATIVE-MODULES-PLAN.md](NATIVE-MODULES-PLAN.md)). There is
   no algorithm here worth C — the two that exist, the hash and the entropy,
   are already engine primitives — and the ext ABI's per-call string copy
   would cost as much as the work.

DATA-PLAN's candidates table (its UUID row: "no gap") compared the two
engines running the same Raku; that is the right comparison for a
`Data::Native` tag, and the row stands. This plan asked a different
question — Raku against C on one engine — and the answer is above.

## Name

Rules from MODULE-WISHLIST: a functional name, no `RakuPP::` universe, and
the class inside is `UUID` regardless. Constraints here: `UUID` is
retupmoca's, `UUID::V4` masukomi's, and a name that reads as one version
(`UUID::V7`) misdescribes a module that does eight. All of the following
were free on both indexes on 2026-09-11.

| candidate | for | against |
|---|---|---|
| **`UUID::Full`** — recommended | says "every version" in one word; keeps the `UUID::` prefix that `zef search` and raku.land match on; sits beside `UUID::V4` as its obvious successor | — |
| `Data::UUID` | the Perl name, so the Perl audience finds it; a UUID is a data value | `Data::` in this project has come to mean the `Data::Native` codec family; nothing else about the CPAN module carries over |
| `UUID::All` | same claim as `Full` | `::All` reads as a bundle that loads everything |
| `UUID::Any` | `Log::Any` shows the idiom works | `Any` is a core type name; in code it reads as one |
| `UUID::RFC9562` | exact | nobody types it |
| `UUID::Simple` | the `HTTP::Simple` house style | "simple" undersells a spec-complete module; the simple one is `UUID::V4` |

The directory is the name with `::` as `-`: `/Users/ash/raku-modules/UUID-Full/`.

## Gates

Ordered by what they are worth, as the other plans do.

1. **RFC 9562 Appendix A, all six vectors** (v1, v3, v4, v5, v6, v7) —
   v3 and v5 computed from the DNS namespace and `www.example.com`; the
   random ones by feeding the RFC's inputs through the explicit-input forms.
   Plus the Nil and Max UUIDs and the four namespace constants.
2. **The ecosystem modules as oracles**: every ID retupmoca's `UUID` and
   `LibUUID` make must parse and round-trip; `is-uuid-v4` must agree with
   masukomi's on 10,000 of its IDs and 10,000 of ours; `ulid` must agree with
   the `ULID` module's `ulid-time` and `ulid-random` on fixed inputs.
3. **Bits and order**: version and variant on 10,000 of each version; a v7's
   `.time` within the test's own clock bounds; 100,000 v7s in one process
   strictly increasing; the counter-overflow and clock-went-backwards paths
   forced with a fixed clock; a two-thread run unique and per-thread ordered.
4. **The masukomi test**: over 1,000 v4s every byte value 0..255 must
   appear somewhere. A 4-bits-per-byte generator cannot pass it.
5. **Both engines, `./test.sh`**, then `rakupp --exe` and `--exe --standalone`
   of a program using the module (the probe shape is known to survive it,
   Prompt::Hidden proved that), then an install into a scratch store —
   `-Ilib` never opens `META6.json`, so a wrong `provides` only shows there.
6. **Nothing leaks**: after `use`, neither `crypt_random_buf` nor `md5` is
   visible in the importer on either engine. On Raku++ the `require` branch
   is never taken, so there is nothing to leak; assert it anyway, because
   the module-`use` leak is a standing engine bug.

## Phases

- **P1 — the distribution**: skeleton, `uuid-v4`, `uuid-v7` with the
  counter, the class with parse/format/compare, `is-uuid`, `is-uuid-v4`,
  the probe and the `Crypt::Random` fallback; gates 1 (v4, v7), 3, 4, 5, 6;
  README in the shape of `notes/README-shape.md`; `notes/UUID-Full.md` as
  the design log; `benchmarks/uuid/` outside the dist with the bench above.
- **P2 — the rest of the RFC**: v3/v5 with the lazy `Digest`, v1/v6, v8,
  nil/max, ULID both ways; the remaining vectors of gate 1; gate 2.
- **P3 — adoption**: a table in the README of the three existing modules'
  call sites and the one-line change for each, since 25 distributions is the
  audience. Whether that becomes upstream pull requests is the user's call;
  nothing is pushed or uploaded from here.
- **P4 — opt-in, unscheduled**: engine primitives `rakupp-uuid-v4`,
  `rakupp-uuid-v7` and a Blob-to-dashed-hex formatter, and/or the
  Raku++-specific format E — only with a workload that shows the 12 µs.
  The module already probes for the primitives.

## Engine bugs found on the way

Each is filed as a task; none blocks P1.

- **`state ($c, $b) = (0, 0)` re-initialises on every call.** A single
  `state $c = 0` is fine; the list declarator is not — `sub h { state ($x,
  $y) = (5, 7); $x++; $y--; "$x $y" }` answers `6 6` three times where
  Rakudo answers `6 6 | 7 5 | 8 4`. This is why the `ULID` module's
  monotonic subtest fails here.
- **`now - now.to-posix[0]` is 10 on Raku++ and 37 on Rakudo.** An
  `Instant` is meant to be TAI-based; the leap-second offset is wrong by 27
  seconds. Harmless to this module (it uses `to-posix`) and wrong for any
  program that compares an `Instant` to a POSIX time.
- **`|*` does not make a WhateverCode.** `@aoa.map(|*)` dies with `Cannot
  invoke non-Callable value of type Slip` (and in a `start` block it came
  back as an empty list, silently); Rakudo curries it to `{ |$_ }`.
  `map({ |$_ })` and `map(*.Slip)` both work.

Not an engine bug, noted for whoever hits it next: on this box `zef list
--installed` shows `Digest:ver<1.1.0>:auth<zef:grondilu>` and Rakudo's
`use Digest` cannot find it, even after a forced reinstall into `home`; the
two engines share `~/.raku`, and that store has needed repair before. The Rakudo numbers above came from the
module's source copied out of `~/.raku/sources` and used with `-I`.

## References

- RFC 9562, *Universally Unique IDentifiers (UUIDs)*, May 2024 — layouts
  §5.1–§5.8, counters §6.2, node §6.10, test vectors Appendix A.
- The ULID specification, github.com/ulid/spec — Crockford base32 and the
  monotonic rule.
- `Prompt::Hidden` in the monorepo — the probe-and-fall-back shape this
  module copies, and the reasons it has that shape.
- [DATA-PLAN.md](DATA-PLAN.md) — the `random` and `digest` tags whose
  primitives this module finds.
