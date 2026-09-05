# Plan: `Data::Native` — one portable `use` for whatever the engine does natively

**Status: the module half is BUILT (2026-09-05); the engine half, P1-P6, is
not started.** `Data::Native`, `Digest::Native` and `Compress::Zlib::Native`
exist in `/Users/ash/raku-modules` with `JSON::Native` and `CSV::Native`
retrofitted alongside them — 1,058 assertions on rakupp, 1,063 on Rakudo.
Extension ABI 3 landed here (`rk_blob`). What remains is P1 to P6: the
`rakupp-*` primitives and `rakulib/Data/Native.rakumod`. Exactly one primitive
exists today, `rakupp-sha1-hex`, and it fails the contract check below.

Probes run 2026-09-05
against `build-arm64/rakupp` and Rakudo v2026.08; every claim marked "probed"
was checked on both. **Two claims in the first revision were wrong and are
corrected below** (`sub EXPORT` scope, and the `:tag` spelling) — the design
survives, the module's shape changed.

## Synopsis

```raku
use Data::Native;

my %rec  = name => 'Ada', langs => <Raku C>, score => 9.5;
my $json = to-json(%rec);
say from-json($json)<langs>[1];      # C
say sha256-hex($json);
say json-backend();                  # core       on Raku++
                                     # JSON::Fast on Rakudo, or any other Raku
```

**This file runs, unchanged, on every Raku.** What differs is what answers:

- On **Raku++**, every call above is the engine's own C. Nothing to install,
  nothing to compile — the functions are in the `rakupp` binary, and
  `use Data::Native` switches them on.
- On **Rakudo, or any other implementation**, `zef install Data::Native` once;
  the same calls then run through the `**::Native` distributions and the
  established modules they stand in for — `JSON::Native` → `JSON::Fast`,
  `Digest::Native` → bduggan's `Digest::SHA256::Native`, and so on. Slower, but
  correct, and the same results byte for byte.

Measured on the 278 KB diagnose corpus, same file, both engines:
`from-json` is 7 ms on Raku++ and 61 ms on Rakudo. That is the whole idea —
write it once, and it is fastest where the engine does it natively.

`use Data::Native <json digest>` takes only those tags; bare `use` takes all.

## The end state — every module, tag and function

What exists when both plans are implemented. Three inventories: the
distributions, the tags with their functions, and the engine primitives.

### Distributions

| distribution | status | interface it mirrors | fallback on other engines |
|---|---|---|---|
| `Data::Native` | **new** — shipped in `rakulib/` with the engine *and* on raku.land | the five tags below | the four `**::Native` modules below, plus `Crypt::Random` |
| `JSON::Native` | exists | `JSON::Fast` | `JSON::Fast` |
| `CSV::Native` | exists | its own (no usable reference existed) | its own pure-Raku `parse-raku`/`write-raku` |
| `Digest::Native` | **new** | `Digest` + `Digest::HMAC`, with the `-hex` twins bduggan spells | `Digest::SHA1::Native` + `Digest::SHA256::Native` (bduggan, C) for SHA-1/256; `Digest` for MD5/224/384/512; `Digest::HMAC` |
| `Compress::Zlib::Native` | **new** | `Compress::Zlib` | `Compress::Zlib` (NativeCall over system `libz`) |
| ~~`Crypt::Random::Native`~~ | not built | — | — |

Every `**::Native` module: our C on Raku++ (ext ABI, compiled at install),
the fallback everywhere else. `Data::Native`: the engine's built-ins on
Raku++, delegation to the row above elsewhere.

### Tags and functions — 27 functions, 5 `*-backend` subs, 32 names

`use Data::Native;` imports all of them; `use Data::Native <json csv>` the
named tags. On Raku++ every name is an engine primitive; the last column is
what answers on any other Raku.

| tag | functions | returns | elsewhere |
|---|---|---|---|
| `json` | `from-json` `to-json` | data / `Str` | `JSON::Native` → `JSON::Fast` |
| `csv` | `from-csv` `to-csv` | rows / `Str` | `CSV::Native` → its Raku implementation |
| `digest` | `md5` `sha1` `sha224` `sha256` `sha384` `sha512` | `blob8` | `Digest::Native` → bduggan's for SHA-1/256, `Digest` for the rest |
| | `md5-hex` `sha1-hex` `sha224-hex` `sha256-hex` `sha384-hex` `sha512-hex` | `Str` | same |
| | `hmac` `hmac-hex` | `Blob` / `Str` | `Digest::Native` → `Digest::HMAC` |
| `zlib` | `compress` `uncompress` (with `:gzip` / `:raw`) | `Buf` | `Compress::Zlib::Native` → `Compress::Zlib` |
| | `gzslurp` `gzspurt` | file ↔ data | same |
| | `crc32` `adler32` | `Int` | same (ours; the reference has none) |
| `random` | `crypt_random_buf` `crypt_random` `crypt_random_uniform` | `Buf` / `Int` / `Int` | `Crypt::Random` directly |
| each tag | `json-backend` `csv-backend` `digest-backend` `zlib-backend` `random-backend` | `Str`: `core`, `native`, or the fallback's name | — |

The `*-backend` subs are exported with their tag by default (open decision 2
below records the choice). Signatures are the reference's, character for
character; where a tag exceeds its reference the difference is listed in that
tag's section.

### Engine primitives — 27, one per function

Registered in `registerBuiltins()` under the mechanical spelling
`rakupp-<function>` — `rakupp-from-json`, `rakupp-sha256-hex`,
`rakupp-crypt_random_buf` — so the probe is `&::("rakupp-$name")` with no
mapping table, and `rakupp-sha1-hex`, which already exists, fits the rule as
is. These are the only names the engine knows; everything above is Raku on
top of them. Another implementation adopts the contract by supplying any
subset under its own prefix.

Three pieces, with one job each:

1. **`**::Native` distributions** (`JSON::Native`, `CSV::Native`, and the new
   `Digest::Native` and `Compress::Zlib::Native` —
   [NATIVE-MODULES-PLAN.md](NATIVE-MODULES-PLAN.md)) keep the interface of the
   popular module they stand in for, and bind **their own XS extension** —
   never an engine primitive. They serve every engine, Rakudo
   included.
2. **The engine implements the functions in the core**, as `rakupp-*`
   primitives.
3. **`Data::Native`** is one module that triggers those core functions. On an
   engine that answers, **no `**::Native` needs to be installed at all** — and
   the same `use` line still works elsewhere, where the distribution copy's
   `use`d reference modules answer instead.

The name is deliberately not `Rakupp::`. [EXTENSIONS.md](../../guide/EXTENSIONS.md#naming-the-rakupp-convention)
reserves that prefix for modules whose behaviour *depends* on Raku++; this one
is the opposite — a fixed, engine-neutral surface that any implementation can
answer natively. The name states the contract, not the supplier.

```raku
use Data::Native;                    # everything (the default)
use Data::Native <json csv>;         # or name the tags
use Data::Native <digest>;           # `:digest` works on rakupp only — see below

say from-json($text)<name>;
say to-csv(@rows, :headers);
say sha256-hex($blob);
```

## Why a module and not a pragma, an env var, or always-on builtins

An earlier draft of this plan registered the names as unconditional builtins.
That is rejected: it makes every program that uses them silently
Rakudo-incompatible, and the best repair available was a `--lint` note telling
you after the fact. A module that exists on both engines fixes the problem
instead of reporting it.

Env vars were considered and dropped for the reason the user gave — they are
less transparent — and for three more: they are invisible to `--lint`, they do
not travel into an `--exe` binary, and they are invisible to the person reading
the file.

## What another implementation would do to adopt it

`Data::Native` is a **contract** — a fixed list of names and signatures, one
tag per family — plus a probe. An engine adopts it in two steps and no more:

1. supply native subs for any subset of the names, under its own prefix (here
   `rakupp-from-csv`; another engine would pick its own), reachable by the
   runtime lookup `&::('…')` that every Raku compiles;
2. add its prefix to the module's probe list, one line.

Everything it does not supply keeps flowing to the `**::Native` distributions.
A partial adoption is a valid adoption — an engine that answers only `json`
gets exactly the `json` speed-up and nothing breaks. The probe is by symbol,
not by `$*RAKU.compiler.name`, so a fork or a renamed build is not locked out
by an identity check.

The distributions are Raku code with a compiled fast path where one exists, so
they are already the cross-engine reference implementation the contract is held
to. Nothing in this design gives rakupp a privileged position beyond being the
first engine to answer.

## What the probes say — and the one requirement they change

### `sub EXPORT` must be at FILE scope, and `:tag` is rakupp-only (probed, corrected 2026-09-05)

An earlier revision of this plan recorded "the tag mechanism works identically
on both engines". **That was wrong** — the probe file wrapped `sub EXPORT` in
`unit module`, and the Rakudo run silently exported nothing while a `from-json`
from elsewhere answered the call. Re-probed properly:

| | rakupp | Rakudo |
|---|---|---|
| `sub EXPORT` inside `unit module Foo;` | runs, exports | **never runs, exports nothing, no error** |
| `sub EXPORT` at file scope (no `unit module`) | runs, exports | runs, exports |
| `use Mod;` (bare) | `EXPORT saw: []` | same |
| `use Mod <json>;` | `["json"]` | same |
| `use Mod <json csv>;` | `["json", "csv"]` | same |
| `use Mod :json;` | `["json"]` | **compile error**: `Error while importing from 'Mod': no such tag 'json'` |

Two rules follow, both load-bearing:

1. **`Data::Native` has no `unit module` line.** `sub EXPORT` sits at the
   file's outermost scope. Inside a package declaration Rakudo does not find
   it, and the failure is *silent* — the worst possible shape.
2. **The documented tag spelling is `<json csv>`, not `:json`.** Rakudo routes
   `:tag` through the `is export(:tag)` selective-export machinery, which a
   `sub EXPORT` module has no part in. rakupp accepts `:json`; that is a
   divergence for the log, and the docs must not show a spelling that only
   works here.

### A failing `EXPORT` is swallowed on BOTH engines (probed)

A `sub EXPORT` that dies does **not** stop the program. rakupp prints
`===WARNING=== Module Foo EXPORT failed: …` and continues; Rakudo prints
nothing at all and continues. Either way the mainline runs with an empty
import, and the first call fails as `Undeclared routine`.

So **"die in EXPORT when a fallback is missing" is not available as a failure
mechanism.** The design instead is: **every tag always exports every one of
its names.** A name with no implementation on this engine is bound to a stub
that throws when *called*, naming the tag, the missing module and the fix
(`zef install …`). That is better than failing at `use` anyway — it is
identical on both engines, it is testable (the conformance gate asserts equal
name sets), and it does not punish a program that imports `:all` and calls
only `from-json`.

### `require` inside a module is not a usable fallback on Rakudo (probed)

Every conditional-load idiom tried inside `sub EXPORT` **or** at the module's
body scope broke the whole export on Rakudo — including the case where the
required module *is* installed (`require ::('Digest::SHA1')`, then
`::('Digest::SHA1::EXPORT::DEFAULT::&sha1')`, gives
`No such symbol`). rakupp handles all of them. The one idiom that works on
both is a plain `use` at file scope — but that is a hard dependency, which the
rakulib copy cannot have.

**Consequence: the rakulib copy and the distribution copy cannot be one
byte-identical file.** They are two files with one exported name set:

- `rakulib/Data/Native.rakumod` — engine primitives only, no `use` of anything.
  It ships with an engine that always has them; missing names bind to stubs.
- the distribution's `lib/Data/Native.rakumod` — `use`s the fallback modules at
  file scope (they are its META6 `depends`), and prefers a native primitive
  over them when one answers.

A CI test asserts the two export exactly the same names, and the cross-engine
conformance suite (see Gates) asserts they behave the same. That replaces the
"one file, shipped twice" idea, which the Rakudo probes ruled out.

### The engines disagree about import precedence (probed)

Two modules both exporting `&from-csv`:

| | rakupp | Rakudo |
|---|---|---|
| both use `is export` | silently, **last `use` wins** | **compile error**: `Cannot import symbol '&from-csv' from 'Other2', because it already exists in this lexical scope.` |
| one uses `sub EXPORT`, the other `is export` | last `use` wins | the **`is export` one wins, in either order** |

Two consequences, and the second changes the requirement:

1. **`Data::Native` must use the `sub EXPORT` protocol, not `is export`.**
   With `is export`, `use Data::Native; use CSV::Native;` is a hard compile
   error on Rakudo — and worse, it *works* on rakupp, so the program would be
   developed here and refused there. The `sub EXPORT` protocol is needed for
   the tags anyway, so this costs nothing.

2. **"The built-in must win regardless of `use` order" cannot be delivered by
   import precedence.** The two engines have different precedence rules and
   neither is ours to change — Rakudo prefers `is export` over `sub EXPORT` in
   *both* orders, rakupp prefers whichever came last. Any engine-side hack that
   made rakupp obey the rule would make rakupp and Rakudo disagree about which
   sub a program is calling, which is the failure this whole design exists to
   avoid.

   The requirement is instead met **by construction**, and it is the
   consistent-interface decision that buys it: if `CSV::Native` on rakupp
   *hands out the engine primitive itself*, then `use Data::Native` and
   `use CSV::Native` bind **the same `Callable` object**, and there is nothing
   left for the order to decide. Same on Rakudo, one level up: `Data::Native`
   delegates to `CSV::Native`, so whichever wins, the code that runs is
   `CSV::Native`'s.

   Order-independence is therefore a **property of the shared implementation**,
   not a rule the loader enforces. It is checked, not assumed — see Gates.

> Spin-off finding, out of scope here: rakupp accepts a duplicate import that
> Rakudo refuses to compile. Worth an entry in the spec-divergences log.

## The invariant

> **One tag ⇔ one reference interface ⇔ one family of engine primitives.**

The reference interface is **the ecosystem's de-facto standard where one
exists, and ours only where none does.** Which is which is a measurement, not
a taste — the reverse-dependency ranking in
[ECOSYSTEM-TOP100.md](../ecosystem/ECOSYSTEM-TOP100.md) — and the survey per
tag is in its section below. A `**::Native` distribution of ours exists for a
tag only when we had to *write* the reference (`CSV::Native`: `Text::CSV` is
15 s where the extension is 107 ms, and has a different API) or to *package an
extension* (`JSON::Native`). Where the ecosystem already has both a standard
and a native fast path, the tag delegates to the standard directly and we ship
nothing.

That is what "replaceable interface" means, precisely:

- the sub **names, signatures and return types** a tag exports are exactly the
  reference's — a program moves between `use Data::Native <digest>` and
  `use Digest::SHA2; use Digest::HMAC;` by editing one line, in either
  direction, on either engine;
- a tag may be a **superset** (accept more input types, define twins the
  reference lacks) but never disagrees with the reference on an input both
  accept, unless the reference is wrong against its own standard — and then
  the divergence is written down;
- every distribution of ours exposes `<thing>-backend()` returning `native`
  (compiled extension), `engine` (the builtin) or the portable fallback's name;
- every distribution of ours reaches the engine the portable way —
  `try &::('rakupp-…')` — never by a name the compiler special-cases.

## The three layers

### L1 — engine primitives, `rakupp-*`

Registered in `registerBuiltins()` beside the existing `rakupp-ext-load`,
`rakupp-sha1-hex` and `rakupp-parse-diagnosis`
([Builtins.cpp:8665](../../../src/Builtins.cpp#L8665)), and reached the way
[EXTENSIONS.md](../../guide/EXTENSIONS.md#writing-a-portable-module) already
documents:

```raku
my &fast = try &::('rakupp-from-csv');   # Nil on Rakudo, sub here
```

**These are the only names the engine knows.** Nothing else in this plan is
compiler-visible.

### L2 — the `**::Native` distributions: XS only

**A `**::Native` module of ours binds its compiled extension and nothing
else.** It never reaches for an engine primitive. Its backends are, in order:
the XS extension it ships, then the portable Raku fallback (its own, or the
ecosystem reference it stands in for). `json-backend()` returns `native` or
`JSON::Fast`; the `engine` value that `JSON::Native` reports today goes away.

That is a change from the first revision, which had these modules re-export
the engine primitive so that `use` order could not matter. Binding XS only is
the better rule for one concrete reason already written into
[EXTENSIONS.md](../../guide/EXTENSIONS.md#naming-the-rakupp-convention): when
an early `JSON::Native` was built into the interpreter, **the distribution's
own test suite silently exercised the built-in copy instead of the extension
it was written for.** A module that binds the primitive re-creates exactly
that hole — its suite would stop testing its own C. Separate implementations
mean each suite tests its own code, and the conformance gate is what proves
they agree.

**Correction to an earlier line in this plan: the extension ABI is
rakupp-only, so these modules do NOT accelerate Rakudo.** `rakupp_ext.h` is
rakupp's own, and a module reaches it through `&::('rakupp-ext-load')`, which
resolves only here (probed: `True` on rakupp, `False` on Rakudo). What a
Rakudo user gets from `use JSON::Native` is the *fallback* — probed:
`json-backend()` says `native` on rakupp and `JSON::Fast` on Rakudo.

That reframes what a `**::Native` module is for, and the honest list is short:

1. **Version skew.** An engine that predates a tag still gets the family by
   `zef install`ing the module — no engine upgrade needed. This is the
   strongest reason, and it is the reason the XS half does not simply become
   dead weight once the core implements a family.
2. **Independent release cadence.** The module versions on its own schedule;
   the core versions with the engine.
3. **Families the core deliberately will not carry**, for size or scope. That
   is the genuine long-term niche for an extension.

It is *not* "Rakudo users get C speed" — they do not, and under the settled decision they will not. A NativeCall path was designed and dropped: measured, it would make Raku++ *slower* (see [NATIVE-MODULES-PLAN.md](NATIVE-MODULES-PLAN.md#why-not-nativecall--measured)). Other engines get the module's fallback — the established module it stands in for — which works everywhere and is why the same program runs on any Raku.

### L3 — `Data::Native`: the core, and only the core

**`Data::Native` binds engine primitives.** It installs no extension, compiles
nothing, and — on an engine that answers — needs no `**::Native` module
installed at all. That is the whole point of it: one `use` line, zero install
steps, on any engine whose primitives answer.

Where a primitive is absent, the name is still exported, bound to a stub that
throws on call naming the tag, the module that supplies it, and the
`zef install` line (see the swallowed-`EXPORT` probe above). Two files, one
export list:

- **`rakulib/Data/Native.rakumod`** — primitives only, no `use` of anything,
  shipped with the engine. Every name either resolves to a primitive or is a
  stub.
- **the distribution's `lib/Data/Native.rakumod`** — same export list, plus
  `use` of the reference modules at file scope (its META6 `depends`) so a
  Rakudo user gets working functions rather than stubs. A primitive still wins
  where one answers.

Neither has a `unit module` line: `sub EXPORT` must sit at file scope or
Rakudo never runs it.

```raku
# one prefix per engine that answers natively; the adoption hook is one line
my constant @PREFIXES = <rakupp>;

my %TAGS =
    json   => { names => <from-json to-json>,                  needs => 'JSON::Fast'    },
    csv    => { names => <from-csv to-csv>,                    needs => 'CSV::Native'   },
    digest => { names => <md5 sha1 sha256 sha512 hmac …>,      needs => 'Digest::Native' },
    zlib   => { names => <compress uncompress gzslurp gzspurt>, needs => 'Compress::Zlib::Native' },
    random => { names => <crypt_random_buf crypt_random …>,    needs => 'Crypt::Random' };

sub native(Str $name) {
    for @PREFIXES -> $p { with try &::("$p-$name") { return $_ } }
    Nil
}
sub stub(Str $tag, Str $name, Str $needs) {
    sub (|) { die "Data::Native: <$tag> needs a native `$name` this engine does not"
                ~ " supply, and $needs is not installed here — `zef install $needs`" }
}
sub EXPORT(*@tags) {
    my $all = !@tags || @tags.any eq 'all';
    my %e;
    for %TAGS.kv -> $tag, %t {
        next unless $all || @tags.any eq $tag;
        for %t<names>.list -> $n {
            %e{"&$n"} = native($n) // fallback($tag, $n) // stub($tag, $n, %t<needs>);
        }
    }
    Map.new(%e)
}
```

`fallback()` is the only line that differs between the two files: it returns
`Nil` in the rakulib copy, and the `use`d reference module's sub in the
distribution copy.

#### `native($n) // fallback // stub` is the settled resolution order — measured

Decided 2026-09-05 after the alternative was costed. The alternative was to
have the engine RECOGNISE `use Data::Native` by name, the way it already
recognises `use Test` and `use NativeCall` ([Interpreter.cpp:2060](../../../src/Interpreter.cpp#L2060)),
and answer it with no file at all. That is not taken, because the ladder above
already reaches every property it would have bought:

| | this design | engine recognition |
|---|---:|---:|
| per call | **0.948 µs** | the same builtin under the same name |
| the `use` itself | +0.1 ms | ~0 |
| installs needed on rakupp | **none** | none |
| other modules parsed | **none** | none |
| `--exe` | **native-compiles, runs standalone** | same |

Three measurements make that table, all on `build-arm64/rakupp`, arm64:

- **The exported builtin is the fastest thing available.** `%e{"&$n"} = native($n)`
  hands out the *primitive object itself*, so a call reaches it with no Raku
  frame: **0.948 µs**, against 1.147 for the same builtin through a captured
  `&::()` and 1.257 behind a one-frame wrapper. A wrapper would cost 33%.
  Whatever else changes, `native()` must keep returning the sub rather than
  something that calls it.
- **The rakulib copy is already free**: a dependency-free module of this size
  costs +0.1 ms to `use`, warm cache. The 26 ms that `use Data::Native` costs
  today is entirely its *distribution* copy loading nine fallback modules —
  which is exactly what the rakulib copy exists to avoid, and why it `use`s
  nothing.
- **`rakulib/` is searched before the install store.** It joins `libPaths_`
  ([Interpreter.cpp:2684](../../../src/Interpreter.cpp#L2684)) and those are
  phase 1 of the resolver, stores phase 2. So an installed `Data::Native` does
  not shadow the engine's copy, and a program on rakupp needs nothing installed
  — without any special-casing.

Against that, recognition would move the tag table, the stub messages, the
unknown-tag error, `<all>` and the claim-registry write into C++, where the
equal-export-sets gate below gets *harder* rather than disappearing.

**What the store-first ordering costs, and the escape.** Because rakulib wins,
an installed newer `Data::Native` is not picked up — the same loss recognition
would have had. The escape is a versioned `use`: `use Data::Native:ver<0.3+>`
must skip rakulib and resolve against the store, so an update can be shipped
without an engine release. That is P6's job and is the reason the `metaVersion`
fallback is listed there.

#### `native()` must probe FUNCTIONALLY, not by name

`try &::("$p-$name")` finding a symbol is not enough, and this is not
hypothetical: **`rakupp-sha1-hex` exists today and returns UPPERCASE hex**,
where bduggan's dists, `OpenSSL::Digest` and this tag all return lowercase. A
by-existence probe would silently change what the module answers the day it
meets that engine.

So a primitive is adopted only if it reproduces a known value — one hash of
`"abc"`, one round trip, one published check string — **and returns the right
type**, since a `-hex` primitive handing back a Blob is not the sub this tag
promises. Lowercasing the answer before comparing is precisely how the
uppercase one would slip through. The distribution copy implements this; the
rakulib copy must too, and P1 should register `rakupp-sha1-hex`'s replacement
in lowercase rather than leave the tag working around it.

#### `Data::Native` wins for its claimed tags, in either order (probed)

The rule: `use Data::Native` beats our `**::Native` modules for the tags it
claimed, whatever the `use` order — but only those tags, so
`use Data::Native <csv>; use JSON::Native;` still takes `to-json` from
`JSON::Native`.

**This is not optional, because Rakudo makes the alternative a compile error.**
Two modules exporting the same name:

```
Cannot import symbol '&to-j' from 'JN', because it already exists in
this lexical scope.
```

So our modules must cooperate or the program does not build. With cooperation —
the second module omitting the contested name from its Map — the rule holds
exactly, probed in all four cells:

| | rakupp | Rakudo |
|---|---|---|
| `use Data::Native; use JSON::Native;` | `CORE` | `CORE` |
| `use JSON::Native; use Data::Native;` | `CORE` | `CORE` |

The protocol — a per-tag claim registry, and a dispatcher for the case where
the `**::Native` module loads first and cannot yet know what is coming — is
specified in
[NATIVE-MODULES-PLAN.md](NATIVE-MODULES-PLAN.md#datanative-must-win--the-cooperation-protocol).

## The tag table — every name, and what it replaces

Five tags. `use Data::Native;` imports all of them; `use Data::Native <json
csv>` imports those two. **`<...>` is the spelling — `:json` compiles only on
rakupp.**

### `json` — 2 names

| | |
|---|---|
| exports | `from-json` `to-json` |
| reference | `JSON::Fast` (rank 1, **170** run-deps) — signatures character for character, output byte-identical |
| also drop-in for | `JSON::Tiny` (rank 6, 40) — same two names |
| our XS module | `JSON::Native` |
| engine state | **codec already in the binary** (`jsonParseValue`/`jfEncode`, [Builtins.cpp:3377](../../../src/Builtins.cpp#L3377)); needs a name, ~200 lines |

### `csv` — 2 names

| | |
|---|---|
| exports | `from-csv` `to-csv` |
| reference | **ours** — `CSV::Native`. `Text::CSV` (rank 94, 7) is 15,245 ms where the extension is 107 ms *and* a different API, so there was nothing to be replaceable with |
| our XS module | `CSV::Native` |
| engine state | to write: `src/DataCsv.{h,cpp}`, a port of the distribution's `csv.c` (498 lines) |

### `digest` — 14 names

| | |
|---|---|
| exports | `md5` `sha1` `sha224` `sha256` `sha384` `sha512` → `blob8`; `md5-hex` … `sha512-hex` → `Str`; `hmac` `hmac-hex` |
| reference | `Digest` (rank 22, 20) for the bare names — `Digest::MD5`/`SHA1`/`SHA2`; `Digest::HMAC` (rank 18, 22) for `hmac`/`hmac-hex` |
| also drop-in for | `Digest::SHA256::Native` (rank 44, 12) and `Digest::SHA1::Native` (rank 51, 11) — the `-hex` twins are their spelling; `OpenSSL::Digest` — same full set |
| our XS module | `Digest::Native` — decided later the same day; see [NATIVE-MODULES-PLAN.md](NATIVE-MODULES-PLAN.md) |
| engine state | SHA-1 in-tree ([Interpreter.cpp:102](../../../src/Interpreter.cpp#L102)); SHA-256 + HMAC in-tree but file-local to `JupyterKernel.cpp`; MD5 and SHA-512 to write |

### `zlib` — 6 names

| | |
|---|---|
| exports | `compress` `uncompress` (`:gzip`/`:raw` adverbs are ours) `gzslurp` `gzspurt` `crc32` `adler32` |
| reference | `Compress::Zlib` (rank 83, 8) — `compress(Blob, Int $level?)`, `uncompress(Blob)`, `gzslurp`/`gzspurt` |
| unblocks | `PDF` `Image::PNG::Portable` `Archive::SimpleZip` `File::Zip` `Avro` `SAT` `Sitemap` `pack6` — all dead on rakupp today |
| our XS module | `Compress::Zlib::Native` — see [NATIVE-MODULES-PLAN.md](NATIVE-MODULES-PLAN.md) |
| engine state | nothing. RFC 1950/1951/1952; inflate ~300 lines, deflate ~300 more. **The only tag that adds a capability rather than speed** |

### `random` — 3 names

| | |
|---|---|
| exports | `crypt_random_buf` `crypt_random` `crypt_random_uniform` (underscores are the reference's) |
| reference | `Crypt::Random` (rank 89, 7) — the only such dist in the top 100, converged by default |
| our XS module | **none** — the OS call *is* the implementation |
| engine state | nothing. `getrandom(2)` / `/dev/urandom` / `BCryptGenRandom`, ~60 lines, rejection sampling for `_uniform` |

### Totals

27 names, 5 tags, ~1,800 lines of new C++. Four `**::Native` distributions:
`JSON::Native` and `CSV::Native` exist; `Digest::Native` and
`Compress::Zlib::Native` are designed in
[NATIVE-MODULES-PLAN.md](NATIVE-MODULES-PLAN.md); `random` is engine-only.


## The tags — detail per family

### `json` — `JSON::Native`

`from-json`, `to-json`, `json-backend`. JSON::Fast's signatures character for
character; output byte-identical.

**Nearly free**: the codec is already in the binary and already pinned against
JSON::Fast — `jsonParseValue`, `jfEncode`, `jfEscape`
([Builtins.cpp:3377–3760](../../../src/Builtins.cpp#L3377)). What is missing is
a name to reach it by: today there is only the compact-only
`Rakupp::Internals::JSON` class, and a wrapper that requires JSON::Fast to be
installed. Gains `JSON::Native` a fast `to-json` with no C compiler and no
JSON::Fast dependency.

### `csv` — `CSV::Native`

`from-csv`, `to-csv`, `csv-backend`. `$src` is `Str`, `IO::Path` or
`IO::Handle`, as the module takes it. TSV falls out: `:sep("\t")`.

**The real work**: a port of the distribution's `src/csv.c` (498 lines) to C++
over `Value` — a port, not a reimplementation, because the extension is what
the module's suite already pins, and re-deriving the edge cases (doubled
quotes, quoted fields spanning lines, CRLF as one grapheme, `:strict`,
duplicate header names) is how divergences get invented. From the module's
measured table (arm64 Mac, Raku++ 3.24.0, 2026-09-02; 100,000 rows / 8.5 MB):

| | parse | parse `:headers` | write |
|---|---:|---:|---:|
| extension | 107 ms | 150 ms | 48 ms |
| Raku implementation, on rakupp | 1,075 ms | 1,778 ms | 3,551 ms |

The engine backend lands on the first row with no build step.

### `digest` — reference: `Digest` + `Digest::HMAC`; no distribution of ours

**The survey.** Reverse runtime-dependents over the REA snapshot of
2026-08-31 ([ECOSYSTEM-TOP100.md](../ecosystem/ECOSYSTEM-TOP100.md)), plus
raku.land on 2026-09-05, plus the installed sources read directly:

| rank | dist | run-deps | exports |
|--:|---|--:|---|
| 18 | `Digest::HMAC` (zef:jjmerelo) | 22 | `hmac($key, $message, &hash, $blocksize = 64) → Blob`, `hmac-hex(…) → Str`. A `Str` key/message is `.encode('ascii')`. |
| 22 | `Digest` (zef:grondilu) | 20 | **no `Digest` module** — provides `Digest::MD5` (`md5 → Blob`), `Digest::SHA1` (`sha1 → blob8`), `Digest::SHA2` (`sha224 sha256 sha384 sha512 → blob8`; `sha256`/`sha512` take `:initial-hash`), `Digest::SHA3`, `Digest::RIPEMD`. **No `-hex` twins.** Pure Raku. |
| 44 | `Digest::SHA256::Native` (zef:bduggan) | 12–13 | `sha256-hex(Str\|Blob\|Supply\|IO) → Str`, `sha256(…) → Blob`. C via NativeCall. |
| 51 | `Digest::SHA1::Native` (zef:bduggan) | 11 | `sha1-hex(Str\|Blob) → Str`, `sha1($in) → Blob`. C via NativeCall. |
| — | `OpenSSL::Digest` (in `OpenSSL`) | — | `md5 md5-hex sha1 sha1-hex sha224 … sha512-hex` |
| — | `Digest::MD5` (github:cosimo, 2017) | not top-100 | a *second dist* whose module name collides with grondilu's `Digest::MD5` |
| 89 | `Crypt::Random` | 7 | the only `Crypt::` in the top 100; no `Crypto::` exists on raku.land at all |

**The ecosystem has converged.** Three unrelated authors agree: bare name →
`Blob`, `-hex` twin → `Str`, and `Digest::HMAC` is the one HMAC interface,
taking the hash *as a sub*, which composes with the bare subs. So the tag's
export list is the **union** of those conventions, and every existing program
written against any row above swaps in `use Data::Native <digest>` unchanged:

```
md5 sha1 sha224 sha256 sha384 sha512        → blob8, as Digest::SHA2 types it
md5-hex sha1-hex … sha512-hex               → Str, as bduggan and OpenSSL::Digest spell it
hmac($key, $message, &hash, $blocksize?)   → Blob
hmac-hex(…)                                → Str
```

**`Digest::Native`, not `Crypto::Native`.** `Crypto::` is not a Raku
namespace (the Perl-heritage prefix is `Crypt::`, and its whole top-100
presence is one RNG); `Digest::` is where every module in the table above
lives. The distribution itself — our C on Raku++, bduggan's two native dists
plus `Digest` and `Digest::HMAC` as its composed fallback elsewhere — is
designed in [NATIVE-MODULES-PLAN.md](NATIVE-MODULES-PLAN.md). On other engines
`Data::Native`'s distribution copy reaches this tag through it, the same way
the `json` tag goes through `JSON::Native`.

**Where the tag is a superset of the reference — written down:**

- **`IO::Path` / `IO::Handle` input**, as `Digest::SHA256::Native` takes it:
  the primitive streams the file. `Supply` is not taken.
- **HMAC block size.** `Digest::HMAC` defaults `$blocksize` to 64 for every
  hash, so `hmac($k, $m, &sha512)` there is a *non-standard* MAC (RFC 2104
  wants B = 128 for SHA-384/512) unless the caller remembers to pass 128. The
  tag's `$blocksize` is optional with **no default**: absent, and `&hash` one
  of the tag's own, the primitive uses the hash's real block size; given, it
  is honoured as written. Identical to `Digest::HMAC` on every call that is
  correct there; the RFC value where `Digest::HMAC` is wrong. (House rule: the
  standard wins where rakupp is closer to it.)
- **HMAC `Str` inputs are `utf8`**, not `ascii`. Identical bytes for every
  input `Digest::HMAC` accepts; works where it dies.
- **`&hash` other than the tag's own** — `hmac` calls it twice, which is all
  HMAC is, so a user-supplied hash keeps working. The native path is only for
  the tag's own subs, recognised by identity.
- **Not supported, throws**: `:initial-hash` on `sha256`/`sha512` (a
  `Digest::SHA2` internal that leaks through its signature); SHA-3, RIPEMD
  and the other `Digest` sub-modules. The tag covers `Digest::MD5`,
  `Digest::SHA1`, `Digest::SHA2` and `Digest::HMAC` — the four with
  dependents.

**The reference runs correctly on rakupp today (probed).** `md5`, `sha1`,
`sha256`, `sha512` of `"abc"` and `hmac-hex(…, &sha256)` from the pure-Raku
`Digest`/`Digest::HMAC` match Rakudo and `shasum` byte for byte on
`build-arm64/rakupp` — the SHA-512 gap
[MODULE-FINDINGS.md](../ecosystem/MODULE-FINDINGS.md) recorded on 2026-08-03
is closed. So the primitives have their oracle *on this engine*: the gate can
compare primitive against pure-Raku reference in one process, on any input,
rather than only against fixed vectors. (Spin-off for the divergences log:
`md5("abc").^name` is `Buf` here and `Buf[uint8]` on Rakudo — a `.^name`
cosmetic, not a digest difference.)

**Fallback hazard on Rakudo**: `require ::('Digest::MD5')` may find cosimo's
dist rather than grondilu's module if that is what is installed, and it does
not export `md5`. The distribution's META6 pins `Digest:auth<zef:grondilu>`,
and the `require` checks `&md5` resolved before trusting it.

**Implemented in-tree, not through a library.** The engine is C++17 and the
standard library, and dlopens nothing for crypto — TLS comes from the
ecosystem's `OpenSSL` distribution over NativeCall
([HTTPS.md](../../guide/HTTPS.md)), not from the engine. Digests are the one
crypto family where writing them is ordinary: fixed, closed specifications
(RFC 1321, FIPS 180-4, RFC 2104), no secret-dependent control flow, and
published vectors. Half of it is already compiled in that way. A dlopen'd
`libcrypto` was considered and rejected: it would put a runtime dependency
under a core-language surface, vary by platform (LibreSSL on OpenBSD; the
OpenSSL 1.1/3 ABI split), and be absent in the WASM playground and in an
`--exe` binary — and for a Rakudo user who wants the C-over-NativeCall route,
bduggan's `Digest::SHA256::Native` already is that. If the surface ever grows
to ciphers or AEAD, *that* is when a library is the right call, not this.

Measured 2026-09-05, `build-arm64/rakupp`, arm64 Mac:

| | throughput |
|---|---:|
| pure-Raku `Digest::SHA1` on rakupp, 1 MB | 0.05 MB/s (18.3 s) |
| pure-Raku `Digest::SHA2` `sha256` on rakupp, 1 MB | 0.04 MB/s (27.3 s) |
| in-tree scalar SHA-1 (`rakupp-sha1-hex`), 16 MB | 261 MB/s |
| `openssl sha256` CLI, 16 MB, *including* process start | 499 MB/s |

The CLI row under-reports OpenSSL (its in-process, hardware-SHA rate on this
chip is well above that); the point is the other direction. The scalar
in-tree code is ~5,000× what a rakupp program gets from the pure-Raku
reference today, and the gap to hardware-accelerated OpenSSL is ~10× — the
first gap is the one a script notices.

| | where | state |
|---|---|---|
| SHA-1 | [`sha1hex`, Interpreter.cpp:102](../../../src/Interpreter.cpp#L102) | shared already (`Interpreter.h:142`) |
| SHA-256, HMAC-SHA256 | [`Sha256` / `hmacSha256Hex`, JupyterKernel.cpp:86](../../../src/JupyterKernel.cpp#L86) | works, RFC 4231-gated by `tools/jupyter-smoke.raku`, but **file-local** |
| MD5, SHA-512 | — | to write, ~120 lines each; SHA-224 and SHA-384 are the 256/512 cores with other IVs and a truncation |

So P3 begins by lifting `Sha256`/`hmacSha256Hex` out of `JupyterKernel.cpp`
into `src/Digest.{h,cpp}` and having the kernel call it — one refactor, no
duplicated implementation, and the RFC 4231 gate keeps covering it.
`JupyterKernel.cpp` is in `rakupp_rt`, not a `--slim` feature archive
(CMakeLists.txt:162, 190), so nothing about the slim seam changes.

### `zlib` — reference: `Compress::Zlib`

`compress`, `uncompress`, `gzslurp`, `gzspurt`, `crc32`, `adler32`,
`zlib-backend`.

**Re-probed 2026-09-05 against rakupp 3.25.0, and this section's premise has
half expired.** `Compress::Zlib` (rank 83, 8 run-dependents) is NativeCall over
`libz`, and its one-shot subs now **work** on rakupp — 105,000 B to 303 B and
back, byte-identical. What still fails is its file wrappers: `gzslurp` and
`gzspurt` go through a `Wrap` class that calls `nqp::p6definite` and die there.

So this is not "the one family where rakupp has no capability at all" any more.
What is left is still real — the file wrappers, no system `libz` dependency, and
the `--exe` and WASM cases below — but the tag is a capability gap for part of
the surface rather than all of it, and the docs should say so rather than repeat
the older framing.

The specs are closed and small: RFC 1951 (deflate), 1950 (zlib wrapper), 1952
(gzip wrapper). Inflate ~300 lines, deflate ~300 more (LZ77 + fixed and
dynamic Huffman), CRC-32 and Adler-32 fall out of the wrappers.

Reference signatures, mirrored exactly:

```
compress(Blob $data, Int $level?)  → Buf      # zlib format, level 0-9, default 6
uncompress(Blob $data)             → Buf
gzslurp($filename, :$bin)                      gzspurt($filename, $stuff, :$bin)
```

**Superset, written down:** `:gzip` and `:raw` adverbs on `compress`/
`uncompress` — the reference reaches those two framings only through its
`Compress::Zlib::Stream` class, and raw deflate plus gzip are exactly what an
HTTP `Content-Encoding` needs. `crc32`/`adler32` are exposed because the
implementation has them anyway and `Compress::Zlib` does not offer them.
**Not in the first cut:** the `Compress::Zlib::Stream` class and `zwrap`
handle-wrapping — streaming inflate/deflate is a second phase, and the
one-shot subs are what the dependents call.

> **This reinterprets a standing rule, deliberately, and the decision is
> recorded here** (user, 2026-09-05). [install.raku:5](../../../tools/install.raku#L5)
> says librakupp "must not carry an HTTP client, an index parser or a tar
> reader", and the plan there sketches a *dlopen'd* zlib as the
> self-containment refinement. An in-tree inflate is none of those three
> things, is smaller than the CSV codec, and unlike a dlopen'd libz it works
> in the WASM playground and inside an `--exe` binary — which is where the
> dependents above are otherwise dead. The installer keeps shelling out to
> `curl` and `tar`; that rule is untouched.

### `random` — reference: `Crypt::Random`

`crypt_random_buf`, `crypt_random`, `crypt_random_uniform`, `random-backend`.

Core Raku has no CSPRNG API — `rand` is not one — and a tag that ships `hmac`
invites "and where do I get a key". `Crypt::Random` (rank 89, 7
run-dependents) is the only such distribution in the top 100, so it is
converged by default:

```
crypt_random_buf(uint32 $len)                        → Buf
crypt_random(PosUInt32 $size = 4)                    → Int
crypt_random_uniform(Int $upper_bound, PosUInt32 $size = 4) → Int
```

The underscore spellings are the reference's and are kept; hyphenated twins
are an open question below, not a default. ~60 lines over `getrandom(2)`,
`/dev/urandom` and `BCryptGenRandom` — no library, same reasoning as the
digests, and this is the one place where *not* using the OS primitive would be
the error. `crypt_random_uniform` rejection-samples rather than taking a
modulus, so the distribution is flat.

This is the only tag admitted on a missing-capability argument with no
measured speed gap.

### What else was considered — measured, not guessed

The question "what belongs in `Data::Native`" has a test: a **fixed, closed
spec** (so a native implementation can be finished and pinned), a **converged
ecosystem interface** to be replaceable with (or none usable, as with CSV), and
a **measured gap** on rakupp that a program notices. Every family in the
top-100 that looked like a candidate was run on both engines on this box,
2026-09-05 (`build-arm64/rakupp`, Rakudo v2026.08):

| family | rank / run-deps | rakupp | Rakudo | verdict |
|---|---|---:|---:|---|
| XML, `XML` `from-xml`, 28 KB / 250 records | 12 / 33 | **65 ms** | 154 ms | **no gap** — rakupp is 2.4× faster. Nothing to do. |
| URI encoding, `URI::Encode` `uri_encode_component`, 300 KB | 8 (`URI`) / 37, 15 / 27 | **623 ms** | 11,300 ms | **no gap** — rakupp is 18× faster. And two interfaces (`URI::Escape`'s `uri-escape` in the `URI` dist, `URI::Encode`'s `uri_encode`/`_component`) that are genuinely different functions, not spellings. Nothing to do. |
| UUID, `UUID.new` ×2000 | 28 / 18, `UUID::V4` 46 / 13 | **19 ms** | 43 ms | **no gap**. Two shapes too (a class, a `uuid-v4()` sub). `uuid4()` already exists in-tree for Jupyter; exposing it is a one-liner if a `random` tag ever lands, but there is no speed case. |
| YAML, `YAMLish` `load-yaml`, 20 KB / 250 records | 7 / 38 | **fails** | 427 ms | **an engine bug, not a tag.** `load-yaml` returns a Failure on rakupp — `Type check failed for an element of %callbacks; expected Callable but got Whatever (*)` — for the simplest sequence of maps. The sweep's "own suite passes" did not cover this shape. Spun off as its own task. Speed is unmeasurable until it parses. The spec verdict stands regardless: YAML 1.2 is not a thing to pin in-tree; when it works, a fast-path wrapper on the loaded module (the JSON::Fast pattern) is the lever, not a codec. |
| base64 | 10 / 35, 35 / 15, 95 / 7 | — | — | **not converged**: three incompatible interfaces (see the row below). Deferred on that ground. |
| gzip / zlib, `Compress::Zlib` | 83 / 8 | **self-fail** | works | **adopted** as the `zlib` tag (above) — the only family where rakupp has no capability at all, rather than a slow one. |
| secure random bytes, `Crypt::Random` | 89 / 7 | — | — | **adopted** as the `random` tag (above), on a missing-capability argument. |
| TOML | `Config::TOML` 84 / 8; `TOML` in the dep layer | — | — | later, maybe: a small closed spec, but two interfaces and nothing of ours needs it. |
| CBOR, MessagePack | dep layer only | — | — | closed specs, byte loops — the right *shape* — but no demand in the ranking. Later if asked. |
| INI | — | — | — | no. No spec, only dialects. |
| XML/HTML *escaping* | — | — | — | four characters; needs no native help. |
| streaming `csv-rows` | — | — | — | not a tag; a later addition to `csv`, and the one thing the modules structurally cannot do well (CSV::Native's README lists it under Scope for exactly that reason). |

The base64 detail, since it is the one that *looks* like it belongs:
`MIME::Base64` (rank 10, **35** run-deps — more than any digest dist) is
class-method shaped, `MIME::Base64.encode(Blob, :oneline, :eol)`/`.decode`/
`.encode-str`; `Base64` (zef:ugexe, rank 35, 15) exports subs
`encode-base64(Str|Blob, :pad, :uri, :str)`/`decode-base64(…, :uri, :bin)`;
`Base64::Native` (zef:dwarring, rank 95, 7, C via NativeCall) exports
`base64-encode(…, :str, :uri) → Buf|Str`/`base64-decode → Buf`. A
sub-exporting tag cannot stand in for the class-shaped leader, and picking
either sub spelling ratifies one minority over the other. Two honest routes if
the speed is wanted: mirror ugexe's spelling as a `base64` tag and say so, or
give the loaded `MIME::Base64` class a native fast path the way
`wrapJsonFastExports` does for JSON::Fast, which serves the 35 without changing
any interface. Not `Encode::` in any case — that prefix means charsets.

**Scope line that falls out**: `Data::Native` is *bytes and structures under
closed specs* — serialization, tabular data, digests, and (if chosen)
compression and random bytes. Not dates, not templates, not HTTP, not
ciphers.

## The one real semantic decision: no module to fall back on

`jsonFastToJsonCall`/`jsonFastFromJsonCall` are written as *fast paths*:
anything they do not cover calls the module's own sub
([Builtins.cpp:3763](../../../src/Builtins.cpp#L3763)). Correct for a wrapper,
impossible for a primitive. Each case must be decided:

- **`from-json` on bad input** — throw. Type stays `X::AdHoc`, so `CATCH`
  blocks written against JSON::Fast still catch it; the message is ours to
  write and should carry the byte offset and line/column, which JSON::Fast's
  does not. (House rule: rakupp does not copy another implementation's prose
  unless a test asserts the wording.)
- **`to-json` of a value outside the ladder** (object, `Code`, `DateTime`) —
  throw, naming the type. JSON::Fast's `jsonify` dies here too; inventing a
  rendering is the one thing a compatible codec must never do.
- **`:sorted-keys` with a `Callable`** — support it. The wrapper punts because
  delegating is cheaper than calling back into Raku; the primitive has the
  `Interpreter&`.
- **NaN / Inf `Num`** — read `$*JSON_NAN_INF_SUPPORT` and throw when unset, as
  the module does.
- **`to-json(Mu)`** — JSON::Fast's parameter is `Any`, so its *binder* rejects
  it; the primitive must too, or this prints `null` here and dies there. The
  existing fast path carries the carve-out already
  ([Builtins.cpp:3611](../../../src/Builtins.cpp#L3611)).

`wrapJsonFastExports` is untouched: `use JSON::Fast` keeps its wrapper and
keeps behaving as the module in every uncovered case. Both call one `jfEncode`.

## Order of work

P7 is done; P1 to P6 are not started. Nothing in P1-P6 is blocked by anything
outside this repository.

- **P1 — L1 JSON primitives.** Split the two wrapper functions into an
  argument-parsing half and an on-uncovered half; the wrapper passes a
  delegator, the primitive a thrower. Register `rakupp-from-json` /
  `rakupp-to-json`. New code: the `Callable :sorted-keys` path and the
  `$*JSON_NAN_INF_SUPPORT` read. No new file, under ~200 lines.
- **P2 — L1 CSV primitives.** `src/DataCsv.{h,cpp}`, the port of `csv.c`.
  ~500 lines.
- **P3 — L1 digest primitives.** Lift `Sha256`/`hmacSha256Hex` to
  `src/Digest.{h,cpp}`; add MD5, SHA-512 and the 224/384 variants; register
  the bare and `-hex` names and `hmac`/`hmac-hex`. Gate: the six
  `Digest::SHA2`/`Digest::HMAC` test files, and the NIST/RFC vectors the pure
  Raku modules already carry, run against the primitives.
- **P4 — L1 zlib primitives.** `src/DataZlib.{h,cpp}`: inflate first (it is
  what unblocks the dependents), then deflate, then the zlib/gzip/raw
  framings and CRC-32/Adler-32. Gate: round-trip against `gzip`/`gunzip` and
  `openssl zlib` on the corpora, plus fixed vectors, plus `Compress::Zlib`'s
  own suite on Rakudo as the oracle for the interface.
- **P5 — L1 random primitives.** `getrandom(2)` / `/dev/urandom` /
  `BCryptGenRandom` behind one entry point; rejection sampling for
  `crypt_random_uniform`. Gate: a distribution check on the uniform sampler
  and a "never returns the same buffer twice" smoke.
- **P6 — `rakulib/Data/Native.rakumod`** and its `t/` coverage. The engine
  half is finished at this point; this is a Raku file and a search-path
  check. Includes the `metaVersion` fallback so a versioned `use` resolves
  against a rakulib module (see L3).
- **P7 — the distributions.** ~~After the engine ships the primitives, never
  before.~~ **DONE 2026-09-05, ahead of the engine half**, because the modules
  turned out not to need the primitives to be written or tested — they resolve
  `native($n) // fallback // stub` and today take the middle rung. Two
  departures from what this section assumed, both forced and both recorded in
  `notes/Data-Native.md` in that repository:

  1. **`Data::Native`'s `depends` are the REFERENCE modules, not our four.**
     A module that `use`s a claim-protocol participant runs that participant's
     `EXPORT` into its OWN scope, and the registry announcement it leaves
     cannot be told apart from one the caller made — so `Data::Native` would
     stand aside from every tag and export nothing. Three repairs were tried
     and all three fail; the information needed (*whose* scope did the
     announcement land in) is not observable from inside `sub EXPORT` on
     either engine. So it depends on `JSON::Fast`, `Digest`, bduggan's two,
     `Digest::HMAC`, `Compress::Zlib` and `Crypt::Random`, and composes the
     digest and zlib tags itself — about sixty duplicated lines, which the
     distributions' own conformance vectors keep honest.
  2. **CSV needed a split**, being the one family whose reference is our own
     module. `need` runs no `sub EXPORT` at all on either engine, so the
     implementation moved to `CSV::Native::Core` — a plain `unit module` with
     no export protocol — and `CSV::Native` became the thin importable face.
     `Data::Native` reaches the Core by full name. That is the general shape
     for any family where our own module is the reference.

  **Standing rule: nothing in that repo is published from here.**

P1 is deliberately thin — the JSON codec already exists, so P1 plus the
`json`-only skeleton of P6 exercises the *whole* stack (tags on both engines,
the rakulib search path, `sub EXPORT`, the order-independence test) before any
large port is written. If the design is wrong anywhere, that is where it
shows, cheaply. P4 is the largest single piece and the only one that adds a
capability rather than speed; it can be split out into its own release if the
first three tags are wanted sooner.

## Gates

- **The cross-engine conformance suite is the central gate.** One corpus per
  tag, one file, run four ways: rakupp via `Data::Native` (core primitives),
  rakupp via the `**::Native` module (XS), Rakudo via `Data::Native` (its
  `use`d references), Rakudo via the `**::Native` module (XS). All four must
  agree value for value — and for `to-json`/`to-csv`, byte for byte. This
  replaces the first revision's shared-`Callable` identity trick, which the
  XS-only rule for L2 removes.
- **Equal export sets.** A test asserts `rakulib/Data/Native.rakumod` and the
  distribution's copy export exactly the same names, and that every name in
  every tag is exported on both engines — present-and-stubbed, never absent
  (the swallowed-`EXPORT` probe is why).
- **Both `use` orders produce equal results** for `Data::Native` +
  `CSV::Native` in either order, on both engines. Not identical `.WHICH` any
  more — equal behaviour, which is the requirement's substance.
- **The distributions' suites are the oracle.** `CSV-Native/t/*.t` and
  `JSON-Native/t/*.t` run against the L1 primitives via a `lib/` shim, on top of
  running unchanged. They already compare value by value against `JSON::Fast`
  and the Raku reference on whichever engine is running.
- **The JSON fast-path regression file stays green, unchanged** — it pins
  `jfEncode`'s bytes and P1 refactors its callers.
- **`tools/jupyter-smoke.raku` stays green** across the P3 lift (RFC 4231).
- **zlib round-trips against the system tools**: `gzip`/`gunzip` and a zlib
  reference over the test corpora, both directions, plus the RFC fixed
  vectors; and `Compress::Zlib`'s own suite on Rakudo as the interface oracle.
  A fuzz pass on inflate — it is the one component here that parses
  attacker-controlled input, so malformed streams must error, never read out
  of bounds. ASAN build, since one exists.
- `t/regression/`: one file per tag for the builtin-only paths — the throw
  cases above.
- **Both engines, every tag spelling**: bare, `<a b>`, `:a`, `<all>`, and an
  unknown tag (which must be an error, not a silent no-op).
- Perf: `perf-guard --check`, per the standing rule — not eyeballed.
- Size: ~1,800 lines of C++ across all five tags is code, not a Unicode-sized
  table, so no `--slim` feature is proposed. SLIM budgets get **re-pinned with
  a measurement** after P4 (the largest piece), not estimated here.

## `--exe`: the one place the choice stops being about speed

Measured 2026-09-05. A program that will be shipped as a standalone binary
**must reach these families through `Data::Native`, never through a `**::Native`
module directly.**

`--exe` embeds the Raku half of a module graph and nothing else. So a program
that says `use Digest::Native` compiles to a binary that embeds six modules and
then cannot run:

```
--exe: embedded 6 modules: Digest::HMAC Digest::SHA2 … Digest::Native
--exe: native libraries the binary will dlopen at run time: <computed at run time>
```

Run beside the distribution it exits **139, a segfault**; run anywhere else it
reports `Cannot locate symbol 'compute_sha256' in native library ''` — the
empty name being `%?RESOURCES`, which does not exist inside an `--exe` binary.
Neither our own extension nor the fallbacks' NativeCall libraries travel. (The
crash is its own bug and is filed; a missing library should raise.)

The primitive route has none of that, because there is nothing to find: the
implementation is already inside the runtime `--exe` links. Verified — a program
using a rakulib-shaped module native-compiled, then ran from `/` with its module
directory deleted.

One detail worth keeping: **a symbolic reference inside a MODULE does not defeat
native compilation**, only one in the main program does. So `native()`'s
`&::("$p-$name")` is free here — checked both ways.

This is the sharpest statement of what the `**::Native` distributions are for,
and it belongs beside the three reasons in
[NATIVE-MODULES-PLAN.md](NATIVE-MODULES-PLAN.md): other engines, version skew,
and families the core will not carry — **not** programs that get shipped as
binaries.

## Not in this plan

`--target=js`: there is no JSON in `src/js-rt/` today, so a transpiled program
cannot call any of this. `JSON.parse`/`stringify` are close but not equal — JS
has one number type where rakupp types a decimal as `Rat` — so a JS lane needs
the typing rebuilt, not a delegation. Tracked against
[TRANSPILE-PLAN.md](TRANSPILE-PLAN.md).

## Docs to update when it lands

New `docs/guide/DATA-NATIVE.md` (the tag table, the invariant, the
order-independence rule, the throw-vs-delegate table).
`docs/guide/REFERENCE.md` (sub list at 1030, index at 1119, a verified example
per name), `FEATURES.md`, `RECIPES.md`, `EXTENSIONS.md` (the invariant belongs
beside the `Rakupp::` naming rule it extends), `CHANGELOG.md`, `README.md`.

## Which tags have a `**::Native` distribution of ours

Decided 2026-09-05, after this section was first written the other way: the
invariant says one tag ⇔ one *reference interface*, and a distribution of ours
is added on evidence, not symmetry. The evidence was then weighed per family in
[NATIVE-MODULES-PLAN.md](NATIVE-MODULES-PLAN.md):

| tag | reference | ours |
|---|---|---|
| `json` | `JSON::Fast` | `JSON::Native` — exists |
| `csv` | *none usable* | `CSV::Native` — exists; we wrote the reference |
| `digest` | `Digest` + `Digest::HMAC`; bduggan's two for native SHA-1/256 | `Digest::Native` — **new**: one dist covering all six algorithms + HMAC, our C on Raku++, bduggan's + `Digest` + `Digest::HMAC` as its composed fallback |
| `zlib` | `Compress::Zlib` | `Compress::Zlib::Native` — **new**: the only family where rakupp has no capability at all; `Compress::Zlib` (NativeCall over `libz`) as its fallback |
| `random` | `Crypt::Random` | **none** — the OS call is the implementation; the core primitive is the whole deliverable |

What each costs is in the companion plan (JSON::Native is 552 lines of C, 180
of Raku, a 72-line `Build.rakumod`, plus extension ABI versioning), and so is
what they are for now that the core carries the same families: version skew,
independent release cadence, and families the core will not carry — **not**
Rakudo speed, since the extension ABI is rakupp-only.

**On `YAML::Native` specifically:** not a tag, so not a module. YAML 1.2 is not
a closed spec, and YAMLish currently *fails* on rakupp for a plain sequence of
maps (spun off as its own task). If YAML ever becomes a tag, the lever is a
fast path on the loaded YAMLish, the `wrapJsonFastExports` pattern.

## Open decisions

All four are closed, three of them by the implementation rather than by
argument.

1. ~~**A `Digest::Native` for naming symmetry only?**~~ **Built, and not for
   symmetry** — see [NATIVE-MODULES-PLAN.md](NATIVE-MODULES-PLAN.md). It covers
   all six algorithms plus HMAC in one install, which the ecosystem did not
   have; it is not a re-export of anything.
2. ~~**Does `Data::Native` export the `*-backend()` subs?**~~ **Yes**, one per
   tag, in the default set. They earn the five names: with three possible
   answers per tag (`core`, the module, a stub) `say json-backend()` is the
   only way to know which is running, and every suite in the campaign prints
   them as a `diag` line.
3. ~~**Unknown tag — error or warning?**~~ **Error**, naming the tag *and*
   listing the ones that exist. The feature-probing worry is answered by the
   spelling itself — `use Data::Native <json>` on an engine that has no json
   tag is a bug in the program, not a capability probe, and a program that
   wants to probe asks `json-backend()` after importing. Note the error is
   *reported* rather than fatal: a dying `sub EXPORT` is swallowed by both
   engines, so what the caller sees is a warning on rakupp, silence on Rakudo,
   and an undeclared-routine failure at the first call. That is why the message
   has to be worth reading.
4. ~~**`sha512` in the first cut?**~~ **All six shipped**, with the `-hex`
   twins and HMAC. The marginal cost was as small as expected: SHA-384/512 is
   the SHA-256 core with 64-bit words and other constants, SHA-224 is SHA-256
   truncated.

### Still to decide

1. **Does a versioned `use` reach the store?** `rakulib/` precedes the install
   store, so an installed newer `Data::Native` is invisible. `use
   Data::Native:ver<0.3+>` skipping rakulib is the escape that lets the module
   be updated without an engine release — see the resolution-order section. P6
   should settle it.
2. **Where do the shared corpora live?** NATIVE-MODULES-PLAN says the engine
   repository, with each distribution's `t/` pulling them. They were written
   the other way round — `Digest-Native/t/vectors/digest.vec` (156 vectors,
   from `openssl`) and `Compress-Zlib-Native/t/vectors/zlib.vec` (67, from real
   libz and the system `gzip`) — because the distributions existed first. The
   direction of ownership should be settled before P3 and P4 write the engine's
   copies against them.
