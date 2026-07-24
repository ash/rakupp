# Milestones

A running timeline of the headline moments in Raku++'s development — the dates,
the numbers, and what landed. This is the quick-reference companion to the
narrative in [LONGREAD.md](../LONGREAD.md) (the round-by-round story) and
[JOURNEY.md](dev/JOURNEY.md) (the method and principles). For the road to 100%
Roast specifically, see [100.md](dev/100.md).

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
| 2026-07-22 → | *(in progress)* | **v2.0** — running the ecosystem's zef modules |

**By the numbers:** v0.1.0 → v1.1.0 in 22 days and 776 commits (2026-07-02 to
2026-07-24).

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
  [UNICODE.md](UNICODE.md).

## The road to modules — v2.0 (Jul 22 → ongoing)

The current campaign: **run the programs people actually write — the ones that
`use` ecosystem modules installed by zef.** Raku++ already reads the same store
zef populates (see [MODULES.md](MODULES.md)); the goal is breadth and depth.

- The `nqp::` compatibility subset (zero-cost when unused) unblocks modules that
  lean on it; package version adverbs (`module M:ver<…>`) parse, unblocking
  JSON::Fast and friends.
- Real modules load and run today — JSON::Fast, URI, Terminal::ANSIColor, … —
  and the top-50 working set is being worked tier by tier (loads clean → usage
  matches Rakudo → own test suite passes). Progress and triage live in
  [V2-MODULES-PLAN.md](dev/V2-MODULES-PLAN.md).
- Stretch flagship: **zef itself running under rakupp**.

Beyond the interpreter, the same source feeds a small constellation —
[raku.online](https://raku.online/) (playground),
[spec.raku.online](https://spec.raku.online/),
[tour.raku.online](https://tour.raku.online/), the raku-corpus differential
target, a `setup-rakupp` GitHub Action, and an OpenBSD release target. The map is
in [ECOSYSTEM.md](ECOSYSTEM.md).

---

*Keeping this current: add a row to the table (and a note under the right phase)
whenever a release is tagged or a headline figure moves — a new synopsis reaching
100%, the file/assertion totals stepping up, or a v2 tier boundary crossed.*
