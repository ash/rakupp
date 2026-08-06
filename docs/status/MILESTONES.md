# Milestones

A running timeline of the headline moments in Raku++'s development — the dates,
the numbers, and what landed. This is the quick-reference companion to the
narrative in [LONGREAD.md](../../LONGREAD.md) (the round-by-round story) and
[JOURNEY.md](../dev/JOURNEY.md) (the method and principles). For the road to 100%
Roast specifically, see [100.md](../dev/plans/100.md).

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
