# The whole ecosystem, under `rakupp test`

The full-population successor to [FRESH100-2026-08-20.md](FRESH100-2026-08-20.md):
**every distribution in the REA index — latest release of each —** put through
`rakupp test` (build hook, dependency install, own suite), then a fix campaign
over the failure clusters, then the population measured again. Six sittings,
2026-08-23 … 2026-08-30; the population was 2,524 dists at the first and
**2,526 at the last**, which is why the denominator moves. The standing figure
is the most recent line of the Timeline below, not the first table.
Every distribution and its verdict is browsable at
[raku.online/modules/ecosystem](https://raku.online/modules/ecosystem/)
(regenerate its snapshot with `sites/modules/tools/distill-ecosweep.raku` in
the raku.online repo when a sweep updates these TSVs).

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

Timeline (the raku.online dashboard mines these dated lines verbatim — keep
the `- YYYY-MM-DD: N of M` shape in every sweep write-up):

- 2026-08-23: 624 of 2,524
- 2026-08-25: 637 of 2,524
- 2026-08-30: 746 of 2,526

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

## Sitting five (2026-08-30): the Getopt::Long gauntlet, closed

The frontier named directly above. **Getopt::Long's suite is green**, so the
single biggest dep-blocker in the population — 55 dists name it — no longer
blocks anything. It took six fixes, and only the first was the one the
frontier note predicted.

**A parameter's user traits never ran at all.** `sub f(:$foo is
option("=s%"))` should call `trait_mod:<is>(Parameter, :option("=s%"))`; the
parser consumed the trait word and its parenthesised argument and threw both
away, so no handler ever saw them — the whole `is option` / `is getopt`
surface the module reads its option specs through was inert. Parameters now
carry `userTraits` exactly as attributes do, and the traits are dispatched
where the routine is declared. Three things had to hold for the mixin the
handler applies to survive: the Parameter meta-object is built ONCE and
cached on the Param (`.signature.params` renders a fresh hash from the struct
every time it is asked, so a copy would forget the trait one line later); a
`does` on a Parameter meta-object mixes in PLACE, as the Attribute branch
already did; and both the role check and the accessor fall back to the
mixed-in role's keys, again as attributes do. Anonymous subs and pointy
blocks run them too — `sub (Str :$foo is option(*.flip)) {}` is handed to the
module inline and reaches none of the declaration paths.

**`does R(:name(value))` made an anonymous parameterized role.** The
positional preset (`does R(7)`) was honoured and the named spelling was not,
so `$param does Formatted::Named(:$argument)` — the form a trait_mod reaches
for when the value is already in a variable of that name — came back matching
nothing. Both spellings now preset the role's attributes, on an object, on a
routine, and on a meta-object.

**A role mixed into a ROUTINE left its type alone.** The mixin is recorded on
the Callable so the object keeps its identity (Path::Finder needs that), but
nothing renamed the type: `.WHAT` still answered the bare `Sub` while `~~` and
the accessors said otherwise. `&main1.WHAT !=== Sub` and a `.^name` carrying
the role's name are two of the suite's assertions.

**Coercion types answered nothing.** `Foo(Str) :$foo` reported a bare `Foo`,
so a module could not tell a coercion from a plain type. A coercion parameter
now reports `Foo(Str)`, its `.HOW` is a `Metamodel::CoercionHOW`, and
`^constraint_type` / `^target_type` / `^coerce` answer — the last running the
target's own `COERCE`.

**`KEY => my $x` now carries the CONTAINER.** The out-parameter idiom:
`get-options-from(@args, 'foo' => my $foo)` hands the module a place to put
the parsed option, and binding the pair's value into a hash then assigning
through it has to reach `$foo`. Scope, stated plainly: Rakudo preserves the
container for ANY variable on the right of `=>`; this does it for the
DECLARATION form only, which is where the idiom lives — `k => $x` still
copies. Two consequences fell out and are fixed with it: a container is
transparent to a type test (a bound scalar is not `Associative`, which is
what `given .value { when Associative {…} }` was reading it as), and it gists
as what it holds rather than as its own FETCH/STORE pair, which `.raku`
already did.

**And the one that was actually costing the last two assertions:** a
non-Positional value is its own single element, `42[0] === 42`. That held for
Int and Str and not for a Regex or a plain object, so `/ ^ (\w+) /[0]` — how
the module spells the match it wants — answered `Any`, the smartmatch became
`$key ~~ Any`, and every option was named "True". Regex and objects
self-index now; an object with its own `AT-POS` never reaches that arm.

**Gates.** t/run 589/589 (the case is
`t/regression/getopt-long-parameter-traits.raku`, and it passes byte-for-byte
under Rakudo 2026.08 too); six roast slices (S02/S04/S05/S06/S12/S32)
**file-identical** against a clean-HEAD baseline build — 306 fully-passing
files before and after, no losses and no gains; perf guard neutral (every
kernel within noise, several a shade faster).

**Measured effect on the blocked cohort.** All 55 dists that named
Getopt::Long, re-run against a scratch store: **3 convert to pass**
(Base64::Native, RKDS, App::Prove6) and the other 52 now RUN their own suites
for the first time — the one-rung-up shape this campaign keeps meeting, and
52 fresh first-errors for the next triage to cluster. Four conversions in
all, counting Getopt::Long itself. The population total is not restated here:
it was last measured on a different engine, and only a re-sweep can move it.

## The re-sweep (2026-08-30, on v3.23.0-8-g5e23726): 746 of 2,526

The population measured again, end to end, on the engine as it stands after
sitting five — the first full re-measurement since 2026-08-25, and the one
v3.23.0 deliberately left undone (its CHANGELOG says so: the 637 figure was
carried forward, not measured). Index refreshed the same day: 15,029 archived
releases, **2,526 dists**, two more than August's population. Per-dist results
in [ecosweep/resweep-2526.tsv](ecosweep/resweep-2526.tsv), the green list in
[ecosweep/green-2526.txt](ecosweep/green-2526.txt).

| verdict | dists | August |
|---|---:|---:|
| **pass** | **746** | 637 |
| self-fail | 1,228 | 1,341 |
| dep-fail | 337 | 425 |
| other | 67 | 95 |
| build-fail | 51 | 23 |
| timeout | 49 | — |
| dep-build-fail | 46 | 13 |
| fetch-fail | 2 | 3 |

`timeout` is a new bucket, not a new failure: August's harness could not tell
a suite that ran out of time from one that exited strangely, and both landed
in `other`. Read `other` + `timeout` = 116 against August's 95 if you want the
comparable pair. `build-fail` and `dep-build-fail` rising while `dep-fail`
falls is the same effect one rung along — dists that never reached their build
step now reach it.

**637 → 746 decomposes as 125 conversions, 18 regressions, 2 new dists** (both
passing). The regressions are real: every one of the 18 was re-measured alone,
single-threaded, on an idle machine, and every one reproduced.

- Eleven fail their own suites — Algorithm::Tarjan, Context, DB::ORM::Quicky,
  Date::Calendar::Gregorian, Date::Calendar::Julian, HTML::Tag,
  JavaScript::D3, Rmv::JIRA, URL, and behind URL its dependent
  HTTP::API::MusicBrainz.
- Four now time out at 120 s: Log::Async and P6TK, plus WebService::Nominatim
  and WebService::Overpass — the last two call remote APIs, so upstream rate
  limiting is not excluded and they are worth re-checking before being read as
  engine work.
- Three fail their native build: Device::Velleman::K8055, RPi::Device::SMBus
  (and RPi::Device::PiGlow behind it), Terminal::Readsecret.

Date::Calendar::Julian and Log::Async were two of the thirteen that August's
re-run converted TO pass, so both have moved twice.

**Getopt::Long blocks nothing.** It passes, and no dist in the population now
names it among its failing dependencies — sitting five's 55-dist cohort,
confirmed at population scale. The blocked cohort is 383 dists (was 421),
waiting on 486 distinct distributions, 288 of them on more than one. The
blockers the most dists wait on are now the async and serialization
constellation: IO::Socket::Async::SSL 63, Log::Timeline 62, CBOR::Simple 62,
IO::Path::ChildSecure 60, IO::Capture::Simple 36, HTTP::UserAgent 36,
NativeHelpers::Blob 30, Slangify 22.

### What the sweep's own instrument got wrong

The first attempt at this sweep produced a complete-looking result file that
was almost entirely fabricated, and the two defects behind it are worth
recording because neither was visible in the output.

**One distribution took the machine down.** Test::Selector 0.4.2's
`t/all.rakutest` re-invokes itself through a generated temp script without
bound under rakupp. The 120-second budget did not stop it: `perl -e alarm`
exec'd the shell, so the alarm killed perl and merely ORPHANED the `sh` and
the `rakupp test` below it, which went on spawning. It reached 2,905 processes
against a 4,000-process ceiling, and nothing on the machine could fork —
including the commands needed to diagnose it.

**The sweep then invented 2,400 verdicts.** `classify()` has no bucket for
"the child never started", so a failed fork fell through to the verdict
`other`, in zero seconds, and the run sprinted to the end of every shard
writing rows that were indistinguishable from measurements except by their
timings. Both are fixed in `tools/eco-fresh/sweep-fresh.raku` (commits
"The sweep's timeout killed one process and orphaned the tree below it" and
"The abort guard called a malformed distribution a broken machine"): perl
forks rather than execs and the alarm kills the process GROUP, and a sweep
that cannot start a child aborts with what it saw instead of recording it.
The guard's first form was too eager — it read the clock alone and stopped a
run over FindBin-libs, whose archive genuinely has no META6.json and which
legitimately fails in 0.8 s — so it now asks whether `rakupp test` ever spoke
and reserves the abort for silence plus an exit code only the wrapper emits.

Test::Selector was measured last and alone, with a process-table watchdog
armed: it reached 1,549 processes, the watchdog killed the tree, and it
records `self-fail`. That is the one verdict in this sweep reached with
intervention rather than on its own.

**Method, for repeatability.** Four shards over four private stores, seeded
first with the 476 modules named by two or more dists (`rank-deps.raku` then
`seed-store.raku`, 450 of 476 installed); budget 120 s per dist;
`OPENSSL_PREFIX=/opt/homebrew/opt/openssl@3`. Run two shards at a time under
`nice -n 10` rather than four, to keep the machine usable — the sweep is
long, not urgent. Zero harness aborts and no watchdog firing across all
2,525 shard dists.
