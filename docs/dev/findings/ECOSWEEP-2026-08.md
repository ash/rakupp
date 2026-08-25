# The whole ecosystem, under `rakupp test`

The full-population successor to [FRESH100-2026-08-20.md](FRESH100-2026-08-20.md):
**every distribution in the REA index — 2,524 dists, latest release of each —**
put through `rakupp test` (build hook, dependency install, own suite), then a
fix campaign over the failure clusters, then a re-run of every non-pass dist on
the fixed engine. Three sittings, 2026-08-23 … 2026-08-24.

## Method

- Index: REA `META.json` of 2026-08-23 — 14,997 dist-versions, 2,524 dists.
- The installer lesson from fresh-100 became tooling first:
  [`tools/eco-fresh/rank-deps.raku`](../../../tools/eco-fresh/rank-deps.raku)
  ranks reverse dependencies (476 modules are named by ≥ 2 dists) and
  [`seed-store.raku`](../../../tools/eco-fresh/seed-store.raku) installs them
  into the sweep store with `--no-test`. A dist's own verdict is untouched —
  `rakupp test` always runs the suites of the dists it was NAMED — but its
  dependencies are already present, so the whole budget goes to its own suite.
  Measured effect: fresh-100 averaged ~45 s per dist; the seeded sweep averaged
  **~1.5 s**, and the full population fits in an afternoon on four shards.
- Budget 120 s per dist (`perl -e alarm`; macOS has no timeout(1)); stores per
  shard; `OPENSSL_PREFIX=/opt/homebrew/opt/openssl@3` (the two-Homebrews trap
  from fresh-100). Engines pinned per pass: the sweep on v3.6.0-50, the re-run
  on v3.6.0-63 plus that day's batch.

## Results

First pass, all 2,524 ([ecosweep/results-2524.tsv](ecosweep/results-2524.tsv)):

| verdict | dists |
|---|---:|
| pass | 624 |
| self-fail | 1,341 |
| dep-fail | 425 |
| other | 95 |
| build-fail | 23 |
| dep-build-fail | 13 |
| fetch-fail / unresolved | 3 |

Re-run of the 1,900 non-pass dists on the fixed engine
([ecosweep/rerun-1900.tsv](ecosweep/rerun-1900.tsv), clusters in
[ecosweep/RERUN-REPORT.md](ecosweep/RERUN-REPORT.md)): **13 converted to
pass** — Crypt::Bcrypt, Date::Calendar::Julian, Digest::SHA1::Native,
Digest::SHA256::Native, FastCGI::NativeCall, Linenoise, Log::Async,
Math::DistanceFunctions::Edit, App::FIT2GPX, Term::termios, Test::Assertion,
Text::Caesar, XML — five of them the native-compiling `builder` family that
could not build at all before. **Green total: 637 of 2,524**
([ecosweep/green.txt](ecosweep/green.txt)).

The modest conversion count is the honest shape of the terrain: each fix tends
to move its cluster ONE RUNG — a dist that failed to parse now runs its suite
and fails a real assertion instead (Form: silent regex swallow → a formatting
assertion; Docker::File: end-of-file mis-scan → an undeclared-variable report
deep in its grammar actions). The ladder is climbed per-dist, and "pass" is
the top rung, not the next one.

## The fix campaign (all committed, each oracle-verified against Rakudo 2026.07)

Batch one — what the sweep's first hours taught the parser and lexer:
`given`/`with` bind `$_` without itemizing; `#`{{ … }}` repeated-bracket
comments; quoted brackets inside regex groups (a SILENT source swallow);
`<?[…]>`/`<![…]>` lookahead classes; sigilless condition binders
(`if EXPR -> \x`); `!(elem)` negated parenthesized set ops; serial
`.hyper`/`.race`.

Batch two — the top-50 push (37 → 41 of the measured top-50 green):
JSON::Fast fast-path allomorph/enum/Mu fixes; `.^method_table`; an
inspectable `&trait_mod:<is>`; socket `.get`/`.lines`; Rakudo-faithful
`.trans`; coercion params ranked by FROM-type in multi dispatch (AST cache
v10); the `builder` META protocol with a hook-scoped `$*VM` toolchain shim;
typed declarations keep `[T]`; `cglobal(&LIB, …)`.

Batch three — what the re-run and the DBIish ladder surfaced (2026-08-24):
- **The u64/u128 lane**: values in [0, 2^64) — SHA-512's whole working set —
  fell into base-1e9 BigInts, where a bit op is a radix conversion. Both
  operands u64/u128-representable → the op runs on machine words (signed
  i128 window for `+&`/`+|`/`+^`, so `+^$x +& $z` with its negative
  intermediate stays native).
- **Expression `BEGIN` evaluates once per node** — it had `do` semantics, and
  Digest::SHA2 indexes `(BEGIN blob64.new: map { frac 3√$_, 64 },
  @primes[^80])[$t]` inside its round loop: the whole 80-root FatRat table
  was recomputed per round, 6,400 square roots per block. The two fixes
  together take 1KB of sha512 from **55 s to 0.16 s** (Rakudo: 0.09 s).
- Every method carries the implicit `*%_`, signature or none.
- `%h{$k} //= RHS` no longer leaves a vivified key when RHS throws.
- A missing native library throws **X::AdHoc**, as Rakudo's does.
- `is native(LIB)` resolves LIB in the DECLARING module (two drivers with
  same-named constants stayed apart), and an undefined lib means the default
  namespace — DBDish::SQLite's natives dlopen'd Pg's `pq` before this.
- Computed name adverbs on classes (`:ver($?DISTRIBUTION.meta<ver>)`) reach
  `.^ver`; the Distribution meta carries Rakudo's `ver` alias.
- The binary-relative `rakulib/` joins the module search path, carrying the
  engine's **shadow modules**: NativeHelpers::Blob and NativeHelpers::CStruct
  reimplemented over engine primitives (`Rakupp::Internals::Blob`) — the
  ecosystem dists of those names scan MoarVM's REPR memory layout by design
  and cannot run on any other engine. DBIish's 01-basic goes fully green on
  the machine's five shipped drivers with this.

Regression pins: `t/regression/ecosweep-batch-three.raku` (+ the
`nativelib-isolation` fixtures), alongside the batch-one and batch-two files.

## The biggest remaining clusters (from the re-run report)

- **77 × "1 of 1 tests failed" + 68 × "(nothing captured)"** — heterogeneous
  tails, one dist at a time.
- **39 × native library missing** — environment (libsndfile, libmagic, zmq…),
  not engine.
- **24 × `make` exited 2** — the builder protocol now RUNS these hooks; the
  Math::Libgsl family's Makefiles want gsl headers and Linux-flavored flags.
  Part environment, part shim tuning.
- **21 × parse: Confused (got '}')** — the P5-compat family and
  Pod::To::HTML/Mustache share a construct worth one parser sitting.
- **15 × parse: expected ) (got ':')** and **15 × expected } at EOF** — two
  more parser clusters (Intl::* and the grammar-heavy set).
- **12 × `.legacy` / nqp surface** (Cro::HTTP among them) and
  **12 × `.AST` (L10N family)** — the metamodel/RakuAST frontier.
- **11 × Sparrowdo::Core::DSL::Template not found** — one resolution question
  holding the whole Sparrowdo constellation.
- **DBIish's 30-pg** wants per-iteration Match lists for quantified positional
  capture groups (`( <a> | <b> )*` with `$0[n].values[0].ast`) — a scoped
  regex-engine feature, the next big single lever.

## Top-50 standing

41 of the measured top-50 pass end-to-end. The rest, by cause: Cro::HTTP
(`nqp::open`), Cro::Core (untriaged hang), HTTP::UserAgent (cookies),
DBIish (01-basic green; suite continues to the quantified-capture gap),
Digest (u128 lane landed — needs a fresh timed run), LWP::Simple (16/18;
the two w3.org files need TLS SNI through the ecosystem IO::Socket::SSL),
Math::Libgsl::Constants (ldconfig-only library finder — fails under macOS
Rakudo too), NativeHelpers::Blob (MoarVM-guts by design; its DEPENDENTS run
via the shadow), PDF::Lite (blocked by Getopt::Long).

## Sitting four (2026-08-25, post-v3.7.0): the cluster levers

The rerun's cluster tables named the levers; this sitting pulled the five
biggest. Each fix oracle-verified against Rakudo 2026.07/08; gates per batch:
t/run 514/514, six roast slices (S02/S04/S05/S06/S12/S32) file-identical
against a clean HEAD-baseline build, perf guard neutral. Regression files:
`t/regression/dynamic-var-caller-chain.raku`,
`t/regression/ecosweep-batch-five.raku` (the latter passes byte-for-byte
under Rakudo too).

**The `if` dist and its cluster (12 dists).** Three fixes in one story:
`Raku.legacy` answers True on the type object (and dies on the instance, as
Rakudo's `Raku:U:` constraint does); the `use Foo:if(EXPR)` adverb is honored
NATIVELY (both of the dist's own implementations patch Rakudo compiler
internals that don't exist here — its EXPORT failure warning is suppressed for
exactly this module); and the real find — **dynamic variables resolved in the
wrong order**. Reads walked the caller chain before the current frame, so a
routine's own `my $*X` lost to its caller's; writes resolved through the
lexical chain only, so a callee's `$*PACKAGE_LOADED++` (the `if` suite's
fixture EXPORT) minted a throwaway local. One resolver now serves reads and
writes: current frame first (frame-bounded at the routine activation), then
each caller innermost-out, then the historical lenient fallbacks. Thirteen
probe cases, all byte-identical with Rakudo.

**The P5* native family (P5getpwnam, P5getgrnam, P5getnetbyname,
P5getprotobyname, P5getservbyname, P5localtime — all green).** The angle
spelling of NativeCall traits — `is repr<CStruct>`, `is symbol<getpwuid>`,
`is native<lib>` — was unparsed (the repr case silently derailed the whole
class body into a forward declaration). Behind it: `has CArray[Str]` dropped
its `[Str]` (the field read back int64 pointers, and P5getgrnam's
walk-until-undefined SIGBUSed past the NULL terminator — elements now deref to
Str with NULL as the undefined Str), `--> PwStruct` where PwStruct is a
CONSTANT aliasing a per-kernel class now boxes, and `$*USER`/`$*GROUP` exist
as IntStr allomorphs.

**Getopt::Long, 0/45 → 34/45.** The single biggest dep-blocker (55 dists
name it). Ten general fixes fell out: `is CORE::Exception` resolves to the
setting's type; package-relative MULTI-SEGMENT type names in multi params
(`Argument::Boolean` inside `unit module Getopt::Long`) — every suffix of a
class name now registers as an alias; `my/state %h{Any:U}` object hashes
(smiley key shapes parse, pair-list init keys type objects distinctly, enum
TYPES subscript as one key, not a slice); Capture `eqv`/`is-deeply` compares
nameds as a map; attribute defaults see the CONSTRUCTED values of earlier
attributes (named args bind during the BUILD walk, not after it); `Mu ~~ Any`
is False; typed `@`/`%` params report `Positional[T]`/`Associative[T]` with
`.of`; `named_names` orders innermost-first (`:fooo(:f(:@foo))` is
`("foo","f","fooo")` — the option key was wrong before);
`Parameter.constraints` is the `all(…)` junction; enums answer the meta
protocol (`.HOW ~~ Metamodel::EnumHOW` for user enums AND builtin
Order/Endian/Bool, `Order.WHO`, `^enum_from_value`). The last 11 tests hang on
one feature: `does ROLE(arg)` mixins on Code/Parameter values plus
`where ROLE` dispatch (`is getopt` traits) — the next metamodel sitting.

**The Crypt::Random chain (Crypt::Random, UUID::V4, Date::Utils green; the
chain unblocked Date::Event and Cro::APIToken one rung each).**
`nqp::open`/`nqp::readfh`/`nqp::closefh` exist now (raw-fd trio;
/dev/urandom short-reads loop to length) — this is also Cro::HTTP's named
blocker. Behind it: `.sprintf(@array)` spreads the array into the directives
(UUID::V4's `"%08x-…".sprintf(@unpacked)` formatted the element count
before); a JUNCTION subscript key autothreads the whole subscript, adverbs
included (`%response{all(<r s i>)}:exists` — Auth::SCRAM::Async, whose
RFC 5802/7677 vectors now pass); same-bare-name attributes of different
sigils (`has Digest $.digest` beside `has &!digest`) no longer clobber one
slot; an unfilled optional SUBSET param outranks a plain-typed twin on
zero-arg dispatch (Rakudo's rule — Date::Utils's `.etype` pair); named params
typed by SUBSETS type-check subset-aware in dispatch; `enum … is export`
inside a `unit class` exports its members; `isa-ok $v, UInt` accepts what the
subset accepts; `class Foo is rw` parses (every public attr writable —
Compress::Zstd); dlopen candidates include /opt/homebrew/lib.

**Closed as not-ours:** the 11-dist Sparrowdo constellation is upstream
bit-rot — their `depends: Sparrowdo` resolves to modern Sparrowdo (0.1.55),
which dropped the `Sparrowdo::Core::DSL::*` API after 0.0.45; they fail under
Rakudo/zef identically. POSIX::PWDENT's source carries its own quirks
(`has Int $.gid:` with a stray colon).

**The Date::Event finish (and what it taught).** Attribute `where`
constraints are now enforced at construction and assignment (`has Numeric
$.lat where { -90 <= $_ <= 90 }`); `enum … is export` inside a `unit class`
exports its members; an enum called with an unknown value answers a FAILURE
(Rakudo's X::Enum::NoValue), with member passthrough and the `but`-mixin
probe (`day($x)` where `$x = 'Today' but day(Tue)`) checked first. The
instructive part: making that Failure DIE where the dist expects took three
wrong attempts before roast S06-advanced/return.t stated the law — **a
Failure passes through any return typecheck untouched, and Nil satisfies
every return constraint**; the real mechanism is that BUILD/TWEAK results
are SUNK, and an unhandled Failure discarded by construction detonates
there. `--> T` return constraints now hold for METHODS exactly as for subs,
and a SUBSET return type rejects an undefined non-Nil value (`--> UInt`
against a hash-miss Any dies, as Rakudo does). Six-slice A/B against a
clean-HEAD build: all gains (S12-attributes/defaults 33→37/37,
S06-advanced/return 94→96/109, S12-class/attributes-required 4/4→6/6,
type-capture 4→7/10, +1 each in instance/attributes/subtypes/type).
Date::Event's five files all pass — the twelfth confirmed conversion.

**Still-open frontiers, sharpened:** Test::Async's `unit test-hub` custom
declarator (EXPORTHOW) gates its four dependents; Parameterizable's
`^parameterize` protocol gates BinaryHeap → Graph → LLM::Graph; the
`does`-mixin-on-values feature gates Getopt::Long's tail and its 55
dependents.
