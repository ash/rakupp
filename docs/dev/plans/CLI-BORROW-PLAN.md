# Plan: borrowed flags — what other toolchains ship that rakupp doesn't

**Status: IDEA BACKLOG, written 2026-08-26 — nothing scheduled, no code.**
A survey, not a commitment: each item names the toolchain it comes from,
what it would do here, and why it fits (or doesn't). Successor in spirit to
the completed [CLI-PLAN.md](CLI-PLAN.md), which set the rule this file
continues: Rakudo stays the compatibility reference, but flags are borrowed
from wherever they are good. Surveyed against `build-arm64/rakupp`
v3.7.0-60-gb9f5fba (2026-08-26); "not present" claims below were checked
against `--help` and `grep` of `src/` on that date, and rot as the tree
moves.

## Where we are — what needs no borrowing

The surface already covers most of what a survey of other toolchains turns
up, so future readers shouldn't re-propose: compile-check (`-c`, with the
undeclared-variable pass), `--lint`, a formatter design
([FMT-PLAN.md](FMT-PLAN.md), approved, code not started), a REPL, Pod
rendering (`--doc SRC`), syntax highlighting (`--highlight`), a wall-time
profiler (`--profile`, JSON option), three compile modes plus `--slim` and
`--exe-info`, a module installer/tester, the complete perl one-liner family
(`-n -p -a -F -0 -i -l`, clustering), two serve modes (`--mcp`,
`--jupyter`), AST/C++ inspection (`--ast`, `--cpp`), and precomp cache
controls. One borrow happened without being named: rustc's `-vV` verbose
version — `--version` already prints commit, build date, platform, and
compiler, which is the bug-report block that flag exists for (FFI backend
stays separate under `--ffi-info`). Added 2026-09-02, from
[issue #50](https://github.com/ash/rakupp/issues/50): `-q`/`--quiet` (pip,
cargo, apt — the near-universal spelling), one option every mode takes
that drops a mode's own narration (`Syntax OK`, `Compiled …`, the
installer's `already installed:`, the REPL banner) and never its product,
warnings or errors; it had existed for `--lint` alone.

## Debugging and introspection

- **`--repl-after`** (Python `-i`) — run the file, then drop into the REPL
  with the program's state still live. The single most-used debugging flag
  Python has, and cheap here because the REPL exists. Our `-i` is taken by
  in-place editing, so it needs the long spelling.
- **`--trace`** (bash `-x`, perl `-d:Trace`) — print each statement as it
  executes, with values. Nearly free in an interpreter that owns the eval
  loop, and Rakudo has nothing built-in (the ecosystem debugger is a
  module). Doubles as teaching material for the tour site.
- **`--stagestats`** (Rakudo's own flag, plus Python `-X importtime`) —
  parse/check/run phase timing and per-module load times. Pairs with the
  precomp cache: it would show a user exactly what precomp buys on their
  program. We time phases internally already; this is surfacing, not
  measuring.
- **`--profile=heap`** (Go pprof heap, valgrind massif) — a memory mode for
  the existing profiler: allocation counts and bytes by routine, peak RSS.
  The JSON-ratio campaign (392-byte `Value`, `std::map` Hash) was exactly
  the hunt this shortens. Related small item: the `--profile=FILE.json`
  format is our own (no `traceEvents`, checked) — emitting Chrome-trace or
  speedscope format instead gets free viewers for zero UI work.
- ~~**Backtrace verbosity control** (`RUST_BACKTRACE=0|1|full`)~~ — DONE
  2026-09-04 as `RAKUPP_BACKTRACE=0|short|full` plus Rakudo's
  `--ll-exception`, alongside the tracer itself
  ([BACKTRACE-PLAN.md](BACKTRACE-PLAN.md), issue #67).

## The developer loop

- **`--watch`** (node `--watch`, `cargo watch`, `dotnet watch`) — rerun on
  file change. Every mainstream toolchain shipped this in the last few
  years because it is cheap and constantly used. Composes with `-c` and
  `--lint` for a live check loop; needs kqueue/inotify or a portable
  mtime poll.
- **`--lint --fix`** (`cargo fix`, `clippy --fix`, `ruff --fix`) —
  auto-apply the mechanical lint fixes. Sequencing matters: the span-based
  rewrite machinery [FMT-PLAN.md](FMT-PLAN.md) settled on is exactly the
  infrastructure fix-application needs (classified spans + the semantic
  gate), so this comes after `--fmt` lands, not before.
- **`rakupp doc SYMBOL`** (`go doc fmt.Printf`, `perldoc -f`, `pydoc`) —
  offline lookup of a builtin/method from the terminal: `rakupp doc trim`.
  The content exists — REFERENCE.md carries 168 subs / 477 methods — so
  this is an index + subcommand, and the subcommand spelling avoids
  colliding with `--doc SRC` (render Pod).
- **`--completions=zsh|bash|fish`** (rustup, deno, pip) — generate shell
  completion scripts. The CLI has enough subcommands and long flags now to
  deserve it, and it is a one-time generator, not a maintenance surface.
- **`rakupp test .`** (`cargo test`, `go test ./...`, `zef test .`) — the
  installer's `test` takes store module names; accepting a path to a dist
  checkout (deps from the store, suite from the working tree) closes the
  local development loop the 200-green campaign lives in.

## Diagnostics as data / editor integration

- **`--json` diagnostics** (rustc `--error-format=json`, gcc
  `-fdiagnostics-format=json`) — machine-readable `-c`/`--lint` output:
  file, line, span, severity, message, one object per finding. Checked: no
  such output exists today. This is the enabling step for any editor
  integration without committing to a protocol, and the natural first
  consumer is a VS Code extension somebody else can write.
- **`--lsp`** (deno lsp, rust-analyzer, gopls) — the bigger version of the
  same idea and the natural third serve mode next to `--mcp` and
  `--jupyter`. Deno's precedent is the right one: no separate install, the
  toolchain binary *is* the language server. Diagnostics from `-c`/`--lint`,
  hover from the reference data, semantic tokens from `--highlight`'s
  classifier, formatting from `--fmt` once it exists. A plan file of its
  own if picked up.
- **`--explain X::Foo`** (rustc `--explain E0382`, Elm's error index) — a
  longer-form explanation of an error class: what it means, a minimal
  failing example, the fixed version. The rules/spec sites already maintain
  oracle-verified examples, which is precisely the content this flag wants;
  the work is routing, not writing.

## Safety and reproducibility

- **Capability sandbox: `--allow-net`, `--allow-read=PATH`, `--allow-run`**
  (Deno; spiritual heir of perl `-T` taint mode) — deny-by-default I/O
  permissions. The most differentiating item in this file: rakupp owns
  every syscall site (open, spawn, socket, NativeCall), which is what makes
  this feasible where Rakudo cannot easily follow. Two in-house consumers
  exist on day one: the raku.online live runner, and `rakupp install` /
  `rakupp test`, which execute arbitrary dist code from the network.
  Deserves its own plan file if picked up; the design questions (is
  NativeCall simply off under a sandbox? is `--allow-read` prefix-based?)
  are real.
- **`--seed=N`** (rspec `--seed`, `PYTHONHASHSEED`) — pin `rand`/`pick`/
  `roll`/hash iteration order for a run, print the seed on failure.
  Directly useful for reproducing flaky rakugrid and Roast failures; small.
- **`--race`-style diagnostics** (Go `-race`) — flag unsynchronized shared
  access now that v3 is parallel-by-default
  ([PARALLEL-PLAN.md](PARALLEL-PLAN.md)). The ambitious one: a real
  happens-before checker is a project, not a task. Parked here so the idea
  has an address.

## Small compatibility niceties

- **`--env-file=.env`** (node `--env-file`, deno) — load environment
  variables from a file before running. Trivial and now expected.
- **`-u` unbuffered stdout** (python) — matters in pipelines; a setup line.
  (Long spelling too; `-u` is free today.)
- **`--stack-size=N`** (node) — a user-visible knob for the deep-recursion
  ceiling we have met more than once (the wasm ~200-frame cap, the
  `onBigStack` work). Honest to expose rather than tune silently.
- **`RAKUPP_OPT`** (`PERL5OPT`, `GOFLAGS`, `NODE_OPTIONS`) — default
  options injected from the environment, prepended to argv. Checked: not
  present today.
- **`--color=auto|never|always` + `NO_COLOR`** (gcc/clang/cargo, the
  no-color.org convention) — checked: `NO_COLOR` appears nowhere in `src/`
  today, so `--highlight --ansi`, error output, and the REPL don't honor
  it. Standard hygiene.
- **perl `-x`** — skip leading text until the shebang line, for programs
  embedded in mail or docs. Tiny, and it completes the perl heritage set
  (`-n -p -a -F -0 -i -l` are all in).
- **Inline script dependencies** (PEP 723, `uv run`, `cargo script`) — a
  comment header in a single-file script declaring its dists, auto-resolved
  through the installer on first run. Fits the one-file script culture the
  showcases live in, but it is a semantic decision (a new header format,
  network at run time), not just a flag.

## The shortlist

If five get picked: **`--watch`**, **`--repl-after`**, and **`--seed`** are
the cheap, immediately-used ones; **`--json` diagnostics** is the strategic
one because it opens the editor door and stages `--lsp`; and the **Deno
sandbox** is the one worth a real plan file — the only item here that would
be a capability Rakudo doesn't have, with the installer/test runner as its
first user inside this repo.

## Deliberate non-borrows

- **A DAP debugger** (node `--inspect`) — the replay-debugger idea for the
  playground was already examined and parked (probes: `callframe` works,
  `MY::` comes back empty, `--dump-ast` carries no line numbers, ~20×
  probe cost). Don't re-open it through the back door as a flag.
- **Lockfiles / `--frozen`** (cargo, npm, uv) — machinery the ecosystem's
  size doesn't justify yet; `rakupp install --check` covers the integrity
  half.
- **`-O` semantic optimization levels for the interpreter** (gcc) — `-O`
  already means something here (the `--exe` codegen passes); overloading it
  with interpreter-mode behavior differences would sell surprises, and
  Rakudo's own `--optimize` history is not encouraging.
- **Warning-filter languages** (Python `-W error::DeprecationWarning`) —
  fine-grained warning routing is a lot of surface for little demand; a
  single promote-warnings-to-errors switch is the most that seems earned,
  and even that can wait for someone to ask.
