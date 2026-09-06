# Plan: borrowed flags — what other toolchains ship that rakupp doesn't

**Status: IN PROGRESS — the CLI campaign, started 2026-09-06.** Working
through this file simplest-first, each flag with its suite pins and its
[guide/CLI.md](../../guide/CLI.md) section landing together. Batch 1
(2026-09-06): `-x`, `--seed[=N]`, `--stack-size=N`, `--env-file=FILE`,
`RAKUPP_OPT`, `--color=WHEN` (which made the REPL honour `NO_COLOR` and
`RAKUPP_COLOR` too) — six flags, 34 new checks in `t/run.raku`'s CLI
section. Batch 2 (same day): `--json` for `-c`/`--lint`, `--stagestats`,
`--trace`, `--repl-after`, `--completions=SHELL` — five more, 25 checks.
Batch 3 (same day): `--watch` and `rakupp doc SYMBOL` — 10 checks.
Thirteen items landed in one day; what remains is the four that are
projects rather than flags (the sandbox, `--profile=heap`, `--race`,
inline script dependencies) plus `--explain` (content routing) and
`--lint --fix` (sequenced after `--fmt`).
Two entries closed without code: `-u` (stdout is already
unbuffered here, issue #51) and `rakupp test .` (the installer took a path
before this file was written); `--lsp` had landed before the campaign.
Items marked ~~struck~~ below are done; the rest is still the backlog it
was.
Originally written 2026-08-26 as a survey, not a commitment: each item
names the toolchain it comes from,
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

- ~~**`--repl-after`**~~ (Python `-i`) — DONE 2026-09-06. The program runs
  through the REPL's own evalString path (so its AST stays alive behind
  its subs — `rakuppRunOn` would free it with the local `Program`), MAIN
  is dispatched by a new `replRunMain`, END blocks wait for the session's
  end, `exit`/`die` are reported and the prompt still opens. Opens the
  session even off a terminal: the flag is the request, and it is how the
  suite drives it.
- ~~**`--trace`**~~ (bash `-x`, perl `-d:Trace`) — DONE 2026-09-06, the
  line-only form: `[trace] file:line  source`, one global-bool test per
  statement in `exec` (the profiler's measured-free cost). Follows calls
  into modules via the live frame's `declFile`; a `use` is traced once,
  when it loads (the hoisting passes execute every `use` first). A routine
  whose last statement is `return` takes a tail-return fast path that never
  reaches `exec`, so the hook sits there too (two sites). Values
  stay a follow-up: printing them means deciding what to gist, and how
  much. The hook's cost, measured (2026-09-06, load 6 on 8 cores, so an
  interleaved A/B of the same tree with and without the `exec` hook,
  min-of-15): asg −0.2%, loopsum −1.6%, fib +1.5% — inside the noise, no
  direction. `perf-guard --check` itself said INCONCLUSIVE that day (every
  kernel +20–55% against baseline, run-to-run span above its 5% gate:
  another session's `doc-examples-diff` run and an 11-hour `Crypt::Random`
  probe each held a core) — rerun it on an idle box before a release.
- ~~**`--stagestats`**~~ (Rakudo's own flag) — DONE 2026-09-06: precomp
  (hit/miss), lex, parse, check, run, then every first module load with
  nesting and inclusive time. A run with `use Outer` (which uses `Inner`)
  reads 97 ms for Outer of which 40 is Inner — module parse is where a
  cold start goes, which is what `--precomp-modules=on` is for.
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

- ~~**`--watch`**~~ (node `--watch`, `cargo watch`, `dotnet watch`) — DONE
  2026-09-06 as a 300 ms poll (the program file by content hash, the -I /
  lib / RAKULIB trees by mtime+size), each run a fresh child process with
  the same command line minus the flag. Run, `-c` and `--lint` take it.
  The watched file disappearing ends the loop — which is how the suite's
  Proc::Async driver stops it.
- **`--lint --fix`** (`cargo fix`, `clippy --fix`, `ruff --fix`) —
  auto-apply the mechanical lint fixes. Sequencing matters: the span-based
  rewrite machinery [FMT-PLAN.md](FMT-PLAN.md) settled on is exactly the
  infrastructure fix-application needs (classified spans + the semantic
  gate), so this comes after `--fmt` lands, not before.
- ~~**`rakupp doc SYMBOL`**~~ (`go doc fmt.Printf`, `perldoc -f`, `pydoc`)
  — DONE 2026-09-06 as `tools/doc.raku`, dispatched like the installer
  and shipped with it (plus REFERENCE.md and FEATURES.md under
  share/rakupp/docs). No index: a scan per lookup over two small files,
  ranked table row › code › prose, heading trail per hit; `--code`,
  `--all`, `.name` = `name`, punctuation symbols as substrings.
- ~~**`--completions=zsh|bash|fish`**~~ (rustup, deno, pip) — DONE
  2026-09-06, generated from one `kFlagDocs` table in `main.cpp` (a new
  flag is a one-line entry there). bash and zsh outputs are parsed by
  their shells in the suite; fish is checked textually.
- ~~**`rakupp test .`**~~ (`cargo test`, `go test ./...`, `zef test .`) —
  ALREADY THERE: the installer takes a path argument (`.` or `./dist`) for
  every command, `test` included, deps from the store and the suite from
  the working tree. Was in before this file and missed by the survey.

## Diagnostics as data / editor integration

- ~~**`--json` diagnostics**~~ — DONE 2026-09-06 for `-c` and `--lint`:
  file, line, severity, rule, message; `parse-error` and
  `undeclared-variable` as rules for the two compile failures; `[]` for a
  clean check. No column — the parser records lines only, so a span waits
  on the parser.
- ~~**`--lsp`**~~ — LANDED before this campaign (diagnostics only, see the
  `--lsp` help entry); hover/semantic tokens/formatting remain its
  follow-ups.
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
- ~~**`--seed=N`**~~ (rspec `--seed`, `PYTHONHASHSEED`) — DONE 2026-09-06.
  `--seed=N` seeds every thread's first draw (the program's thread gets N,
  later threads N+1, N+2… so workers draw distinct sequences); bare
  `--seed` picks one and prints `rakupp: --seed=N` to stderr, the rspec
  move. Hash iteration order needed no pinning — it is insertion order
  here and never varied. `srand` still wins after it.
- **`--race`-style diagnostics** (Go `-race`) — flag unsynchronized shared
  access now that v3 is parallel-by-default
  ([PARALLEL-PLAN.md](PARALLEL-PLAN.md)). The ambitious one: a real
  happens-before checker is a project, not a task. Parked here so the idea
  has an address.

## Small compatibility niceties

- ~~**`--env-file=.env`**~~ (node `--env-file`, deno) — DONE 2026-09-06.
  dotenv's grammar (comments, `export`, both quote styles, trailing
  comments); the shell's own variables win, node's rule; applied where the
  option is read, so `RAKULIB` from the file reaches the loader.
- ~~**`-u` unbuffered stdout**~~ (python) — NO FLAG NEEDED: `$*OUT` has
  been unbuffered since issue #51 (out-buffer 0, Rakudo's behaviour), so a
  `say` reaches a pipe as it is written. Documented in the guide instead.
- ~~**`--stack-size=N`**~~ (node) — DONE 2026-09-06. `N[K|M|G]`, a bare
  number is MiB (node's KB would make `64` unrunnable), from 1M; the
  recursion guard already reads the real thread size, so the ceiling
  follows automatically (64M ≈ 1,800 frames, 1G ≈ 30,000). A size the OS
  refuses is reported and the default used.
- ~~**`RAKUPP_OPT`**~~ (`PERL5OPT`, `GOFLAGS`, `NODE_OPTIONS`) — DONE
  2026-09-06. Prepended before the option scan; options only — `-e`, `-`,
  `--`, a file, a `-ne` cluster are refused by name, so the environment can
  never replace what runs.
- ~~**`--color=auto|never|always` + `NO_COLOR`**~~ — DONE 2026-09-06. The
  flag sets the `RAKUPP_COLOR` knob the backtrace renderer already read,
  and the REPL (prompt, input highlighting, echo, errors) now reads the
  same knob and `NO_COLOR`. `--highlight --ansi` is left alone: an explicit
  format request is not a colour preference.
- ~~**perl `-x`**~~ — DONE 2026-09-06, the perl heritage set is complete.
  One divergence: skipped lines are blanked rather than removed, so error
  line numbers match the file as an editor shows it.
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
