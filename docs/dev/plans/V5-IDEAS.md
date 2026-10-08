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
| 6. a capability sandbox | picked up 2026-10-08 → **[SANDBOX-PLAN.md](SANDBOX-PLAN.md)**; the entry below stays until it lands |
| 7–10 | unchanged, below |
| 11. Raku inside the database | added 2026-10-08, below |

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

*Roast reached 100.00% in v5.0.0 (1,423 of 1,424 files,
[100.md](100.md)), so everything here is language Roast does not test.*

- **Macros.** The parser has no `macro` at all; Rakudo runs
  `use experimental :macros` with `quasi`. RakuAST is done and is "the
  reference design for metaprogramming's endgame" (RAKUDO-TECHNIQUES item 3), so
  this is reachable for the first time.
- **`.resume` across a routine call.** Resuming continues after the throw when
  the `CATCH` sits in the block that threw; when the throw is inside a called
  routine, the call returns an undefined value and the rest of the routine is
  skipped. **EVAL lexical isolation** (a recurring S02 tail); the `nqp::` ops
  the setting-layer code reaches for.
- **`use` inside a closure** runs when the closure first runs, not at compile
  time (JSON::Tiny's `t/01-parse.t`, last test).
- **Pod** — `--doc` renders and S26 passes; the `Pod::To::*` family is what the
  ecosystem's documentation tooling runs on.

**The number:** each item closed with a probe that matches Rakudo.

## 6. A capability sandbox

*Picked up 2026-10-08: [SANDBOX-PLAN.md](SANDBOX-PLAN.md) answers both design
questions below.*

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
  build; the playground cannot load a module (planned in
  [V6-PLAN.md](V6-PLAN.md), P7). WASI would make one artifact run
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

## 11. Raku inside the database

PostgreSQL serves each connection from its own single-threaded backend
process. That is the shape `rakupp.h` already has: one interpreter per process
([EMBEDDING.md](../../guide/EMBEDDING.md), "One interpreter per process"). An
interpreter costs about 7 MB of resident memory and a few milliseconds to start
(`rakupp -e 'say 1'`, measured 2026-10-08 on an Apple M3), so every backend can
carry one. What a query gains: grammars over text columns
(`SELECT (parse_invoice(body)).* FROM inbox`), grapheme-level strings, and
exact `Rat` arithmetic.

- **SQLite first.** A loadable extension is one C file:
  `sqlite3_create_function_v2` registers `raku(code, args…)` and user-defined
  functions, each one an `rk_call`. SQLite has no exact decimal type, so `Rat`
  sums are the clearest gain there. It is the reverse direction of
  [showcase/sqlite](../../../showcase/sqlite), where Raku calls SQLite.
- **PostgreSQL: `plraku`.** A procedural-language handler —
  `CREATE FUNCTION … LANGUAGE plraku`, inline `DO` blocks, a validator —
  mapping `numeric`↔`Rat`, `text`↔`Str`, `jsonb`↔`Hash`, arrays↔`List` and
  `NULL`↔ a type object. Without a sandbox it can only be an untrusted
  language (`plrakuu`, superuser only, as `plperlu` is); the embedding switch
  in [SANDBOX-PLAN.md](SANDBOX-PLAN.md) is what a trusted `plraku` stands on.
- **DuckDB** runs many threads inside one process, so it waits for concurrent
  interpreters ([EMBED-PLAN.md](EMBED-PLAN.md), E5).

**The number:** `SELECT raku('1/10 + 2/10 == 3/10')` answers `1` in `sqlite3`
from a loadable extension, gated; then a `plraku` function passing its own
`pg_regress` suite.

## 12. Places to put it

A list of 2026-10-08: hosts where a property the engine already has is the
reason to choose it. Those properties are one file from `--exe --standalone`,
a start-up of a few milliseconds (§11), the C API, Raku.js, grammars that
report the line, column and rule where a parse fails, and exact `Rat` and
`Int` arithmetic. The spreadsheet formulas from the same list are built
([bindings/spreadsheets](../../../bindings/spreadsheets/README.md)). The
databases are §11, the sandboxed hosts wait for §6, and Node, Swift and
Android are in §7.

- **Slots that run any executable.** Each starts a program per event, which a
  standalone file that starts in milliseconds fits. Each is a cookbook recipe
  first.
  - An AWS Lambda custom runtime: a `bootstrap` that loops on the Runtime API,
    built with `--exe`.
  - Pandoc filters: the document tree arrives as JSON on stdin and goes back on
    stdout. A `Pandoc::Filter` module, and a filter that runs code blocks and
    pastes in their output.
  - `kubectl`, `git` and `gh` subcommands, Ansible modules and Terraform
    `external` data sources, all of which take an executable that reads
    arguments or JSON.
  - pre-commit hooks for `--fmt` and `--lint`.
  - `FROM scratch` images holding only the binary.
- **Grammars as the product.**
  - **Grammar → language server.** A user's grammar for their own format gives
    diagnostics (line, column, failing rule) in any LSP editor. The MCP
    `raku-parse` tool already reports the failure, and `rakupp --lsp` has the
    transport ([IDE-PLAN.md](IDE-PLAN.md)).
  - A grammar workbench in the browser: the tree as you type, the failing rule
    marked. It grows from `showcase/web/regex.html`.
  - Grammar → GBNF for llama.cpp's constrained decoding, so that a model's
    output parses with the same grammar. Speculative: only a subset of
    grammars would translate.
- **Exact arithmetic as the reason.**
  - Plain-text accounting: a Ledger/hledger journal parser and balance report
    on `Rat`, as a showcase.
  - A launcher calculator: a Raycast or Alfred extension, or a macOS Shortcuts
    action, where `1/3 + 1/6` is `0.5` and `2**200` has all its digits.
- **More hosts on the C API**, beyond §7's: Julia through `ccall`, with no
  compiled glue; TIC-80, the fantasy console that takes its languages as C
  modules (check the size with `--slim` first).
- **In the browser, with no server.**
  - A browser extension that puts a Run button on Raku code blocks (Rosetta
    Code, GitHub READMEs, Stack Overflow), with Raku.js as released.
  - Quarto takes any Jupyter kernel, so Raku in Quarto documents may need only
    docs and a test. An Obsidian plugin would need a small Raku.js wrapper.
  - A Unicode inspector: paste text, see its graphemes, codepoints, names and
    properties.
  - *Crafting Interpreters, Raku edition*: the showcase's Lisp, Forth,
    JavaScript, Perl and Python interpreters as a book whose every stage runs
    in the page.
- **Small hardware.** GPIO on a Raspberry Pi through NativeCall to libgpiod;
  an OpenWrt package for ARM and RISC-V routers.
- **After the sandbox (§6).** A grader image for courses and self-hosted
  judges (Raku Koans in [live/ADOPTIONS.md](../../../live/ADOPTIONS.md) has
  the test-driven shape already); a Discord or Matrix bot that runs snippets;
  user scripting inside other applications.

**The number:** each item picked up leaves one runnable recipe or showcase
whose output was captured from a real run, and its line moves to the plan
that built it.

---

*Keeping this current: when an item lands or is decided against, move it to the
plan that closed it and delete it here. When the next major is chosen, its
survivors become that version's section of VERSIONS.md.*
