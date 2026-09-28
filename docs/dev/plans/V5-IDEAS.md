# After v6 — the unscheduled list (was the v5.0.0 candidate list, 2026-09-17)

*Not a plan and not a campaign. This file collected everything the repository
and its findings named as open, wanted or deferred, so that the v5 pillars would
be chosen from a complete list. **They were chosen on 2026-09-26: v5 is about
errors ([V5-PLAN.md](V5-PLAN.md)) and v6 about speed ([V6-PLAN.md](V6-PLAN.md)),
both listed in [VERSIONS.md](VERSIONS.md).** By this file's own rule, every
item those plans took was removed from here. What remains is unscheduled, with
its evidence, so that nothing is lost. The next major is chosen from it.*

Where each theme went:

| theme | now |
|---|---|
| 1. correctness at depth | **V5-PLAN** — the regressions, crashes, silent wrong answers, the ecosystem queue's clusters (metamodel fidelity among them) and regex code blocks under backtracking. The survey found that the Cro cluster is not a lever: Rakudo fails 20 of 21 Cro distributions here. |
| 2. concurrency | the `evalCall` race and the Rat literal cache race → **V5-PLAN B2**; the worker tax → **V6-PLAN P6**; what remains is below |
| 3. speed | **V6-PLAN** in full — issue #47, the representation endgame, inline caches, native math phases 2–3, exceptions (a P0 measurement), the regex residue, reference cycles |
| 4. the compiler stops falling back | **V6-PLAN P5/P6** — the `--exe` fallback table (re-measured: 773 of 1,189 on `spectest.data`), `--cnp`, `--target=js`; its disagreements → **V5-PLAN B5** |
| 5. language surface | the parallel Roast campaign that v5 is tagged on; what Roast does not test stays below |
| 6–10 | unchanged, below |

---

## 2. Concurrency — what v5 and v6 did not take

- **The TSan ratchet is six programs long.** Parallel mode runs six correctness
  programs strict under ThreadSanitizer at zero reports; everything else is
  still skipped, un-skipped one program at a time as it comes clean. V5-PLAN B2
  adds the `evalCall` repro; the rest of the ratchet is here. Two Roast files
  are ledgered exceptions since the parallel flip —
  `S17-promise/nonblocking-await.t` and `6.c/MISC/bug-coverage-stress.t`
  ([PARALLEL-PLAN.md](PARALLEL-PLAN.md)).
- **One interpreter per process.** A second `Interpreter` re-points ~9 process
  statics and the thread-local execution context; a scratch host must hand them
  back or the main one segfaults later. Every binding inherits this. The
  embedding plan's one named open question is host functions that are `async`
  ([EMBED-PLAN.md](EMBED-PLAN.md), "Threads at the boundary").

**The number:** the whole parallel stress list and S17 strict under TSan at zero
reports; two interpreters in one process, gated.

## 5. Language surface still missing

*The parallel Roast campaign takes whatever the default set tests; the items
below stay here only as far as Roast does not reach them.*

On 2026-09-17 Roast stood at 676 of 1,464 files and 200,843 of 219,610
assertions, measured before the harness applied Roast's own fudge. At HEAD on
2026-09-26 it was 1,201 of the 1,434 files of `spectest.data`. Rakudo's own ceiling on the raw files is
~96.9% ([100.md](100.md)). The cheap wins are
spent — moving the file count now takes whole features. By synopsis, the weakest:

| synopsis | assertions passing |
|---|---:|
| S10 packages | 53% |
| S26 Pod | 66% |
| S11 modules | 73% |
| S16 I/O | 75% |
| S04 blocks, phasers | 83% |
| S07 iterators | 84% |

- **Macros.** The parser has no `macro` at all. RakuAST is done and is "the
  reference design for metaprogramming's endgame" (RAKUDO-TECHNIQUES item 3), so
  this is reachable for the first time.
- **`.resume`** control flow (the `ResumeEx` teardown no longer wedges, the
  semantics are still unimplemented); **EVAL lexical isolation** (a recurring S02
  tail); `prefix:<~^>`; the `nqp::` ops the setting-layer code reaches for.
- **Pod** — `--doc` renders, S26 is at 66%; the `Pod::To::*` family is what the
  ecosystem's documentation tooling runs on.

**The number:** files fully passing; assertions net of the skip/todo shield
(90.5% today); the per-synopsis table with no cell under 80%.

## 6. A capability sandbox

[CLI-BORROW-PLAN.md](CLI-BORROW-PLAN.md) calls this "the most differentiating
item in this file": `--allow-net`, `--allow-read=PATH`, `--allow-run`, deny by
default. It is feasible here because rakupp owns every syscall site — open,
spawn, socket, NativeCall — where Rakudo cannot easily follow. Two consumers exist
on day one: the raku.online live runner, and `rakupp install` / `rakupp test`,
which run arbitrary code from the network. The plan says "deserves its own plan
file if picked up"; the open design questions are whether NativeCall is simply
off under a sandbox and whether `--allow-read` is prefix-based. Adjacent:
Rakudo's `X::SecurityPolicy` for a string interpolated as a regex, left open by
issue #81.

**The number:** every distribution's tests run under the sandbox during a sweep;
a planted exfiltration in a test file fails closed, gated.

## 7. Where it runs, and from what

- **Android** (issue #87, "not a feature request per se") — the questioner's own
  observation is that a self-contained native binary is a well-suited base. A
  Termux build is the cheap probe; an NDK cross-compile with the same floor gate
  as Linux is the real one. iOS follows the same shape.
- **A fresh WebAssembly build.** The engine behind `--fallback=wasm` is a July
  build; the playground cannot load a module. WASI would make one artifact run
  in every runtime rather than only a browser. Raku Koans
  ([live/ADOPTIONS.md](../../../live/ADOPTIONS.md)) builds on the released
  Raku.js and works around three things it lacks: the release is
  `web,worker` only, so its CI loads it under Node through Emscripten's
  `instantiateWasm` (a `RAKUJS_ENV=node` build exists and is not released);
  `rakupp_run()` cannot be stopped by the host, so a watchdog kills the
  worker; and recursion in a Web Worker ends at 59 levels in Chrome and 32 in
  Safari ([rakujs/INTERNALS.md](../../../rakujs/INTERNALS.md)), with no
  main-thread retry of the kind the playground has.
- **More hosts on the one C API.** The JavaScript binding is Bun-only
  (`bun:ffi`); Node needs N-API. Emacs Lisp is requested (issue #44, with
  raku-mode and org-babel named). Ruby, PHP, Java, .NET, Swift and Lua each cost
  one thin binding over `rakupp.h`. Wolfram's `RakuEval` prints and then returns
  `True` (issue #56).
- **Distribution channels.** A Homebrew tap and the curl-pipe installer exist;
  deb/rpm, AUR, nix, Scoop and winget do not. The `setup-rakupp` GitHub Action
  is staged locally and not pushed. The glibc-2.28 floor and the manylinux
  container are unproven in CI until pushed.

**The number:** the hosts-by-platforms matrix in bindings/README.md with a CI job
behind every cell.

## 8. Modules and the supply chain

- **`rakupp.lock`** — [LOCKFILE-PLAN.md](LOCKFILE-PLAN.md) is written and nothing
  is built. Note that CLI-BORROW-PLAN, three days earlier, listed lockfiles as a
  deliberate non-borrow ("machinery the ecosystem's size doesn't justify yet");
  the two documents disagree and one has to be right.
- **The wishlist.** Twelve Tier-A modules, every one pure-Raku-feasible and
  therefore both engines' ([ecosystem/MODULE-WISHLIST.md](../ecosystem/MODULE-WISHLIST.md)):
  `HTTP::Simple`, `Data::Schema`, `JSON::Schema`, `Log`, `CLI`, `Terminal::Rich`,
  `HTML::DOM`, `YAML`, `AWS::S3`, `Cache`, `Resilience`, `Prometheus`. The UUID
  distribution is drafted, unbuilt, and unnamed ([UUID-PLAN.md](UUID-PLAN.md));
  base64 has three incompatible interfaces and was deferred on that ground.
- **Installer leftovers** — `zef uninstall`, ecosystem fetch, exit codes;
  `rakupp update` and `rakupp env` as named follow-ons.

**Decided nearby:** no `RakuPP::` namespace; `Crypt::Random::Native` is not built.

## 9. Developer experience

- **The language server is diagnostics only** and flagged "needs review" since
  it left its side worktree. Completion, hover, go-to-definition and rename are
  what an editor expects, and RakuAST now carries the tree they would walk.
- **A debugger is a deliberate non-borrow** — the replay debugger for the
  playground was probed and parked (`MY::` came back empty, the AST dump carried
  no line numbers, ~20× probe cost), and CLI-BORROW-PLAN says not to reopen it
  through a flag. Listed here so the parking is visible; it comes back only with
  those probe blockers cleared, and backtraces P1–P3 have since landed.
- **Backtraces P4** (columns, JS parity); the one open `--fmt` family (R1
  reaching inside a multi-line regex); `--lint` grew three rules in v4.0.0 and
  has room for more. There is no `--coverage`, and test tooling beyond `Test` is
  wishlist item B13.
- **Kernel and notebook** — the Jupyter kernel runs; rich display and widgets do
  not exist.

## 10. Measurement

The whole-ecosystem sweep is V5-PLAN B6. The `--exe` no-fallback count was
re-measured on 2026-09-26 (773 of 1,189 on `spectest.data`), and issue #90 is a wrong answer, so it
went to V5-PLAN B3. Left here:

- raku-eye's weekly run has open decisions (REA yanked dists; the Rakudo policy)
  in [RAKU-EYE-PLAN.md](RAKU-EYE-PLAN.md); Rakugrid and Rakumap points are
  recorded at release time by hand.

Outside the engine and not release material, but open: the print edition, the
JavaScript tutorial, the deck on GitHub Pages, short-video outreach.

---

*Keeping this current: when an item lands or is decided against, move it to the
plan that closed it and delete it here. When the next major is chosen, its
survivors become that version's section of VERSIONS.md.*
