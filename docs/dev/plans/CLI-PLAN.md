# Plan: the command line — a real option surface

**Status: in progress — steps 1–4 landed 2026-08-07** (the parser, MAIN
usage/#17, `-M`/`-m`, and the `-a`/`-F`/`-l`/`-0777`/`-0` family — the last
with byte-identical perl differentials for `-lane` and `-0777 -p` in the
suite). Remaining: `-i[.ext]` (step 5), `--profile` (step 6), guide/CLI.md
(step 7). Two documented Raku-flavored divergences from perl: `-F STR` is a
literal separator with `\t`-escapes (`-F/…/` is the regex form — perl regex
syntax is not Raku regex syntax), and records arrive chomped. Step 2 (MAIN
usage fidelity, the section below): all four fixes in, six oracle cases
byte-identical to Rakudo including streams and exit codes,
`t/regression/main-usage-17.raku` passes under both engines, Roast
197,091 (baseline band), local suite 356/356. Close issue #17 on push. 40 CLI goldens pin every pre-existing spelling in `t/run.raku`
(written first, against the old binary); `main.cpp`'s argv[1] cascade is now
one two-phase scan, and the measured breakages below all pass, plus `-v`,
`--target=parse|ast`, position-independent `-h`/`-V`, mode-conflict errors,
and `--` ending the options phase. Suite 355/355 both before and after.
The smallest of the three **v3.0.0**
pillars ([VERSIONS.md](VERSIONS.md); the others are
[PARALLEL-PLAN.md](PARALLEL-PLAN.md) and [LTM-PLAN.md](LTM-PLAN.md)).
Goal: make `rakupp`'s command line behave like a grown compiler's — options
composable and position-independent — complete the Perl one-liner family,
including in-place file editing (`-i`), and ship a first profiler
(`--profile`). Rakudo stays the compatibility reference, but flags are
borrowed from wherever they are good (Perl, Python, Ruby, GCC-style
toolchains).

Measurements and behaviors in this file: 2026-08-07, `build-arm64/rakupp`
at v2.0.0, Rakudo 2026.07, Perl 5 (`/opt/local/bin/perl`).

## Where we are

What exists today: `-e` (with attached form and `-ne`/`-pe` clusters), `-n`,
`-p`, `-I`, `--doc`, `-` (program from stdin), a REPL, and a family of
*modes* — `--bundle`/`--aot`/`--exe` (with `-o`, `-O`), `--lint`, `-c`,
`--ast`, `--ast-roundtrip`, `--cpp`, `--highlight`, `--precomp-*`,
`--help`/`-h`, `--version`/`-V`, `--ffi-info`.

The structural weakness: `main.cpp` dispatches modes by testing `argv[1]`
literally, so every mode flag is recognized **only in position 1**, and each
mode re-implements its own little argument scan. Measured today:

```
rakupp --lint file.raku            # works
rakupp -I lib --lint file.raku     # "Illegal option --lint"
rakupp -c -I lib -e 'say 1'        # "Cannot open file: -I"
rakupp -v                          # "Illegal option -v"   (only -V/--version work)
```

None of these should fail. There is also no `-M` (load a module before the
program), no `-i` (in-place edit), no `-a`/`-F` (autosplit) — the Perl
one-liner family is half-finished at `-n`/`-p`.

## Step 1 — one option parser

Replace the `argv[1]` cascade with a single two-phase scan shared by every
mode:

1. **Options phase** — consume flags in any order until the first
   non-option token (the program file, `-e CODE`, or `-`). `--` ends the
   options phase explicitly. Options are described by one static table
   (name, arg-shape, handler) from which `--help` is generated, so the help
   text can never drift from reality again.
2. **Program phase** — everything after the program token is the program's
   `@*ARGS`, completely untouched (this already works and must not change:
   `rakupp script.raku --lint` passes `--lint` to the script).

Modes (`--exe`, `--lint`, `--ast`, …) become values of one `mode` field set
during the options phase; mutually-exclusive combinations get a real error
message instead of silent misparsing.

Compatibility guarantees — everything that works today keeps working, with
tests pinning each spelling:

- `-e CODE`, `-eCODE`, `-ne'...'`, `-pe'...'`, `-np`
- `-I path`, `-Ipath`, repeatable
- `-o out`, `-oout`, `-O`/`-O2`/`-Os` in compile modes
- bare `rakupp --ansi FILE` as `--highlight --ansi FILE`
- `rakupp - args` and stdin-as-program
- the "Illegal option" banner for unknown flags (Rakudo prints it and exits
  0; we keep matching that, bug-compatibly)

## Step 2 — the Perl line-loop family, completed

The wrapper today is: `-n`/`-p` wrap the program in
`for lines() -> $_ is copy { … }`. `lines()` merges all files in `@*ARGS`
(verified: two files read in sequence), which is right for `-n`/`-p` but has
no per-file boundaries — and `-i` needs them.

### `-i[.ext]` — in-place editing

Perl semantics, which are the reference here:

- Only meaningful with `-n`/`-p`; requires file arguments (refuse with a
  clear error when `@*ARGS` is empty — no silent stdin fallback).
- For each file: read it, run the loop body with output redirected to a
  temporary file *in the same directory*, then rename over the original.
  Preserve permissions. `say`/`print` inside the loop go to the new file.
- `-i.bak` (extension **glued**, as in Perl — `-i .bak` means "no backup,
  file .bak") first renames the original to `file.bak`.
- A file that fails to open is reported and skipped; the exit code reflects
  that a file was skipped.

Implementation: `-i` switches the wrapper from one merged `lines()` loop to a
per-file outer loop generated in Raku (open temp handle, `$*OUT`
temporarily rebound, rename at the end). The current-file name is exposed as
`$*ARGFILES`-style state so scripts can use it. Doing it in the generated
wrapper (not in C++) keeps the semantics visible and testable with `--ast`.

### `-a` and `-F` — autosplit

With `-n`/`-p`, `-a` adds `my @F = .words;` at the top of the loop body
(Perl's whitespace split). `-F STR` changes the separator to a literal
string; `-F/RE/` to a regex (`my @F = $_.split(/RE/);`). `@F` is a plain
lexical inside the loop — same muscle memory as Perl, no new global.

### `-0[octal]` — record separator

Staged: `-0777` (slurp mode — the loop body runs once per *file* with the
whole file in `$_`; this is the common one and composes with `-i`) and plain
`-0` (NUL-separated records, the `find -print0` partner). Paragraph mode
(`-00`) and arbitrary octal values are deferred until someone needs them.

### `-l`

In Perl, `-l` chomps input and appends the separator on output. Our loop
already behaves that way (`lines()` chomps; `-p` prints with `.say`), so
`-l` is accepted as a no-op for muscle memory and documented as such.

## Step 3 — borrowed from other compilers

Each with its origin and a decision:

| Flag | From | Decision |
|---|---|---|
| `-M Module` (repeatable) | Rakudo, Perl | **Add.** Prepends `use Module;` to the source — on the same first line, semicolon-joined, so line numbers in errors don't shift. `-M Module=arg,arg` (Perl's import-list form) deferred. |
| `-m module` | Rakudo | Alias of `-M` (Rakudo treats them nearly the same; we don't have `no`-style unimport anyway). |
| `-v` | Perl, Rakudo | **Add** as alias of `--version`. |
| `--target=parse\|ast` | Rakudo | **Add** as an alias of `-c` / `--ast` — pure muscle-memory compatibility, three lines. |
| `-r lib` | Ruby | Not added — it is `-M` under another name. |
| `-s` (rudimentary switch parsing) | Perl | **Rejected** — `MAIN` already parses named args properly; `-s` would be a worse duplicate. |
| `-S` (search `PATH` for the script) | Perl | **Rejected** — shebangs and `PATH` handle this. |
| `python -m` (run installed module as a script) | Python | **Deferred** — needs a "module with `MAIN`" convention; interesting for `rakupp -m zef …` one day, not now. |
| `--watch` (rerun on file change) | Node, Deno | **Deferred** — attractive for the playground/dev loop, but it is a feature, not a flag; needs its own small design (what to watch, how to debounce). |
| `--profile` | Rakudo | **Add — in scope for v3.0.0** (promoted from deferred after the off-cost measurement; see Step 5). |

Also in scope for this step, since the parser rewrite makes them nearly free:

- `-h`/`--help` and `-v`/`-V`/`--version` recognized in any position.
- `-c` composing with `-I` and `-M` (syntax-check a file that uses local
  modules — today impossible).
- `--lint` composing with `-I` (same reason; measured broken today).

## Step 4 — MAIN usage fidelity ([issue #17](https://github.com/ash/rakupp/issues/17))

The other half of the command line is the one rakupp *generates* — the
`Usage:` text a script's users see. The first inbound report on it,
[#17](https://github.com/ash/rakupp/issues/17), reproduces on v2.0.0 and
turns out to be two divergences in the usage builder (both verified against
Rakudo 2026.07):

```raku
sub MAIN(
    Str :$foo is required,  #= some stuff
    Bool :$verbose = False, #= verbose mode
) { … }
```

| | rakupp v2.0.0 | Rakudo |
|---|---|---|
| usage line | `[--foo=<Str>] [--verbose] -- some stuff verbose mode` | `--foo=<Str> [--verbose]` |

1. **The reported bug**: with no `#|` doc on the MAIN candidate itself, the
   *parameters'* `#=` descriptions are concatenated into the ` -- …` suffix.
   That suffix is real Rakudo behavior — but only for the sub's own leading
   `#|` doc (verified: with `#| frobnicate the widget` present, both engines
   print exactly ` -- frobnicate the widget`). The fix: the suffix comes
   from the candidate's `#|` doc or nothing; parameter docs belong only in
   the per-option lines below, where both engines already agree.
2. **Found while reproducing**: a named parameter marked `is required`
   prints bracketed as optional (`[--foo=<Str>]`); Rakudo prints it without
   brackets. Requiredness must reach the usage renderer.

Acceptance: the issue's example byte-identical to Rakudo with and without a
`#|` line, plus the same check for `is required` positionals and a
`--foo=<Str> is required` + default-carrying mix; close #17 when it ships.
Independent of the option-parser work — this lives in the usage generator,
not `main.cpp` — so it can land any time.

**Landed 2026-08-07** — and reproducing it surfaced two more divergences,
both fixed and oracle-verified in the same pass: a `#=` on the line where
the signature *closes* documents the ROUTINE (`sub MAIN(Int $x) {} #= doc`
→ `<x> -- doc`, no option entry — the parser now defers param-doc claims
until the closing line is known, since a comment runs to end-of-line a
close-line `#=` is necessarily after the `)`); and an explicit `--help`
prints the usage to *stdout* with exit 0, where a failed dispatch (bare
`-h` included) keeps stderr/exit 2. `is required` now sets the param's
required flag everywhere (previously only the `!` marker did). Regression:
`t/regression/main-usage-17.raku`, byte-exact on both engines. Left open
(pre-existing, confirmed against the pre-change binary): parameter-level
`.WHY` returns Nil — the doc is parsed and stored but not plumbed into
`Parameter` introspection.

## Step 5 — `--profile`, a first profiler

Promoted into scope (2026-08-07) because the only reason to defer it — fear
of a hot-path tax — was measured away. The prototype experiment: entry+exit
hook branches (`if (profiling) …` + an RAII exit guard) on `callCallableRaw`
and `invokeMethod`, disabled at runtime, cost **nothing measurable** — fib
(~1.66M recursive calls) 724 → 724 ms, a 1M-method-call bench 1130 → 1117 ms,
all deltas inside the ±1.5% run-to-run noise band with no directional bias.
Even with the hooks *firing* into a no-op counter, fib moved +0.7%. So the
hooks ship always-compiled, gated by a runtime flag — no build variant, no
`#ifdef`, no perf-guard risk.

One portability lesson the prototype taught the hard way: it used
`__attribute__((noinline))` and `__builtin_expect`, and when those lines
accidentally reached CI they broke the MSVC build outright (run
31168875033, 2026-08-07). The real hooks use portable spellings only —
a plain `if` on the flag (the branch predictor needs no hint at this cost
level, per the measurement) and MSVC-compatible attributes where one is
truly needed.

Scope for the first version, deliberately modest:

- **Instrumented, routine-level**: the two hooks above (user subs/blocks via
  `callCallableRaw`, methods via `invokeMethod`). Per-routine: call count,
  inclusive and exclusive wall time, maintained on a shadow stack of
  (routine, start, child-time) entries — the interpreter already tracks
  frames well enough to serve `callframe(N)`, so naming comes free
  (routine name + `file:line` from the declaration).
- **Output**: `--profile` prints a table to stderr at exit, sorted by
  exclusive time; `--profile=FILE` writes it to a file, and a `.json`
  extension switches to a machine-readable dump (Rakudo's
  extension-selects-format convention). No HTML, no flamegraphs, no heap
  profiling in v1 — each is listed as a possible follow-up, not scope.
- **Boundaries stated up front**: builtins are attributed to their *caller*
  (we hook user-code routine entry, not the builtin dispatch chain);
  `--exe`-compiled binaries are out of scope (no interpreter inside — use
  the OS profiler there); with the parallel default landing in the same
  release, per-thread counters merge at exit, and the profiler must be
  TSan-clean under [PARALLEL-PLAN.md](PARALLEL-PLAN.md)'s stress suite.
- **The gate it must pass**: `perf-guard --check` unchanged with profiling
  off — which the prototype already demonstrated — and a regression test
  that a profiled run's *output* (the program's own stdout) is byte-identical
  to an unprofiled run.

## Documentation

- A new **guide/CLI.md** (created in step 7) — the full option reference
  plus a one-liner cookbook with Perl↔rakupp translations (`perl -pi.bak -e
  's/a/b/'` ↔ `rakupp -pi.bak -e 's/a/b/'`, autosplit examples, `-0777`
  slurp tricks), and a `--profile` walkthrough explaining inclusive vs
  exclusive time and the builtins-attributed-to-caller rule. The cookbook
  doubles as the acceptance-test corpus.
- `--help` regenerated from the option table.
- README quickstart gains the one-liner examples; FEATURES.md gets a short
  "command line" row. Concepts that need a sentence of explanation in the
  guide: what "in-place" actually does (temp file + rename, not seek+write),
  and why `-i` without files is refused.

## Tests and gates

- A new `t/run.raku` section drives the built binary (the harness already
  runs `$*EXECUTABLE` with args) through every flag spelling above with
  golden outputs — including the `-i` rename/backup/permissions behavior in
  a scratch directory, and `-i` behavior when one of three files is
  unreadable.
- Differential tests: the same one-liners run under `perl` (for `-i`, `-a`,
  `-F`, `-0777`) and under `raku` (for `-M`, `-c`) where the reference
  engine is present; skipped otherwise.
- Full Roast + battery unchanged. The only interpreter-adjacent change is
  the `-n`/`-p` wrapper source; its current form is pinned by a golden
  first.

## Order of work

1. Option-table parser + position independence + `-v`, `--target` aliases
   (pure `main.cpp`; no interpreter changes). Goldens for every existing
   spelling *first*, then the refactor against them.
2. MAIN usage fidelity — [#17](https://github.com/ash/rakupp/issues/17) and
   the `is required` bracket bug (the usage generator; independent of the
   parser work, can land any time).
3. `-M`/`-m`.
4. `-a`, `-F`, `-l`, `-0777`/`-0` (wrapper generation only).
5. `-i[.ext]` (temp-file plumbing, `$*OUT` rebinding, rename/backup).
6. `--profile` (the shadow stack, the table/JSON writers, the parallel-mode
   merge).
7. guide/CLI.md + README + help text.

Steps 1–4 are each a day-scale item; steps 5 and 6 are the substantial
ones. This plan has no dependency on the parallel or LTM work (except the
profiler's thread-merge detail) and can land in v2.x minor releases ahead
of the v3.0.0 tag, per the [VERSIONS.md](VERSIONS.md) convention.
