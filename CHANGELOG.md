# Changelog

Release notes for tagged releases. Numbers are measured, not projected;
methodology for all Roast figures is in [docs/status/COUNTING.md](docs/status/COUNTING.md).

## v3.6.0 (2026-08-21) — the Perl 5 lessons

| | v3.5.0 | v3.6.0 |
|---|---:|---:|
| Roast assertions (all declared) | 198,628 | **198,642** |
| Roast files fully passing | 630 / 1,462 | **633 / 1,464** |
| Local regression suite (`t/run.raku`) | 491 | **499** |
| `say "Hello"` compiled with `--exe` | 8,493,752 B | **8,511,112 B** |
| …compiled with `--exe --slim` | 5,246,376 B | **5,247,200 B** |

The release trio (`--workers=4`) gave 631 / 629 / 633 files with 16-19
timeouts; the quoted run's fully-passing list contains every file the others
passed. Against the last **published** per-file map (v3.14.0 — the v3.5.0
release skipped the site republish, which this release makes up), the
file-list diff is **clean**: the one file below the baseline in all three
runs (`S32-list/map_function_return_values.t`) is timing-marginal and scores
2/2 re-run alone — the documented timeout flutter, not a regression.

### An outside remark became a kernel, and the kernel reached perl

perlancar described native-compiled Raku++ as "only twice slower than
Perl 5" — without saying what he ran. A hash-fill workload built here to
probe the claim was indeed about twice perl's speed, and profiling it found
the factor of two was three removable constants — `%h.values` materializing the whole hash as a Pair
snapshot it then discarded (56% of the kernel), a second element-wise copy in
array assignment, and a `Value` built and destroyed per literal interpolation
part — and none of them the hashing itself. The workload now ships as
`tools/bench/hashfill.{raku,pl}`, the same program line for line in both
languages, and `run-bench.raku` grew a `perl` column behind the same
byte-identical-output gate as the other engines.

After the fixes and the `ValueHash` payload below, the full mode ladder
measured in one sitting reads: perl 81.8 ms, `--exe -O3` 82.1 ms (a
statistical tie), `--exe -O` 84.0 ms, `--exe` 92.9 ms, interp 250.1 ms —
[BENCHMARKS.md](docs/status/BENCHMARKS.md) "vs Perl 5" has the tables.

### The Perl 5 study, and its first applied item

[PERL5-TECHNIQUES.md](docs/dev/findings/PERL5-TECHNIQUES.md) reads the Perl 5
sources against this interpreter's hot paths: eight techniques, each grounded
in the file that implements it, ranked by expected payoff. The first landed in
this release: **ValueHash** replaces the hash payload's
`std::map<std::string, Value>` (a red-black tree — O(log n) with a full
string comparison per level) with a compact insertion-ordered hash storing
each key's hash beside it, compare-hash-before-bytes, over dense entries with
a power-of-two probe index. Reference stability across autovivifying inserts
is kept by construction (deque-backed, append-only, erase marks); the sorted
rendering `std::map` gave for free (`Hash.gist`, quanthash gist, `.Str`) sorts
explicitly now, which is what Rakudo does. The `hash` kernel: native
20.5 → 13.1 ms, interp 49.4 → 42.1 ms; non-hash kernels unchanged.

The insertion-order audit came to three files and one real bug: `Mix.total`
summed fractional weights in a `double`, so its rounding depended on
iteration order — it now sums exactly through the numeric tower (Rat stays
Rat, Rakudo's answer). Two `objecthash.t` assertions that only ever passed by
the accident of sorted iteration (their typed-object-hash feature is
unimplemented and the hash content already wrong) are honestly failing now.

### DESTROY exists

Roast's `destruction.t` spun forever waiting for DESTROY submethods that had
never been implemented — the file used to "finish" only because its
`await start { loop }` was not really spinning before the async layer landed.
The protocol, in the Raku spirit that timely destruction is not guaranteed:
construction parks instances of DESTROY-declaring classes in a registry
(anything else pays two map lookups); a sweep runs the chain — each class's
own submethod, child first, the reverse of BUILD — for entries the registry
alone still owns, triggered by `$*VM.request-garbage-collection` (a real hook
now), by allocation pressure, and at program end before the END phasers.
`destruction.t`: timeout → 6/6. The 300k-construction kernel measures no cost.

### Also in this release

- `.fc` folds ASCII down again — the v3.5.x ASCII fast path sent fold-case
  through `toupper`; `S32-str/fc.t` is back at 12/12.
- A missed method call no longer re-executes the class body — hoistSubs'
  forward-reference record was never retired after the declaration ran, so
  the method-not-found fallback exec()'d the whole body again, statements and
  all. `S12-methods/class-and-instance.t`, which had regressed in the v3.5.x
  cycle (13 tests against a plan of 12), is back at [PASS] 12/12.
- `--exe -o` naming a directory is refused up front rather than left to the
  linker.
- The values-path fixes above also serve the interpreter: `%h.values` /
  `.keys` / `.kv` / `.pairs` / `.antipairs` skip the discarded snapshot
  everywhere.

### Performance accounting

**The perf-guard baseline was re-recorded this release, and here is why.** The
old baseline (2026-08-11, v3.14.0) was measured on the previous machine
(Darwin 24.6); this release's gates ran on a different one (Darwin 25.5),
where every kernel reads 10-48% over that stale baseline — including the
v3.5.1 release binary itself, rebuilt from its tag and measured the same
hour (fib 773.5 ms against the baseline's 656.4). Same-machine, same-hour
A/B against that v3.5.1 binary shows **zero regressions**: fib 773.5 → 727.5,
hash 50.0 → 43.5, strpass 200.4 → 190.2, subcall 339.6 → 330.2, the rest
within noise. The baseline file now records this machine; the old `best`
figures stand as the standing debt the guard reports.

Left open, named: `roles-6e.t`'s
role-ordered DESTROY/BUILD sequence. Two comparison rows are **omitted
rather than stale-quoted**: the distribution battery (49/59 at v3.5.0) and
the documentation-examples corpus (949 at v3.5.0) both live on the previous
machine and were not re-run here. The in-repo docs differential
(`tools/doc-examples-diff.raku`) did run: 208 MATCH, 9 DIFFER (version
strings and Proc addresses — the documented expected set), 13 RAKUDO-FAILS,
45 BOTH-FAIL.

## v3.5.1 (2026-08-20) — the Raku.js build finds its headers

A build-script fix and nothing else: `v3.5.0..v3.5.1` is one file, and no
engine source is in it. The interpreter is byte-for-byte the v3.5.0 engine
plus its version string, so that release's measured figures stand unchanged.

`rakujs/build.sh` compiled with `-Iinclude` but not `-Iinclude/rakupp`, and
`src/ExtCtx.h` includes `"rakupp_ext.h"` — the flat spelling, which resolves
after an install and which CMake has carried a path for since the extension
ABI landed. So `EmbedApi.cpp` compiled everywhere except the WebAssembly
pipeline, and nothing noticed until the v3.5.0 release run exercised that job
and shipped without the Raku.js bundle.

v3.5.0's tag is left where it is: it names the commit its binaries were built
from, and moving a published tag is how a release ends up serving assets from
two different commits.

## v3.5.0 (2026-08-20) — the 6.e language revision

| | v3.14.0 | v3.5.0 |
|---|---:|---:|
| Roast assertions (all declared) | 195,992 | **198,628** |
| Roast files fully passing | 594 | **630** |
| Documentation examples byte-identical | 948 | **949** |
| Distributions passing their own suite | 48 / 59 | **49 / 59** |
| Local regression suite (`t/run.raku`) | 433 | **491** |
| `say "Hello"` compiled with `--exe` | 8,087,112 B | **8,493,752 B** |
| …compiled with `--exe --slim` | 4,856,936 B | **5,246,376 B** |

On the version number: this follows v3.14.0 and is not a revert. The release is
named for the language revision it carries; **4.0.0 is reserved** for the
modules and embedding milestone. Note that package managers order 3.5.0 *below*
3.14.0, so an upgrade will not be offered by version comparison alone.

Six Roast runs on one machine gave 629 / 629 / 629 / 629 / 628 / 630 files with
13-15 timeouts; the quoted figures are the last run, whose file list contains
every file the others passed plus one. Two files dropped in the 628 run
(`S17-channel/stress.t`, `integration/99problems-51-to-60.t`) and both score
5/5 and 37/37 re-run alone — the documented timeout flutter, not a regression.
**The gate is the file list, and against the release's reference run it is
clean: zero regressed, one gained.**

### `use v6.e.PREVIEW;` turns on the whole of 6.e, and nothing else turns it on

The whole of [6E-PLAN](docs/dev/plans/6E-PLAN.md). The support matrix at
[raku.online/spec/6e](https://raku.online/spec/6e/) runs 51 tracked changes four
ways — Rakudo 6.d, Rakudo 6.e, Raku++ 6.d, Raku++ 6.e — and now reads **50 full,
0 divergent, 0 partial, 1 not implemented**, against 23 / 1 / 19 / 8 when the
plan was written. The one is RakuAST, which is [its own
campaign](docs/dev/plans/RAKUAST-PLAN.md) and deliberately postponed.

Three decisions shaped it:

- **The revision is a property of the code, not of the process.** Each
  compilation unit records the revision it was compiled under, each code object
  inherits it from its unit, and the runtime reads the revision of the code that
  is *executing*. A sub compiled under 6.e keeps 6.e semantics when a 6.d
  mainline calls it, and a module written for either revision works inside a
  program written for the other.
- **Under 6.d, do the 6.d thing — always.** Thirteen 6.e behaviours had been on
  by default, which made them 6.d divergences rather than 6.e features. A 6.d
  program must not be able to tell which engine it is running on.
- **The additions are gated too** — `snip`, `snitch`, `nano`, `trans`, sub-form
  `rotor`, `.nomark`, `IO::Path.stem`, `Format`/`Formatter` and the rest are
  invisible under 6.d, exactly as they are in Rakudo. Hiding a routine that
  works can only break code; the deciding argument is that a program's meaning
  must not depend on the engine, and that the per-unit revision makes it safe.

Three items are deliberately **not** gated, each for its own reason, and are
named in the plan: multi-character `.succ` string ranges (blocked on the
deferred Str-range work), nested same-name packages (6.d's silent stash
replacement, which Rakudo's own warning calls legacy), and blocks accepting
extra positionals (an engine-wide arity gap, not a 6.e matter).

### Real-world sweeps

- **The Weekly Challenge, round two.** Challenges 371-387 — 351 files by 21
  authors, written after the engine was already passing 196k assertions. Thirteen
  fix batches took byte-identical output from 184 of 246 counted files to 229 of
  252 (**74.8% → 90.9%**), each batch gated on a full Roast run.
- **The 100 newest distributions in the ecosystem** ([the first freshness
  sweep](docs/dev/findings/FRESH100-2026-08-20.md)): 16 pass their own suite, 32
  fail on their own account, 49 never reach their tests. Its parse cluster was
  proven ours by a `raku -c` control and fixed — a named slurpy in a declaration
  list, a sigilless capture as invocant, a typed pointy parameter on a statement
  condition, chained statement modifiers, and `with` after a parenless call.
- **`require ::($name)` had stopped loading anything**, found by this release's
  battery gate. `require` evaluated its operand, so the symbolic ref resolved as
  a *symbol* — and the module it names is by definition not yet loaded. Bisected
  to the v4 arc; broken for a week and through the whole 6.e campaign. Every
  distribution that loads a driver, font or plugin by computed name was affected.

### Notes

- **Slim size budgets raised** to 5.5 MB (`--slim=-all`) and 6.0 MB (bare
  `--slim`) on darwin. A language revision's worth of engine put the smallest
  possible `hello` at 5,246,376 bytes locally and 5,263,960 on CI's universal
  build, over a 5.0 MB line pinned from one machine at v3.14.0. The budgets are
  tripwires against a cut silently stopping; the growth is recorded here rather
  than absorbed silently.
- **`perf-guard --check` is green** against the v3.14.0 baseline: no kernel more
  than 5% slower, `strscan` 9.4% faster. A same-load A/B against a rebuilt
  v3.14.0 binary agrees.
- **`tools/slim-diff.raku` now runs every child with stdin closed.** A corpus
  `-ne` one-liner parked forever on input nobody sends, and the 30-second cap
  did not reap them: one release run left ~2,200 processes alive on the machine.
- **Known and open**: Log::Async's `t/14-frame` scores 5, 0 and 1 of 6 across
  three runs of the same binary — a real race in async delivery, present in
  v3.14.0 too, and the reason the battery figure moved by one in both directions
  during this release's gates.

## v3.14.0 (2026-08-11) — only what the program needs

| | v3.1.0 | v3.14.0 |
|---|---:|---:|
| Roast assertions (all declared) | 197,111 | **195,992** |
| Roast files fully passing | 595 | **594** |
| Documentation examples byte-identical | 951 | **948** |
| Distributions passing their own suite | 48 / 59 | **48 / 59** |
| Local regression suite (`t/run.raku`) | 406 | **433** |
| `say "Hello"` compiled with `--exe` | 9,830,680 B | **8,087,112 B** |
| …compiled with `--exe --slim` | — | **4,856,936 B** |

Four Roast runs on one machine gave 594 / 595 / 591 / 594 files at
195,992 / 196,811 / 195,979 / 195,817 assertions with 13 / 12 / 18 / 16
timeouts — no repeating count in the first three, so a fourth broke the tie
(the same thing happened at v3.0.1), and the quoted figures are the repeating
594-file profile's. The headline reads lower than v3.1.0's published 197,111,
and the honest comparison says that is the machine, not the code: a v3.1.0
binary rebuilt from its tag and run the same day on the same machine scored
197,089 over 594 files with 14 timeouts — its published 595 / 11 does not
reproduce today either, and the ~1,100-assertion spread is two or three
600-assertion files crossing the timeout line in either direction. The
definitive check: across the 1,314 files that ran to completion in BOTH that
reference run and this release's quoted run, the net assertion delta is
**−4** — four documented timing-flap files (S17 promise/supply, pick.t, one
integration file) each one assertion short. At assertion granularity, on the
same machine on the same day, the two binaries are equal.

**The gate that matters is the file LIST, and it is clean.** Diffed against
that same-day v3.1.0 reference: one file each way, both members of the
documented flap set, both passing solo on the binary that "lost" them
(`set_intersection.t` 579/579, `list-quote-junction.t` 16/16). Zero real
regressions, zero real gains.

### The SLIM campaign: a compiled binary stops carrying what it cannot reach

The whole of [SLIM-PLAN](docs/dev/plans/SLIM-PLAN.md), P0 through P5, in one
release. The number a stranger can re-measure: `say "Hello"` compiled with
`--exe --slim` is **4,856,936 bytes** — down 50.6% from v3.1.0's 9.83 MB —
while every program in `t/regression` and `examples/` produces byte-identical
stdout, stderr and exit status built slim and built full (the differential
gate, `tools/slim-diff.raku`: 241 of 270 corpus
programs byte-identical, 0 different, the rest named as non-compiling,
nondeterministic or timed out; the module-battery leg adds 51 of 60 dist
test files byte-identical, 0 different).

- **Level `safe` is the new no-flag default**: compiled binaries are
  dead-stripped and symbol-stripped (9.83 → 8.09 MB), no feature removed, no
  analysis run. `--slim=none` is the old output; `safe,+symbols` keeps the
  symbol table for readable crash reports.
- **Bare `--slim` means `auto`**: a scan over the program plus every embedded
  module proves features unreachable and cuts exactly those. Four features
  exist — `unicode-names` (uniname/uniparse/unival), `unicode-collation`
  (unicmp/coll/.collate), `unicode-props` (uniprop Script/Block/Bidi_Class),
  `eval` (EVAL/require/regex code blocks) — behind a five-archive split of
  the runtime (`librakupp_{rt,parse,ucd_names,ucd_coll,ucd_props}.a` plus a
  stub archive). Anything the scan cannot decide keeps the feature; any
  dynamic construct (EVAL, `::($name)`, `."$name"()`, `<$re>`, an unembedded
  module) keeps everything and says so on stderr. `--slim=max` cuts on
  static evidence and ignores the dynamic constructs — unsound by design.
- **A wrong cut throws, never lies**: every cut feature's entry points throw
  `X::Feature::NotBuilt` — typed, catchable (`when X::Feature::NotBuilt`),
  naming the feature and the rebuild flag. The negative suite
  (`t/slim/run.raku`, 48 checks) proves it feature by feature.
- **Explicit `±feature`** overrides any level (`-all,+unicode-names`;
  a named feature beats the `unicode` group beats `all`), and every conflict
  is a loud error naming the alternatives.
- **The key documents itself**: `--slim=help` (grammar + feature table with
  the real archive sizes), `--slim=list` (keep/cut per feature with the
  reason and bytes, no compile), `--slim=why:FEAT` (every site forcing the
  keep, with module and line), `--slim=verify` (build slim AND full, run
  both, emit only on byte-agreement — refuses wrong cuts and
  nondeterministic programs alike, measured).
- **Every binary carries a manifest**: `rakupp --exe-info BIN` prints the
  version, compile mode, slim level and cut list, read by byte-scan so it
  survives stripping.

### Correctness fixes the campaign surfaced

The differential gate and the stub discipline found real bugs, all fixed
here:

- **Native codegen silently mis-ran regexes that touch program variables** —
  `/a $x c/` failed to match and `/ a { $n = 42 } b /` skipped its block in
  every `--exe` binary, because C++ locals live outside the regex engine's
  interpreter-side environment. Such patterns now fall back to bundling
  (correct output, a `note:` explains); grammar rules and named-regex
  declarations stay native — their blocks run in match context, which works.
- **Ordinary numification reached the cuttable numeric-value table**:
  `"٤٢".Int` transliterates Nd digits through their numeric values. Decimal
  digits moved to a never-cut decade-starts table (`uniDigitValue`), and the
  cross-check against the full table found the old private copy had been
  **missing twelve newer-script decades** (Garay, Tulu-Tigalari, Sunuwar,
  Kawi, Tangsa, Kirat Rai, Nag Mundari, Ol Onal, …) — multi-digit numbers in
  those scripts lexed wrongly in every prior binary. Ol Onal's zero sits at
  U+1E5F1, a decade not aligned to `…0`, which is why it is a table and not
  a formula. After the fix every Nd digit in Unicode agrees: `.Int` equals
  `unival`.
- **`<:Lu>` under `-unicode-props` threw**: the property dispatcher built its
  script-name set — touching the cuttable SCRIPTS table — before the
  category checks. Categories, POSIX-ish names and binary properties now
  resolve from never-cut tables first.
- **`X::Feature::NotBuilt` could be swallowed into a silent no-match/no-op**
  by eight lenient catch sites in the regex machinery. It is now its own C++
  type, rethrown exactly where leniency must not apply; ordinary errors stay
  lenient.

### Also in this release

- `t/run.raku` grew 406 → 433 (every slim level, feature and directive has a
  golden) and `t/slim/run.raku` (48 checks) plus `tools/slim-diff.raku`
  joined the release gates (RELEASING.md gate 4b).
- `perf-guard --check` against the v3.1.0 baseline: every kernel is 1.9-3.0%
  FASTER (fib -2.5%, asg -1.9%, loopsum -3.0%, hash -3.0%, strscan -2.3%,
  strpass -2.5%, subcall -2.5%). The baseline was re-recorded at this release,
  per the discipline RELEASING.md documents.
- The battery leg of the differential found `use-ok` (the builtin Test
  module's runtime `require`) invisible to the scan — three dist load-tests
  threw under `--slim` where full builds passed. The scan now treats
  `use-ok`, `eval-dies-ok`/`eval-lives-ok` and string-form `throws-like` as
  the dynamic constructs they wrap.

The documentation-examples row moved 951 → 948 inside its documented ±5
band (Rakudo randomizes hash iteration order per process; the moved rows are
`Set`/`Bag`/`Mix` examples whose *oracle* output drifts between runs).

Deliberately left open: flipping `auto` to the default (SLIM-PLAN P6, gated
on several consecutive green releases plus a field-built binary); the scan
for `--aot` (P7); a codegen env-bridge so variable-touching regexes compile
natively instead of bundling.

## v3.1.0 (2026-08-11) — Raku++ becomes something you can link against

| | v3.0.1 | v3.1.0 |
|---|---:|---:|
| Roast assertions (all declared) | 197,080 | **197,111** |
| Roast files fully passing | 594 | **595** |
| Documentation examples byte-identical | 952 | **951** |
| Distributions passing their own suite | 48 / 59 | **48 / 59** |
| Local regression suite (`t/run.raku`) | 398 | **406** |

Three Roast runs on one machine gave 197,110 / 197,112 / 197,111 assertions
across 595 / 595 / 595 files with 11 timeouts each — an unusually tight band,
so the quoted figure is the middle of three identical profiles rather than a
repeating count picked out of a spread.

**The gate that matters is the file LIST, and it is clean.** Diffed against a
v3.0.1 build in a worktree: zero regressions, and zero real gains. Three files
looked like gains and were not — `S17-procasync/{stress,no-runaway-file-limit,
many-processes-no-close-stdin}.t` timed out in a loaded reference run and pass
on the v3.0.1 binary when run alone. They are reported here as noise because
that is what they are.

### rakupp is now a library, in both directions

The headline is an ABI, and it has two halves that share one value vocabulary
rather than growing two.

- **`librakupp` exists.** Built with `-DRAKUPP_BUILD_SHARED=ON` and now shipped
  in every release archive beside `librakupp_rt.a` — PIC, hidden visibility,
  soname, and an export table that is exactly the 43 `rk_*` entry points the
  headers declare, with no interpreter internals leaked. That last part is
  checked by `tools/embed-smoke.raku` on every CI run rather than assumed.
- **Extension ABI 2** ([EXTENSIONS.md](docs/guide/EXTENSIONS.md)) — `rk_call`,
  `rk_call_value` and `rk_can` let native code call back INTO Raku, which was
  missing in both directions; `rk_error`/`rk_clear_error` carry a Raku exception
  across a C frame without unwinding through it, and re-raise it with its
  ORIGINAL type if the extension declines to handle it; `rk_root`/`rk_unroot`
  give a value that outlives its call. Additive, with a downgrade handshake, so
  an extension built against ABI 1 keeps loading unchanged — proven against a
  binary compiled from the previous header.
- **An embedding API** ([EMBEDDING.md](docs/guide/EMBEDDING.md), `rakupp.h`) —
  `rk_new`/`rk_free` with the three host-hostile behaviours (the 1 GiB stack
  thread, the process-wide `SIGPIPE`, the owned stdout) all opt-in; `rk_eval`
  over the REPL's own mechanism, so an interpreter is a session; `rk_run` for
  whole-program semantics; output capture and stdin feeding. `rk_ctx` hands a
  host the same context an extension gets, so a host reaches every accessor —
  and `rk_call` — through one vocabulary.
- **Raku.js runs on the public API.** `rakujs/rakupp_web.cpp` includes
  `rakupp.h` and nothing from the interpreter; the hand-rolled `std::cin` rdbuf
  swap it carried for two years is now `rk_set_input`. Porting it is what added
  `rk_run`: a playground wants a program, not an expression.

**Extensions had never worked on Linux or the BSDs.** A plain ELF executable
keeps its symbols out of `.dynsym`, so the first `rk_*` call died with an
undefined-symbol error — and because a well-written module falls back quietly,
the only symptom was the fallback's timings. Fixed with a `--dynamic-list` that
exports the `rk_*` glob and nothing else. Windows was broken too, in the plain
sense that neither MSVC nor MinGW would compile the loader.

### Two crashes, and a silent data loss

All three are reachable from ordinary code, because v3.0.0 made parallel the
default — no `start` block or thread of your own is required to hit them.

- **Concurrent regex matching segfaulted**, roughly one run in four. Two
  independent data races found with ThreadSanitizer: a string literal
  NFC-normalized itself *in place* on first evaluation, mutating a `std::string`
  inside an AST node every thread shares; and `$/` scoped outward to a frame
  that every worker shares, so N threads assigned into one `std::map` at once.
- **Concurrent writes to an open handle lost data** — twelve threads writing a
  line each to a rebound `$*OUT` produced eleven lines, because appending to the
  handle's buffer was a read-modify-write on shared state.
- **The per-node eval caches published a pointer without its payload.** Relaxed
  atomics let a reader see the pointer without the bytes it addressed — benign
  on x86, not on arm64, which is what this project ships.

### Behaviour changes that may affect your code

- **`eqv` is now type-aware about containers.** A `List`, an `Array`, a `Seq`
  and a `Slip` are different types, so `(1,2) eqv [1,2]` is now `False`, as it
  is on Rakudo. Itemization still does not count: `[1,2] eqv $[1,2]` stays
  `True`. If a test compares a `.map`/`.grep` result against a `(…)` literal it
  will start failing — correctly; add `.List` to the left side. This flushed out
  seven such comparisons in this repo's own suite.
- **An untyped routine parameter is `Any`-constrained**, so `sub f($x) { }; f(Mu)`
  is now a type-check failure. A *block*'s untyped parameter stays
  `Mu`-constrained, so `(-> $x {…})(Mu)`, `for (Mu) -> $x` and `.map(-> $x {…})`
  are unaffected.
- **`|c` no longer flattens a single `Array` argument**, so
  `sub wrapper(|c) { inner(|c) }` forwards what it was given. This had been
  silently breaking real code: `JSON::Native`'s (then `Rakupp::JSON`) `to-json([1,2,3])` returned `1`.
- **`printf` honours a rebound `$*OUT`** instead of writing to the terminal.
- **`my $x; $x === Any` is `True`**, matching Rakudo.

### Performance

No regression, and the baseline was **re-recorded at this release** — it had
been reading `2026-07-29 (v1.5.1)`, three releases stale, which
[RELEASING.md](docs/dev/RELEASING.md) lists as a known blind spot. Every kernel
had drifted faster than that reference (`fib` −14.6%, `hash` −10.2%, `strscan`
−9.6%), so it could no longer have caught anything short of a tenth. `loopsum`
keeps its 1.0.0 `best` so the old debt stays visible rather than being reset.

### Deliberately left open

- A real `rakujs/build.sh` run: emscripten is not installed on the machine this
  was cut from, so the Raku.js port was verified by compiling and linking it
  natively against the real headers, with only `EMSCRIPTEN_KEEPALIVE` stubbed.
- `my Any $x = Mu` is still accepted in *assignment* — a different mechanism
  from binding, with a conservative allow-list that needs subtyping before `Any`
  can join it.
- A `my` inside the parenthesised `((my $y = 5) for ^1)` form does not leak to
  the enclosing scope as Rakudo's does. The plain statement-modifier spelling is
  correct on both engines.

## v3.0.1 (2026-08-09) — the procedure, run

v3.0.0 was tagged without running [the release procedure](docs/dev/RELEASING.md).
This release runs it. Every figure below was measured on the 3.0.1 binary, on
one machine, in one pass — which is the whole point of the procedure, and is
why the numbers here disagree with the ones v3.0.0 published.

| | v3.0.0 published | v3.0.1 measured |
|---|---:|---:|
| Roast assertions (all declared) | 197,191 | **197,080** |
| Roast files fully passing | 594 | **594** |
| Documentation examples byte-identical | 945 | **952** |
| Distributions passing their own suite | 47 / 59 | **48 / 59** |
| Local regression suite (`t/run.raku`) | 398 | **398** |

The Roast figures are lower because they are real: three runs on this machine
give 197,060 / 197,036 / 197,060 for the v3.0.0 code, and four runs of this one
give 197,082 / 197,056 / 197,098 / 197,080 across 595 / 593 / 594 / 594 files,
with 12–16 files timing out per run rather than the 5 the v3.0.0 notes claim.
The quoted figure is the repeating profile, not the best of them; the fourth
run exists because the first three produced no repeating file count. The
recurring timeouts are the scheduler and IO timing files, which are
load-sensitive; the count is reported here rather than tuned away.

### What v3.0.0 shipped wrong

- **The version was never bumped.** `project(RakuPP VERSION …)`, the Guix
  package and `flake.nix` all still said `2.0.0`, so every v3.0.0 binary
  answered `rakupp --version` with `2.0.0`.
- **The MSVC build did not link.** `rakuppJsonFastShimSource` was declared as
  a block-scope `extern` inside a function in `namespace rakupp`. That names
  `rakupp::…` under Clang and GCC but `::…` under MSVC, so the Windows job
  failed with `LNK2019` while the MinGW job (GCC) stayed green — which is
  exactly the shape a single-toolchain check misses.
- **Three of the five platform binaries on the release are from the wrong
  commit.** The tag was moved after the release was published: the
  `linux-x86_64`, `macos-universal` and `windows-x64` assets are dated
  2026-08-08 22:59 and were built before the JSON::Fast batch. Only the
  OpenBSD and MinGW assets came from the tagged commit, because those were the
  only jobs that passed on it.
- **A regression case passed only on machines that had two zef modules.**
  `t/regression/module-compat-cluster.raku` imports `JSON::Name` and
  `JSON::Unmarshal`, and `is json-name` is a trait, so the import has to be
  compile-time and cannot be probed from inside the file. It now declares
  `#?requires` for both, which the runner already understood.
- **The documentation figures disagreed with each other.** `docs/status/ROAST.md`
  and the README carried 197,191 / 218,772 while COUNTING, FEATURES, GUIDE,
  HIGHLIGHTS, OVERVIEW and ROADMAP still carried 197,090 / 218,675, and the
  timeout count read 5 in one file and 10 in another.

### JSON::Fast is no longer vendored

v3.0.0 compiled JSON::Fast's own source into the binary as a C++ string
literal and served it ahead of any copy on disk. It is not our module to
carry: the arrangement pinned users to 0.19 whatever they had installed,
silently, and put a third-party source tree inside the compiler with nothing
but a comment recording it — in a project whose first line advertises no
third-party dependencies. `NativeJsonFast.cpp`, the `loadModule` intercept
and the internal `rakupp-json-from-parts` builtin are all gone, along with the
`RAKUPP_JSON_FAST` escape hatch that existed only to switch between them.
`use JSON::Fast` now loads the module from disk and runs it as ordinary Raku.

The dist bar does not drop with the shim removed — it goes UP, 47/59 to 48/59,
for a reason unrelated to JSON (see Supply.wait below); JSON::Fast's own suite
matches Rakudo file for file, 13/14 against Rakudo's 13/14.

### Strings are shared, not copied

Removing the shim was only affordable because the reason for it turned out to
be a defect rather than a fact about interpretation. `Value` held its Str as a
`std::string` **by value**, and `Value` is copied on every argument pass, every
operand evaluation and every list element — so a long string was memcpy'd once
per operation. On top of that the "ASCII fast path" in the `nqp` scanning ops
re-scanned the string prefix on each call to decide whether a byte index was a
codepoint index. Both costs are O(length) per *character examined*, which makes
any tokenizer written in Raku O(n²) no matter how it is written.

`CowStr` (see [Value.h](src/Value.h)) keeps short strings inline, where
`std::string`'s own small-buffer optimisation already makes a copy free, and
promotes anything from 64 bytes up into a shared immutable body — so copying
becomes a refcount bump. Promotion is eager rather than lazy-on-first-copy
because rakupp runs work in parallel: a lazy promotion would have to mutate the
source from a const copy constructor. Because the promoted body is immutable it
is also a sound place to cache the two properties the scanning ops kept
recomputing, so `nqp::ordat`/`eqat`/`substr`/`chars` and `.chars` now answer
from it.

JSON::Fast parsing the same generated documents. Both rakupp columns run the
module as ordinary Raku; the v3.0.0 shim column is what v3.0.0 actually did
instead, and is a C++ parser rather than the module:

| input | v3.0.0 code, shim off | v3.0.1 | Rakudo | v3.0.0 as shipped (shim) |
|---|---:|---:|---:|---:|
| 51 KB | 255 ms | 102 ms | — | — |
| 103 KB | 777 ms | 190 ms | — | — |
| 208 KB | 3,245 ms | 381 ms | — | — |
| 421 KB | 13,969 ms | **764 ms** | 51 ms | 5 ms |

The scaling matters more than any single row: doubling the input used to
quadruple the time, and now doubles it — the rows above step by 1.86×, 2.00×,
2.00×. rakupp remains ~15× slower than compiled Rakudo on this workload, and
that residue is not an algorithmic defect: measured directly, a loop iteration
costs ~0.46 µs here against Rakudo's ~0.02 µs, so ~23× is what tree-walking an
AST costs against JIT-compiled bytecode. JSON::Fast is a per-character `nqp::`
loop, the worst possible shape for a tree-walker. Closing that gap is a
bytecode VM, not an optimisation.

Four sites were fixed, each the same mistake in a different place: an "ASCII
fast path" that re-derived whether the text was ASCII on every call, by
scanning it. `nqp::ordat`, `eqat`, `substr` and `chars` were the first three;
`nqp::index`, `findnotcclass` and `iscclass` were found afterwards by asking
why Rakudo was still faster than the remaining gap explained, and
`findnotcclass` alone — called once per string token, scanning the whole
document each time — was still quadratic on its own. `nqp::strtocodes` also
ran a full Unicode normalization pass over ASCII, which every normalization
form leaves unchanged.

**Read that table against the shim, not only against the column next to it.**
The middle column is the same interpreter with the shim switched off, so the
11× is the honest measure of what changed in the engine — but it is not what
v3.0.0 users had. v3.0.0 as shipped parsed that file in **5 ms**, because a
C++ parser was doing the work. Against that, 764 ms is **153× slower**, and
anyone who came to depend on v3.0.0's JSON::Fast speed will feel it. That
regression is the deliberate price of not shipping someone else's module
inside the compiler; the engine improvement is what keeps the price at 153×
rather than 2,800×. If the gap matters for your workload, the parse is still
available as ordinary Raku — it is simply no longer secretly replaced.

### `Supply.wait` never blocked

Found by re-measuring the module battery after the speedup, which is the
point of re-measuring: a performance change is also a concurrency change,
because it reorders every race in the system. `Log::Async` dropped two test
files, and the failing one reported `expected: ["one", "three"]` against
`got: ["one", "three"]` — not an equality bug but a list still being filled.

`Supply.wait` returned `True` immediately for every Supply; the `wait` case
sat in the same trivially-true line as `done`/`close`/`quit`. Measured, 0 ms
against Rakudo's 317 ms on a supplier completed after 300 ms. Anything using
it as a barrier was never synchronised, and whether it worked came down to
interpreter speed — the pre-3.0.1 binary won that race in Log::Async's suite
and a faster one lost it. A live Supply now waits on its Supplier's recorded
completion, polling under the supplier's own stripe with `sleepYield` between
checks so the emitter can run; `quit` records its state too, so a quit supply
releases `wait` instead of hanging it.

It blocks 304 ms where Rakudo blocks 317. Log::Async goes to 17/17 — better
than the 15/17 it managed before any of this release's work — which is what
takes the distribution bar from 47/59 to 48/59. Roast gained a fully-passing
file too, S17 among them. Left open: Rakudo also rethrows the
quit exception out of `wait` and we return `True`; and `Log::Async`'s
`t/14-frame` is flaky in parallel mode on the previous binary too, which looks
like `callframe` under worker threads.

Gate results on the 3.0.1 binary: `t/run.raku` 398/398; `perf-guard --check`
OK, no kernel more than 5% slower (fib −5.3%, asg −7.0%, hash −4.3%, loopsum
+4.5% and still behind its best — standing debt, not new); `run-optbench` OK,
interpreter, `--exe`, `--exe -O` and Rakudo agreeing on every kernel; the
conformance sweep at 952 documentation examples and 24 operator divergences in
4 clusters over 121 operators and 833 expressions.

Left open deliberately: the `perf-guard` baseline is still the one recorded at
v1.5.1 — it was not re-recorded for v2.0.0 or v3.0.0, so the gate has been
comparing against an old reference. It passes against it, so re-recording is a
separate decision rather than something to slip into this release. The guard
also has no string kernel, which is why a change of this size in the string
representation could pass it without being measured; the JSON table above is
standing in for one.

## v3.0.0 (2026-08-09) — faster by default

| | v2.0.0 | v3.0.0 |
|---|---:|---:|
| Roast assertions (all declared) | 197,090 | **197,191** |
| Roast files fully passing | 594 | **594** |
| Documentation examples byte-identical | 952 | **945**† |
| Distributions passing their own suite | 50 / 59 | **47 / 59**\* |
| Local regression suite (`t/run.raku`) | 312 | **398** |

The v3.0.0 Roast figure is measured **with both new defaults on** — the
configuration users actually get — and sits at the top of a four-run band
(197,186–197,191) with an identical 5-file timeout list per run, against the
GIL's 11 timeouts. †±5 documented band (Rakudo randomizes hash order per
process; the moved rows are Rakudo-vs-doc drift, not ours). \*A bar-raise in
the reference environment, not a regression: at v2.0.0 Rakudo could not load
the `Test::META` dependency chain, so `t/*meta*` files sat outside every
comparison; they now count — and 47/59 clears that stricter bar.

### JSON::Fast ships native

The ecosystem's most-depended-on module is the one module an interpreter
cannot serve: its parser walks text one grapheme at a time, and the 332 KB
SPDX license list `License::SPDX` loads cost 52.7 s (compiled Rakudo: ~1 s).
`use JSON::Fast` now loads an embedded shim — the module's own source with
only the parse machinery swapped for a C++ parser at full fidelity
(`Str.Numeric` number typing with arbitrary-precision `Int` and exact `Rat`,
surrogate pairs, strict escapes, `:immutable` `Map`/`List`, `:allow-jsonc`,
grapheme-accurate `X::JSON::AdditionalContent` positions). `to-json`, the
exception class and the `EXPORT` protocol stay the module's own code, and its
own 14-file suite is the acceptance gate: 14/14. The SPDX parse is 6.6 ms —
faster than compiled Rakudo's 32 ms. `RAKUPP_JSON_FAST=0` falls back to the
disk module for one release.

The speed uncovered six general faults, each fixed and Rakudo-verified: a
`*%named` slurpy's `where` clause was invisible to multi dispatch (an
empty-only `.new` candidate swallowed every call and recursed); attribute
user traits (`is json-name`, `is unmarshalled-by`, `is specification`) now
reach the Attribute meta-object, with the JSON::Name / JSON::Unmarshal /
META6 role checks and accessors answered from them; the precomp cache
dropped `use Foo:ver<…>` constraints entirely (works-fresh, breaks-cached);
the paren spelling `:ver(v0.0.20+)` wasn't parsed; an unbound dynamic read
as a defined empty container so `@*META-CANDIDATES // <defaults>` never fell
back (and a `my @*dyn` is now visible to its own initializer); and
`".".IO.parent` answered `"."` instead of `".."`. `License::SPDX.new`: 727
licenses in 0.3 s, previously never completed. `Test::META` passes 3/3.

### The three pillars

Planned 2026-08-07 ([VERSIONS.md](docs/dev/plans/VERSIONS.md)), landed in two
days of gated batches:

- **A grown-up CLI** ([CLI-PLAN.md](docs/dev/plans/CLI-PLAN.md)): one
  two-phase option parser; the perl one-liner surface (`-n -p -a -F -l -0`,
  in-place `-i[.bak]`, `-M`/`-m`, clusters like `-lane`) with live `perl`
  differentials in the suite; `--profile` with a routine-level wall profiler
  (fib(15) golden: exactly 1,973 calls); MAIN usage and Unix argument
  conventions oracle-verified against Rakudo, 34-case matrix.
- **Parallel by default** ([PARALLEL-PLAN.md](docs/dev/plans/PARALLEL-PLAN.md)):
  `start`/worker threads run on all cores; `RAKUPP_GIL=1` is the escape hatch
  (one release). The campaign hardened the runtime to the written memory
  model — a race in user data must never corrupt the interpreter: thread-local
  execution registers, decided-once atomic AST caches, striped containers, a
  locked worker registry, per-supplier mutexes, channel workers with parallel
  manners, daemon teardown, and a real compare-and-swap retry loop for `cas`
  (the thread.t livelock was three threads ABBA-deadlocked on the stripe
  pool). Parallel-mode measurement discipline lives in
  [PARALLEL-SPEEDUP.md](docs/guide/PARALLEL-SPEEDUP.md): 3.72× contention-free
  at N=4 on 4P cores.
- **True LTM by default** ([LTM-PLAN.md](docs/dev/plans/LTM-PLAN.md)):
  `|` alternation and protoregex dispatch rank by declarative prefix via a
  Thompson NFA per alternation — never executing user code during ranking —
  with lexical/grammar/`<sym>` expansion routes, `:m` literals and modeled
  `<ws>`; `RAKUPP_LTM=0` is the legacy probe (one release). Gates: the flag
  beat the probe on full Roast in every pair of the campaign with its fail
  set a strict subset; battery verdicts identical; spec-site 368/368 runnable
  examples in both settings. `internals/REGEX-LTM.md` documents the machinery.

Fixed along the way, each with a both-engine regression file: react/whenever
deferred activation with LAST/QUIT phasers (issue #18), geometric sequences
going exact past int64 (`(1, 2, 4 ... *)[110]` is now 2^110, not int64-max),
the 100-element list-gist cap, `Channel.list` draining until close, plain-regex
`<?{…}>` assertions evaluating for real, and the `@arr` interpolation ranking
as the LTM `|` it is in Rakudo.

## v2.0.0 (2026-08-07) — other people's code, honestly counted

| | v1.8.0 | v2.0.0 |
|---|---:|---:|
| Distributions passing their own suite | 32 / 59 | **50 / 59** |
| Roast assertions (all declared) | 197,060\* | **197,090** |
| Roast files fully passing | 633\* | **594** |
| Documentation examples byte-identical | 945 | **952** |
| Local regression suite (`t/run.raku`) | 280 | **312** |

\*inflated — see the next paragraph. The Roast figure is the middle of three
runs (197,090 / 197,087 / 197,092; 594 / 593 / 594 files — the suite's known
±3-assertion flap band) with matching denominators.

### The honest bar

The release-defining change is subtractive. `subtest "description" => { … }` —
the Pair form, which is how most of Roast (and nearly every ecosystem module)
writes subtests — **never ran its body**: the block arrived as a Pair value the
`subtest` builtin didn't extract, so every such subtest auto-passed as empty.
Fixing one line re-measured the world: the top line dropped 197,320 → 194,980
(−2,340 vacuous passes, −39 "fully passing" files) as the hidden bodies started
actually testing, and the rest of the cycle earned the total back honestly —
v2.0.0's 197,090 clears a bar v1.8.0's 197,060 never faced. Do not compare
Roast numbers across this boundary without that correction. Three
distributions' suites also fell when their subtests woke up and were won back
with general fixes before release.

### 32 → 50 of 59 distributions

The bar is unchanged from v1.5.2 — a distribution counts only when its own
`zef` install-time suite passes — and the additions are the deep end of the
top-50: **Cro::HTTP, Cro::Core, DBIish, HTTP::UserAgent, JSON::Fast,
Data::Dump, Config, XML, Text::Utils, Log::Async, HTTP::Tiny, IO::Glob** and
more. As ever, almost none of it was module-specific: the standout general
fixes include per-loop-execution `state` frames (Cro's `++state` counter),
lvalue-mode method calls (`$obj[$i] = v` through `return-rw`), object `.Str`
identity + structural `eqv`, parameterized-role `::T` binding and role puns,
`callwith`/`callsame` through built-in parents, and the reflection contract
(`^methods`, `.params`, `get_value`) that `Data::Dump` reads. The two
remaining DIFFs are known and scoped: `AttrX::Mooish` (deep Rakudo-metamodel
emulation, its own campaign) and `NativeHelpers::Blob` (MoarVM-bound by
design — it pokes MoarVM's C object layout).

### The pre-2.0 review

Before this release the whole hand-written source (~57k lines) got a
fresh-eyes review — nine parallel passes, five gated fix batches, every batch
held to zero Roast/battery/corpus regressions
([docs/dev/findings/REVIEW-2.0.md](docs/dev/findings/REVIEW-2.0.md)). It
removed ~170 lines of provably dead dispatch arms, fixed four oracle-verified
parser divergences (`@a»²`, `∞ < 5`, `$(EXPR with X)`, `:16<…>` radix
literals past 64 bits — the last un-blocked a 2,035-test Roast file), made
named/slurpy `where` constraints smartmatch, and closed six compiler-only
divergences — compiled anonymous subs and methods now have their `return`
boundary (a compiled `sub { fail … }` used to abort the binary), and two AST
fields that never survived the precomp cache now do. **The AST cache format
is now v3**: existing caches are silently re-parsed once after upgrading.

### `Supply.interval` is a real timer; `done` is a real control exception

`Supply.interval($interval, $delay?)` was a finite stand-in that emitted 0..4
instantly; it now ticks on real time, forever, with Rakudo's exact semantics
(first value after `$delay`, immediately by default). Making it real exposed
three long-standing async gaps, all fixed: `done` now *exits* the whenever
block / supply body / react body it is in; `whenever $supplier` taps the
supplier's `.Supply` instead of running the block eagerly; and `whenever
$promise` on an unkept promise registers and fires once with the kept value —
so the standard `react { whenever signal(SIGINT) {…}; whenever $kill { done } }`
server-shutdown shape works end to end.

### Performance

`perf-guard --check` passes against the recorded v1.5.1 baseline: `fib`,
`asg` and `hash` at or under baseline, and the `loopsum` debt the
battery-50 correctness work had introduced is **paid off** rather than
shipped. The per-loop-execution `state` frame (which Cro's `++state`
counter made necessary) put one extra scope hop on every lookup in every
loop — ~5% on `loopsum`. A conservative AST scan now skips the frame for
loops that provably declare no `state` (anything the scanner can't rule
out keeps it), cached per loop node; the state-reset semantics are
unchanged on every sensitive Roast file. The interpreter/compiler
agreement gate (`run-optbench`) passes 4-way on every kernel.

## v1.8.0 (2026-08-03) — other people's code

The whole release is about running code nobody here wrote. v1.5.2 set the
standard — a distribution counts as working only when its own `zef`
install-time test suite passes — and this is the largest move that number has
made. Almost none of it was module-specific work: the modules found general
bugs, and fixing those is what moved everything at once.

| | v1.7.0 | v1.8.0 |
|---|---:|---:|
| Roast assertions (all declared) | 196,590 | **197,060** |
| Roast files fully passing | 631 | **633** |
| Distributions passing their own suite | 18 / 59 | **32 / 59** |
| Operator divergences from Rakudo | 30 | **24** |

The Roast figure is the repeating profile of three runs (197,060 / 197,060 /
197,062, 633 files each time) with identical denominators, so no file stopped
emitting TAP — see [docs/status/COUNTING.md](docs/status/COUNTING.md) for why
that check matters more than the percentage.

### `URI` 0.3.8: 88 of 222 assertions, then 222 of 222

One distribution taken end to end in a day, by **twenty general interpreter
fixes and not one line of `URI`**. The ones with the widest blast radius:

- **Every subscript target takes the whole comma list.** `%h<k> = 1, 2` stores
  `$(1, 2)`, unlike `my $x = 1, 2`, which is item assignment. This was wrong for
  ordinary hashes and arrays, not only for `URI::Query`.
- **A `Proxy` is a container, not a value** — it was being rendered and compared
  as itself rather than through its `FETCH`.
- **A coercion parameter is the LEAST specific match.** `Str() $x` scored equal
  to an exact nominal type, so `multi method authority(Str() $a)` tied with
  `multi method authority(Nil)` and won on declaration order.
- **One answer for type conformance.** `~~` knew the whole story — a "does"
  table, the numeric and string towers, the class and role ancestry walk — while
  parameter dispatch had a second, laxer copy that returned a blanket true for
  any type object. The third bug of that day whose cause was a duplicated lookup
  that had drifted from its original; they are now one function.
- **A typed attribute resets to its TYPE, not to `Any`**, so `$!x = Nil` followed
  by `$!x .= new` works.
- **A `Hash` flattens to its `Pair`s**, so an empty one contributes nothing.

### `zef` installs, and seven more distributions pass

`zef` 1.1.3 runs under Raku++ and its install writes a real `CompUnit`
repository entry, so `install` → `use` works end to end. That needed `$*HOME`,
`$?DISTRIBUTION`, package-name adverbs, and `$*REPO.need`/`.installed`. `XML`
and `YAMLish` reached `zef test` along the way.

**The parser could not see installed modules, only ones on a `lib` path.** A
`use` is resolved to a *file* at parse time so the parser can scan it for the
operators it declares; that resolver was a second copy of the loader's, and it
only knew about search paths. A zef-installed module therefore contributed no
operators, and a program using one died with `Undefined routine 'SPACE'` from
inside a grammar. The two resolvers are now one.

### TLS runs, certificates and all

`IO::Socket::Async::SSL` works under Raku++, verification included. Three
general fixes got it there, none about TLS:

- **A `constant` as a return type.** `sub get_dh2048() returns DH` with
  `my constant DH = Pointer` died "Type 'DH' is not declared" the moment it
  returned: the return check consulted classes, subsets and native names but
  never the routine's own closure, which is where a constant lives.
- **`unless` accepted an `orwith` chain** it should have refused, so a dispatch
  written as `if !$!ssl {} elsif … {} orwith $!connected-promise {}` re-ran its
  handshake branch after every read.
- **`===` compared any kind-tagged hash by its rendering.** Right for
  `Set`/`Bag`/`Mix`, wrong for every reference type that is a hash underneath:
  two distinct `Promise`s both render `Promise`, so they compared identical, and
  a socket retiring a finished write with `.grep({ $_ !=== $p })` discarded every
  write it had. A follow-up pass extended identity to the four reference types
  stored as plain scalars — `Buf`, `Instant`, `Duration` — and gave `Capture` the
  value semantics it actually has. `Buf.new(1,2) === Buf.new(1,2)` was True.

**Left open, deliberately:** under the GIL a `Lock` is a no-op, so a `start`
block taking a lock runs inside the holder's critical section where Rakudo makes
it wait. Giving `Lock` a real recursive mutex fixes the ordering and then
deadlocks, because the module holds one process-wide lock across socket I/O and
the GIL and the mutex are taken in opposite orders. Fixing it properly means
making an `await` inside a lock release the GIL the way Rakudo's thread-pool
await does. The comment at the site now says this instead of claiming the GIL
already serialises.

### A precompiled-parse cache

Modules are parsed once and the AST is cached under `~/.cache/rakupp/precomp`,
which takes a module's cost from 16 ms to 5.7 ms interpreted and 19 ms to 6.6 ms
under `--exe` — 38% off total startup for `use XML` plus a mainline. There is no
bytecode: Raku++ walks the AST, so the AST already *is* its code representation,
and nothing on the hot path moved.

**Both halves are off by default** (`--precomp-modules=on`, `--precomp-files=on`,
or `RAKUPP_PRECOMP_*`): Raku++ does not write to a user's disk unasked.

Validation is by **content hash**, never by timestamp — `touch` changes nothing,
and an edit backdated to 1970 is still picked up, which is the failure mode of
Python's default `.pyc` scheme. One entry per source file, so twenty edits of a
module leave one entry, and the search path is part of the key because `-I`
decides *which file* a `use` resolves to.

Every bug found in this cache has been in invalidation rather than in the
serializer, including the last one: **an entry whose source file was deleted was
never removed.** It is now a third state, reported as `orphan` by
`--precomp-info` and dropped as Raku++ goes — after writing an entry it clears
any orphan sharing that entry's fan-out directory, which is 1/256th of the cache
and costs a few small reads. No timer, no age policy. See
[docs/guide/CACHING.md](docs/guide/CACHING.md).

### Compiled binaries carry their modules

`--exe`, `--aot` and `--bundle` embed the transitive `use` graph as serialized
ASTs, so a compiled binary runs with the module tree deleted.

This also fixed a class of bug rather than an instance: **`--aot` was silently
lossy.** The emitter listed each node type's fields by hand and dropped whatever
nobody remembered, so `my Int(Str) $n = "42"` compiled into a binary that threw a
type error. It now emits the same serialized blob the cache uses and cannot have
that shape of bug again. `--ast-roundtrip FILE` is the verification tool: 824
Roast files and 375 battery modules round-trip byte-identically.

### `showcase/modinfo`

A distribution inspector built on **seventeen ecosystem modules at once** — the
kind of program that only works if the engine does. Writing it produced sixteen
general interpreter fixes and zero module workarounds, nine of them from
`IO::Glob` alone, and its output is byte-identical under both engines.

### Everything else

- **NativeCall now marshals through `libffi`.** The library is `dlopen`ed at
  runtime, never linked and never vendored, so the binary keeps its
  no-dependency property and platforms with no libffi to load fall back to the
  previous fixed-prototype path. `rakupp --ffi-info` reports which backend is
  live; `RAKUPP_FFI=0` forces the fallback, and `RAKUPP_FFI=/path/to/lib` picks
  a specific library. `FFI_DEFAULT_ABI`'s numeric value differs per target *and*
  per libffi release, so it is probed rather than hardcoded: candidates are
  tried until one passes a self-test of real calls (`strlen`, `abs`, `ldexp`,
  `ldexpf`), and if none does the backend disables itself. That probe earned its
  keep immediately — this machine's arm64 libffi wants ABI 1 and its x86-64
  slice wants ABI 2, and neither matches the value a table built from current
  libffi headers would have used. What it fixes:
  - **`num32` arguments and returns were silently wrong.** A C `float` was
    passed and read as a `double`: `ldexpf(3e0, 2)` answered `0` and
    `strtof("2.5")` answered `5.3e-315`. Both are now exact.
  - **Variadic C functions were silently wrong.** A slurpy now marks where C's
    `...` begins — `sub snprintf(Buf, size_t, Str, *@args --> int32)` — and each
    variadic argument is typed from its runtime value under C's default argument
    promotions. `snprintf($b, 64, "%d and %s and %.2f", 42, "hi", 3.5e0)`
    produced `"0"` before and produces `42 and hi and 3.50` now. The spelling
    matches Rakudo's, which reads a trailing slurpy the same way, so this is a
    parity fix rather than an extension.
  - **More than 8 integer or 8 float arguments** was a clean `X::NYI`; there is
    no cap now.
  - **Callbacks are `ffi_closure`s.** Parameters are typed from the `Callable`'s
    own signature instead of arriving as six raw `long`s, the return is typed,
    the arity is unbounded, and so is the number of distinct callbacks (the old
    trampoline pool held 64 and silently handed back a null pointer past that).
    A callback arriving on a thread the interpreter never entered is now
    detected and ignored with a warning instead of being run without a scope to
    run in. A W^X policy that refuses `ffi_closure_alloc` falls back to the pool.
  - **Without libffi, the cases the fallback cannot express now throw** rather
    than computing garbage. That is four cases: `num32`, variadics, more than 8
    integer or 8 float arguments, and more than 64 distinct callbacks. The last
    one used to hand C a **null function pointer** — `qsort` with a null
    comparator hung the process, a long way from the line that caused it.
  - **`RAKUPP_FFI=/path/to/libffi` that cannot be loaded is no longer silently
    ignored.** It fell through to the built-in candidate list and used whatever
    the system shipped, which is the opposite of what naming a library asks
    for. It now reports the failure and runs on the fallback.
  - **`--exe` gave a different answer than the interpreter for a variadic
    call.** The compiled bridge rebuilds a native sub's parameter list as
    generated C++ and did not carry `slurpy`, so the binary prepared a variadic
    call as a fixed one and put the `...` arguments in registers instead of on
    the stack: `snprintf(Str, 0, "%d-%s", 42, "hi")` answered 5 interpreted and
    8 compiled.
  - **`RAKUPP_FFI_TRACE=1` logs every crossing to stderr as it happens** —
    library, resolved C symbol, the marshalled arguments and the raw return.
    It reports what actually crossed rather than re-printing the Raku values, so
    a marshalling bug shows up in the trace instead of being hidden by it. No
    external tracer can do this: lldb/ltrace/dtrace see C symbols, and the
    interpreter's own C++ calls `strlen`/`malloc` constantly, so a breakpoint
    cannot separate a crossing the program asked for from one the runtime made.
  - New guide: [docs/guide/FFI.md](docs/guide/FFI.md) — whether you need to
    install libffi (no), whether it works compiled as well as interpreted (yes),
    the type map, the variadic story, how to *prove* a call reached C (pick the
    test subject with care — `sqrt` and `abs` are Raku builtins and print the
    same either way, while `strlen` is not in the language at all and so is
    evidence by itself), and how to trace calls live.
- **`is repr('CUnion')` works** — every field at offset 0, the type as wide as
  its widest member.
- **`nativesizeof` answered from its own private table** and disagreed with the
  marshaller that had to place the value: it said `int` was 4 bytes where every
  call passed 8, and it answered a flat 8 for a `CStruct` class rather than the
  struct's real size (24 for `int32`/`num64`/`uint8`). It now answers from the
  same width table the marshaller uses, and lays out CStruct/CUnion classes.
- **C's `bool` is one byte**, not a machine word. The width table said 8, which
  made every `CArray[bool]` stride wrong and read a whole register back from a
  one-byte return.

- **The string bitwise operators combine CODEPOINTS, not UTF-8 bytes.**
  `~&`, `~|` and `~^` on a `Str` operated on the encoded bytes, which agrees
  with Rakudo for ASCII and is wrong for everything else: `"A" ~^ chr(0xFF)`
  answered two characters (0x82, 0xBF) where Rakudo answers one (0x00BE), and
  the bytes left behind were not valid UTF-8. A `Buf` still combines bytes,
  which is what a Buf is.
- **`prefix:<~^>` declines, as Rakudo does** ("not yet implemented"). It used to
  complement the UTF-8 bytes and hand back the result as a `Str`, so `~^ "1"`
  produced a string reporting `.ords` of 0x0E while printing the byte 0xCE.
  There is no agreed meaning for the complement of a codepoint, so inventing one
  would only be a new divergence. That stray byte had travelled into a generated
  page on the website and aborted a build, since BSD sed refuses an illegal byte
  sequence under a UTF-8 locale.

- **`say $*RAKU` printed nothing.** The object answers every accessor through
  the method dispatcher, but `say` renders from the Value alone, and a `Raku` /
  `Compiler` value carried no data at all — so it fell through to an empty
  string. It now gists as `Raku (6.d)`, following `use v6.c`/`v6.e`, and the
  compiler as `Raku++ (2026.07)`, which is the shape Rakudo uses (name plus the
  version *that* object reports, not the language revision in both). `.Str` is
  the bare name, as in Rakudo.
- **`dd` renders `.raku` and dispatches it.** It hardcoded `.gist`, so a type
  with a `.raku` of its own printed a generic rendering — a user class showed
  `C<obj>` where Rakudo shows its `.raku`, and `dd Any` printed `(Any)` instead
  of `Any`. `dd $*RAKU` now gives the constructor form.
- **`$*RAKU.compiler.auth` is the person who wrote the compiler** rather than
  "The Raku Community", which stays the *language's* authority.
- **A module's exported sub beats a built-in of the same name under `--exe`.**
  `sub val() is export` in a module, `say val()` in the program: the interpreter
  printed the module's answer and the compiled binary printed the built-in's
  `Nil`. Native codegen resolves a call by name at compile time and emitted a
  cached builtin pointer for anything in the builtin table, so the run-time
  environment lookup that finds the module's `&val` never happened — while the
  interpreter checks that environment *before* the table. Codegen now knows
  which names the `use`d modules export and routes those calls through the
  environment (`-O`'s direct named-builtin calls too). The deliberate carve-out
  is unchanged: a **non**-exported module sub of a built-in's name stays
  module-private, so the importer still gets the built-in. `--aot`/`--bundle`
  interpret and were never affected.

- **`IO::Handle.flush` did not exist.** These handles buffer in memory until
  `.close`, so a program that flushes deliberately — a log, or a trace file
  something else reads *while* it runs — saw nothing until exit, and calling
  `.flush` died rather than doing nothing.
- **`&MAIN`'s usage text now matches Rakudo's**, byte for byte, for a `multi
  MAIN` with docs, defaults and short options. It differed in four ways at once
  and leaked a routine's `#|` onto its own parameters in a one-line signature.
- **`IO::Socket::INET.new` produced a value reporting as `Socket`**, so a sub
  with an `IO::Socket::INET` constraint rejected its own listener. The async
  socket mapped its internal kind to the Rakudo type one line away in the same
  switch; the synchronous one never did.
- **`$( … )` in a regex interpolated nothing.** Only an identifier was spliced
  into a pattern, so `/ 'Content-Length: ' $($body.bytes) /` reached the regex
  parser verbatim — the `$` read as the end anchor. It did not error; it quietly
  matched something else.
- **`next without $x` read `without` as the loop label.** The optional-label
  guard excluded the block keywords, which do not include `with`/`without`
  because everywhere else those start a term. Affects `last`, `next` and `redo`.
- **`Buf.new($blob)` stored the blob's element count** as its single byte. A
  `Blob` is `Positional` and the constructor's flattener knew `Array` and `Range`
  but not a buffer, so it fell through to numifying it.
- **String methods take an ASCII fast path.** Over a leading run of plain ASCII a
  byte index and a grapheme index are the same thing, so the codepoint expansion
  can be skipped entirely; the nqp string ops also stopped copying the haystack.

Documentation corrected in the same pass: the README and `docs/guide/MODULES.md` both
still promised that *"a missing or broken `use` is a warning, not a fatal
error — the rest of your program keeps running"*. That stopped being true; a
`use` that cannot be found or fails to compile is now fatal and exits non-zero,
as in Rakudo. The old leniency mostly hid real failures — a half-loaded module
produced phantom output that read as a working program.

Known and not fixed here: `Buf ~^ Buf` answers a `Str` rather than a `Buf`, and
gets the wrong bytes. Pre-existing and identical before and after this change.

## v1.7.0 (2026-08-01) — the interpreter gets faster, and two more modules pass

Two threads. The interpreter learned to **specialise the shapes hot loops are
made of**, which is the largest single speed-up since the performance campaign;
and the module work continued against the standard set in v1.5.2 — a module
counts as working only when its own `zef` install-time test suite passes.

| | v1.5.2 | v1.7.0 |
|---|---:|---:|
| Roast assertions (all declared) | 196,568 | **196,590** |
| Roast files fully passing | 630 | **631** |
| Distributions passing their own suite | 11 / 59 | **18 / 59** |
| `fib`, interpreted | 478.6 ms | **422.4 ms** |
| `$a OP $b` in a loop | 840.8 ms | **686.5 ms** |
| `@a[$i]` in a loop | 195.3 ms | **178.4 ms** |

### Performance — node specialization

`evalBinary` and `evalIndex` now recognise four syntactic shapes — `$var OP
literal`, `literal OP $var`, `$var OP $var`, `@arr[$var]`/`@arr[literal]` —
record that verdict on the node, and take a path that avoids what the general
one must do for the general case: the variable is read by pointer rather than
copied out as a 376-byte `Value`, the literal is built once instead of on every
visit, and the temporal/hyper/zip probes are skipped, none of which can apply to
two plain scalars.

Measured with a **control kernel** — pure method dispatch, which the change
cannot touch, and which does not move (−0.3%):

| kernel | before | after |
|---|---:|---:|
| `$a OP $b` | 840.8 ms | 686.5 ms (−18.3%) |
| `$a OP 1` | 728.9 | 600.0 (−17.7%) |
| `fib` | 478.6 | 422.4 (−11.7%) |
| `asg` | 395.1 | 349.5 (−11.5%) |
| `@a[$i]` | 195.3 | 178.4 (−8.7%) |

Only the syntactic *shape* is cached, never the variable or its value: both are
looked up and type-checked on every evaluation, so a variable that changes type
mid-loop simply stops taking the fast path. It needs no flag and cannot change
semantics. Written up, with the guards and the three prototypes that failed, in
[docs/internals/NODE-SPECIALIZATION.md](docs/internals/NODE-SPECIALIZATION.md).

The measurement that shaped it is worth repeating: classical constant folding,
the obvious "optimise the AST" move, had **nothing to fold** — 37 sites in
51,353 nodes across 48 real programs, and zero constant conditions.
[tools/ast-opportunity.raku](tools/ast-opportunity.raku) reproduces that count.

`--exe` is unchanged. Codegen already kept variables in C++ locals, and with
`-O` its typed int lane never materialises a `Value` at all.

### Everything else

- **`$*ARGFILES` exists** ([#14](https://github.com/ash/rakupp/issues/14)). It
  was undefined, so the awk-style one-liner
  `$*ARGFILES.lines>>.words.classify(*[1])` produced an empty Bag. The handle
  is built on ACCESS — a program that never mentions it neither reads files nor
  blocks on stdin — and spans every file in `@*ARGS`, falling back to `$*IN`.
  Fixing it also fixed in-memory handles generally: the lines/get/words path
  had no branch for a captured handle, so `run(…, :out).out.lines` had been
  returning nothing. An unopenable file is now fatal, as in Rakudo.
- **Undefined `classify`/`categorize` keys keep their gist** (`Nil`, `(Any)`,
  `(Int)`) instead of collapsing to the empty string.
- **`/<$var>/` and `/<@array>/` compile the interpolated text as a REGEX**
  ([#15](https://github.com/ash/rakupp/issues/15)); the bare `/$var/` and
  `/@array/` forms stay literal, as they always were.
- **ASCII hyper markers accept Unicode operators** — `>>÷>>` died where `»÷»`
  worked, because the ÷→/ ×→* −→- ≥≤≠ aliases were applied only in the
  guillemet branch. All 8 marker spellings × 6 inner operators now agree with
  Rakudo, at equal and unequal operand lengths. A hyper's result also mirrors
  its left operand's shape (Array in → Array out), where rakupp always
  returned a List.
- **`.wrap` works on built-in routines.** `&dir.wrap(…)` had no effect: `&dir`
  minted a fresh `Callable` each time it was evaluated, so the wrapper went onto
  a throwaway — and a bare `dir(…)` call reaches the builtin through a direct
  table lookup that consults no `Callable` at all. `&builtin` now answers one
  cached routine per name, and the three dispatch sites check it for wrappers.
- **The `X::IO` exception family can be constructed.** rakupp already threw
  these types from its own IO builtins, but `X::IO::Dir.new(path => …,
  os-error => …)` said "No such method 'new'". All thirteen now exist, compose
  Rakudo's message text, and match under `when`. `.throw` also reports a plain
  `has $.message` attribute instead of falling back to the type name.
- **`symlink` and `link` exist as subs**, and `symlink` absolutizes its target
  the way Rakudo does — the OS reads a relative target relative to the *link's*
  directory, so `symlink("t/a/d", "t/b/link")` used to dangle. `.readlink` is
  an `IO::Path` method, as in Rakudo (which has no `readlink` sub).
- **`.resolve` resolves.** It only absolutized the path, leaving symlinks in
  place: on macOS `$*TMPDIR.resolve` stayed `/var/folders/…` where the kernel
  reports `/private/var/folders/…`, so any comparison against a real path
  failed. It now realpaths the longest existing prefix and appends the rest
  verbatim, matching Rakudo on every probe including missing tails and `..`.

Together these take File::Find's distribution suite from dying at test 23 to
29/29 on the same branch Rakudo takes.

- **`method FALLBACK`** is implemented: a class may catch every unresolved
  method, receiving the name first and then the original arguments. It is looked
  up last, so it never shadows a real method.
- **`unit monitor Foo;`** parses. Only the block form of OO::Monitors'
  declarator was recognised, so a module written in the file-scoped form read its
  attributes with no package open ("You cannot declare an attribute here").
- **A sigilless `constant` is a term**, not a listop. `CSI ~ $str` was parsed as
  a call `CSI(~$str)` and died "Undefined routine 'CSI'".
- **An exclusive START in a range subscript is honoured**: `@a[1^..3]` begins at
  index 2. The literal-range subscript path read `..^` only.
- **Slice assignment distributes ONE level.** It deep-flattened the right-hand
  side, so `@a[0..2] = ([1,2],[3,4],Nil)` spread `[1,2]` across two keys instead
  of putting an Array in each slot.
- **Nil stored into a container element restores that element's default** —
  `(Any)`, the declared element type, or `is default(…)` — which is the rule
  scalars already followed. It applies to element and slice assignment, list
  initialisation, and push/unshift/append/prepend. A bare List still keeps its
  Nils, since a List's elements are not containers.
- **Slicing an undefined base yields a slice, not a lone value**: `my $x;
  $x[1..3]` is three `(Any)`s.

Terminal::ANSI's suite goes 2/8 → 8/8 on these.

### Windows

- **`symlink`, `link` and `.readlink` work on Windows.** The sub forms were
  added POSIX-only in this cycle, which broke the MSVC build outright; they now
  map onto `CreateSymbolicLinkA` (directory flag detected from the target,
  unprivileged-create attempted first so Developer Mode suffices),
  `CreateHardLinkA`, and `GetFinalPathNameByHandleA`, all in `Platform.h` where
  the rest of the Win32 shims live, and all setting `errno` so the `X::IO`
  messages stay truthful.

## v1.5.2 (2026-07-31) — modules, measured by their own test suites

A behaviour release. The headline is a change of *standard*: a module now counts
as working only when **its own test suite passes** — the files `zef` runs at
install time — instead of a one-line API probe. Everything below was found by
holding real distributions to that bar, and every fix is a general interpreter
fix, not a module accommodation.

| | v1.5.1 | v1.5.2 |
|---|---:|---:|
| Roast assertions (all declared) | 196,395 | **196,568** |
| Roast files fully passing | 625 | **630** |
| Regression tests | 149 | **180** |

Roast: 196,568 / 217,060 declared (90.6%), 630 / 1,462 files, from the repeating
profile of three runs (two identical). Performance is unchanged-to-better on
every guard kernel against the v1.5.1 baseline (fib −1.7%, loopsum −1.1%,
hash −1.3%, asg +2.4% — all inside the 5% gate).

### Unicode

- **Multi-digit non-ASCII numerals parse.** `"٤٢".Int` is 42 (it threw before);
  every category-Nd script works, via each digit's Numeric_Value.
- **`.lc` applies Final_Sigma.** `"ΣΊΣΥΦΟΣ".lc` is `σίσυφος` — a word-final Σ
  lowercases to ς, while `ΣΣ.lc` is `σς` and a lone Σ stays σ.
- **`m:i` does full case folding.** `"Weiß" ~~ m:i/WEISS/` matches, both
  directions, and a match may not end mid-fold. Needed a fold-aware literal
  matcher (CaseFolding's F-entries: ß/ẞ, the ff/fi/fl ligatures, ŉ, İ, ΐ, ΰ,
  ς→σ) and a parser peephole merging adjacent literals — per-character nodes
  could never span a one-to-many fold. Roast's `ignorecase.t` 76 → 96.
- **`.collate`** wired to the existing DUCET machinery.
- A follow-up fix to the folding work: `<?before [ … ]>` reads its bracket as a
  non-capturing **group** again. The `<?[…]>` / `<![…]>` class-assertion
  shorthand applies only to the keyword-less form, and treating the
  keyword form's bracket as a character class broke YAMLish's block-scalar
  rule.

### Modules

Encode, Trap and File::Temp now pass their full suites; Digest::MD5 computes the
RFC 1321 vectors byte-for-byte. The general fixes behind that:

- `&(EXPR)` — the parenthesised Callable contextualiser — parses.
- A class's `CALL-ME` dispatches when the class is called **by name**
  (`Trap(my $*OUT)`), ahead of the `T(x)` coercion protocol.
- **`is raw` parameters write back** — they never did, in subs, methods or
  multis; and an explicit `C:U:` invocant no longer shifts the rw pairing.
- `open` understands **`:rw`, `:exclusive`, `:update`** (all three were silently
  dropped, so File::Temp's claim-a-name call failed).
- **Positional list bind** — `my ($a, $b) := (1, 2)` threw "Target is not
  assignable"; only `=` and the named form worked.
- **A module's `END` phaser runs at process end**, not at load, and before the
  mainline's own — a module loaded later cleans up earlier.
- Typed **`buf16/32/64` address ELEMENTS, not bytes**, in `.push`/`.pop`/
  `.shift` and `.write-uintN`; and `my buf8 $b .= new` no longer dies.
- **`for` over a Blob iterates its elements** unless the value sits in a scalar
  container (plain itemization, pinned against Rakudo).
- `(my $x .= new)[$i] = v` is an lvalue (`.=` is a MethodCall, not an Assign).
- `∘` composition sits at **structural** precedence, not multiplicative;
  `X[R%]` (a bracketed cross-metaop with a reversed base) fuses; `Xxx` is
  **thunky**, re-evaluating its left per replication; `parse-base` exists as a
  sub.
- Postfix `++`/`--` on an undefined numeric returns the type's **zero**.
- `$^b` and a later bare `$b` are **one variable**, not two diverging copies.
- **`$*RAKU.compiler.version` answers the oracle era** (`v2026.07`) — the Rakudo
  release Raku++ is verified byte-identical against — **reversing the v1.0.0
  decision** to report rakupp's own release there. Modules gate with
  `$*RAKU.compiler.version < v2023.12` to ask *do I have modern semantics?*, and
  `v1.5.x` compares as a pre-2000 Rakudo, so every such module refused to load.
  `.name` stays `Raku++` and `.release`/`.id` keep the real rakupp version;
  identify the engine with `.name`. (Landed in this release, recorded late.)

### Fixes

- **`my $x = … if $cond` declares `$x` even when the condition is false** — a
  compile-time declaration, as Raku specifies. Fixed for the mainline, blocks
  and subs, and then ([#13](https://github.com/ash/rakupp/issues/13)) for
  **method bodies**, which had no declaration hoisting at all. The first fix
  looked complete because every probe used `$a`, and `$a`/`$b` are exempt from
  rakupp's undeclared-variable check — a vacuous pass that hid a whole scope.
- **`run()`'s `:env` and `:cwd` reach the child.** Both were parsed and
  silently dropped, so any harness isolating a child's environment inherited
  the parent's instead.
- **NativeCall symbols resolve once per sub**, not per call: a crossing cost a
  flat ~67 µs (dyld rescanned its search path for every failed `dlopen`
  candidate) and now costs 0.2–0.8 µs. A `Pointer is rw` out-parameter arrives
  as a real slot address rather than NULL. `is native(EXPR)` accepts a constant
  or `%?RESOURCES<libraries/…>` expression, and `libraries/*` resources resolve
  to the platform library name.
- **`--exe` compiles NativeCall subs** instead of silently returning `Any`;
  shapes it cannot carry fall back to bundling.
- `when`/`default`/`succeed` match cooperatively instead of throwing a C++
  exception per row, and Array mutators no longer copy the whole array before
  dispatch — together these took a real SQLite import from 197 s to 17 s.

### Packaging

- **Nix flake** ([#5](https://github.com/ash/rakupp/issues/5)): NixOS cannot run
  the generic prebuilt binary, so `nix run github:ash/rakupp` builds from
  source. Path-gated CI builds and smoke-tests it.
- **GNU Guix** ([#6](https://github.com/ash/rakupp/pull/6), by @4zv4l): the
  repository is a Guix channel; CI builds the package and smoke-tests the store
  output.

## v1.5.1 (2026-07-29) — faster, and smaller files

**No behaviour change at all**: Roast is 196,395 / 217,060 and 625 / 1,462 files
both before and after, byte for byte. This release is entirely about how the
interpreter spends its time and how the source is arranged.

| kernel | v1.5.0 | v1.5.1 | |
|---|---:|---:|---:|
| fib | 903.3 ms | 744.1 ms | **−17.6%** |
| strcat | 13.5 ms | 12.3 ms | −8.9% |
| hash | 41.0 ms | 38.0 ms | −7.3% |
| streq | 547.2 ms | 509.6 ms | −6.9% |
| loopsum | 206.0 ms | 197.3 ms | −4.2% |

### Performance

Five candidates were ranked from a profile. Three landed, one was measured and
abandoned, one was measured and never attempted. The full record — including
what did *not* work and why — is in
[docs/dev/experiments/PERF-CAMPAIGN.md](docs/dev/experiments/PERF-CAMPAIGN.md).

- **A call's argument vector is MOVED, not copied** (−9% on call-heavy code, from
  four lines). `evalCall` built a `ValueList`, then passed it to `callCallable` —
  which takes it **by value** — as an lvalue, copying the vector and every
  376-byte `Value` in it on every sub call, for a local about to die. Found by
  asking the profile *who allocates*: `evalCall` was 514 of ~580
  malloc-attributed samples.
- **The method invocant passes by const reference** (−3.4% dispatch-heavy). It
  was by value — 376 bytes and up to eleven atomic refcount bumps per method
  call — to serve four arms out of 352 that rewrite it. Those four take a copy on
  demand. The compiler now enforces the rest: an arm that quietly mutates the
  invocant no longer compiles.
- **`Env`'s rarely-used containers moved behind a lazy pointer** (−2.4%
  call-heavy), 256 → 72 bytes. Eight associative containers for `is rw`,
  `temp`/`let`, `is default` and `is dynamic` were constructed and destroyed for
  every call *and* every block; they are allocated on first write now. Reads stay
  allocation-free, which matters because the `temp`/`let` checks run on every
  scope exit.

Measured and **not** adopted, recorded so they are not retried:

- **Shrinking `Value`** — packing its flags removed 23 bytes of padding (376 →
  360) and ran **2.5% slower** over six alternating rounds, with hot field
  offsets unchanged. Struct size is not the lever here, and the relationship is
  not even monotonic. Reverted; the planned ~600-site change was dropped.
- **Slot-indexed locals** — billed as the biggest architectural win, it measures
  a **~4% ceiling** (hash lookup 2.8% of `fib`, string ops 1.2%) for the riskiest
  change on the list. Not attempted.
- **A hash map or switch for the dispatch chain** — see
  [docs/dev/experiments/METHOD-DISPATCH-EXPERIMENT.md](docs/dev/experiments/METHOD-DISPATCH-EXPERIMENT.md).
  56% of the arms dispatch on the invocant TYPE and cannot be name-indexed, and a
  map lookup costs what ~19 `MName` comparisons cost.

### Maintainability

- **`methodCallInner` was 9,138 lines** — 61% of `Builtins.cpp` in one function.
  Split into four files (`Builtins.cpp` 14,979 → 7,876 lines; the function 9,138
  → 2,095). They are ordered SEGMENTS of one chain, not categories: the chain is
  order-sensitive, so a new arm belongs where its priority is. `std::optional`
  return lets every arm keep its original `return`, so nothing inside was
  rewritten.
- That also fixed CI: the `linux-gcc` job had crept from 12 to 25 minutes because
  GCC's optimiser is superlinear in function size (`-O3` on that one file was 88s
  against clang's 27s). **It now runs in 1m32s.**
- `t/run.raku` reports *which* regression check failed — it discarded the stderr
  line naming it, so a CI failure could only say `exit=0 last-line='FAIL'`.

### Process

- **A release can no longer ship a performance regression**:
  `rakupp tools/perf-guard.raku --check` gates against a recorded baseline and
  exits non-zero. [docs/dev/RELEASING.md](docs/dev/RELEASING.md) documents all
  five gates.

## v1.5.0 (2026-07-29) — narrowing the measured gap with Rakudo

**The goal of this release was to make the differences between Raku++ and Rakudo
smaller, and to stop the ones we fix from coming back.** Everything below is one
of those two things: closing divergences that
<https://raku.online/spec/rules/divergences/> actually measures, or turning a
property we care about into a gate that fails a release rather than a number
someone has to remember to look at.

| | v1.2.6 | v1.5.0 |
|---|---:|---:|
| Documentation examples byte-identical to Rakudo | 936 | **944** |
| …of which Raku++ is the one that is wrong | 144 | **122** |
| Operator-matrix divergences | 72 | **30** |
| Roast assertions (all declared) | 196,381 | **196,395** |
| Roast files fully passing | 622 | **625** |
| Regression tests | 137 | **149** |

Both halves of that page are measured here: the per-type documentation sweep
(1,451 examples) and the operator behaviour matrix (833 expressions over 121
operators). Together the page's "Raku++ differs" count went **202 → 152**.

Two caveats on reading those numbers. The conformance count has a **±5 flap
band** — the `Set`/`Bag`/`Mix`/`Map` examples move in both directions between
runs of identical code, because Rakudo randomizes hash iteration order per
process; two sweeps of the same tree gave 934 and 939. And six of the remaining
operator rows are `prefix:<~^>`, where *Rakudo* answers "not yet implemented" and
Raku++ works — counted as a divergence, but not a defect.

Roast itself flaps by a file: 624↔625 fully passing and 10↔11 timeouts across
runs, which moves the assertion total by about 20 either way. The figures above
are one coherent run, and the conservative of the three taken.

### Fixed

- Reported: [#8](https://github.com/ash/rakupp/issues/8) — two `--exe` codegen
  bugs, both about writing through a subscript. A closure mutating a captured
  container **by key** (`%bag{$k}++`, `@r[$i] += 1`) was not recorded as
  mutating it, so the variable was captured `const` and the generated C++ would
  not compile; and `.push` on a not-yet-existing element (`@ready[2].push(10)`)
  mutated a temporary and vanished, which is why the reporter's program looped
  forever once it did compile. The interpreter was always right; only the
  compiler diverged.

- **`@(…)` did not keep its Array.** `@(%h<k>).raku` gave `(1, 2)` where Rakudo
  gives `[1, 2]` — the contextualiser converted to a List instead of only
  stripping the itemisation. Found while writing the containers FAQ page against
  a live binary.

- Reported: [#9](https://github.com/ash/rakupp/issues/9) — an object hash
  (`my %h{Int}`) lost its key type. `%h{+$k} = 66` came back as the Str `"33"`,
  and `.raku` rendered a plain `{"33" => 66}`. The store is a
  `map<std::string, Value>`, so a key is a lookup *string* and its real value has
  to come from somewhere: a Set/Bag/Mix parks it in the count's `pairKey`, an
  object hash can rebuild it from the declared key type. Both now go through one
  `hashEntryKey`, which is what `.keys`, `.pairs`, `.kv`, `.antipairs`,
  `.invert`, `.sort` and iteration all ask. `.raku` renders the declaration that
  rebuilds it (`(my Any %{Int} = 33 => 66)`) rather than a Str-keyed literal that
  would not round-trip, and `.^name` reports `Hash[Any,Int]` — on the name only,
  since `typeName()` is what dispatch and error messages key on.

  Still open, and the reason this is narrowed rather than general: a key type
  that cannot be rebuilt from a string — a class, or bare `Any`/`Mu`, where
  Rakudo distinguishes `%h{3}` from `%h<3>` and Raku++ cannot — stays a `Str`.
  That is the pre-existing "Hash keys are plain strings" limit, unchanged.

- **A declared type did not survive compilation.** Fixing #9 in the interpreter
  left `--exe` still printing `{"33" => 66}`, and the cause was broader than
  object hashes: codegen emitted a bare `Value::array()`/`makeHash()`/`any()`
  for *every* declaration and dropped the declared type on the floor. So
  `my Int $x` was `(Any)` compiled and `(Int)` interpreted, `my Int @a` was an
  Array of `Mu`, and `my %h{Int}` lost its key type entirely. Both sides now go
  through the interpreter's own `typedDefault` via a small `rtTypedDefault`
  shim, rather than the compiler keeping its own idea of what a declaration
  means. Top-level declarations become C++ globals, so the type had to be
  carried there too.

- **An enum value numified to itself, tag and all.** `b.Numeric` and `+b`
  rendered as `b`, while `.Int` and `.value` — which build a fresh Int —
  correctly gave `1`: one value answering three ways. Both coercion paths now
  return a plain Int.

### Conformance: closing divergences

Measured against the two data sets behind the divergences page. The operator
matrix collapsed hardest, because two of these are parse- and lex-level and so
cascade across every operator they touch.

- **`Nil` is a TERM, not a routine** (~15 operator rows). The parser fell through
  to its general identifier path, so anything that could begin a listop argument
  turned into a call: `Nil ~ 1` parsed as **`Nil(~1)`** and died with "No such
  method 'Nil' for invocant of type 'Str'"; `Nil ff 1` as `Nil(ff 1)`. `max`,
  `min`, `X`, `Z`, `but`, `^`, `minmax`, `notandthen` and `?^` were all the same
  bug. This is also what took Roast from 624 to 625 files.
- **A non-numeric string operand is an error, not a silent 0** (9 rows).
  `"a" +& "b"` answered `0`: the bitwise, repeat and approx-equal operators each
  reached for `toInt()`/`toNum()` directly, while ordinary arithmetic had long
  gone through `numifyStrOrThrow`. One `strictNum` helper now serves `+&` `+|`
  `+^` `+<` `+>` `x` `xx` `≅` and the two prefix forms.
- **The flip-flop family** (8 rows). The state machine was implemented but three
  of its eight spellings were unreachable: `^ff`, `ff^` and `^ff^` lexed as three
  tokens, so the carets read as prefix/postfix on the operands. It also answered
  `Any` while off where Rakudo answers `Nil`. All six variants now match Rakudo
  element for element.
- **`Nil` is a set element** (6 rows). `Nil (|) 1` dropped it — while a `Nil`
  *inside a list* had always been kept, which was the tell that the scalar case
  was simply excluded.
- **An anonymous mixin role is named `<anon|N>`** (5 rows): `1 but 2` is an
  `Int+{<anon|1>}`, not the bare `Int+{}`.
- **`0..^N` renders as `^N`** (3 rows), for `.gist` and `.raku` alike — Int-zero
  only, so `0.0..^5`, `0..^5e0` and `0..^0.5` keep the long form.
- **`+<` after a term is the shift** (2 rows). `1 +< "2"` lexed as `+` followed by
  a word list and swallowed the rest of the line. The term test is deliberately
  narrow: a bare identifier may be a LISTOP, and treating `is-deeply ~<2>, '2'`
  as a shift cost all 119 assertions of `S02-literals/allomorphic.t` before the
  rule was tightened to literals, variables, closers and the five identifiers
  that can never be a listop.
- **Methods the documentation exercises that did not exist**: `Proc::Async.command`,
  `Format.directives`, `Buf.splice`, `Blob.unpack`, and `.files` on a
  `CompUnit::Repository`. `Buf.splice` is the interesting one — it mutates in
  place, and a Buf's bytes are a plain `std::string` rather than a shared_ptr the
  way an Array's elements are, so unlike `Array.splice` it cannot mutate through
  a copy. `methodCall` takes its invocant BY VALUE, so it lives beside `bufBitOp`
  as a `Value&`-taking member.
- **An `IO::Handle` gists as an `IO::Handle`** and Strs as its path — it had no
  rendering of its own and dumped `buffer`/`mode`/`path` as a hash, the same root
  cause as the Proc dump in [#10](https://github.com/ash/rakupp/issues/10).
- **`IO::Path::Parts` subscripts positionally**, in declaration order:
  `$parts[0]` is the volume Pair. (Only the subscript — Rakudo treats it as ONE
  item for `.list`, `.pairs` and `for`, which spreading it broke in the other
  direction, so that was tried and reverted.)
- **`Complex.round($scale)`** honours the scale, per component. Each component
  goes through the scalar path rather than repeating the arithmetic — doing it
  again in doubles gave `-3.9000000000000004` for the imaginary part.

### Semantic-duplication audit — batches B7–B11

One rule implemented in more than one place, so that fixing a bug in one copy
leaves the other wrong. Each batch is gated on the full Roast run.

- **B7 — value identity has one home (`whichOf`).** `.WHICH` identified an
  object by its address; the quanthash key identified it by its rendering, so
  two distinct objects collapsed into one Set element (`set($x, $y).elems` was
  1). `===` and `eqv` also lacked Range and parametric-type arms. Fixing the
  Range case exposed a bug it had been masking: `Rat.Range` carried saturated
  int endpoints where they should be ±Inf.
- **B8 — a type object stringifies empty.** `Int.Str` was already `""` while
  `~Int`, `"{Int}"`, `put Int` and `.join` gave `(Int)`. This was tried once
  before and backed out because quanthash keys were built from `toStr`; B7's
  `baggyKeyStr` was the missing prerequisite. +37 assertions, 622 → 624 files.
- **B9 — an exception's payload is its type object, everywhere.** Seventeen
  throw sites stored a Str naming the class, a Str that was never a class name
  (`"op"`, `"Not callable"`), or nil — so `$!.message` died with "No such
  method". Two of them were in the code generator, meaning a multi with no
  matching candidate could not be caught by class under `--exe` at all.
- **B10 — one class and one sentence for an immutable-container write.**
  `$s<1> = 5` threw `X::Assignment::RO`, `$s<1>:delete` threw `X::Immutable`;
  same rule, so a `CATCH` caught one and missed the other. Only the
  subscript-write sites moved — Rakudo keeps `X::Immutable` for a different rule
  that Roast asserts.
- **B11 — divide-by-zero is one Failure with one wording.** Three verbatim copies
  of the zero check each returned a bare Failure *type object*, which carries no
  exception and never detonates, and the one message built hard-coded `infix:<%%>`
  whatever operator was used. That exposed a second half: `try` cleared `$!`
  unless something was *thrown*, so a block that returns a Failure left `$!`
  unset.

### Performance, and a gate to keep it

- **Fixed an ~11.6% interpreter regression on `fib`** that had accreted across
  the v1.2.x cycle. `hoistExprDecls` pre-declares a `my` buried in a ternary or
  nqp branch so a sibling branch can see it — and it ran on every sub call and
  block entry, walking the body's whole expression tree through a
  `std::function` that allocates. For `fib`, whose body has nothing to hoist,
  that walk ran ~2.7M times for nothing. Whether a body holds such a declaration
  is a **static property of its AST**, so it is decided once and remembered on
  the owning `Block` / `Callable`; the dynamic half — whether the variable needs
  defining on this particular call — is unchanged.

  Found by sampling `fib(32)` under the 1.0.0 binary and under HEAD and diffing
  the top-of-stack tallies: the lambda was ~6% of runtime and absent from 1.0.0
  entirely. `fib` 911.0 → 830.9 ms; `asg` and `hash` are now past their 1.0.0
  bests.

- **A release can no longer ship a performance regression.**
  `rakupp tools/perf-guard.raku --check` compares against
  [`tools/perf-baseline.raku`](tools/perf-baseline.raku) — the last release's
  times, 5% tolerance — and **exits non-zero**. The baseline tracks two numbers
  per kernel: `baseline` (enforced) and `best` (the fastest ever measured, and
  when — reported but not enforced), so a regression that was once accepted
  cannot quietly become the new normal.

  This is the second time a gradual interpreter regression has slipped through:
  an 8–22% one crossed v0.7.1→v0.9.0, which is why `loopsum` and `hash` are in
  the guard at all. Printing four numbers and trusting someone to remember last
  release's has now failed twice, so it is a gate.

- **[docs/dev/RELEASING.md](docs/dev/RELEASING.md)** is new — there was no release
  checklist. It documents all five gates (Roast, local suite, performance,
  compiler agreement, conformance) and, for each, what it has actually caught.

### Documentation

- **[docs/guide/faq/](docs/guide/faq/)** — a new section, six pages: running external
  commands, containers and itemisation, compiling, performance, debugging, and
  where Raku++ and Rakudo differ. Every runnable snippet is executed on both
  engines and must produce identical output; where the two genuinely diverge the
  page says so rather than documenting whichever is convenient.
- Spec and tour links repointed from `ash/raku-spec` / `ash/raku-tour` to
  `ash/raku.online` (`sites/spec`, `sites/tour`), across six files.
- **Benchmarks re-measured**, and a table corrected. `docs/guide/faq/performance.md`
  labelled its first column "interpreter" while holding `run-optbench`'s `--exe`
  numbers, understating compilation by an order of magnitude — 5M integer
  accumulation is 1342 ms interpreted, not the 295 ms printed there. Re-measured
  with a third column, since `--exe` alone turns out to be most of the win. The
  "4×–50× with `-O`" claim in two FAQ pages came from the same mislabelled
  column; the real span against the interpreter is 4× (string building) to 503×
  (the prime sieve), and both pages now name the ends rather than a middle.
- All measured figures refreshed against a single clean run — including several
  that had gone stale independently of the headline: ROAST.md claimed "≈39% of
  files" and "about a sixth produces no TAP" (it is a tenth), the no-TAP
  denominator note said 101 files (88), and the per-synopsis table was a release
  old. README now states that its figures are measured on `main`, since `main`
  is ahead of the v1.2.6 tag.

### Known, not fixed

- `:(…) ~~ :($ where …)` is True here, False in Rakudo — Rakudo fails a
  signature smartmatch whenever the target carries *any* `where` constraint,
  since it cannot statically verify one.
- A `rotor` **sub** is registered that Rakudo has no routine for
  (`rotor(1..6, 2)` gives `((2))` here, "Undeclared routine" there).
- A missing semicolon between statements is accepted where Rakudo rejects it;
  `die` prints no backtrace. Both documented in the FAQ as gaps.
- **152 divergences remain**, and the last stretch is not like the first. Six of
  the operator rows are `prefix:<~^>`, where *Rakudo* answers "not yet
  implemented" and Raku++ works — counted against us but not a defect. Around
  ten more are hash-iteration order (we iterate sorted, Rakudo randomizes) and a
  few are environment-bound (`Kernel|2` wants `x86_64` on an arm64 machine). The
  rest is a long tail of singletons — `substr-rw` write-through proxies, Pair
  container binding, leap-second tables, the Metamodel HOW types — where the
  clustering that made this release cheap has run out. An honest floor is
  somewhere near 125–135 without changing what the number measures.
- `IO::Path::Parts` is Positional only for subscripts; `.list`/`.pairs`/`for`
  still spread it where Rakudo treats it as one item. Closing that needs the
  zen-slice `$x[]` semantics, which touches every subscript in the language.
- The interpreter is still ~2% behind its 1.0.0 best on `fib` and `loopsum` —
  within measurement spread, but the `best` column keeps reporting it.

## v1.2.6 (2026-07-28) — Proc rendering

A point release for one user-visible bug found just after v1.2.5 shipped.

### Fixed

- `say shell("ls")` printed the listing **twice**: once as the child ran, once
  inside the Proc's own gist. A Proc had no gist of its own, so it fell through
  to the generic hash rendering, which prints every slot — including the
  captured `out-str`. It renders in Rakudo's shape now, byte-identical apart
  from the pid:
  `Proc.new(in => IO::Pipe, out => IO::Pipe, err => IO::Pipe, os-error => Str,
  exitcode => 0, signal => 0, pid => 58353, command => ("ls",))`.
  The argv is rendered .raku-style so a Str argument keeps its quotes and a
  one-element command keeps its trailing comma; the child's pid is plumbed out
  of the spawn on both platforms rather than reported as Any.

  The same missing gist explains the tab-separated key/value dump in
  [#10](https://github.com/ash/rakupp/issues/10) — that was never a Windows
  artefact, it is what a Proc looked like everywhere.

Roast and documentation-conformance numbers are unchanged from v1.2.5
(196,381 assertions, 622 files, 936 examples).

## v1.2.5 (2026-07-28) — correctness, and one rule in one place

A maintenance release. Two threads: closing documentation divergences, and a
systematic audit for **semantic duplication** — one rule implemented in more than
one place, so that fixing a bug in one copy leaves the other wrong.

| | v1.2.0 | v1.2.5 |
|---|---|---|
| Documentation examples byte-identical to Rakudo | 835 | **936** |
| Roast assertions (all declared) | 196,052 | **196,381** |
| Roast files fully passing | 611 | **622** |
| Regression tests | 113 | **137** |

### Fixed

Reported: [#7](https://github.com/ash/rakupp/issues/7) `.min`/`.max`/`.minmax`
took `&by` as the first ARGUMENT rather than the first CODE argument, so an
adverb in front of the block silently dropped it and the extremum was taken over
the raw elements. [#10](https://github.com/ash/rakupp/issues/10) `run`/`shell`
failed on Windows with exitcode -1 and no output — four causes: `shell` ran a
hardcoded `/bin/sh`, the spawn passed a possibly-invalid stdin handle to
`STARTF_USESTDHANDLES`, a failed spawn reported nothing at all, and every
argument was quoted unconditionally (which breaks a `cmd.exe` switch).

Silent wrong answers, none of which Roast caught:

- `my $x = 5; say "$x-1"` printed **4**. A `-` continues an identifier only
  before a letter or `_`; the string-interpolation scanners accepted a digit, so
  the arithmetic result was interpolated.
- `fail` under `--exe` **aborted the binary** with an uncaught ReturnEx; fixing
  it exposed a second copy of the same rule, codegen inlining "undefined"
  without the Failure case.
- `.Array` did not decontainerize: `$v.Array.elems` said 3 while
  `my @a = $v.Array` bound 1. Found by running showcase/perl against real perl,
  where three of six examples produced wrong output.
- `.raku` silently **dropped every inherited attribute**, so an object could not
  round-trip through EVAL. `.gist` and `.raku` of a hookless object are one
  renderer now.
- `('fig'..'banana')` built 1,000,000 elements and peaked at 952 MB; the
  emptiness guard compared lengths before values.
- `for 'a'..'c' { .say }` printed `0` under `--exe`.
- `∞/∞` gave Inf — the lexer read the `/` as opening a regex.

Also: exact Mix weights, `Nil` subscripts, `Junction.defined`, allomorph
identity in sets and `===`/`eqv`, `.perl` aliased once rather than in sixteen
places, Uni and ISO-8601 rendering moved into the value model, `@.`/`%.`
attributes coercing to their sigil's container, and the MSVC build (an
`__attribute__` MSVC does not know had left windows-x64 red).

### Not done, deliberately

Rakudo iterates an equal-length multi-char Str range as a per-position cross
product; we still use a succ chain. Quanthash identity keying needs Hash keys to
carry their key object. `42[2]` should throw X::OutOfRange — correct in
isolation, but it turns a soft failure into a file-killing throw until the
`@p[0]` list-assignment bug behind it is fixed. Each reason is recorded at the
site, not only here.

## v1.2.0 (2026-07-26) — documentation conformance

A release measuring one thing: how much of the *official Raku
documentation* Raku++ now reproduces exactly. Every runnable example in the docs
is executed on both engines and classified three ways (see
[CONFORMANCE.md](https://github.com/ash/raku.online/blob/main/sites/spec/CONFORMANCE.md)); the number to
watch is `ok` — documentation, Rakudo and Raku++ all agreeing.

| Verdict | before | now |
|---|---:|---:|
| `ok` — all three agree | 596 | **835** |
| `rakupp-differs` — Raku++ is wrong | 471 | **237** |
| `all-differ` — needs a human | 176 | 164 |
| `doc-drift` — the docs are stale | 104 | 112 |
| `rakudo-differs` — Rakudo is the odd one out | 16 | 15 |

Roast moved with it: **194,904 → 196,052** assertions and **598 → 611** files
fully passing, with no removals from the fully-passing list.

Sixteen batches, each gated on both suites and each with a regression test that
passes under Rakudo too. The larger pieces:

- **The custom Iterator protocol.** A class with its own `.iterator` decides what
  iterating it means, for both `for` forms and for an Iterable held in a `$`. The
  blocker was not the protocol but binding: `my @a := SubclassOfArray.new` coerced
  the object to a plain Array and threw the class away, so a user `.iterator` was
  unreachable however it was written.
- **Itemization.** A `$` container itemizes the list or hash it holds, which
  fixed `list()`, `.item`, `my @b = $t` and the `$(…)` render marker together.
- **Definiteness types as first-class.** `Foo:D` in term position was parsed and
  discarded, so the constraint existed only inside a signature; it now rides on
  the type value, and `.^name`, smartmatch and `.^base_type` all follow.
- **Runtime class creation** via `Metamodel::ClassHOW.new_type`, and the HOW
  spellings of the MOP operations.
- `.match`'s occurrence adverbs (`:continue`, `:pos`, `:x`, `:nth`, `:1st`…),
  `.split`'s separator adverbs, `.lines`/`.words` arguments, a fuller `substr`,
  `:ignoremark` on the Str search routines.
- `DateTime.new(date => …)`, which had been dropping the date entirely, plus an
  exact clock: fractional seconds parse as a Rat, so `.day-fraction` and the
  Julian dates stay rational.
- `.round($scale)` in exact arithmetic; non-numeric string coercions answering a
  Failure; `exit` ending the process from any thread.

Two behaviours were deliberately NOT copied. Rakudo's `first(…, :end, :kv)`
reports an index counted from the end where `:end, :k` reports the true one; the
consistent answer is implemented instead, leaving one documented example
permanently in `rakupp-differs`. And treating every subscript target as a
list-assignment — which is what Rakudo parses — costs 210 emitted Roast tests,
so the computed-key case (`%h{%other.keys} = …`) is parked rather than bought at
that price.

## v1.1.0 (2026-07-24) — 100% Unicode (S15)

Every S15 (Unicode / strings / NFG) assertion now passes: **91,752 / 91,752**,
80 of 82 files fully passing. The one non-passing file, `S15-nfg/concat-stable.t`,
is a *performance* timeout (its O(n²) concat loop meets an O(n) `Array.shift`),
not a correctness gap — every assertion in it passes when it finishes. The full
UCD case tables also lifted string-heavy tests suite-wide: the run went from
576 → 598 files fully passing (194,506 → 194,901 assertions), no regressions.

- **Full Unicode case mapping.** `uc`/`lc`/`tc`/`fc` are driven by complete UCD
  tables — simple mappings from `UnicodeData.txt`, full (1:N) mappings from
  `SpecialCasing.txt`, and case folding from `CaseFolding.txt` (generated by
  `tools/gen_unicode_case.raku`). The old hand-rolled ranges missed whole blocks
  (Latin Extended Additional, …), so `Ḍ.lc` used to be a no-op. Case change is
  now **NFG-aware**: it maps each grapheme's base, keeps the combining marks, and
  places them correctly when the base expands (`"ﬀ̣".uc` → `F̣F`).
- **Grapheme-level (NFG) regex.** `.`, character classes, and `<:Prop>` assertions
  match a whole grapheme cluster; an enumerated class matches a multi-codepoint
  grapheme only when its member is that exact grapheme (`\c[A, COMBINING…]`),
  never a bare base codepoint (`S15-nfg/regex.t`).
- **Complete `uniprop`.** Age, Block (proper names), Line_Break, Word_Break,
  Sentence_Break, Grapheme_Cluster_Break, East_Asian_Width, Hangul_Syllable_Type,
  Joining_Type/Group, Decomposition_Type, Numeric_Type, the case-mapping and
  Bidi_Mirroring_Glyph properties, plus Emoji/Full_Composition_Exclusion/
  Changes_When_NFKC_Casefolded/Bidi_Mirrored as strict binary properties
  (unknown names are now `False`, not a lenient match). `uniprop` on a type
  object throws `X::Multi::NoMatch`.
- **`Buf.decode('utf-16' | 'utf-32')`** decodes fixed-width code units (with
  surrogate pairing and BOM detection) into NFG strings; `Uni.new(…)` gains
  `.raku`/`.gist` and the `Uni(97)` coercion form.

### Ecosystem & concurrency — a live Cro server, byte-for-byte with Rakudo

- **A real Cro HTTP server runs**, verified byte-identical to Rakudo end-to-end
  (route + `Cro::HTTP::Server` over real sockets/threads). This drove a cluster
  of general fixes: on-demand `supply {…}`/`whenever`/`tap` wiring with
  done-propagation and CLOSE/QUIT/LAST phasers; `IO::Socket::Async`
  (listen/read/write on GIL-parked worker threads); method-frame `state` vars;
  EVAL'd anonymous `regex {…}` as first-class closures that run their code
  blocks and assertions; proto-token action dispatch; and coercion-param
  multi-dispatch.
- **`signal(SIGINT, …)`** — OS signals delivered as a Supply, so the standard
  `react { whenever signal(SIGINT) { $server.stop; done } }` shutdown works
  (self-pipe dispatcher; the Signal enum members resolve to their OS numbers).
- **`.lines` strips `\r\n`** (not just `\n`), matching Rakudo — an HTTP
  response's `.lines[0]` no longer keeps a stray `\r` (`S15`-adjacent
  `S32-str/lines.t` 9 → 13).
- **Typed blobs** (`blob16/32/64` little-endian words) — element-wise `.elems`/
  index/list/`for`/`Z`/coerce, `.Int` = element count, plus radix digit-lists
  `:256[…]`, colon-arg list precedence, `( expr; )` grouping, and native
  `uint32 @a.push` wraparound. Together these make **pure-Raku `Digest::SHA1`
  byte-identical** (`sha1("abc")` = `a9993e36…`).
- **Ecosystem module fixes** (batch 11) unblocked HTTP::UserAgent, Text::Utils,
  and others: if/elsif binding traits (`-> $x is copy`), alternative regex
  delimiters, indented POD, enum trait args, `!=:=`, Blob-is-not-Stringy
  dispatch, `Capture.new`/`Signature.ACCEPTS`, `Mu.return`, and a streaming
  `Encoding::Registry` decoder. `$*RAKU.compiler.version` now reports Raku++'s
  own version, not a faked Rakudo date. Tier-2 module battery: **37 / 50**
  genuinely byte-identical (both engines run and agree).

### Packaging

- **OpenBSD is now a packaged release target** (`rakupp-openbsd-x86_64.tar.gz`).
  OpenBSD (amd64, base clang/libc++) had been a build+smoke portability gate
  since PR #3, but its binary was never packaged or attached to a Release; the
  release job now installs, dist-layout `--exe`-smokes, tars, and attaches it
  alongside the macOS/Linux/Windows assets.

### Earlier post-1.0 fixes, all Roast-gated:

- **Hyper compound assignment**: `@a <<+=>> n` applies the base op elementwise
  and mutates in place (all spellings; advent2009-day06.t now fully passes).
- **Undeclared-attribute errors print the `===SORRY!===` compile banner** with
  `file:line` (the exception carries filename/line, X::Comp style).
- **Default `.new` binds declared public attributes only** — stray named args
  no longer enter the attribute store; plain `.name` is no longer universal
  (a user instance without one dies X::Method::NotFound); attributive
  `:$!attr` / `:$.attr` parameters (BUILD/TWEAK style) are actually
  implemented, including `:$!x = default` initialization.
- **New lint rule `new-arg-matches-no-attribute`**: warns when a literal named
  argument to a locally-declared class's `.new` matches no public attribute
  (the default constructor silently ignores it). Zero false positives across
  the 1,900-file corpus; three true catches.
- **Corpus round-2 batch**: glued `-ne'…'`/`-npe'…'` one-liner flags; typed
  scalars reject undefined values (`my Int $i = $undef` dies); big-part
  Rat→Num converts with a single correct rounding; bare `$` is a true
  anonymous state variable (`say ++$` numbers lines); substitution
  replacements decode qq escapes (`s/$/\n/`).
- **Match numification follows the Str ladder** (`+$0` of digits is Int).
- Corpus differential: **1,532 / 1,812 exact matches (84.5%)** on the
  reorganized corpus (rounds 2–4 in
  [docs/dev/findings/CORPUS-DIFF.md](docs/dev/findings/CORPUS-DIFF.md)).

## v1.0.0 — 2026-07-22

Everything since v0.9.1 (2026-07-20), 65 commits — the "90% campaign": many
small, fully-gated legs, each run against the complete Roast suite with zero
fully-passing-file regressions.

### Headline

- **1.0**: the campaign target set for this release — 90% of all declared
  Roast tests — is reached. **194,496 / 216,066 declared assertions pass
  (90.0%)**, up from 189,102 / 214,384 (88.2%); **583 / 1,462 files fully
  pass** (was 558). 97.4% of tests that actually run pass. (The declared
  denominator grew because files that previously died before announcing a
  plan now declare their real, often larger, plans.)
- **A regression-test suite is born**: [t/regression/](t/regression) — one
  self-contained case per bug we introduced and had to fix (21 cases,
  auto-discovered by `t/run.raku`; the suite is at 79 checks).

### Language & runtime

- **Quanthashes grew up** (`Set`/`Bag`/`Mix` + `*Hash` variants): `.of`/`.keyof`
  report the real value/key types; `Bag[Int]`-style parameterization is
  enforced end-to-end (declaration, `Set[Str].new`, assignment — bad keys
  throw `X::TypeCheck::Binding`); the immutable three reject re-initialization,
  autovivification, and element assignment; `^Inf .Bag` throws a typed
  `X::Cannot::Lazy` with `.what`; non-numeric/NaN/Inf/complex weights throw
  typed conversion errors; `%h<a>--` on a `BagHash` removes the key at zero;
  `.new` follows the single-arg rule (a quanthash argument is ONE element, a
  plain Hash iterates, bare named args are swallowed); the coercers flatten one
  level through bare Lists only, keeping `».Bag` nodal.
- **`index`/`rindex`**: splatted multi-needle form (`.index("a", "o", :i)`) and
  needle lists, with `:i`/`:ignorecase`; out-of-range start positions return a
  typed `X::OutOfRange` **Failure** (so `fails-like` semantics hold).
- **`sort`**: `:k` (sorted indices) and `:by(&cmp)` on both sub and method
  forms; `NaN` orders last and is `eqv`-identical to itself; a declared
  0-arity comparator is rejected.
- **`RUN-MAIN` / CLI**: full command-line parsing — repeated options become
  arrays, values are `val()`-allomorphed, `--` ends options, `--/name`
  negates, `%*SUB-MAIN-OPTS<named-anywhere>` honoured.
- **Declarator pod**: leading `#|` and trailing `#=` comments attach to subs,
  classes, and parameters and surface through `.WHY`.
- **Arity enforcement** for direct calls to subs with declared signatures
  (too many positionals now die, with the lenient carve-outs Rakudo has).
- **Typed exceptions throughout**: unknown operator categories
  (`X::Syntax::Extension::Category`), blank `:sym<>` (`…::Null`), pod
  `=begin` without an identifier, `else if`/`elsif` misspellings, malformed
  loop specs, stubbed packages (`X::Package::Stubbed`), method-not-found with
  `.method`/`.typename`, undeclared return types, and more.
- **Str ranges**: `'a'..'z'` is a real Range (codepoint-stepped endpoints,
  containment, `min`/`max`, `.pick`/`.roll`, reversibility).
- **Sequence operator**: generator + literal endpoint is properly lazy
  (`1, 2, * + 1 … 10` stops on exact match; runaway-capped).
- **Unicode quoting**: the curly-quote family (`‘’ “” „ ‚`), CJK corner
  brackets with nesting, and arbitrary paired-punctuation delimiters.
- **Numification follows the Str ladder everywhere it should**: `+"9"`,
  `+$0` (Match captures), and prefix `-` yield `Int`/`Rat`/`Num` like Rakudo,
  and allomorphs answer `.isa` on both faces.
- Also: `once` blocks, `.VAR.dynamic`, `pairup`, `samemark`, `roots`,
  `trusts` declarations, for-loop sub-signatures on the multi-element path,
  regex bodies containing `{ … }` code blocks, and `--highlight` support for
  multi-line embedded comments (`` #`( … ) ``).

### Tooling, benchmarks, ecosystem

- **Benchmarks re-measured at release** (per the runbook): kernels and the
  `-O` suite within noise of v0.9.1's snapshot (sieve 50.5× with lanes;
  `mandel` 0.13 s vs Rakudo 0.47 s). The YAMLish grammar workload drifted
  ~8% slower over the campaign week — bisected to gradual accretion across
  parse/regex hot paths, no single culprit; recorded in
  [docs/status/BENCHMARKS.md](docs/status/BENCHMARKS.md) as a post-1.0 item.
- **Suite infra**: the test-server `stop-server` used `pkill -f` with the
  full script path — a regex in which `raku++` is invalid, so no server ever
  died; 54 zombies had accumulated and one answered a later run's INCR with a
  stale count. Servers are now killed by basename.
- REFERENCE.md appendices regenerated from source (188 subs, 571 methods);
  spec.raku.online's conformance map generator now parses its counting block
  from the results file instead of hard-coded literals.

## v0.9.1 — 2026-07-20

Everything since v0.9.0 (2026-07-19), 80 commits. Every change is gated on the
full Roast suite with no fully-passing file regressions.

### Headline

- **Roast: 558 / 1,462 files fully pass** (was 533). Passing assertions grew from
  187,749 to **189,102** — **88.2%** of the 214,384 declared tests, and 97.4% of
  the tests that actually ran.
- **New `--lint` mode**: a static analyzer that parses a program and reports
  likely mistakes without running it — unused variables, unused lexical
  routines, redeclarations, unreachable code, self-assignment, constant
  `if`/`unless` conditions, and numeric comparison of a string literal (all
  warnings), plus unused parameters and redundant trailing `return` (notes).
  Exits 1 on any warning, so it drops into CI or a pre-commit hook. The rules
  are deliberately conservative — interpolation and regex pattern text count as
  uses, and `EVAL`/symbolic references stand the "unused" rules down — to keep
  false positives near zero on Raku's dynamic constructs. Rule reference in
  [docs/guide/LINT.md](docs/guide/LINT.md); one-rule-per-file demos in
  [examples/lint/](examples/lint).

### Language & runtime

- **Shaped multidimensional arrays**: `my @a[2;3]` / `Array.new(:shape)` —
  declaration, fill, `.shape`, and multi-dim `AT`/`EXISTS`/`ASSIGN-POS`. Iteration
  (`keys`/`values`/`kv`/`pairs`/`flat`) yields leaves with index tuples;
  list ops (`join`/`map`/`sort`/`pick`/…) delegate to the leaves; `.gist`/`.raku`/
  `.clone` are structured; assignment is structurally validated (nested must match
  dims, shape-mismatch and flat-list throw); fixed-dim mutators/`reverse`/`rotate`
  throw. Closes `decl`/`assign`/`methods`/`multi_dimensional_array`.
- **Fractional numeric ranges**: `-1.5..1.5`, `1.1..^3.1` keep their real
  endpoints and step by 1 across `list`/`for`/`min`/`max`/bounds/`gist`.
- **Regex**: conjunction operators `&` / `&&` (all terms match at one position,
  span the last); numbered capture aliases `$N=(…)`; named array `@<name>=(…)`
  and hash `%<name>=(…)` capture aliases; the `:exhaustive`/`:ex` modifier.
- **Version** comparison: Unicode-letter alpha parts, numeric-before-alpha
  ordering, trailing-alpha-before-release, insignificant trailing zeros, and
  underscores preserved in `<>` word-quote spellings — `version.t` fully passes.
- **Negative-index semantics** now match Rakudo: an out-of-range negative
  subscript returns a `Failure` (`X::OutOfRange`), not a Python-style wraparound
  (`@a[*-1]` is how you index from the end); `:exists` on it is `False`; indexing
  an unhandled `Failure` propagates it.
- `classify`/`categorize` gain Hash and Array classifiers and `:into(%h)`
  appending; `round()` the sub delegates to the method (honouring a scale arg and
  NaN/Inf); Set-from-pairs uses value truthiness while Bag/Mix keep numeric
  weight; `.tree` (nested view / depth-limit / per-level closures) and
  `.^parameterize` (`Set.^parameterize(Str)` is `Set[Str]`).
- Numeric-literal underscores must sit between two digits (`1__0`, `100_`,
  `1_000_____000` are now rejected, in mantissa and exponent alike). String→number
  coercion learns `:N<>` radix, the `0d` prefix, and Complex / unicode-minus forms.

### Spec faithfulness

Fixes from building **spec.raku.online** and diffing against Rakudo — each a
behaviour where Raku++ had diverged: NFC/NFG string normalization; a lexical
regex shadowing a same-named built-in subrule; `.isa` as strict class inheritance
(roles excluded); round-half-up, `wordcase`, `comb(Int)`, `split :skip-empty`,
`indent`, `List.invert`; `qq{}` brace-delimited interpolation; `where`
enforcement and Capture/Map/Seq gists.

### Performance

- **`~=` string building is O(n) again in every mode.** The NFC-normalization
  work in v0.9.0 had made in-place append re-normalize the whole accumulator on
  each `~=`, turning `strcat` O(n²) (~360 ms). Appending pure-ASCII now skips the
  re-normalize; non-ASCII appends still normalize across the join. `strcat` is
  back to ~12 ms interpreted (15× Rakudo) and correctness is unchanged.

### Ecosystem & docs

- New [docs/status/ECOSYSTEM.md](docs/status/ECOSYSTEM.md): the projects built on this
  interpreter (Raku.js, raku.online, spec.raku.online, raku-corpus, the Homebrew
  tap), how they connect, and the release runbook for rebuilding the wasm and
  redeploying the sites after a version bump.
- REFERENCE.md inventory refreshed to 183 subroutines / 562 methods; FEATURES and
  the benchmark tables brought current.

## v0.9.0 — 2026-07-19

Everything since v0.7.1 (2026-07-16), 147 commits. Every change is gated on the
full Roast suite with no fully-passing file regressions.

### Headline

- **Roast: 533 / 1,462 files fully pass** (was 501). Passing assertions grew from
  171,817 to **187,749** — **87.5%** of the 214,569 declared tests, and 96.9% of
  the tests that actually ran.
- **`--exe` native binaries now have interpreter-parity recursion depth on every
  platform**: the generated `main()` runs the whole program on the same 1 GiB
  big-stack thread the interpreter uses (macOS/Windows also carry link-time stack
  flags). Deep recursion that the interpreter handles no longer crashes a native
  build.
- **Windows `--exe` works out of the box** (GitHub issue #1 closed): the generated
  `main()` no longer collides with the CRT `__argv` macro, MSVC builds default to
  the static CRT so native links don't fail `LNK2038`, a compiler is found on
  `PATH`, and `vcvars` is bootstrapped when `cl` isn't in the shell.

### Language & runtime

- Reduction metaops thunk their operands: `[&&]`/`[||]`/`[//]`/`[andthen]`/… and
  their `[\op]` scans short-circuit without evaluating later operands.
- 6.e array/hash multislices with star/list adverbs (`@a[*;0;*]:delete`,
  `%h{*;"b";"c"}`), and `@a[*-1, *-2]` list-slices now resolve `*`/`*-1` against
  the length.
- Set/Bag/Mix family: `for`-loops over `.values`/`.kv`/`.pairs` of a
  `SetHash`/`BagHash`/`MixHash` alias the weights (a weight of 0, or negative for
  MixHash, removes the element); `.ACCEPTS`/`.STORE`/`.Capture`, the coercer
  calls, and `class MySet is Set` subclassing.
- `Pair.value` is a writable container; typed-container multi dispatch
  (`multi f(Int @a)`); a slurpy multi candidate is now correctly the least-narrow
  tiebreaker.
- Regex `m:nth(N)`/ordinal/`:nth(*)`/list-and-`:global` counted adverbs; `Buf`
  `subbuf` Callable/`*` forms, `Buf.new(Range)`, `.allocate` fills; compile-time
  "Useless use … in sink context" warnings on the mainline.
- Containers: `@a = …` / `%h = …` refill the existing container in place, so
  bindings, captures, and closures track the reassignment.
- Correctness fixes from the pre-release review: `.Int` on a string/match wider
  than int64 is now exact (was 0); a brace character in a string inside an
  embedded regex code block parses correctly.

### Native compile (`--exe`) & the browser

- Caught builtin errors answer `.message` inside a native `CATCH` (was a bare type
  payload); `exceptionFor` synthesizes real exception objects for `X::`-named
  payloads. Block-final `if`/`given` is a pointy block's value; `Less`/`Same`/
  `More` and `PromiseStatus` resolve to real enum values under native name-term
  lookup.
- New [docs/guide/MEMORY.md](docs/guide/MEMORY.md): reserved-vs-resident memory and the
  measured recursion depths per mode (interpreter / `--exe` / WebAssembly).
- New [docs/guide/COMPILERS.md](docs/guide/COMPILERS.md): which compiler and architecture to
  use — arm64 vs. x86_64/Rosetta on macOS, Clang vs. GCC (with a measured
  ~1.3–2× gap on this codebase), MSVC vs. MinGW on Windows — both for building
  Raku++ and for the compiler `--exe` invokes.
- New showcases on the WebAssembly playground: a JavaScript/TypeScript
  interpreter, a Scheme, and a Forth, each written in Raku.

### Concurrency

- `react`/`whenever` no longer hangs when an eager `start { $s.emit(…); $s.done }`
  runs before the react taps the supply — the Supplier records its done state so a
  late tap closes immediately.
- A `.then` registered on a `start`/Promise before its worker settles now fires
  (was silently dropped).
- `CurrentThreadScheduler.cue` rejects `:every`, as in Rakudo.

### Robustness (pre-release review)

An independent multi-reviewer pass over the sources fixed eight default-build
defects: the regex greedy quantifier no longer overflows the stack on long runs
(`/\d+/` over millions of chars), `substr-eq` with only an adverb no longer reads
out of bounds, plus the correctness and concurrency items listed above.

### Known limitations

- **`RAKUPP_PARALLEL=1` (the opt-in GIL-free mode) is experimental and not
  production-safe.** Under it, `Channel`, the shared Rat-literal cache, and
  worker-side `class`/`EVAL` are not fully synchronized and can race. The default
  cooperative-GIL build (what ships and what every example uses) is unaffected.
- **Native (`--exe`) recursion is uncatchable if it overflows**: a compiled
  program that recurses past its stack dies with a signal rather than a catchable
  `X::Recursion` (the interpreter throws). See docs/guide/MEMORY.md.
- A native `given`/`when`/`CATCH` with a bare `my` declaration *between* clauses
  fails to compile (a `goto` past an initializer) instead of falling back to
  bundling; wrap the declaration in its own block.
- The parse-only entry points (`--cpp`/`--ast`) can overflow the stack on
  pathologically deep bracket nesting; ordinary execution is shielded.

## v0.7.1 — 2026-07-16

Everything since v0.5.1 (2026-07-13), ~100 commits. (A 0.7.0 tag was cut but
never published — its Windows build was broken — and is folded into this
release.)

### Headline

- **Roast: 501 / 1,462 files fully pass** (was 419) — the 500-files milestone.
  Passing assertions grew from 157,293 to **171,817**; the declared-test
  denominator also grew (191,546 → 213,617) because parse fixes keep
  surfacing plans that previously died unannounced, so per-test percentage
  moves less than the absolute counts (~80% declared).
- Ten zero-regression campaign batches (R1–R3, NM1–NM6), each gated on the
  full suite with no pass-list drops and equal-or-faster benchmarks.

### Language & runtime

- **`$*SCHEDULER.cue`** is implemented: `:at`/`:in`/`:every`/`:times`/`:stop`/
  `:catch`, `Cancellation` (`.cancel`/`.cancelled`), `.loads`,
  `.uncaught_handler`, `CurrentThreadScheduler`; NaN/±Inf delay semantics and
  argument-combination errors match the spec. Cued jobs run on worker threads
  with a drift-free deadline clock.
- **`subtest 'desc' => { … }` (the Pair form) now executes its body** — it
  used to pass vacuously. Landed together with seven batches of the
  pre-existing bugs it exposed (Rat 0-denominator cluster, `categorize-list`/
  `classify-list`, `.toggle`, non-flattening `:=`, strict `fails-like`,
  `substr-eq`, Capture semantics, …).
- `return` is `Routine`-only (a bare block's `return` returns from the
  enclosing routine); cooperative `return` works inside a method's loops.
- `&?BLOCK` / `&?ROUTINE` resolve lazily from the frame (`&?ROUTINE` outside
  a routine is a parse error, as in Rakudo).
- `X but VALUE` mixins compose a constant method named by the value's type.
- Weighted `pick`/`roll` on Bag/Mix draw without materializing pools;
  `roll(*)` is an infinite lazy sequence.
- DateTime: exact (non-float) seconds, leap-second table, fixed-offset
  timezones, single-numeric POSIX constructor.
- Junctions: `all`/`none` autothread outside `any`/`one`; whatever-curry wins
  over junction autothreading; the standard matcher-method exemptions
  (`grep`, `first`, `classify`, `comb`, `subst`, …).
- MAIN: Rakudo-compatible dispatch strictness, generated `$*USAGE` (including
  `#=` declarator-pod option descriptions), `sub USAGE` takes over the
  failure path, CLI arguments bind as allomorphs.
- Implicit `$a`/`$b` in paramless blocks removed (post-GLR semantics sweeps:
  element itemization, `Z`-comma, stacked zip/cross metaops, one-level
  operands, min/max flattening, rotor pairs, rw loop params, …).
- New builtins and methods across the campaign — inventories now stand at
  **179 subroutines / 505 methods** ([docs/guide/REFERENCE.md](docs/guide/REFERENCE.md)):
  `Lock::Async`, minimal `IO::CatHandle`, `FileHandle.encoding`,
  `List.lazy`, `cross(:with)`, the hyperbolic-trig family
  (`sech`/`cosech`/`cotanh` + inverses), `%%` by zero throwing
  `X::Numeric::DivideByZero`, Set↔Bag↔Mix coercions, `TYPE ~~ TYPE` role
  smartmatch, and more.
- `SIGPIPE` is ignored process-wide: TCP servers survive client disconnects.
- EVAL-only statement strictness ("two terms in a row") with typed
  `X::Syntax::Confused` parse errors.

### Parser

- A `}` at end of line terminates the statement (Rakudo's rule) — previously
  `x => {…}` followed by an `if`/`else` chain could silently re-parse as a
  statement modifier.
- The tight-paren reduce call `[+](…)` takes only its parens — it used to
  swallow the rest of the enclosing comma list, inflating some test files'
  emitted-test counts for years.
- Variable subscript adverbs (`%h{$k}:exists`-family with variable keys),
  adverbed zen slices, dative method syntax (`name $obj: args`), `INIT` as an
  expression, contextualizer circumfixes, comma-list shapes.

### Native codegen (`--exe`)

- `s///`, `$0` captures, and post-GLR slips compile natively (the pastebin
  showcase no longer needs `--bundle`).

### Raku.js — new subproject

- The unmodified C++ interpreter compiled to WebAssembly, with a browser
  playground: worker-based execution with live streaming output and a Stop
  button, syntax-highlighting editor, theme switcher, 24 bundled examples.
  Live at [raku.online](https://raku.online/).
- First performance measurements (experimental; [rakujs/README.md](rakujs/README.md)):
  1.3–6.8× slower than the native interpreter on a clean host, dominated by
  the `-fexceptions` call trampolines; Node vs Bun comparison included.

### Real-world output parity

- **Perl Weekly Challenge corpus** (10,428 community solutions run under
  both engines): byte-identical stdout+status went **2,663 → 4,056** across
  15 fix batches ([docs/dev/findings/PWC-DIVERGENCES.md](docs/dev/findings/PWC-DIVERGENCES.md)).
- **Raku course**: the generator reproduces the full 1,483-page course
  byte-for-byte identically to Rakudo after two rounds of divergence fixes.

### Showcases & tests

- Seven new showcase programs, each with a README: **lisp** (a Scheme on a
  Raku grammar), **pastebin** (HTTP on raw sockets), **markdown** (grammar →
  HTML), **chat** (concurrent TCP), **forth** (a stack machine), **kvstore**
  (a key-value protocol), **rakus** (a static HTTP file server).
- New `t/` regression suite (47 checks: golden example outputs + showcase
  behaviour), wired into CI on the POSIX platforms.

### Performance

- Cold start is **~2 ms** (best of 200 spawns; previously documented ~12 ms).
- Full benchmark refresh against Rakudo v2026.06
  ([docs/status/BENCHMARKS.md](docs/status/BENCHMARKS.md)): the interpreter is ahead on 8
  of 9 kernels (fib remains Rakudo's, 1.7×), `--exe` ahead on all 9.
- A perf regression found and reversed mid-campaign: eager `&?BLOCK`/
  `&?ROUTINE` frame bindings cost +40% on call-heavy code; the lazy
  resolution above restored the baseline.

### Platforms & CI

- MSVC: `clock_gettime(CLOCK_REALTIME)` replaced with `std::chrono` (this is
  what broke the unpublished 0.7.0 tag's Windows build); srand seeding
  widened to 64-bit (was UB on LLP64 and wasm32).
- Windows suite: portable process cleanup in `t/run.raku`.

## v0.5.1 — 2026-07-13 and earlier

Pre-changelog releases: v0.5.1, v0.5.0, v0.1.0. History is in git and the
docs as they stood at each tag.
