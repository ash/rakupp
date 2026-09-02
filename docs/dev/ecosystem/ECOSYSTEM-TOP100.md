# The measured ecosystem top-100, and what it drags in

Supersedes [ECOSYSTEM-TOP50.md](ECOSYSTEM-TOP50.md) as the working set. Same
method, twice the depth, and — the part the top-50 never had — **the transitive
dependency closure underneath it**, because a top-100 dist does not go green
until everything it installs does.

Ranking: reverse **runtime**-dependency over the Raku Ecosystem Archive
(`rea-meta.json` of 2026-08-31; 15,044 dist-versions, 2,529 dists, latest
version per dist by release-date). `run` = distinct dists whose runtime
`depends` resolve to it. Ties are broken by all-phase dependents, then by name;
the boundary is genuinely ragged — 86 dists have `run` ≥ 8 and 23 tie at 7, so
ranks 87-100 are the first 14 of that tie. `Rakudo`/`nqp`/`zef`/`Test` are
excluded throughout.

Verdicts are the 2026-08-30 whole-ecosystem sweep at **v3.23.0-8-g5e23726**
([ECOSWEEP-2026-08](../findings/ECOSWEEP-2026-08.md)); the `RakuAST` column is
the flag from raku.online's `tools/rakuast-fallout.raku` (see
[RAKUAST-PLAN.md](../plans/RAKUAST-PLAN.md)). Raw data:
[top100-battery.tsv](top100-battery.tsv).

## The battery

| layer | dists | pass | not |
|---|--:|--:|--:|
| the top 100 | 100 | 62 | 38 |
| its dependency closure (runtime + build, transitive) | 64 | 33 | 31 |
| test-depends of the above | 5 | 0 | 5 |
| **battery** | **169** | **95** | **74** |

The closure is shallow — 52 of the 64 dependencies are direct, 10 at depth 2,
2 at depth 3 — and it is where a third of the remaining work sits. Nine
dependency-layer dists each block two or more of the 100:

| blocker | top-100 dists behind it | first error |
|---|--:|---|
| CBOR::Simple | 4 | `Buf[uint8]` encodes as major type 3 (text), not 2 (bytes): got `100,…` where CBOR wants `68,…` |
| IO::Path::ChildSecure | 4 | `code returned a Failure` |
| Log::Timeline | 4 | `Timestamp is an Int` |
| Font::AFM | 3 | type check in assignment to `$g` |
| Hash::int | 3 | `did it create a list` |
| PDF::Grammar | 3 | parse: expected `}` at line 39 |
| CSS::Writer | 2 | parse at line 496 |
| Native::Packing | 2 | README code sample |
| Text::SubParsers | 2 | `01-basic-usage.t` |

## What the RakuAST transition costs this battery

Two rows, both `legacy`, both the same cause: **Text::CSV** (rank 94, 7 runtime
dependents) has `use Slang::Tuxic` in `lib/Text/CSV.rakumod:3` — the library,
not only its 33 test files — so **Slang::Tuxic** rides in as a dependency. A
slang attaches to grammar hooks Rakudo is replacing at 2026.09, so this is not
engine work: implementing the old hook builds on something upstream is deleting,
and stock Rakudo meets the same wall.

Not abandoned, though, and the index says so: **Slang::Tuxic already
runtime-requires `Slangify`**, the activation interface the announcement points
authors at, as do 11 of the other 18 `Slang::` dists. Slangify eases activation
rather than finding the new hooks, so the flag stands — but it means the pair is
upstream work *in progress*, and if Tux lands on the new hooks Text::CSV becomes
ordinary work again.

No battery member carries `needs-AST` — **the RakuAST surface blocks none of the
top 100**, which is what makes postponing it to the RAKUAST-PLAN phases free.

## The 2026-09-01 re-check at v3.24.0-9-g7da95b8

The whole 100 re-run from a **private, initially empty store** — so unlike the
2026-08-30 whole-ecosystem sweep, every dependency's own suite ran too. That is a
stricter question, and the two numbers answer different ones:

| question | result |
|---|---|
| passes its own suite, dependencies assumed installed (2026-08-30 sweep) | 62 / 100 |
| passes end to end from an empty store (this re-check) | 58 / 100 |

The net difference is not a regression. Five rows — `Text::Utils`, `WWW`,
`HTTP::Easy`, `IRC::Client`, `Config::TOML` — turned `dep-fail` because a dependency
the shared store had already installed got its own suite run this time; one,
`LWP::Simple`, went the other way (`timeout` → `pass`). Every one of those blockers
self-fails in the committed data too.

Behind the 42 non-passing rows are **26 distinct dists that fail on their own
code** — the actual work list, since everything else is cascade:

| root | in the 100 | chains it stops | first error |
|---|---|--:|---|
| CBOR::Simple | dependency | 4 | # Failed test 'cbor-encode produces correct blob' at CBOR-Simple-0.1.4/t/01-ba |
| Gnome::N | #49 | 3 | Undefined routine 'void' |
| IO::Capture::Simple | dependency | 3 | # Failed test 'OH HAI! in captured string' at raku-io-capture-simple/t/capture |
| Color::Names | #91 | 2 | # Failed test 'X11 loads ok' at Color-Names-2.0.0/t/01-basic.rakutest line 11 |
| Compress::Zlib | #83 | 2 | # Failed test 'Get data when a chunk is deflated' at P6-Compress-Zlib-master/t |
| Hash::int | dependency | 2 | # Failed test 'did it create a list' at Hash-int-0.0.7/t/01-basic.rakutest lin |
| IO::Socket::Async::SSL | #64 | 2 | The socket was closed during negotiation |
| Math::Libgsl::Constants | #23 | 2 | # Failed test 'The module can be use-d ok: Math::Libgsl::Constants' at Math-Li |
| Pod::Load | #60 | 2 | # Testing strings |
| Slangify | #93 | 2 | Useless use of constant string ?"Successfully slanged" in sink context (line 1 |
| Text::SubParsers | dependency | 2 | # Failed test at dist/t/01-basic-usage.t line 138 |
| AttrX::Mooish | #66 | 1 | # Failed test 'Class Basics' at AttrX-Mooish-v1.0.10/t/010-base.custom.rakutes |
| Cairo | #67 | 1 | Variable $.header used where no 'self' is available |
| Clipboard | #68 | 1 | /bin/sh: xclip: command not found |
| Crane | dependency | 1 | ===SORRY!=== Parse error at line 67: Error while compiling module Crane::Trans |
| CSS::Grammar | #96 | 1 | # Failed test 'css1 ws warnings' at CSS-Grammar-0.4.3/t/compat.t line 76 |
| DSL::Shared | #24 | 1 | # Failed test at dist/t/Array-of-regexes-matches.rakutest line 36 |
| Font::AFM | dependency | 1 | Type check failed in assignment to $glyph-name; expected Str but got Any (Any) |
| Intl::LanguageTag | dependency | 1 | ===SORRY!=== Parse error at line 533: Error while compiling module Intl::Langu |
| Method::Protected | dependency | 1 | # Failed test 'can we increment' at Method-Protected-0.0.4/t/01-basic.rakutest |
| NativeHelpers::Blob | #42 | 1 | Can't determine actual Offset |
| PDF::Grammar | dependency | 1 | ===SORRY!=== Parse error at line 39: expected } (got '/#6E') |
| PSGI | dependency | 1 | # Failed test 'buf body' at PSGI-1.2.3/t/encode-psgi-response.rakutest line 17 |
| Template::Mustache | #61 | 1 | ===SORRY!=== Parse error at line 614: Error while compiling module Template::M |
| Terminal::Width | #81 | 1 | tput: No value for $TERM and no -T specified |
| Text::MiscUtils | #76 | 1 | Undefined routine 'nqp::unipropcode' |

Plus **`Log::Async` (#31, 17 dependents), which hangs**: `t/03-log.rakutest` was
still running after nine minutes under a 600 s budget and had to be killed. Its
`timeout` verdict in the 2026-08 sweep was the same hang, not slowness.

Two of the 26 are not engine work at all and should be recorded environment-moved
rather than fixed: **Clipboard** (#68) wants `xclip`, and **Terminal::Width** (#81)
wants `$TERM` — the same discipline the P5* rows get in
[RAKUAST-PLAN.md](../plans/RAKUAST-PLAN.md). That leaves **24 engine-side roots**
for the last 42 rows, and the top three — `CBOR::Simple`, `Gnome::N`,
`IO::Capture::Simple` — clear ten of them between them.

`Slangify` (#93) stays on the list despite the `legacy` cluster around it: it is
the *migration* interface, not a casualty, and it fails here on a plain sink-context
bug (`Useless use of constant string … in sink context`) that also stops `Air` and
`Slang::Tuxic`. **Text::CSV** (#94) is the row that leaves — it reaches the 100
only through `Slang::Tuxic`, whose hook is upstream's to re-land.

## The 2026-09-02 sitting: the closure's roots, twenty-odd faults deep

The 24 engine-side roots the 2026-09-01 re-check named were walked one at a
time, each reproduced against Rakudo 2026.08 before it was touched. The fixes
are all engine-side and uncommitted-then-committed together; the gates held
throughout: `t/run.raku` 617/617 (with the new
`t/regression/top100-roots-2026-09-02.raku`, which passes byte-for-byte under
Rakudo too), the six roast slices (S02/S04/S05/S06/S12/S32) **file-identical**
against a clean-HEAD baseline build, and a direct best-of-five of the binary
against that same baseline within 2% — perf-neutral.

**Four roots convert end to end** (their whole suite green from the seeded
store), and one of them is the sitting's headline:

| root | in the battery | was | what it unblocks |
|---|---|---|--:|
| **Log::Async** | #31 | `timeout` (hung at exit) | 17 dependents |
| Text::SubParsers | dependency | `self-fail` | 2 |
| Font::AFM | dependency | `self-fail` | 3 |
| PSGI | dependency | `self-fail` | 1 (HTTP::Easy) |

A fifth, IO::Capture::Simple, converted mid-sitting on an eager `is rw`
write-through chain, but that walk was O(n²) where a cursor is threaded
`is rw` through a recursion — JSON::Fast (#1, 237 dependents) parses deep input
that way — so the chaining was reverted to a single hop and IO::Capture::Simple
reverts to open. It needs the container/binding model, not the hot path.

Log::Async did not fail a test — it **hung after the last one**. Its `END`
phaser is `logger.done`, which starts a worker to close a Supply and then waits
on that Supply; rakupp drained its workers *before* running END phasers, so the
worker never ran and the wait never returned. END now runs with the pool still
alive, as Rakudo's does, and the 17-file suite is green.

The other roots each turned on a specific, previously-missing behaviour, and the
same fixes move a further cluster of roots most of the way without closing them:

- **CBOR::Simple** — `nqp::istype(Mu, Any)` is 0, a Buf is a Blob and not a Str,
  an Instant is Real and not a Num, a native array is an `array` that names its
  element, and an enum member past int64 keeps its value rather than clamping to
  2^63-1. `t/01-basic` and `t/06-typed-arrays` go green; three tag cases and the
  pre-existing `t/02-malformed` hang (present in HEAD, not this sitting) remain.
- **CSS::Grammar** 42 → 6 failing, **PDF::Grammar** 26 → 11, **Template::Mustache**
  from a parse error to a running suite with 9 left — behind the logical-newline
  regex class (`\n` and a `<[…]>` class member match a CRLF as one grapheme, as
  in Rakudo), a `#` comment inside a `<?{…}>` code assertion, an escaped bracket
  inside a `< word list >`, and grammar actions on a rule reached only through a
  non-capturing call firing on success as well as on failure.
- **Color::Names**, **Gnome::N**, **Pod::Load**, **NativeHelpers::Blob**,
  **Method::Protected** each have their reported error fixed and a representative
  file green; their full suites still fail a tail (Gnome::N's `NativeLib.t` fails
  under Rakudo too — environment; Method::Protected wants `nextcallee`).

The fix families, by the ecosystem law each restores: constant export from a
`unit class` body; `nqp::create`/`getattr`/`eqaddr`/`hllbool`/`strtocodes` and
the `getuniprop_*` trio; a CStruct field written from inside a method reaching
native memory; an attribute typed by a constant that aliases a native type; an
`is rw` parameter's write reaching a variable two frames up; `next`/`last` as an
operand throwing rather than going cooperative (so `my Str $s = %h{$_} // next`
does not finish the assignment first); an empty Slip is undefined; `with(` glued
to its paren is a call; a native callback typed from the routine's declared
signature (the OpenSSL ALPN selector read its length byte eight-wide and looped
forever without it); the Pod::Load precompilation surface as a shim; and the
NativeHelpers::Blob/Pointer shadow modules made real views onto engine storage.

## The 100

| # | dist | run | version | auth | verdict | RakuAST |
|--:|---|--:|---|---|---|---|
| 1 | JSON::Fast | 237 | `0.20.1` | zef:timo | pass |  |
| 2 | Cro::HTTP | 54 | `0.8.13` | zef:cro | timeout |  |
| 3 | File::Temp | 53 | `0.0.12` | zef:raku-community-modules | pass |  |
| 4 | Terminal::ANSIColor | 52 | `0.14` | zef:raku-community-modules | pass |  |
| 5 | HTTP::UserAgent | 48 | `1.2.0` | zef:raku-community-modules | self-fail |  |
| 6 | JSON::Tiny | 40 | `1.0` | cpan:MORITZ | pass |  |
| 7 | YAMLish | 38 | `0.1.3` | zef:leont | pass |  |
| 8 | URI | 37 | `0.3.8` | zef:raku-community-modules | pass |  |
| 9 | File::Find | 37 | `0.2.5` | zef:raku-community-modules | pass |  |
| 10 | MIME::Base64 | 35 | `1.2.5` | zef:raku-community-modules | pass |  |
| 11 | IO::Socket::SSL | 34 | `0.0.4` | zef:raku-community-modules | pass |  |
| 12 | XML | 33 | `0.3.6` | zef:raku-community-modules | pass |  |
| 13 | Test::META | 31 | `0.0.20` | zef:jonathanstowe | pass |  |
| 14 | DBIish | 27 | `0.6.8` | zef:raku-community-modules | pass |  |
| 15 | URI::Encode | 27 | `1.0` | zef:raku-community-modules | pass |  |
| 16 | HTTP::Tiny | 26 | `0.2.6` | zef:jjatria | pass |  |
| 17 | Sparrowdo | 23 | `0.1.55` | zef:sp1983 | pass |  |
| 18 | Digest::HMAC | 22 | `1.0.7` | zef:jjmerelo | pass |  |
| 19 | Method::Also | 22 | `0.0.10` | zef:lizmat | pass |  |
| 20 | LibraryMake | 20 | `1.0.5` | zef:jjmerelo | pass |  |
| 21 | File::Directory::Tree | 20 | `0.2` | zef:raku-community-modules | pass |  |
| 22 | Digest | 20 | `1.1.0` | zef:grondilu | pass |  |
| 23 | Math::Libgsl::Constants | 20 | `0.0.13` | zef:FRITH | self-fail |  |
| 24 | DSL::Shared | 19 | `0.2.11` | zef:antononcube | self-fail |  |
| 25 | Hash::Merge | 19 | `2.0.0` | cpan:TYIL | pass |  |
| 26 | File::Which | 18 | `1.0.1` | github:azawawi | pass |  |
| 27 | Terminal::ANSI | 18 | `0.0.25` | cpan:BDUGGAN | pass |  |
| 28 | UUID | 18 | `1.0.0` | github:retupmoca | pass |  |
| 29 | OpenSSL | 17 | `0.2.9` | zef:raku-community-modules | pass |  |
| 30 | Text::Utils | 17 | `4.0.2` | zef:tbrowder | pass |  |
| 31 | Log::Async | 17 | `0.0.17` | zef:bduggan | timeout |  |
| 32 | LWP::Simple | 16 | `0.109` | zef:dwarring | timeout |  |
| 33 | HTTP::Status | 16 | `0.0.5` | zef:lizmat | pass |  |
| 34 | JSON::Class | 16 | `0.0.6` | zef:vrurg | pass |  |
| 35 | Base64 | 15 | `0.0.2` | github:ugexe | pass |  |
| 36 | Data::Dump | 15 | `0.0.18` | zef:tony-o | pass |  |
| 37 | DateTime::Format | 15 | `0.1.5` | zef:raku-community-modules | pass |  |
| 38 | Sparrow6 | 15 | `0.0.93` | zef:sp1983 | pass |  |
| 39 | Distribution::Builder::MakeFromJSON | 14 | `0.6` | github:niner | pass |  |
| 40 | PDF::Content | 14 | `0.9.11` | zef:dwarring | self-fail |  |
| 41 | Color | 14 | `1.004001` | zef:raku-community-modules | pass |  |
| 42 | NativeHelpers::Blob | 13 | `0.1.13` | zef:raku-community-modules | self-fail |  |
| 43 | Data::TypeSystem | 13 | `0.1.8` | zef:antononcube | pass |  |
| 44 | Digest::SHA256::Native | 13 | `1.0.1` | zef:bduggan | pass |  |
| 45 | OO::Monitors | 13 | `1.1.7` | zef:raku-community-modules | pass |  |
| 46 | UUID::V4 | 13 | `1.0.0` | zef:masukomi | pass |  |
| 47 | Shell::Command | 12 | `1.2` | zef:raku-community-modules | pass |  |
| 48 | Cro::Core | 12 | `0.8.10` | zef:cro | pass |  |
| 49 | Gnome::N | 12 | `0.1.54` | zef:martimm | self-fail |  |
| 50 | Config | 11 | `3.0.4` | cpan:TYIL | pass |  |
| 51 | Digest::SHA1::Native | 11 | `1.0.1` | zef:bduggan | pass |  |
| 52 | NativeHelpers::Array | 11 | `0.0.6` | zef:jonathanstowe | pass |  |
| 53 | PDF::Lite | 11 | `0.0.15` | zef:dwarring | self-fail |  |
| 54 | WWW | 11 | `1.005007` | zef:raku-community-modules | pass |  |
| 55 | Test::Output | 10 | `1.3` | zef:lizmat | pass |  |
| 56 | Cro::WebSocket | 10 | `0.8.10` | zef:cro | timeout |  |
| 57 | MacOS::NativeLib | 10 | `0.0.6` | zef:lizmat | pass |  |
| 58 | Date::Calendar::Strftime | 10 | `0.1.1` | zef:jforget | pass |  |
| 59 | Gnome::Glib | 10 | `0.1.12` | zef:martimm | self-fail |  |
| 60 | Pod::Load | 10 | `0.7.2` | zef:jjmerelo | self-fail |  |
| 61 | Template::Mustache | 10 | `1.2.6` | zef:raku-community-modules | self-fail |  |
| 62 | Term::termios | 10 | `0.2.8` | zef:krunen | pass |  |
| 63 | LLM::Functions | 10 | `0.5.10` | zef:antononcube | self-fail |  |
| 64 | IO::Socket::Async::SSL | 9 | `0.8.2` | zef:raku-community-modules | self-fail |  |
| 65 | Air | 9 | `0.1.32` | zef:librasteve | self-fail |  |
| 66 | AttrX::Mooish | 9 | `1.0.10` | zef:vrurg | self-fail |  |
| 67 | Cairo | 9 | `0.2.7` | github:timo | self-fail |  |
| 68 | Clipboard | 9 | `0.1.2` | zef:antononcube | self-fail |  |
| 69 | Gnome::GObject | 9 | `0.1.12` | zef:martimm | self-fail |  |
| 70 | HTML::Escape | 9 | `0.0.1` | github:moznion | pass |  |
| 71 | HTTP::Easy | 9 | `1.3` | zef:raku-community-modules | pass |  |
| 72 | Intl::CLDR | 9 | `0.7.6` | zef:guifa | self-fail |  |
| 73 | IRC::Client | 9 | `4.0.15` | zef:lizmat | pass |  |
| 74 | Math::Libgsl::Matrix | 9 | `0.6.1` | zef:FRITH | dep-build-fail |  |
| 75 | PDF::Font::Loader | 9 | `0.8.15` | zef:dwarring | dep-build-fail |  |
| 76 | Text::MiscUtils | 9 | `0.0.13` | zef:japhb | self-fail |  |
| 77 | XDG::BaseDirectory | 9 | `0.0.15` | zef:jonathanstowe | pass |  |
| 78 | NativeLibs | 9 | `0.0.9` | zef:raku-community-modules | pass |  |
| 79 | PDF | 9 | `0.6.15` | zef:dwarring | self-fail |  |
| 80 | Terminal::WCWidth | 9 | `0.1.5` | zef:raku-community-modules | pass |  |
| 81 | Terminal::Width | 9 | `1.1` | zef:raku-community-modules | self-fail |  |
| 82 | META6 | 8 | `0.0.1` | zef:dss | pass |  |
| 83 | Compress::Zlib | 8 | `1.1.0` | github:retupmoca | self-fail |  |
| 84 | Config::TOML | 8 | `0.1.3` | zef:raku-community-modules | pass |  |
| 85 | Crust | 8 | `0.0.1` | github:tokuhirom | dep-fail |  |
| 86 | Pod::To::Markdown | 8 | `0.2.2` | zef:raku-community-modules | self-fail |  |
| 87 | Test::Mock | 7 | `1.8` | zef:raku-community-modules | self-fail |  |
| 88 | Cro::WebApp | 7 | `0.10.1` | zef:cro | self-fail |  |
| 89 | Crypt::Random | 7 | `0.4.1` | github:skinkade | pass |  |
| 90 | HTTP::Tinyish | 7 | `0.4.1` | zef:skaji | pass |  |
| 91 | Color::Names | 7 | `2.0.0` | zef:thundergnat | self-fail |  |
| 92 | Image::Markup::Utilities | 7 | `0.1.2` | zef:antononcube | pass |  |
| 93 | Slangify | 7 | `0.0.5` | zef:lizmat | self-fail |  |
| 94 | Text::CSV | 7 | `0.022` | zef:Tux | self-fail | legacy |
| 95 | Base64::Native | 7 | `0.0.10` | zef:dwarring | pass |  |
| 96 | CSS::Grammar | 7 | `0.4.3` | zef:dwarring | self-fail |  |
| 97 | CSS::Properties | 7 | `0.10.9` | zef:dwarring | self-fail |  |
| 98 | Getopt::Long | 7 | `0.4.2` | cpan:LEONT | pass |  |
| 99 | JSON::Name | 7 | `0.0.7` | zef:jonathanstowe | pass |  |
| 100 | LLM::DWIM | 7 | `0.0.6` | zef:bduggan | self-fail |  |

## The dependency layer

| dist | depth | needed by | version | auth | verdict | RakuAST |
|---|--:|--:|---|---|---|---|
| DateTime::Parse | 1 | 7 | `0.9.1` | github:sergot | pass |  |
| if | 1 | 6 | `0.1.5` | zef:raku-community-modules | pass |  |
| CBOR::Simple | 2 | 4 | `0.1.4` | zef:japhb | self-fail |  |
| Compress::Zlib::Raw | 1 | 4 | `1.0.1` | github:retupmoca | pass |  |
| Cro::TLS | 1 | 4 | `0.8.10` | zef:cro | pass |  |
| HTTP::HPACK | 1 | 4 | `1.0.3` | zef:raku-community-modules | pass |  |
| IO::Path::ChildSecure | 1 | 4 | `1.2` | zef:raku-community-modules | self-fail |  |
| JSON::JWT | 1 | 4 | `1.1.2` | zef:raku-community-modules | pass |  |
| Log::Timeline | 1 | 4 | `0.5.2` | zef:raku-community-modules | self-fail |  |
| TinyFloats | 3 | 4 | `0.0.5` | zef:japhb | pass |  |
| Encode | 1 | 3 | `0.0.3` | github:sergot | pass |  |
| Font::AFM | 1 | 3 | `1.24.10` | zef:dwarring | self-fail |  |
| Hash::int | 1 | 3 | `0.0.7` | zef:lizmat | self-fail |  |
| PDF::Grammar | 1 | 3 | `0.3.6` | zef:dwarring | self-fail |  |
| CSS::Writer | 1 | 2 | `0.3.3` | zef:dwarring | self-fail |  |
| DateTime::Grammar | 2 | 2 | `0.1.3` | zef:antononcube | pass |  |
| License::SPDX | 1 | 2 | `3.28.0` | zef:jonathanstowe | pass |  |
| Native::Packing | 1 | 2 | `0.0.6` | zef:dwarring | self-fail |  |
| Text::SubParsers | 1 | 2 | `0.1.4` | zef:antononcube | self-fail |  |
| WWW::Gemini | 1 | 2 | `0.0.25` | zef:antononcube | pass |  |
| WWW::LLaMA | 1 | 2 | `0.1.3` | zef:antononcube | pass |  |
| WWW::MistralAI | 1 | 2 | `0.1.3` | zef:antononcube | pass |  |
| WWW::Ollama | 1 | 2 | `0.0.7` | zef:antononcube | pass |  |
| WWW::OpenAI | 1 | 2 | `0.3.20` | zef:antononcube | pass |  |
| WWW::OpenRouter | 1 | 2 | `0.0.3` | zef:antononcube | pass |  |
| Abbreviations | 2 | 1 | `2.2.3` | zef:tbrowder | pass |  |
| AlgorithmsIT | 1 | 1 | `0.0.4` | zef:tbrowder | pass |  |
| Apache::LogFormat | 1 | 1 | `*` | github:lestrrat | self-fail |  |
| Backtrace::AsHTML | 1 | 1 | `0.0.1` | github:moznion | self-fail |  |
| Cookie::Baker | 1 | 1 | `*` | github:tokuhirom | self-fail |  |
| Crane | 1 | 1 | `0.1.2` | zef:raku-community-modules | self-fail |  |
| Cromponent | 1 | 1 | `0.0.14` | zef:FCO | dep-fail |  |
| CSS::Module | 1 | 1 | `0.7.7` | zef:dwarring | self-fail |  |
| CSS::Module::CSS3::Selectors | 2 | 1 | `0.0.6` | zef:dwarring | self-fail |  |
| CSS::Nested | 2 | 1 | `0.0.1` | zef:FCO | pass |  |
| CSS::Specification | 2 | 1 | `0.5.3` | zef:dwarring | self-fail |  |
| Date::Names | 1 | 1 | `2.3.2` | zef:tbrowder | pass |  |
| Getopt::Tiny | 1 | 1 | `*` | github:tokuhirom | self-fail |  |
| Hash::MultiValue | 1 | 1 | `0.7` | cpan:HANENKAMP | self-fail |  |
| HTTP::MultiPartParser | 1 | 1 | `*` | github:tokuhirom | self-fail |  |
| HTTP::Parser | 2 | 1 | `0.0.2` | github:tokuhirom | self-fail |  |
| HTTP::Server::Tiny | 1 | 1 | `0.0.2` | github:tokuhirom | dep-fail |  |
| Intl::LanguageTag | 1 | 1 | `0.12.7` | zef:guifa | self-fail |  |
| Intl::LanguageTaggish | 2 | 1 | `0.2` | zef:guifa | pass |  |
| IO::Blob | 2 | 1 | `0.0.1` | github:moznion | self-fail |  |
| IO::Glob | 1 | 1 | `0.9.0` | cpan:HANENKAMP | pass |  |
| IO::Path::XDG | 1 | 1 | `0.2.0` | cpan:TYIL | pass |  |
| JSON::OptIn | 1 | 1 | `0.0.2` | zef:jonathanstowe | pass |  |
| Lingua::NumericWordForms | 1 | 1 | `0.6.2` | zef:antononcube | pass |  |
| LLM::Prompts | 1 | 1 | `0.2.15` | zef:antononcube | self-fail |  |
| Log | 1 | 1 | `0.3.2` | github:whity | pass |  |
| Math::DistanceFunctions::Edit | 1 | 1 | `0.1.3` | zef:antononcube | pass |  |
| Math::Libgsl::Complex | 1 | 1 | `0.0.5` | zef:FRITH | build-fail |  |
| Method::Protected | 1 | 1 | `0.0.4` | zef:lizmat | self-fail |  |
| Pod::To::HTML | 1 | 1 | `0.9.0` | zef:raku-community-modules | self-fail |  |
| Prompt | 1 | 1 | `0.0.11` | zef:lizmat | self-fail |  |
| silently | 1 | 1 | `0.0.7` | zef:lizmat | pass |  |
| Slang::Tuxic | 1 | 1 | `0.0.5` | zef:raku-community-modules | self-fail | legacy |
| Text::Levenshtein::Damerau | 1 | 1 | `0.2.0` | github:ugexe | pass |  |
| Text::Markdown | 1 | 1 | `1.1.1` | zef:JJMERELO | self-fail |  |
| TOML | 1 | 1 | `3` | zef:tony-o | self-fail |  |
| Trap | 2 | 1 | `0.0.5` | zef:lizmat | pass |  |
| User::Language | 1 | 1 | `0.5.2` | zef:guifa | self-fail |  |
| X::Intl | 3 | 1 | `0.1` | zef:guifa | pass |  |
| Cro::HTTP::Test *(test-dep)* | — | 0 | `0.8.1` | cpan:JNTHN | self-fail |  |
| IO::Capture::Simple *(test-dep)* | — | 0 | `v0.0.2` | zef:jjmerelo | self-fail |  |
| Test::Async *(test-dep)* | — | 0 | `0.1.17` | zef:vrurg | self-fail |  |
| Test::Util::ServerPort *(test-dep)* | — | 0 | `0.0.5` | zef:jonathanstowe | pass |  |
| Test::When *(test-dep)* | — | 0 | `2.1` | zef:raku-community-modules | pass |  |
