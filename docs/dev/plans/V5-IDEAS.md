# v5.0.0 — the candidate list (collecting, 2026-09-17)

*Not a plan and not a campaign. Every shipped major followed the pattern in
[VERSIONS.md](VERSIONS.md): one number a stranger can re-measure, a written plan
before the code, the standing gates on every batch. This file is the step before
that — everything the repository and its findings currently name as open, wanted
or deferred, in one place, so the v5 pillars are chosen from a complete list
rather than from whatever was on the table the day the choice was made.*

Each item names **where the gap was measured**, **the number it would move**, and
**what has already been decided nearby** — the last so that nothing here quietly
re-proposes something a measurement or the user already closed. Items are grouped
by theme; the closing section lists the framings a major could be *named* for.

---

## 1. Correctness at depth — the code that still fails on this engine

The largest measured gap in the project. Of 2,529 distributions, 1,006 pass their
own tests here; Rakudo, through the same harness on the same machine, passes
1,791 ([findings/ecosweep/RAKUDO-BASELINE-2026-09-16.md](../findings/ecosweep/RAKUDO-BASELINE-2026-09-16.md)).
The 794-distribution queue between the two, by how this engine fails:

| failure mode | dists |
|---|---:|
| a wrong value, nothing raised | 365 |
| other error | 178 |
| parse error | 100 |
| no such method | 70 |
| undefined routine or name | 39 |
| type check | 19 |

**The 365 do not cluster** — each is a narrowing of something that works in a
sibling spelling, and each is one distribution at a time. The clusters that do
exist:

- **Metamodel fidelity.** A module-supplied metaclass gets only its `compose`
  called — `new_type`, `add_attribute`, `add_method` never dispatch — so
  `OO::Monitors` is a plain class and loses increments. `AttrX::Mooish` (and
  `Cooklang` behind it) wants persistent `Attribute` objects with
  `get_attribute_for_usage`; a parameterized role is punned into a distinct class,
  so `R2[Int] ~~ R2[Cool]` never reaches the conformance rule
  (v4.0.0 CHANGELOG, "what the denominator join found"). ROADMAP.md named this
  "the next campaign" at v2.0.0 — a BUILDPLAN-style construction hook,
  `Attribute` MOP objects with `get_value`/`set_value`, per-instance `does`
  mixins — and it is still the wall. Red's next wall is compile-time `my`
  (issue #77).
- **The network/async cluster** — 168 distributions behind Cro and the async
  socket layer. `IO::Socket::Async::SSL` went 7 of 8 in v4.0.0; the next
  blockers are a `Supply` type check in binding (`Cro::HTTP::BodyParser::*`), a
  parse error inside `Cro::HTTP` itself, `Cro::HTTP::Client` hanging over https,
  and the missing `CompUnit::Repository.candidates` that `HTTP::Tiny` uses
  (memory: both http clients fail over https at the module layer while the TLS
  transport is fine).
- **Known wrong answers with repros, never batched:** `is export` does not export
  user-defined operators (silent wrong result — the worst class); `.append($h<k>)`
  spreads; `[ %( ) ]` does not flatten; `return` outliving its routine; a lazy
  `.map` returned from a `when` block is reified; a `LEAVE` phaser's own exception
  is swallowed; `$?FILE` inside a module names the main program; hash variables
  with non-Latin names are not assignable; ideograph numerals shadow identifiers
  ([findings/BUGS.md](../findings/BUGS.md), [findings/TRIAGE.md](../findings/TRIAGE.md)
  — the sixteen July quirks there need a re-verify pass before anything is
  planned from them).
- **Regex code blocks under backtracking** — `/(\d) { say $0 }/` re-runs the
  block on every backtrack; a fix must defer side effects to the accepted path.
  `:exhaustive` is the same family. Attempted once and backed out
  (ROADMAP.md). Native array boxing is the one spec-site divergence left
  ([findings/SPEC-DIVERGENCES.md](../findings/SPEC-DIVERGENCES.md) #14).

**The number:** distributions passing of 1,791, published against that ceiling
(1,006 today, 56%). The second number, which the board cannot give: the
**cold-store** count — install from nothing and test — estimated at ~785 against
the warm 1,006 by the closure model, and never measured directly. The whole
sweep was not re-run for v4.0.0; the live page shows 2026-08-30.

**Decided nearby:** an enum's trailing `does role` is parsed and dropped, on
purpose (Rakudo does not compose it either). The hashed CURI store stays hashed.

## 2. Concurrency that is safe to ship

- **A data race in `evalCall` segfaults the process** — call one sub from four
  promise threads, 200 rounds, about half the runs die. Found by the v4.0.0 gate,
  measured as pre-existing, and recorded rather than fixed because "a data-race
  fix is a project rather than a release task"
  ([findings/EVALCALL-RACE-2026-09-17.md](../findings/EVALCALL-RACE-2026-09-17.md)).
  This is the single most concrete open item in the repository.
- **The TSan ratchet is six programs long.** Parallel mode runs six correctness
  programs strict under ThreadSanitizer at zero reports; everything else is
  still skipped, un-skipped one program at a time as it comes clean. The Rat
  literal's `cacheN`/`cacheD` (a mutable `shared_ptr`) is a known deferred race
  class. Two Roast files are ledgered exceptions since the parallel flip —
  `S17-promise/nonblocking-await.t` and `6.c/MISC/bug-coverage-stress.t`
  ([PARALLEL-PLAN.md](PARALLEL-PLAN.md)).
- **The worker tax.** A worker thread's own loop runs ~15% slower than the main
  thread's; a one-thread fan-out measures 0.85×, and four threads on one
  `atomicint` are a net loss. The P4 thread pool was deferred at 1.41× CPU
  inflation — an optimization, not a gate.
- **One interpreter per process.** A second `Interpreter` re-points ~9 process
  statics and the thread-local execution context; a scratch host must hand them
  back or the main one segfaults later. Every binding inherits this. The
  embedding plan's one named open question is host functions that are `async`
  ([EMBED-PLAN.md](EMBED-PLAN.md), "Threads at the boundary").

**The number:** the nine-line repro at 0 deaths in 1,000 runs; the whole
parallel stress list and S17 strict under TSan at zero reports; two interpreters
in one process, gated.

## 3. Speed where the profile still points

What has been measured out, so it is not re-proposed: compiling grammars to
low-level code (caps at ~2×, 56–62% of a capturing parse is Match/memo churn);
a register IR or flat threaded loop (slot-indexed locals ~4% ceiling; re-measured
2026-09-02, still not the win); hashing the method-dispatch chain (slower than the
chain); shrinking `Value` by repacking (2.5% slower); constant folding (0.7
foldable sites per 1k nodes). Each has its file under
[experiments/](../experiments/). What remains open:

- **Issue #47 is still open** — `Math::NumberTheory` and `Graph` ~40% slower than
  Rakudo on macOS. [DISPATCH-PERF-PLAN.md](DISPATCH-PERF-PLAN.md) found the storage
  was not the problem and neither was dispatch; `objects` itself is level with
  Rakudo since the construction batch (objnew −34%). The two modules need a
  fresh profile, not a hypothesis.
- **The value representation endgame.** The 24-byte head/body scalar
  (PERL5-TECHNIQUES item 1, PHP7's 16-byte zval as "the endgame run to
  completion", Lua's TValue as the fourth confirmation) remains open;
  [VALUE32-PLAN.md](VALUE32-PLAN.md) priced the last two halvings. The JSON::Fast
  gap was the representation, not the tree-walker: the `std::map` half is fixed
  (31 ns/entry), the array path is what is left.
- **Inline caches and type versions** — the invalidation half "we have not
  designed yet" (PYTHON3 item 2; Ruby's object shapes; JSC's structures and
  watchpoints; MoarVM's interned callsites). The method-dispatch experiment's own
  conclusion names the prize: dispatch on the invocant type first, then the name
  within that type — the 198 type-guard arms are what make the chain long.
- **Native math phases 2–4** — unboxed typed slots in the pads the frame
  machinery already has ([NATIVE-MATH-PLAN.md](NATIVE-MATH-PLAN.md)); phase 1
  turned out to be a control-flow defect worth more than the rest combined.
- **Control flow without C++ exceptions.** Every `return`, `next` and `last` is a
  throw. `--exe` output sits 1.3–6.8× off hand-written native "dominated by
  `-fexceptions`", and the WASM build ships classic exceptions because
  `-fwasm-exceptions` is uneven across browsers (LONGREAD.md). One change, two
  backends.
- **The regex engine's residue** — the tree-churn fixes that survived the
  grammar-compilation verdict as ordinary work; the ~8M-character scan limit that
  answers "no match"; `Array.shift` at O(n), which is why `concat-stable.t` is
  the sole S15 miss.
- **Reference cycles** — "the leak we have chosen, and its eventual bill"
  (PYTHON3 item 7). The nested-sub closure cycle was patched once; a general
  answer is a cycle collector, and VALUE32 notes that values ceasing to be
  refcounted is the change that would reshape the representation question too.

**The number:** `perf-guard --check` against Rakudo on all fifteen kernels;
JSON::Fast's ratio; issue #47's two modules at parity; `--exe` against the
native reference.

## 4. The compiler stops falling back

- **`--exe` cannot dispatch most multi methods.** The emitted guard sees
  positional arity and nominal type only: a required named is invisible, `where`
  and `:D`/`:U` never enter, and a candidate declared in an ancestor is
  unreachable — paid for with a fallback, "the remaining half not lost"
  ([findings/TRIAGE.md](../findings/TRIAGE.md), 2026-08-20). Also falling back:
  sub-signature destructures, a `Range` bound to `@c`, roles/packages, symbolic
  refs, `s///`; and the `--aot` lossiness the precomp cache exposed.
- **`--target=js` parity.** `LtmNfa` is not ported; `:nth`/`:x`/`:P5`/`:m` are
  not; `sleep` in a Worker is not; a `use`d module is neither transpiled nor
  rescued by `--fallback=wasm` (the WASM engine has no filesystem — issue #83,
  "playground does not support modules"). The gate `t/js/run.raku` is blind to
  `MAIN` arguments.
- **A third backend** — Rust was named "later" in
  [TRANSPILE-PLAN.md](TRANSPILE-PLAN.md), with an IR before a third backend
  explicitly ruled out.

**The number:** fully-passing Roast files that compile with `--exe` and no
fallback (389 of 416 at v1.0; not re-measured since); programs judged rather than
refused by the JS gate.

## 5. Language surface still missing

Roast stands at 676 of 1,464 files and 200,843 of 219,610 assertions; Rakudo's
own ceiling on the raw files is ~96.9% ([100.md](100.md)). The cheap wins are
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
  in every runtime rather than only a browser.
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

## 10. Measurement, which any v5 needs first

- The whole-ecosystem sweep after v4.0.0 is scheduled and not run.
- raku-eye's weekly run has open decisions (REA yanked dists; the Rakudo policy)
  in [RAKU-EYE-PLAN.md](RAKU-EYE-PLAN.md); Rakugrid and Rakumap points are
  recorded at release time by hand.
- The `--exe` no-fallback count has not been re-measured since v1.0.
- Issue #90 (nested exported `CStruct` classes coerce wrongly) is the newest open
  engine bug and belongs to whichever theme picks up NativeCall.

Outside the engine and not v5 material, but open: the print edition, the
JavaScript tutorial, the deck on GitHub Pages, short-video outreach.

---

## What a v5 could be named for

Every major so far was one capability seen from several directions. The
candidate framings the list above supports, each with its stranger-measurable
number:

1. **Raku you can trust** — §1 correctness at depth, §2 concurrency safety, §6 the
   sandbox. Numbers: distributions of 1,791; the race repro at zero; a sweep run
   sandboxed. This is where the evidence is heaviest: the largest measured gap,
   the only shipped segfault, and the item the CLI plan already ranked first.
2. **Raku everywhere** — §7 platforms and hosts, §4's WASM half, §8's channels.
   Number: the matrix with CI behind every cell.
3. **Raku that is fast** — §3, the representation endgame plus inline caches plus
   control flow without exceptions. Numbers: the fifteen kernels, JSON::Fast,
   issue #47.
4. **Raku that compiles** — §4 end to end, `--exe` without fallback and a JS
   backend at parity. Number: Roast files compiled with no fallback.
5. **The rest of the language** — §5, macros and the six weak synopses. Number:
   files fully passing.

A major can carry one of these, or two that share a substrate the way v4's
embedding and extension ABI did. It cannot carry five.

---

*Keeping this current: when an item lands or is decided against, move it to the
plan that closed it and delete it here; when the pillars are chosen, this file's
survivors become the v5 section of VERSIONS.md and the file is retired.*
