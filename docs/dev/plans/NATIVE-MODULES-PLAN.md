# Plan: the `**::Native` distributions — our C, fastest on Raku++, portable everywhere

**Status: DESIGN, not approved — code not started.** Companion to
[DATA-PLAN.md](DATA-PLAN.md), which covers the engine primitives and
`Data::Native`. This file covers the modules. Probes 2026-09-05,
`build-arm64/rakupp` + Rakudo v2026.08.

## TL;DR — the rules, in plain words

**There are two ways to get fast data code, and they are for different people.**

1. **`Data::Native` — for people using Raku++.**
   One `use` line, nothing to install, nothing to compile. It switches on
   functions that are already inside the `rakupp` binary. This is the easy
   path and the one most people should take.

   ```raku
   use Data::Native;            # everything
   use Data::Native <json csv>; # or just what you need
   ```

2. **`JSON::Native`, `CSV::Native`, `Digest::Native`, … — for people writing
   portable code.**
   Ordinary distributions you install with `zef`. They work on *every* Raku,
   so a program using them runs anywhere.

**The rules we agreed:**

- **Each `**::Native` module ships C source.** When it is installed on Raku++,
  that C is compiled and used — the fastest we can go.
- **On any other Raku, the same module still works**, just without our C. It
  quietly uses the well-known module it stands in for (`JSON::Fast` for JSON,
  `Digest` for hashes, and so on). Nothing breaks; it is only slower.
- **So our C makes Raku++ fast. It does not make other Rakus fast.** We looked
  at whether it could, using NativeCall, and measured that it would make
  Raku++ *slower* — so we did not do it. That is a deliberate choice, not an
  oversight.
- **`Data::Native` needs none of those modules.** The functions are in the
  engine already.
- **A module's interface is copied from the popular module it replaces**, name
  for name. Swapping one for the other is a one-line edit, and you can swap
  back.
- **If you load both, `Data::Native` wins** — but only for the tags you asked
  it for. `use Data::Native <csv>; use JSON::Native;` gives you the engine's
  CSV and `JSON::Native`'s JSON. Order does not matter.
- **One piece of C, not two.** The same C files are used by the engine and by
  the modules, so the two can never drift apart and give different answers.

**What we are building:** three new modules — `Digest::Native` (hashes: MD5,
SHA-1/224/256/384/512, HMAC), `Compress::Zlib::Native` (gzip and zlib), and
possibly `Crypt::Random::Native` (secure random bytes, and probably not worth
it). Plus moving the C that `JSON::Native` and `CSV::Native` already have into
the shared place.

## The decision (settled 2026-09-05)

1. **Every `**::Native` module keeps the `JSON::Native` approach**: C against
   `rakupp_ext.h`, compiled at install by its `Build.rakumod`, loaded through
   `&::('rakupp-ext-load')`.
2. **The goal is the fastest possible C when the module runs on Raku++.** The
   ext ABI builds Raku values in C, which is the only way to reach that for a
   codec — see the measurement under "Why not NativeCall" below.
3. **On other implementations the module still works, in fallback mode.**
4. **`Data::Native` activates the engine's built-in functions**, and needs no
   `**::Native` module installed.

A NativeCall shim was designed and is **dropped**. Consequence accepted:
our C accelerates Raku++ only.

## Why not NativeCall — measured

NativeCall cannot return a Raku `Hash` or walk one from C, so the only shape it
allows a codec is a *scanner*: C returns token offsets, Raku builds the values.
The README's own 278 KB corpus (`tools/bench/diagnose/d800.json`: 3,200 hashes,
1,601 arrays, 14,400 leaves) — whole parse, against the cost of merely
*allocating* that many values from Raku, which is the floor such a design sits
on:

| | `from-json` | Raku-side allocation alone | floor as % of parse |
|---|---:|---:|---:|
| Rakudo (JSON::Fast) | 61 ms | 12 ms | 19% |
| rakupp (core fast path) | 7 ms | 14 ms | **194%** |

On Raku++ a scanner is a guaranteed regression — building the values from Raku
costs twice the entire parse, before the scanner runs. The ext ABI exists
precisely to avoid that.

**Probed, and the reason fallback mode is what other engines get:**
`rakupp-ext-load` resolves `True` on rakupp and `False` on Rakudo, so
`json-backend()` reports `native` here and `JSON::Fast` there. That is the
current behaviour and this decision keeps it.

## Fallback means "the ecosystem reference", not "pure Raku we wrote"

Worth stating because it removes real work: a module's fallback is **the
established module it stands in for**, whatever that is made of. We write a
pure-Raku implementation only where no reference exists.

| module | fallback | is the fallback itself native? |
|---|---|---|
| `JSON::Native` | `JSON::Fast` | no — tuned `nqp` |
| `CSV::Native` | its own `parse-raku` (we wrote it; `Text::CSV` was 15,245 ms) | no |
| `Digest::Native` | `Digest` + `Digest::HMAC` | no — pure Raku |
| `Compress::Zlib::Native` | `Compress::Zlib` | **yes** — NativeCall over system `libz` |

The last row is the point: **we do not write a pure-Raku inflate.** On another
engine `Compress::Zlib::Native` delegates to `Compress::Zlib`, which is already
C. A hand-written Raku inflate would be hundreds of lines that nothing would
ever want to run.

## The architecture: one C core, two consumers

The part of this design that survives NativeCall being dropped — and the part
that matters most, because without it every family repeats the JSON situation:
**`JSON::Native`'s C and the engine's C++ codec are today two independent
implementations of one format**, held together only by a test suite.

```
src/native/          ← canonical, in the engine repo, plain C99
  md5.c sha1.c sha2.c hmac.c      bytes in, bytes out
  inflate.c deflate.c crc32.c     bytes in, bytes out
  json_core.c csv_core.c          algorithm; value construction via callbacks
  (no C++, no RkValue, no engine headers)
```

Two consumers, each a thin wrapper:

1. **librakupp** — a C++ wrapper building `Value`. Gives the `rakupp-*`
   primitives that [DATA-PLAN.md](DATA-PLAN.md) exposes and `Data::Native`
   activates.
2. **each module's ext-ABI shim** (`shim_ext.c`) — the same core, with `RkValue`
   construction.

Measured coupling in the C we ship today shows this is feasible: `json.c`
touches `rk_` on 35 of 552 lines (6%), `csv.c` on 59 of 498 (12%). The value
construction is at the boundary; the algorithm underneath is portable C. For
the byte families (digest, zlib, random) the split is trivial — those have no
value construction at all.

Canonical copy lives in the engine repo because that one is gated by Roast, the
regression suite and `perf-guard`. Each distribution vendors the C files, and
CI asserts the vendored bytes hash equal. **One algorithm, one set of vectors,
two ~80-line wrappers.**

## The backend ladder, and what each engine gets

Every `**::Native` module resolves in this order:

| | Raku++ | other Rakus | `*-backend()` reports |
|---|---|---|---|
| 1. engine primitive, if `Data::Native` claimed this tag | ✓ | ✓ where that engine has one | `core` |
| 2. our ext-ABI extension | ✓ | — | `native` |
| 3. the ecosystem reference it stands in for | ✓ | ✓ | e.g. `JSON::Fast` |

**Rows 1 and 2 are both our C, and row 1 is expected to be at least as fast**,
because a builtin skips the ABI boundary and the call arena. So on Raku++ the
`**::Native` extension is not a speed win over `Data::Native` — its value is
version skew (an engine predating the tag), independent release cadence, and
families the core deliberately will not carry. Once both exist, measure and
record which wins rather than assuming; JSON::Native's README currently has the
extension at 5.0 ms against 7.7 ms for the engine path, but that engine path
goes through a class method and module dispatch, not the direct builtin this
plan adds.

## The three missing modules

Names probed free on raku.land, 2026-09-05. The naming rule is
`<Reference>::Native` — the module you can swap in for `<Reference>` — which is
what `Digest::SHA1::Native` already does.

### `Digest::Native` — the strongest case

Replaces: `Digest` (`Digest::MD5`/`SHA1`/`SHA2`) + `Digest::HMAC`.
Exports the 14 names in DATA-PLAN's `digest` tag.

The real gap it fills: bduggan's two dists cover **only SHA-1 and SHA-256**,
each a separate install, and neither does MD5, SHA-512 or HMAC natively.
`Digest::HMAC` is pure Raku on every engine. One distribution covering
md5/sha1/224/256/384/512 + HMAC, native everywhere via NativeCall, is
something the ecosystem does not have.

C: ~450 lines (`md5.c sha1.c sha2.c hmac.c`). Half already exists in-tree —
`sha1hex` ([Interpreter.cpp:102](../../../src/Interpreter.cpp#L102)) and the
Jupyter `Sha256`/`hmacSha256Hex` — and moving those to `src/native/` as C is
the same lift DATA-PLAN's P3 already schedules.

### `Compress::Zlib::Native` — moderate case, one honest caveat

Replaces: `Compress::Zlib`. Exports the 6 names in the `zlib` tag.

On rakupp it is the difference between working and not (`Compress::Zlib`
self-fails there, taking `PDF`, `Image::PNG::Portable`, `Archive::SimpleZip`,
`File::Zip`, `Avro`, `SAT`, `Sitemap` and `pack6` with it). On Rakudo its value
is narrower: no system `libz` dependency, and no per-call NativeCall crossing
into a library whose ABI varies.

**The caveat, stated up front: our inflate will not beat zlib.** zlib is thirty
years of hand-tuned C with assembly fast paths; a clean-room RFC 1951
implementation lands somewhere well short of it. The claim is *availability and
self-containment*, not speed, and the README must say so rather than let a
benchmark table imply otherwise.

C: ~600 lines (`inflate.c deflate.c crc32.c`). Inflate first — it is what
unblocks the dependents.

### `Crypt::Random::Native` — recommend NOT building it

Replaces: `Crypt::Random`. Exports the 3 names in the `random` tag.

`Crypt::Random` is already a thin native binding and works on Rakudo. Under
the settled decision our module would be: our C on Raku++, and delegation to
`Crypt::Random` everywhere else. But `Data::Native <random>` already gives
Raku++ the same C with no install — so the distribution's *entire* remaining
value is version skew on an engine too old to have the tag. That does not pay
for a distribution.

**Recommendation: build the core primitive (`Data::Native <random>`), skip the
distribution.** Listed here so the decision is on the record rather than an
omission.

## `Data::Native` must win — the cooperation protocol

DATA-PLAN's requirement: `use Data::Native` beats our `**::Native` modules for
the tags it claimed, regardless of `use` order — but only those tags, so
`use Data::Native <csv>; use JSON::Native;` still takes `to-json` from
`JSON::Native`.

**Probed, and it is not optional.** Two modules exporting the same name are a
hard compile error on Rakudo:

```
Cannot import symbol '&to-j' from 'JN', because it already exists in
this lexical scope.
```

So the modules *must* cooperate or the program will not build. And when the
second one yields the contested name, the rule works exactly — probed, all four
cells:

| | rakupp | Rakudo |
|---|---|---|
| `use Data::Native; use JSON::Native;` | `CORE` | `CORE` |
| `use JSON::Native; use Data::Native;` | `CORE` | `CORE` |

The protocol, per tag:

- `Data::Native`'s `EXPORT` records the tags it claimed in a process-global
  registry before returning.
- Each `**::Native`'s `EXPORT` consults the registry; for a claimed family it
  omits those names from its Map, leaving `Data::Native`'s already-installed
  symbols standing.
- **When the `**::Native` loads first** it cannot know what is coming, so it
  exports normally — and would collide. It therefore exports a **dispatcher**
  that resolves its target on first call: the core primitive if the registry
  says that tag was claimed by then, else its own backend. `Data::Native`
  loading later sets the registry and does not re-export a claimed name.

That indirection is the price of order-independence, and it is one Raku frame
on the first call, not per call, if the dispatcher memoises after the mainline
starts. It applies only to our own modules; `JSON::Fast` and the ecosystem
references are untouched.

## Gates

- **The vendored C is byte-identical to the engine's** — a CI hash compare per
  distribution. This is the gate that makes "one algorithm" true rather than
  aspirational.
- **The conformance suite runs every backend row**: core, ext-ABI, NativeCall,
  and pure fallback, over one corpus per family, on both engines. All must
  agree value for value; `to-json`/`to-csv` byte for byte.
- **Order-independence**, both orders × both engines × claimed and unclaimed
  tags — the four-cell table above, as a test.
- **Inflate is fuzzed.** It is the one component here that parses
  attacker-controlled bytes; malformed streams must error, never read out of
  bounds. ASAN build.
- Digest vectors: NIST/RFC, plus the pure-Raku `Digest` as a same-process
  oracle on arbitrary input (it is byte-correct on rakupp today).

## Order of work

Strictly after DATA-PLAN's P1–P5 — the core primitives are the shared C's first
consumer, and writing the modules first would mean writing the C twice.

1. **`src/native/` extraction.** Move the existing SHA-1
   ([Interpreter.cpp:102](../../../src/Interpreter.cpp#L102)) and the Jupyter
   `Sha256`/`hmacSha256Hex` into plain C there; librakupp consumes them. No
   behaviour change; `jupyter-smoke` is the gate.
2. **`Digest::Native`** — ext-ABI shim over that core, `Digest` +
   `Digest::HMAC` as the fallback. The first module built the new way.
3. **`Compress::Zlib::Native`** — inflate first (it is what unblocks the
   dependents), then deflate; `Compress::Zlib` as the fallback.
4. **The cooperation protocol**, retrofitted into `JSON::Native` and
   `CSV::Native`, and their C moved into `src/native/` so each format stops
   having two implementations.
5. `Crypt::Random::Native` — only if someone asks.

## Open decisions

1. **Do `JSON::Native` and `CSV::Native` keep their ext-ABI extensions?** Yes
   under this plan — row 2 stays the fastest path on rakupp for value-building
   codecs, and version skew keeps it useful. But the C should move to
   `src/native/` + shims so it stops being a second implementation.
2. **Is the JSON/CSV *scanner* over NativeCall worth building** (row 3 for the
   value-building families)? **Measured 2026-09-05, and the answer splits by
   engine.** The README's own 278 KB corpus (`tools/bench/diagnose/d800.json`:
   3,200 hashes, 1,601 arrays, 14,400 leaves), parse time vs the cost of
   merely *allocating* that many Hash/Array/Str from Raku — which is the floor
   any scanner-plus-Raku-builder design sits on:

   | | `from-json` | Raku-side allocation alone | floor as % of parse |
   |---|---:|---:|---:|
   | Rakudo (JSON::Fast) | 61 ms | 12 ms | 19% |
   | rakupp (core fast path) | 7 ms | 14 ms | **194%** |

   On **rakupp** a NativeCall scanner is a guaranteed regression: building the
   values from Raku costs 2× what the whole parse costs today, before the
   scanner or the offset-decoding loop is counted. The ext ABI builds the tree
   in C, and that is exactly why it exists. **`JSON::Native` and `CSV::Native`
   keep the ext ABI; it is not replaced by NativeCall.**

   On **Rakudo** the floor is 19%, so a scanner could in principle approach
   ~2× over JSON::Fast — but JSON::Fast is tuned `nqp` and the offset-decoding
   loop is not free, so the realistic ceiling is lower. Worth a prototype
   *after* the byte families ship the NativeCall path; not in this plan.
3. **`Crypt::Random::Native` at all?**
4. **Vendoring vs a shared C distribution.** Vendoring with a hash gate is
   proposed; a single `Native::CSource` dist that the others depend on is the
   alternative, and trades duplication for a dependency edge.
5. **Does `Digest::Native` fall back to `Digest` (pure Raku, ~0.04 MB/s on
   rakupp) or to bduggan's `Digest::SHA*::Native` where installed?** The
   pure-Raku fallback is correct but very slow; preferring a native sibling
   when present is faster but adds optional dependencies and a second code
   path to test.
