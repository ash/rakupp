# Plan: the `**::Native` distributions — our C, fastest on Raku++, portable everywhere

**Status: BUILT 2026-09-05.** `Digest::Native` and `Compress::Zlib::Native`
exist in `/Users/ash/raku-modules`, and step 4 — the cooperation protocol in
`JSON::Native` and `CSV::Native` — is done. What is left of the order of work
is step 1, moving the conformance corpora into this repository beside the
engine implementations that will read them. Companion to
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
  bduggan's `Digest::SHA1::Native` and `Digest::SHA256::Native` plus `Digest`
  for hashes, and so on). Nothing breaks; it is only slower.
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
- **The engine's C and a module's C are separate**, on purpose. Sharing one
  copy would force the engine's code into a shape that suits the modules, and
  the engine's path is the one most people use. We accept two copies and put
  the effort into stopping them from disagreeing: one shared set of test
  inputs both must pass, and a written note whenever they differ deliberately.

**What we are building:** two new modules — `Digest::Native` (hashes: MD5,
SHA-1/224/256/384/512, HMAC) and `Compress::Zlib::Native` (gzip and zlib).
Secure random bytes stay engine-only (`Data::Native <random>`); a
`Crypt::Random::Native` distribution was considered and is not built. Plus
moving the C that `JSON::Native` and `CSV::Native` already have into the
shared place.

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
| `Digest::Native` | `Digest::SHA1::Native` + `Digest::SHA256::Native` (bduggan) for SHA-1/256; `Digest` for MD5/224/384/512; `Digest::HMAC` | **partly** — the two most-used algorithms are C via NativeCall; the rest pure Raku |
| `Compress::Zlib::Native` | `Compress::Zlib` | **yes** — NativeCall over system `libz` |

The last row is the point: **we do not write a pure-Raku inflate.** On another
engine `Compress::Zlib::Native` delegates to `Compress::Zlib`, which is already
C. A hand-written Raku inflate would be hundreds of lines that nothing would
ever want to run.

## The architecture: independent C, deliberately — and how it is kept honest

**Decided 2026-09-05: the engine's C and each module's C are independent
implementations.** A shared `src/native/` core was designed and is not taken.

**Why.** The `rk_*` value construction is not at the edge of a codec, it is at
the leaves of its recursion — `csv.c` touches `rk_` on 59 of 498 lines, `json.c`
on 35 of 552. Factoring one algorithm out for two consumers therefore means
either a callback per constructed value (an indirect call at every leaf — 19,201
of them on the 278 KB corpus, defeating inlining on the *engine's* hot path) or
a token/offset intermediate that both sides then walk. Both put a tax on the
core to serve the module, and the core is the path `Data::Native` gives
everybody with no install. The engine implementation should be free to be the
fastest thing we can write.

**The honest cost.** Two implementations of one format can drift, and today's
`JSON::Native` already shows the shape of it: its C and the engine's C++ codec
are separate, and only the test suite holds them together. Nothing here makes
that better; the decision is that a shared core would make the core worse by
more than the drift costs.

**What we do about it instead** — this is the standing commitment, not a
nice-to-have:

1. **One corpus, both implementations.** The conformance corpora and vector
   sets live in the engine repo and are run by *both* the engine's regression
   suite and each distribution's `t/`. Same inputs, same expected bytes. A
   divergence is a failing test on whichever side moved, even though the code
   is not shared.
2. **A pointer in each file's header.** The engine's codec and the module's C
   each name the other as its twin, so a fix in one prompts a look at the
   other. Cheap, and it is the thing that actually gets remembered.
3. **Deliberate differences are written down**, in the plan and in both
   headers — an adverb one supports and the other does not, an error the module
   raises that the core cannot. Undocumented divergence is the failure mode;
   documented divergence is a design.
4. **A fix in one is not done until the other is checked.** Part of the
   definition of done for any codec bug, on either side.

**Where converging later is free, if we ever want it.** The reasoning above is
about *value-building* codecs. `digest` and `zlib` are bytes in, bytes out —
they construct no Raku values, so `rk_` appears only in the entry/exit shim and
a shared `sha256(const uint8_t*, size_t, uint8_t[32])` would cost the core
nothing. If the two-copies discipline ever proves expensive, those two families
are where to converge first, and json/csv are where not to.

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

## The two new modules, and the one not built

Names probed free on raku.land, 2026-09-05. The naming rule is
`<Reference>::Native` — the module you can swap in for `<Reference>` — which is
what `Digest::SHA1::Native` already does.

### `Digest::Native` — the strongest case

Replaces: `Digest` (`Digest::MD5`/`SHA1`/`SHA2`) + `Digest::HMAC`.
Exports the 14 names in DATA-PLAN's `digest` tag.

The real gap it fills: bduggan's two dists cover **only SHA-1 and SHA-256**,
each a separate install, and neither does MD5, SHA-512 or HMAC natively.
`Digest::HMAC` is pure Raku on every engine. One distribution covering
md5/sha1/224/256/384/512 + HMAC — our C on Raku++, and on other engines as
native as the ecosystem already gets — is something the ecosystem does not
have.

C: ~450 lines (`md5.c sha1.c sha2.c hmac.c`), the module's own copy. The
engine grows its own in DATA-PLAN's P3, which lifts `sha1hex`
([Interpreter.cpp:102](../../../src/Interpreter.cpp#L102)) and the Jupyter
`Sha256`/`hmacSha256Hex` out of their current homes. Twins by the rule above:
same vectors, headers naming each other.

**Fallback on other engines: the established native modules, as hard
dependencies — the `JSON::Fast` pattern** (decided 2026-09-05, revising an
earlier pure-Raku-only choice). Per algorithm:

| names | fallback | what it is |
|---|---|---|
| `sha1`, `sha1-hex` | `Digest::SHA1::Native` (zef:bduggan) | C via NativeCall, built at install with `LibraryMake` |
| `sha256`, `sha256-hex` | `Digest::SHA256::Native` (zef:bduggan) | same |
| `md5`, `sha224`, `sha384`, `sha512` and their `-hex` | `Digest` (zef:grondilu) | pure Raku — nothing native exists for these short of `OpenSSL::Digest`, which drags system OpenSSL |
| `hmac`, `hmac-hex` | `Digest::HMAC` (zef:jjmerelo) | pure Raku padding around whichever `&hash` it is given — with bduggan's `&sha256` the two hash calls inside are native |

All four are `depends`, not optional — one fallback path, composed, exactly as
`JSON::Native` depends on `JSON::Fast` rather than probing for it. The earlier
objection ("optional dependencies and a second path to test") was an objection
to making them *optional*; as required dependencies there is nothing extra to
test. bduggan's names and signatures are the tag's already, so delegation is
name-for-name.

Cost of the dependency, stated: bduggan's dists build their C at install
(`depends: LibraryMake`, `build-depends: Shell::Command`), so a C compiler and
`make` are needed — the same requirement `JSON::Native`'s own extension
already has. They are well-trodden: `Cro::WebSocket` (via `Cro::HTTP`, rank 2)
depends on `Digest::SHA1::Native`.

### `Compress::Zlib::Native` — moderate case, one honest caveat

Replaces: `Compress::Zlib`. Exports the 6 names in the `zlib` tag.

~~On rakupp it is the difference between working and not.~~ **Re-probed
2026-09-05: half of that expired.** `Compress::Zlib`'s `compress`/`uncompress`
now work on rakupp 3.25.0; what still fails is `gzslurp`/`gzspurt`, which go
through a `Wrap` class calling `nqp::p6definite`. The honest case is what
survives: the file wrappers, no system `libz` dependency, no per-call NativeCall
crossing into a library whose ABI varies, and — the one that turned out to
matter most — it is the only route that works inside an `--exe` binary and in
the WASM playground, where there is no library to dlopen. See DATA-PLAN's
`--exe` section: a program using this module *directly* cannot be shipped as a
binary at all.

**The caveat, stated up front: our inflate will not beat zlib.** zlib is thirty
years of hand-tuned C with assembly fast paths; a clean-room RFC 1951
implementation lands somewhere well short of it. The claim is *availability and
self-containment*, not speed, and the README must say so rather than let a
benchmark table imply otherwise.

C: ~600 lines (`inflate.c deflate.c crc32.c`). Inflate first — it is what
unblocks the dependents.

### `Crypt::Random::Native` — not built (decided 2026-09-05)

Replaces: `Crypt::Random`. Exports the 3 names in the `random` tag.

`Crypt::Random` is already a thin native binding and works on Rakudo. Under
the settled decision our module would be: our C on Raku++, and delegation to
`Crypt::Random` everywhere else. But `Data::Native <random>` already gives
Raku++ the same C with no install — so the distribution's *entire* remaining
value is version skew on an engine too old to have the tag. That does not pay
for a distribution.

**Decided: the core primitive (`Data::Native <random>`) is the whole
deliverable; no distribution.** Kept here so the reasoning is on the record
rather than the name simply being absent. If version skew ever turns out to
matter for this family, it is a ~30-line shim over the same `csprng.c` and can
be added without changing anything else.

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

- **One corpus, run against every backend row** — core, ext-ABI, and the
  fallback — on both engines, per family. All must agree value for value;
  `to-json`/`to-csv` byte for byte. **With the C deliberately not shared, this
  is the gate that keeps the two implementations honest**, so it is the one to
  build first and the one that must never be skipped.
- **The corpora live in the engine repo** and each distribution's `t/` pulls
  them, so neither side can quietly test something easier than the other.
- **Order-independence**, both orders × both engines × claimed and unclaimed
  tags — the four-cell table above, as a test.
- **Inflate is fuzzed.** It is the one component here that parses
  attacker-controlled bytes; malformed streams must error, never read out of
  bounds. ASAN build.
- Digest vectors: NIST/RFC, plus the pure-Raku `Digest` as a same-process
  oracle on arbitrary input (it is byte-correct on rakupp today).

## Order of work

~~After DATA-PLAN's P1–P5~~ — the modules were built first, and did not need
the primitives: they resolve their own extension, then their fallback, and the
engine rung is one the ladder simply has not reached yet.

1. **The shared corpora and vector sets**, in the engine repo, with the
   engine-side regression files that consume them. **The only step still
   open**, and now a question of direction rather than of writing them: they
   exist, in the distributions —
   `Digest-Native/t/vectors/digest.vec`, 156 vectors generated from the system
   `openssl`, and `Compress-Zlib-Native/t/vectors/zlib.vec`, 67 generated from
   real libz and the system `gzip`, including eight malformed streams that must
   be refused. Whether they move here or the engine's suite pulls them from
   there should be settled before P3 and P4 write the engine's copies.
2. ~~**`Digest::Native`**~~ **DONE.** ~450 lines of C, the composed fallback,
   the vectors as its gate. Measured at 317 MB/s for MD5 against 0.08 for the
   pure-Raku reference on rakupp.
3. ~~**`Compress::Zlib::Native`**~~ **DONE**, and further than planned: inflate
   *and* deflate, with dynamic Huffman rather than fixed. A fixed-Huffman
   encoder was written first and produced output 34% larger than libz, which
   was too visible a cost for the module's main use; with dynamic coding it is
   within 4% and smaller than libz at level 1. Inflate is fuzzed under ASAN and
   UBSAN, 360,000 streams, no report.
4. ~~**The cooperation protocol**, retrofitted into `JSON::Native` and
   `CSV::Native`~~ **DONE.** All four families now pass the order-independence
   table in both orders on both engines. CSV needed a split to get there —
   `CSV::Native::Core` holds the implementation with no export protocol, and
   `CSV::Native` is the importable face — because `Data::Native` has to
   delegate its `csv` tag to this distribution and a module that `use`s a
   protocol participant poisons its own registry read. `need` runs no
   `sub EXPORT` on either engine, which is what made the split work.

## Open decisions

1. ~~Do `JSON::Native` and `CSV::Native` keep their ext-ABI extensions?~~
   **Decided: yes.** Row 2 stays the fastest path on rakupp for value-building
   codecs, and version skew keeps it useful. Their C stays their own, per the
   architecture decision.
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
3. ~~`Crypt::Random::Native` at all?~~ **Decided 2026-09-05: not built.**
   The core primitive is the deliverable (see its section above).
4. ~~Vendoring vs a shared C distribution.~~ **Decided 2026-09-05: neither —
   independent implementations.** Sharing would pull the engine's codec toward
   a shape that suits the modules; see the architecture section for the
   reasoning and for the four things that keep divergence small instead.
5. ~~Does `Digest::Native` fall back to `Digest` or to bduggan's
   `Digest::SHA*::Native`?~~ **Decided 2026-09-05: bduggan's two dists as hard
   dependencies for SHA-1/256, `Digest` for the rest, `Digest::HMAC` for HMAC**
   — the `JSON::Fast` pattern, composed per algorithm. See the module's
   section.
