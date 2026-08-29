# Milestones

A running timeline of the headline moments in Raku++'s development — the dates,
the numbers, and what landed. This is the quick-reference companion to the
narrative in [LONGREAD.md](../../LONGREAD.md) (the round-by-round story) and
[JOURNEY.md](../dev/JOURNEY.md) (the method and principles). The
forward-looking counterpart — what each major version *set out* to do, and
the plan for the next one — is [VERSIONS.md](../dev/plans/VERSIONS.md). For
the road to 100% Roast specifically, see [100.md](../dev/plans/100.md).

Every figure here is measured, not projected; the methodology is in
[COUNTING.md](COUNTING.md).

## At a glance

| Date | Release | Headline |
|---|---|---|
| 2026-07-02 | — | Initial commit: a Raku interpreter **and** native compiler in C++ |
| 2026-07-10 | **v0.1.0** | ~300 Roast files passing; Homebrew tap |
| 2026-07-14 | — | **Raku.js** — the same interpreter in the browser via WebAssembly |
| 2026-07-16 | **v0.7.1** | NativeCall FFI (dlsym, no libffi); browser showcases |
| 2026-07-19 | **v0.9.0** | the satellite sites take shape (playground, examples) |
| 2026-07-22 | **v1.0.0** | **90% of declared Roast** (194,496 / 216,066); ~39% of files fully pass |
| 2026-07-24 | **v1.1.0** | **100% of S15 (Unicode) assertions** (91,752 / 91,752) |
| 2026-07-26 | **v1.2.0** | **835 documentation examples byte-identical to Rakudo** (from 596) |
| 2026-07-28 | **v1.2.5** | **936 documentation examples byte-identical** (from 835); semantic-duplication audit begins |
| 2026-07-28 | **v1.2.6** | Proc rendering — `say shell(…)` no longer prints its output twice |
| 2026-07-29 | **v1.5.0** | **the measured gap with Rakudo, halved** — 202 → 152 divergences; a performance gate on every release |
| 2026-07-29 | **v1.5.1** | **fib −17.6%** — argument vectors moved not copied; the 9,138-line dispatcher split into four files (CI's GCC job 25m → 1m32s) |
| 2026-07-31 | **v1.5.2** | **a module counts as working only when its own test suite passes** — 630 Roast files, 180 regression tests |
| 2026-08-01 | **v1.7.0** | **node specialization** — the largest single interpreter speed-up since the performance campaign (`$a OP $b` −18.3%); **18 / 59 distributions** pass their own suites (from 11) |
| 2026-08-03 | **v1.8.0** | **other people's code** — `zef` installs and `use` works end to end; `URI` 88 → 222 of 222 on twenty general fixes; TLS runs with certificate verification; a precompiled-parse cache; **32 / 59 distributions** pass their own suites (from 18) |
| 2026-08-07 | **v2.0.0** | **other people's code, honestly counted** — **50 / 59 distributions** pass their own suites (from 32); Pair-form subtests actually RUN (the honest Roast bar: −2,340 vacuous passes, re-earned by real fixes); a nine-pass fresh-eyes review of the whole source; `Supply.interval` is a real timer and `done` is a real control exception |
| 2026-08-08 | **v3.0.0** | **parallel by default, true LTM, and the CLI overhaul** — `start` runs on real threads with no flag (the GIL survives only as `RAKUPP_GIL=1`); longest-token matching done right; the perl one-liner family (`-n`/`-p`/`-a`/`-i`), `--profile` |
| 2026-08-09 | **v3.0.1** | the `Value` string representation replaced under a 3-run measurement discipline; the release procedure itself becomes a documented, gated artifact ([RELEASING.md](../dev/RELEASING.md)) |
| 2026-08-11 | **v3.1.0** | **something you can link against** — the extension ABI (`rakupp_ext.h`, ABI 2: `rk_call`, rooted handles) and the embedding API (`rakupp.h`: `rk_eval`/`rk_run`) share one value vocabulary; `librakupp` ships with a policed export table; Raku.js rides the public API; ten interpreter bugs found by the ABI's own gates |
| 2026-08-11 | **v3.14.0** | **only what the program needs** — `--exe --slim` proves features unused and cuts them: `say "Hello"` 9.83 → **4.86 MB** (−50.6%); a wrong cut throws typed `X::Feature::NotBuilt`, never lies; `list`/`why:`/`verify` introspection; the differential gate (241/270 corpus + 51/60 battery programs byte-identical, 0 different); twelve missing Unicode digit decades and a platform-identity lie fixed on the way |
| 2026-08-20 | **v3.5.0** | **the 6.e language revision** — `use v6.e.PREVIEW;` turns on the whole of 6.e and nothing else does: the revision travels with the CODE (per compilation unit, so a 6.e module works inside a 6.d program and back), the thirteen behaviours that were on by default are gated, and the additions are invisible under 6.d as they are in Rakudo. Matrix **50 of 51 full, 0 divergent** (RakuAST is the one, deliberately). Roast **198,628 / 218,626 (90.9%)**, **630 / 1,462 files**; battery 49/59; Weekly Challenge round two 74.8% → 90.9% byte-identical; `require ::($name)` un-broken |
| 2026-08-20 | **v3.5.1** | **the Raku.js build finds its headers** — one build-script line: `rakujs/build.sh` lacked the flat `include/rakupp` path that CMake has carried since the extension ABI landed, so `EmbedApi.cpp` compiled everywhere except the WebAssembly job and v3.5.0 shipped without its playground bundle. No engine source in the diff; v3.5.0's figures stand |
| 2026-08-21 | **v3.6.0** | **the Perl 5 lessons** — a study of the Perl 5 sources ([PERL5-TECHNIQUES](../dev/findings/engines/PERL5-TECHNIQUES.md)) and its first applied item: `ValueHash` replaces the tree-backed hash payload with a stored-hash insertion-ordered table (the `hash` kernel 20.5 → 13.1 ms native); the outside "twice slower than Perl 5" remark becomes the `hashfill` kernel (a workload built here to probe it — the report itself named no program) with a `perl` harness column and ends at a statistical tie (`--exe -O3` 82.1 ms vs perl 81.8); **DESTROY exists** (registry + child-first sweep on GC request, allocation pressure, program end); `.fc` folds ASCII again; `Mix.total` sums exactly. Roast **198,642 / 218,561 (90.9%)**, **633 / 1,464 files** |
| 2026-08-27 | **v3.20.0** | **the cooldown: one implementation of everything, and fez installs** — a 360° review ([REVIEW-3.7](../dev/findings/REVIEW-3.7.md)) worked to completion in three gated batches: eight silent wrong answers fixed (compiled `[<]` chains, `.IO.open` parity, `slurp :bin`, armed Failures, `let`-in-method, and the CORE `ProtocolType` enum); the twin implementations killed (one reduce, one Z/X, one hyper compound assign with the marker strictness enforced, one attribute walk — `class D is DateTime` constructs like Rakudo's); three measured micro-opts with a new `regexloop` guard kernel, and a use-after-free in multi-method deferral found by ASan. The perf gate's red turned out to be its own baseline (pivot forensics; re-recorded). Issue #37 closed: **fez installs with its tests green**, zef works end-to-end (suite 1/10 → 8/10 files), `uninstall` 40 s → 0.17 s. Version jumps to 3.20 to clear Homebrew's v3.14.0. Roast **198,791 / 218,608 (90.9%)**, **638 / 1,464 files** (band 638/638/637, per-file-diffed); battery **97/105** of the grown tier-2 list; regression suite 567 |
| 2026-08-27 | **v3.20.1** | **v3.20.0, correctly named** — the v3.20.0 tag was cut one commit before the `project(VERSION …)` bump, so its binaries self-reported 3.7.0. Per the release rules a published tag is never moved: same release, built from a commit whose binaries, docs and dashboard agree on the name. v3.20.0 marked superseded. |
| 2026-08-29 | **v3.21.0** | **the state after the changes** — the first of three consolidation releases ([VERSIONS.md](../dev/plans/VERSIONS.md)), adding no campaign: what had accumulated on main since the previous tag, measured together on one machine in one sitting. Issues #38 (twelve bugs behind one number-theory module), #39 (`take-rw` takes the container), #40 (a `ver<X+>` floor stops being answered with an older release), #41 (`sleep 333` sleeps 333 seconds) and #42 (HTTP::Tiny, broken in v3.20.1 — a predicate block's Regex answer read as plain truth, so `$/` was never set); plus TAP::Harness green, exact Bag/Mix counts, bound Lists refusing the resizing mutators, `--lsp`, Knuth algorithm D for long division, and an uninstall that removes every version behind a name. Roast **198,943 / 218,803 (90.9%)**, **643 / 1,464 files** (band 643/642/639/643, no file regressed); regression suite 578; battery 49/59, unchanged. The perf gate's baseline was re-recorded and the reason it moved is NOT understood — the commit that recorded the old numbers no longer reproduces them — so `best` keeps the earlier figures and v3.22.0 owns finding the cause. |
| 2026-08-29 | **v3.22.0** | **the instruments, fixed and then proved** — the second consolidation release. v3.21.0 passed all seven gates and shipped a silent wrong answer (`Digest`'s RIPEMD hashed every input incorrectly); this release fixes the instruments and then asks each gate to prove it can fail. **8 / 8 gates detect a planted defect** (`tools/prove-gates.raku --all`), which no release has claimed before. The wrong answer was not in `.flat` as the plan assumed but in parameter BINDING — a List argument must stay a List — and reading the other `coerceArray` sites found two more instances of the same defect. Six gate defects fixed: the Roast harness spliced children's stderr through four status lines a run (now 0 across three runs, and the file list is archived as data in [roast-lists/](roast-lists/)); a compiled binary re-ran itself without bound (2,633 processes at load 95 during this sitting, now a peak of 1); slim-diff killed a process GROUP, which never worked because every child leads its own; `perf-guard` AND `run-optbench` both defaulted to the x86_64 build on an arm64 box; the battery compared a run against itself; the figure grep could not see a bare table cell. Part B eliminated four more causes of the moved perf baseline (build nondeterminism, binary layout, the allocator, the min-of-3 metric) without finding it — gate 3's one-directional blindness is now stated in RELEASING.md. Roast **198,956 / 218,764 (90.9%)**, **642 / 1,464 files** (band 642/642/644, zero regressions against the union); regression suite 579; battery **50/59**. The 360° source review this release was scoped to carry is NOT done — three files of eighty-three. |
| 2026-08-24 | **v3.7.0** | **the whole ecosystem, and a new oracle** — every one of the **2,524 distributions** in the zef ecosystem put through `rakupp test` (build hook, dependency install, own suite), the failure clusters fixed, then re-run: **637 pass their own suites** ([ECOSWEEP](../dev/findings/ECOSWEEP-2026-08.md); [every dist listed](https://raku.online/modules/ecosystem/)), 421 more blocked only by a dependency. The oracle era moves to **Rakudo 2026.08**. `rakupp install` becomes a first-class installer on zef's own index and store; Wolfram Language joins as the sixth binding host. The Digest pair goes 55 s → 0.16 s on 1 KB of sha512 (a u64/u128 machine-word lane, and expression `BEGIN` evaluating once per node instead of per use); DBIish `01-basic` green on all five shipped drivers. `$*VM.config` stops being the string "moar" — caught by the module battery and by nothing else. Roast **198,679 / 218,605 (90.9%)**, **633 / 1,464 files**; battery **50/59**; regression suite 512 |

**By the numbers:** v0.1.0 → v2.0.0 in 36 days (2026-07-02 to 2026-08-07).

---

## The first burst — a working implementation (Jul 2–10)

The initial commit already carried a hand-written lexer, a recursive-descent
parser with a Pratt expression core, a tree-walking evaluator, **and** a native
code generator (`--exe`). From there the loop was: pick a failing
[Roast](ROAST.md) file, make it pass, repeat.

- **Jul 6** — ~254 Roast files; 6.e features (hyperslices, `HyperWhatever`),
  proto-token multi variants, `unit class/role/grammar`, S19 (command-line) to 100%.
- **Jul 7** — `--cpp` (print the C++ that `--exe` generates) and `-O` optimizer
  levels; S05-substitution closed; ~277 files.
- **Jul 8–9** — S16 (I/O) work to ~291 files; parameterized native containers;
  `augment` on built-in types; **NativeCall** — `is native` C FFI via `dlsym`,
  no libffi. [COUNTING.md](COUNTING.md) lands to fix the methodology (~57% of all
  declared tests, ~20% of files at the time).
- **Jul 10** — **v0.1.0**, ~300 files. Homebrew tap (`brew install ash/rakupp/rakupp`).

## Reach and surface — the browser and the ecosystem around it (Jul 10–20)

The interpreter grew a second life beyond the terminal.

- **Jul 13** — **v0.5.0 / v0.5.1**.
- **Jul 14** — **Raku.js**: `src/` compiled to WebAssembly with Emscripten — the
  exact same semantics running client-side, no server. An in-page playground
  (editor + live output) with the `examples/` built in.
- **Jul 16** — **v0.7.1**. Raku.js measured against native (clean-host wasm tax
  1.3–6.8×); browser showcases; `Str.succ`/`.pred` across Unicode scripts.
- **Jul 19–20** — **v0.9.0 / v0.9.1**. The playground gains the language
  showcases (Lisp/Forth/JS interpreters written in Raku), stdin, and shareable
  URLs — the seed of [raku.online](https://raku.online/).

## The 90% campaign → v1.0.0 (Jul 21–22)

A concentrated two-day push, driven entirely by the Roast tail: typed exception
diagnostics (matching Rakudo's exact `X::` types and messages), no-TAP unlocks
(parse fixes that revive whole files), `RUN-MAIN`, declarator pod, Unicode quote
families, `Set`/`Bag` completeness, and dozens more.

- **Jul 22** — **v1.0.0: 90% of declared Roast** — **194,496 / 216,066**
  assertions; **~39% of files fully pass** (the strict all-or-nothing bar). The
  language was proven; what remained was the ecosystem.

## v1.1.0 — 100% Unicode (Jul 24)

A pause in the module work to close S15 (Unicode / strings / NFG) completely.

- Full UCD case mapping (`uc`/`lc`/`tc`/`fc`), NFG-aware at the grapheme level;
  grapheme-level regex; complete `uniprop`; UTF-16/UTF-32 decode.
- **S15 at 100% of assertions — 91,752 / 91,752**, 80/82 files. (The lone
  holdout, `concat-stable.t`, is a performance timeout, not a correctness gap.)
- The complete case tables lifted string-heavy tests suite-wide: **576 → 598
  files fully passing, 194,506 → 194,904 assertions**, no regressions. See
  [UNICODE.md](../guide/UNICODE.md).

## Conformance, then speed — v1.2.0 → v1.5.1 (Jul 26–29)

Two measured campaigns back to back, each with its own yardstick.

- **Documentation conformance (v1.2.0 → v1.2.5).** Every runnable example in the
  official docs, executed on both engines and classified three ways: **596 → 835
  → 936 byte-identical**. The sweep also started the semantic-duplication audit —
  the same rule implemented twice in the C++ source, diverging.
- **Jul 29 — v1.5.0: the measured gap with Rakudo, halved.** 202 → 152
  divergences across the documentation sweep and the operator behaviour matrix
  (833 expressions over 121 operators). The other half of the release was
  turning properties we care about into **gates that fail a release** rather than
  numbers someone has to remember to check — performance among them.
- **Jul 29 — v1.5.1: no behaviour change at all**, Roast byte-for-byte identical
  before and after. Five candidates ranked from a profile; three landed, one was
  measured and abandoned, one measured and never attempted (**fib −17.6%**, from
  moving a call's argument vector instead of copying it). The 9,138-line
  dispatcher split into four files took CI's GCC job from 25m to 1m32s.

## The road to modules — v2.0 (Jul 22 → ongoing)

The current campaign: **run the programs people actually write — the ones that
`use` ecosystem modules installed by zef.** Raku++ already reads the same store
zef populates (see [MODULES.md](../guide/MODULES.md)); the goal is breadth and depth.

- The `nqp::` compatibility subset (zero-cost when unused) unblocks modules that
  lean on it; package version adverbs (`module M:ver<…>`) parse, unblocking
  JSON::Fast and friends.
- **Jul 24 — a live `HTTP/1.1 200 OK` over TLS**, through `IO::Socket::Async::SSL`
  and the system OpenSSL on Raku++'s own NativeCall. "Get HTTPS working" was
  never one feature: it was a chain of ~13 independent bugs, each hidden behind
  the last, and nearly every one a *general* correctness bug — the Roast numbers
  went up the whole way. The story is in [HTTPS.md](../guide/HTTPS.md).
- Real modules load and run today — JSON::Fast, URI, Terminal::ANSIColor, … —
  and the top-50 working set is being worked tier by tier (loads clean → usage
  matches Rakudo → own test suite passes). Progress and triage live in
  [V2-MODULES-PLAN.md](../dev/ecosystem/V2-MODULES-PLAN.md).
- **Jul 31 — v1.5.2 changed the standard.** A module counts as working only when
  **its own test suite passes** — the files zef runs at install time — instead of
  a one-line API probe. Encode, Trap, File::Temp and Digest::MD5 cleared the new
  bar, and every fix behind them was a general interpreter fix (`is raw`
  write-back, `CALL-ME` by name, `open :rw/:exclusive/:update`, a module's `END`
  at process end), not a module accommodation.
- **Aug 1 — v1.7.0: 11 → 18 of 59 distributions** past that bar, on ten more
  general fixes (`$*ARGFILES`, `method FALLBACK`, `.resolve`, `symlink`/`link`,
  the `X::IO` family, one-level slice assignment). The same release added
  **node specialization** — the interpreter recognises the four syntactic shapes
  hot loops are made of and takes a path that skips what the general case must
  do: `$a OP $b` −18.3%, `fib` −11.7%, against a control kernel that does not
  move. Only the shape is cached, never the variable or its value.
- **Aug 3 — v1.8.0: 18 → 32 of 59 distributions**, the largest move the number
  has made. `URI` went 88 → 222 of 222 assertions in a day on twenty general
  fixes with not one line of `URI` touched; `XML` and `YAMLish` reached
  `zef test`; and the parser stopped being blind to installed modules, which had
  meant a zef-installed module contributed none of its operators. Three fixes
  found running `IO::Socket::Async::SSL` gave TLS with certificate verification —
  none of them about TLS. Modules are also parsed once now and cached as ASTs
  (38% off `use XML` plus a mainline), and `--exe`/`--aot`/`--bundle` carry the
  modules they use, so a compiled binary runs with the module tree deleted.
- **The stretch flagship landed: `zef` itself runs under rakupp**, and its
  install writes a real repository entry, so `install` → `use` works end to end.
- **Aug 7 — v2.0.0: 32 → 50 of 59 distributions, on an honest bar.** The
  release-defining move was subtractive: Pair-form subtests (`subtest "…" =>
  {…}` — most of the suite's) had never run their bodies and auto-passed;
  making them run cost 2,340 vacuous passes and 39 "fully passing" files, and
  the campaign then re-earned the total with real fixes — Cro::HTTP, DBIish,
  HTTP::UserAgent and the rest of the top-50 among them. A nine-pass
  fresh-eyes review of the whole hand-written source closed the cycle:
  ~170 lines of dead dispatch arms out, four oracle-verified parser
  divergences and six compiler-only ones fixed, `Supply.interval` became a
  real timer, and `done` a real control exception.

## v3 — the language grows up operationally (Aug 8–11)

- **Aug 8 — v3.0.0: parallel by default and true LTM.** The two deepest
  architecture items on the v3 list landed together: `start` blocks run on
  real OS threads with no opt-in flag (the cooperative GIL survives only as
  an escape hatch), backed by ~63 thread-local execution contexts, a shared
  read-only AST, and two once-published cache disciplines that ThreadSanitizer
  signed off; and the regex engine's longest-token matching became the real
  UAX-shaped thing. The CLI grew the perl one-liner family and a wall-time
  profiler.
- **Aug 11 — v3.1.0: rakupp becomes a library, in both directions.** One value
  vocabulary serves extensions calling in (`rakupp_ext.h`, ABI 2 — `rk_call`,
  rooted handles, a call-scoped arena) and hosts embedding the interpreter
  (`rakupp.h` — `rk_eval` session semantics, `rk_run` program semantics).
  `librakupp` exports exactly the `rk_*` surface and nothing else, gated in
  CI; the browser playground became the embedding API's first real customer.
  The ABI's own smoke tests found ten interpreter bugs, from `|c` capture
  flattening to two SIGSEGV-grade data races.
- **Aug 11 — v3.14.0: only what the program needs.** The SLIM campaign end to
  end: dead-strip by default, the runtime split into five archives behind an
  accessor seam, throwing stubs for four cuttable features, and a scan that
  proves unreachability over the program plus its embedded module graph —
  `say "Hello"` compiles to 4.86 MB, half of v3.1.0's binary. Wrong cuts are
  structurally loud (typed `X::Feature::NotBuilt`, rethrown through eight
  once-lenient catch sites), and the release gate is a behaviour differential:
  every corpus and battery program byte-identical built slim and built full.
  The campaign's collateral finds: native codegen had been silently mis-running
  regexes that touch program variables; twelve newer scripts' digits never
  lexed as numbers in any prior binary; and `$*KERNEL.name` said "darwin" on
  every platform since the mac-only days — caught, in the end, by a Linux CI
  runner objecting to a darwin-only test gate.

## v3.5 — the 6.e revision, gated (Aug 19, unreleased)

- **Aug 19 — 6.e is implemented, and 6.d is 6.d again.** The revision became a
  property of the *code* rather than of the process: each compilation unit
  records what it was compiled under, every routine is stamped with its unit's
  revision, and the call path switches to the callee's — so a module written for
  6.e keeps its semantics when a 6.d program calls it, and a 6.d mainline is not
  changed by loading one. (Rakudo differs: `use`-ing a 6.e module there loads
  `CORE.e` process-wide, which its own 6.d mainline then sees.) On top of that,
  the thirteen behaviours that had been simply *on* went behind the pragma, the
  nineteen divergences were fixed, and the missing routines were written —
  `nano`, `trans`, `snitch` as a sub, `.Callable`, `.nomark`, `IO::Path.stem`.
  The scoreboard is measured, not asserted: [raku.online/spec/6e](https://raku.online/spec/6e/)
  runs all 51 tracked changes four ways (both engines × both revisions) and reads
  **50 full, 0 divergent, 1 not implemented** against 23/19/8 when the campaign
  started, with 46 of them gated. RakuAST is the one left, deliberately. Cost:
  about 1% on the interpreter hot path, at the noise floor, A/B'd against a
  same-arch build of the pre-campaign commit.

Beyond the interpreter, the same source feeds a small constellation —
[raku.online](https://raku.online/) (playground),
[raku.online/spec](https://raku.online/spec/),
[raku.online/tour](https://raku.online/tour/), the raku-corpus differential
target, a `setup-rakupp` GitHub Action, and an OpenBSD release target. The map is
in [ECOSYSTEM.md](ECOSYSTEM.md).

---

*Keeping this current: add a row to the table (and a note under the right phase)
whenever a release is tagged or a headline figure moves — a new synopsis reaching
100%, the file/assertion totals stepping up, or a v2 tier boundary crossed.*
