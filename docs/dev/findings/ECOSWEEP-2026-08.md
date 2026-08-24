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
