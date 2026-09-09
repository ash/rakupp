# The command line

Everything `rakupp` accepts on the command line: running programs, the
perl-style one-liner family (including in-place editing), module preloading,
the environment and reproducibility knobs (`--env-file`, `RAKUPP_OPT`,
`--seed`, `--stack-size`), what a run did as it did it (`--stagestats`,
`--trace`, `--repl-after`), the developer loop (`--watch`, `rakupp doc`,
shell completion), the profiler, and the inspection/compile modes. Mode flags
(`-c`, `--lint`, `--exe`, …), `-I`, `-M` and the run knobs are
position-independent and composable — `rakupp -I lib --lint prog.raku` and
`rakupp --lint -I lib prog.raku` are the same command — and everything after
the program token belongs to the program. The perl line-loop family
(`-n -p -a -l -0 -i -F`) is the exception: it is recognized only *before* a
mode flag, so `rakupp -n -c prog.raku` works while `rakupp -c -n prog.raku`
answers `Illegal option -n`. The refusal exits 0 and does nothing, so a
scripted `rakupp --lint -a f.raku` passes while analysing nothing — put the
family first. `-x` composes in either order.

```
rakupp prog.raku --lint      # --lint is in prog.raku's @*ARGS, not ours
```

A bare `--` ends option parsing early (`rakupp -- -strange-name.raku`).
Unknown options print `Illegal option …` and Rakudo's usage line, and exit 0, for
compatibility.

## Running programs

| Form | Meaning |
|---|---|
| `rakupp FILE ARGS…` | run a file; `ARGS` land in `@*ARGS` |
| `rakupp -e 'CODE' ARGS…` | one-liner (`-e'CODE'` glued also works) |
| `rakupp - ARGS…` | program text from stdin |
| `… \| rakupp` | same, implicit — for *running*: `-c` and `--lint` want the input named (`rakupp -c -`), and a bare `… \| rakupp -c` is a usage error, exit 4 |
| `rakupp` | the REPL, at a terminal |

- **A single-dash long option is accepted as a typo.** `-lint`, `-exe`,
  `-color=never`, `-env-file=x.env` and the other long names are unambiguous —
  none is a valid short cluster — so rakupp takes them, at any option position
  and with a `=VALUE`, and says so on stderr:
  `note: treating '-lint' as '--lint'`. The courtesy stops where the options
  stop: at the program file, at `--`, and at `-e`, after which the tokens are
  the program's.
- `-I <path>` — add a module search directory (repeatable, `-Ipath` works).
- `-M <module>` — load a module before the program runs (repeatable;
  `-MFoo` glued and `-m` both work — `-m` is a Perl-ism Rakudo rejects).
  It is not applied to a REPL session: `rakupp -M Foo` at a terminal opens a
  session without Foo. `--mcp` and `--jupyter` do preload.
  The program behaves as if it began with `use Foo;` *on its own first
  line*, so error line numbers do not shift.
- `-x` — perl's flag: the program starts at the first line that begins
  with `#!` and names `raku`; whatever precedes it (a mail header, the prose
  of a document the script is embedded in) is not code. A file with no such
  line is refused (`No Raku script found in input`, exit 2), and so is `-x`
  with `-e`. One divergence from perl: the skipped lines are blanked, not
  removed, so an error's line number still matches the file as an editor
  shows it (perl counts from the `#!` line). Clusters like the rest of the
  perl family (`-nx`). `-x` itself composes with `-c` and the other source
  modes in either order; the rest of the family must precede them (see above).
- `--env-file=FILE` — load `KEY=VALUE` lines into the environment before
  anything runs; see [The environment](#the-environment) below.
- `--seed[=N]`, `--stack-size=N` — pin the random generator, size the
  recursion ceiling; see [Pinning a run](#pinning-a-run---seed-and---stack-size).
- `--color=auto|always|never` — ANSI colour on stderr and in the REPL; see
  [When something dies](#when-something-dies).
- `--stagestats`, `--trace`, `--repl-after` — what a run did, as it did it;
  see [Inside a run](#inside-a-run---stagestats---trace-and---repl-after).
- `--completions=bash|zsh|fish` — a completion script for the shell; see
  [Shell completion](#shell-completion).
- `--watch` — rerun on change; see [The developer loop](#the-developer-loop---watch).
- `rakupp doc SYMBOL` — look a builtin, method or operator up offline; see
  [Looking things up](#looking-things-up-rakupp-doc).
- `--doc` — after the run, render the program's POD to stdout.
- `-q` / `--quiet` — drop what a mode says about itself; see
  [Quiet](#quiet-mode) below.
- `-v` / `-V` / `--version`, `-h` / `--help` — from any position.
  `--version` reports the release, the Raku version implemented, and the
  build's own identity: the `git describe` commit it came from, the build
  date, the platform it targets and the compiler that made it. Quote it
  whole in a bug report.

### Quiet mode

`-q` (or `--quiet`) is one option, taken by every mode, before or after
its command. It drops the lines a mode prints *about itself* — progress,
success, banners — and never a mode's product, its warnings or its errors.
A quiet run that succeeds prints nothing; one that fails prints what it
always did.

| Mode | What `-q` drops |
|---|---|
| `install`, `reinstall`, `uninstall`, `test` | the plan, progress, `already installed:`, `provided by rakupp:`, `done:`, and the per-dist detail under `--list`'s identity lines — not warnings, refusals, failures, the identity lines themselves, or the `--check` and `--dry-run` reports |
| `-c` | `Syntax OK` (the exit code is the verdict) |
| `--lint` | the summary line; findings stay |
| `--exe`, `--aot`, `--bundle` | `Compiled …` and the embedded-module list; a module that could *not* be embedded is still reported |
| the REPL, `--mcp` | the banner |
| `--jupyter-install`, `--precomp-clean`, `--precomp-*=on\|off`, `--ast-roundtrip` | the success line |
| everything else | nothing — accepted, no effect |

## The one-liner family

The perl flags, with Raku semantics where the two disagree (each
divergence listed below):

| Flag | Meaning |
|---|---|
| `-n` | run the body once per input line, in `$_` (files in `@*ARGS`, else stdin) |
| `-p` | `-n`, then print `$_` after each line |
| `-a` | autosplit the record into `@F` (implies `-n`) |
| `-F<sep>` | set the autosplit separator (implies `-a`) |
| `-l` | accepted as a no-op — lines already arrive chomped, `-p` re-adds the newline |
| `-0777` | slurp mode: one record per **file** |
| `-0` | NUL-separated records (the `find -print0` partner) |
| `-i[.ext]` | with `-n`/`-p`: edit the argument files in place |

Flags cluster exactly as in perl: `-lane`, `-pi.bak`, `-0777pe`. An `e`
ends the cluster and takes the code (`-ne'say $_'`); an `i` takes the rest
of the token as the backup extension — including perl's famous `-pie` trap,
where the `e` becomes the extension.

### Cookbook: perl ↔ rakupp

```bash
# uppercase every line
perl   -ne 'print uc'            f.txt
rakupp -ne 'say $_.uc'           f.txt

# second whitespace-separated field
perl   -lane 'print $F[1]'       f.txt
rakupp -lane 'say @F[1]'         f.txt

# CSV second column (literal separator)
perl   -F, -lane 'print $F[1]'   f.csv
rakupp -F, -ane  'say @F[1]'     f.csv

# split on a run of digits (regex separator — note the explicit /…/)
perl   -F'[0-9]+'  -lane 'print join "|", @F'   f.txt
rakupp '-F/\d+/'   -ane  'say @F.join("|")'     f.txt

# sed-style in-place edit, with a backup
perl   -pi.bak -e 's/foo/bar/'                  *.txt
rakupp -pi.bak -e '$_ = $_.subst("foo", "bar")' *.txt

# whole-file (slurp) transform, in place
perl   -0777 -pi -e 's/\n\n+/\n/g'                        f.md
rakupp -0777 -pi -e '$_ = $_.subst(/\n\n+/, "\n", :g)'    f.md

# consume find -print0
find . -name '*.log' -print0 | perl   -0ne 'print "$_\n"'
find . -name '*.log' -print0 | rakupp -0ne 'say $_'
```

Inside an `-i` loop the current file is available as `$*ARGV`:

```bash
rakupp -ni -e 'say $*ARGV.IO.basename ~ ": " ~ $_' *.conf
```

### Where we deliberately differ from perl

- **`-F STR` is a literal separator**, with `\t`-style escapes (`-F'\t'` is
  a TAB). Perl treats every `-F` value as a perl regex — but perl regex
  syntax is not Raku regex syntax (`[0-9]+` means something else here), so
  the regex form is the explicit `-F/…/` spelling, in Raku regex syntax.
- **Records arrive chomped** (Raku's `lines()` semantics); `-p` re-adds the
  terminator (`\n`, or `\0` under `-0`). This is what perl's `-l` adds, so
  `-l` is a no-op here.
- **`-i` refuses loudly where perl is silent**: no file arguments is an
  error (perl edits *stdin* with a warning); `-i` without `-n`/`-p` is an
  error (perl silently ignores it); a file that cannot be opened is
  reported, skipped — and the exit code is 1 (perl exits 0). `-0` (NUL
  records) does not combine with `-i` at all; use line mode or `-0777`.

## Pinning a run: `--seed` and `--stack-size`

**`--seed=N`** pins `rand`, `pick`, `roll` and everything else the random
generator feeds, so a run reproduces: the same seed gives the same
sequence, on the program's thread and on every `start` block's thread (each
thread draws its own sequence, seeded from `N` plus the order in which the
threads first asked). An explicit `srand` in the program still wins after
it, as it always did. The seed only covers the random generator — hash
iteration order is insertion order here and never varied, so there is
nothing else to pin.

A bare **`--seed`** picks a seed, announces it on stderr, and runs with it —
which is how a flaky test gets a number to rerun with:

```
$ rakupp --seed t/flaky.raku
rakupp: --seed=64757666
…
$ rakupp --seed=64757666 t/flaky.raku      # the same run
```

The seed is not Rakudo's: rakupp's generator is `erand48`, so `--seed=42`
here and `srand(42)` under Rakudo produce different numbers.

**`--stack-size=N[K|M|G]`** sizes the stack of the thread the program runs
on, which is the recursion ceiling: the interpreter stops a runaway
recursion with `X::Recursion` while a margin of stack remains, and the
depth it allows is a function of this size. The default is 1 GiB — about
50,000 nested Raku calls on this engine — and a bare number is MiB
(`--stack-size=64` is 64 MiB, about 3,000 calls), from `1M` upwards. The
depths for every mode live in one place,
[MEMORY.md](MEMORY.md#recursion-depth-in-practice). It
shapes a *run*: a program, the REPL, an `--mcp` or `--jupyter` session. A
size the OS refuses is reported and the program runs on the default
instead. (`RAKUPP_MAIN_THREAD=1`, the knob Cocoa GUI programs use, runs the
program on the process's own stack, and this flag does not apply.)

## The environment

**`--env-file=FILE`** loads `KEY=VALUE` lines into the environment before
anything runs — the option is applied where it is read, so a `RAKULIB` from
the file is seen by the module loader, and a later option sees the
variables too. The grammar is dotenv's, the useful subset: blank lines and
`#` comments are skipped, an `export ` prefix is allowed, a value may be
single-quoted (taken raw) or double-quoted (`\n`, `\t`, `\r` and `\\`
apply), and an unquoted value runs to the end of the line with a trailing
` # comment` dropped. **A variable the shell already has is kept** — the
file supplies defaults, it does not override the environment (node's rule
too). A file that cannot be opened is an error, as is a line that is not
`KEY=VALUE` (reported with its line number). Repeatable; later files do not
override earlier ones either, for the same reason.

```
$ cat .env
DATABASE_URL=sqlite://dev.db
export DEBUG=1
$ rakupp --env-file=.env app.raku
```

**`RAKUPP_OPT`** holds standing options, prepended to every `rakupp`
command line — `PERL5OPT`, `NODE_OPTIONS` and `GOFLAGS` are the same idea.
Tokens are whitespace-separated; a single- or double-quoted token keeps its
spaces. The variable may hold *options only*: a token that would start the
program (`-e`, `-`, `--`, a file name, a `-ne`-style cluster) is refused
with the token named, so an environment can shape how programs run but
never replace what runs. The command line's own options come after the
variable's, so they win where order matters (a later `-I` outranks an
earlier one); a mode from the variable and another from the command line
is the usual `Cannot combine` error.

```
$ export RAKUPP_OPT='-I lib -M Test::Helpers'
$ rakupp t/thing.raku        # runs as rakupp -I lib -M Test::Helpers t/thing.raku
```

`--watch` is accepted here too, and then applies to every run, which is rarely
what a shell profile wants. The watch loop's own child carries
`RAKUPP_WATCH_CHILD=1` and ignores the inherited flag, so a `--watch` in
`RAKUPP_OPT` re-runs your program rather than spawning watchers of watchers.

Output buffering needs no flag: `$*OUT` is unbuffered, as under Rakudo, so
a `say` reaches a pipe as it is written (python's `-u` is the default
here). An output-heavy program can buy the block buffer back with
`$*OUT.out-buffer = 65536`.

## Inside a run: `--stagestats`, `--trace` and `--repl-after`

**`--stagestats`** (Rakudo's flag) prints, on stderr once the run is over,
how long each phase took and how long every module load inside the run
took — nested where a module's own `use` loads another:

```
$ rakupp --stagestats -I lib app.raku
…the program's output…
Stage precomp (miss):     0.32 ms
Stage lex     :     0.03 ms
Stage parse   :     0.15 ms
Stage check   :     0.09 ms
Stage run     :    99.80 ms
  use Outer                         97.18 ms
    use Inner                       40.22 ms
  use JSON::Native                   2.26 ms
```

`precomp` is the parsed-program cache lookup ([CACHING.md](CACHING.md)):
a hit replaces `lex` and `parse` outright, which is the number the cache
exists to show you. `check` is the undeclared-variable pass. Module loads
happen inside `run` (a `use` executes when the program starts), so their
times are part of that line, not extra to it; a module already loaded is
not listed again. The program's own output is untouched.

**`--trace`** (bash `-x`) prints every statement to stderr as it runs — the
file, the line and the source line, following calls into subs and into
modules:

```
$ rakupp --trace -I lib prog.raku
[trace] prog.raku:2  sub f($n) {
[trace] prog.raku:6  for 1..2 -> $i {
[trace] prog.raku:7  say f($i);
[trace] prog.raku:3  my $k = $n + 1;
2
[trace] prog.raku:7  say f($i);
[trace] prog.raku:3  my $k = $n + 1;
3
[trace] lib/Outer.rakumod:1  unit module Outer; use Inner; sub outer() is export { inner() }
```

Declarations come first — a `sub` is hoisted before the mainline starts,
which is why line 2 is traced before line 6 — and a loop body is traced
once per iteration. A `use` is traced once, when it loads. A statement
that is one of several on a line is traced by its line, so the line text
repeats. Values are not shown; the line is.

**`--repl-after`** (python's `-i`) runs the program, then opens a session on
whatever it left behind: its variables, subs and classes are live at the
prompt, `\v` lists them, `\q` leaves. Arguments after the file are the
program's `@*ARGS`, a `sub MAIN` is dispatched the way it would be in a
plain run, and the program's `END` blocks run when the session ends, as
the session's own do. An `exit` in the program is reported, not obeyed —
the point of the flag is what comes after — and so is an uncaught error,
whose backtrace prints and whose state is then there to inspect:

```
$ rakupp --repl-after app.raku data.csv
Raku++ 3.25.0 — \h for help, ^D to exit
…app.raku's output…
(app.raku finished; its declarations are live — \v lists them, \q quits)
> say %totals
```

The banner comes first, before the program's own output: the session is
announced when the run starts, not when the prompt appears.

The session opens whether or not stdin is a terminal — the flag is the
request — so a script can pipe questions into it. `-q` drops the banner
and the "declarations are live" line.

The key that ends a session is the console's, not rakupp's: `^D` on Unix,
`^Z` on an empty line followed by Enter on Windows. The banner and `\h` name
whichever one applies where they are printed. `\q` and `exit` work everywhere.

## `--profile`

`--profile` prints a routine-level wall-time profile to stderr after the
run; `--profile=FILE` writes it to a file, and a `.json` extension switches
to the machine-readable form.

```
$ rakupp --profile tools/bench/fib.raku
514229
Profile — wall time; builtins are attributed to their caller
  excl(ms)   incl(ms)      calls  routine
   340.598    340.598    1664079  fib (/Users/ash/raku++/tools/bench/fib.raku)
```

(The routine's file is printed as the path the run resolved, never a bare
basename; the milliseconds are one machine's and will differ on yours.)

Reading it:

- **calls** — exact invocation count.
- **incl(ms)** — wall time inside the routine *and everything it called*.
  Recursion counts once: a recursive routine's inclusive time is the wall
  time of the whole tree, not the sum over every frame.
- **excl(ms)** — inclusive minus time spent in *profiled* callees. Builtins
  are not separately profiled, so their cost lands in the calling routine's
  exclusive time — a routine that spends its life in `.sort` will show that
  time as its own.
- Multi candidates are profiled per candidate (same name, one row each);
  bare blocks land in their enclosing routine.
- The program's own output is untouched — a profiled run's stdout is
  byte-identical to an unprofiled one.

Boundaries: `--profile` applies to interpreted runs only (a `--exe`-compiled
binary has no interpreter inside — use the OS profiler there), and the
disabled hooks cost nothing measurable, so there is no separate
"profiling build".

## Checking and inspecting

| Flag | Meaning |
|---|---|
| `-c` | compile-check only, print `Syntax OK` (parse + the undeclared-variable check — BEGIN does not run, unlike Rakudo); `-q` drops the `Syntax OK` |
| `--lint` | static analysis; `-q` drops the summary (see [LINT.md](LINT.md)) |
| `--ast` | print the parsed AST (`--dump-ast`, `--target=ast` are aliases) |
| `--target=parse` | Rakudo-compatible alias of `-c` |
| `--ast-roundtrip` | prove the AST survives the precomp cache format |
| `--highlight` | syntax-highlight to HTML (`--ansi` for terminals) |
| `--precomp-*` | the parsed-module cache (see [CACHING.md](CACHING.md)) |
| `--ffi-info` | which FFI backend NativeCall will use (see [FFI.md](FFI.md)) |
| `--exe-info BIN` | a compiled binary's embedded build manifest (version, mode, `--slim` cuts) |

### Diagnostics as data: `--json`

`-c --json` and `--lint --json` print the findings as one JSON array on
stdout, one object per finding, and nothing else there:

```
$ rakupp --lint --json prog.raku
[
{"file": "prog.raku", "line": 1, "severity": "warning", "rule": "unused-variable", "message": "'$x' is declared but never used"},
{"file": "prog.raku", "line": 4, "severity": "error", "rule": "undeclared-variable", "message": "'$y' is not declared"}
]
```

`severity` is `error`, `warning` or `note`; `rule` is the stable id
([LINT.md](LINT.md) lists them), with `parse-error` and
`undeclared-variable` for the two compile failures; `line` is 1-based and
there is no column — the parser records lines. A clean `-c --json` prints
`[]` (no `Syntax OK`). Exit codes are unchanged: `-c` exits 2 on a parse
error and 1 on an undeclared variable, `--lint` 2 / 1 / 0 for errors /
warnings / clean. The `--lint` summary line still goes to stderr, where a
consumer reading stdout never sees it (`-q` drops it). This is the
editor-integration surface for a tool that does not speak LSP; the one
that does is `--lsp`.

### `--lsp`: the language server

`rakupp --lsp` speaks the Language Server Protocol on stdin/stdout, for an
editor that talks to a server rather than shelling out per file. It is a
**diagnostics** server: it parses what the editor sends and answers with the
same findings `-c` and `--lint` produce, so the errors an editor underlines are
the errors a build would report. It has no completion, hover or
go-to-definition, and it answers nothing else.

Start it the way the editor wants — a command of `rakupp --lsp`, no arguments —
and it runs until the editor closes the connection. Malformed or hostile input
is dropped rather than fatal: the framing rejects an impossible
`Content-Length`, the parser caps nesting depth, and a bad `\u` escape ends
the string instead of the process.

### Undeclared variables are refused before the program runs

An undeclared variable is a compile error, not a run-time one. Before a program
starts — and under `-c`, `--cpp`, `--bundle`, `--aot` and `--exe` — rakupp walks
the whole unit and refuses it if a variable is used that nothing declares:

```
$ rakupp t.raku
===SORRY!=== Error while compiling t.raku
Variable '$y' is not declared
at t.raku:3
------> say ⏏$y;
```

Nothing of the program runs, so a typo on line 90 no longer shows up after 89
lines of output. The check is one-sided by design: it stands down and says
nothing whenever the unit can conjure names it cannot see — `EVAL`, a symbolic
reference `::($name)`, `require`, or an import it cannot resolve on the module
search path — because refusing a program that works would be far worse than the
late error it replaces. `no strict` silences it too, but only where the pragma
reaches: it is lexical, so the check is back on at the closing brace of the
block that asked for it (and `use strict` turns it back on inside one). For the
same reason it reports a name only when the source declares it *nowhere*; a
variable declared in another scope is left to the run.

The REPL is untouched (each line is its own unit, and a mistake there costs you
one line), and so is an embedding host's `rk_run`, whose interpreter may already
hold globals no static pass over the source can see. `RAKUPP_NO_DECLCHECK=1`
turns the check off.

`--lint` is the separate, softer tool: warnings about declared-but-unused
variables, unreachable code and the like, none of which stop a program. It does
report this check too, as an `error:` line rather than a warning, so that
analysing a file never says less than running it would — a file `rakupp` refuses
must not come back from `--lint` as "no issues found".

## When something dies

An uncaught error prints its message, then where it happened and how the
program got there:

```
boom in baz with 2
  in method Foo::baz at t1.raku line 3
      3 |     method baz($x) { die "boom in baz with $x" }
  in method Foo::bar at t1.raku line 2
  in sub helper at t1.raku line 7
  in block <unit> at t1.raku line 10
```

The message stays line 1, so a script that reads it with `head -1` keeps
working. Below it, one line per live call, innermost first, and the source
line of the frame the error came from.

| Knob | Meaning |
|---|---|
| `--ll-exception` | every frame, nothing folded, no limit (Rakudo's flag) |
| `RAKUPP_BACKTRACE=full` | the same, as an environment variable |
| `RAKUPP_BACKTRACE=0` | the message alone, no frames |
| `--color=auto\|always\|never` | ANSI colour: `auto` (the default) means a terminal, unless `NO_COLOR` is set; `always` colours even into a pipe; `never` never. The same switch governs the REPL's prompt, echo and error colour. `--colour` is accepted too |
| `NO_COLOR` | no ANSI colour, by the no-color.org convention (present and non-empty) |
| `RAKUPP_COLOR=0` / `=1` | what `--color=never` / `always` set; the environment form |

On Windows `auto` asks one more question: a console only *renders* an escape
sequence once virtual-terminal processing is on, and rakupp turns it on at
startup. Where that fails — a console older than Windows 10 1511 — `auto` gives
you plain text rather than a prompt reading `<-[1;32m>`. `--color=always` still
overrides, for a session piped into something that does render escapes.

The whole story — errors with two positions (`fail`, `await`), how `warn` and
syntax errors report, and the `$!.backtrace` API — is
[TRACER.md](TRACER.md).

## The developer loop: `--watch`

`--watch` runs the command, then reruns it whenever the program file
changes — or any `.raku`/`.rakumod` under the `-I` directories, `lib/` or
`RAKULIB`. It composes with `-c` and `--lint`, which makes a live check
loop:

```
$ rakupp --watch --lint prog.raku
prog.raku:1: warning: '$x' is declared but never used [unused-variable]
rakupp --lint: 1 warning, 0 notes in prog.raku
[watch] exit 1 — watching prog.raku (^C stops)
[watch] prog.raku changed; rerunning
rakupp --lint: no issues found in prog.raku
[watch] exit 0 — watching prog.raku (^C stops)
[watch] prog.raku is gone; stopping
```

That is a directory with no `lib/`. The line reads "watching prog.raku **and
the module directories**" only when there is something else to watch: a `-I`
path, a `RAKULIB` entry, or a `lib/` beside the program.

Each run is a fresh process with the same command line (minus `--watch`),
so nothing leaks between runs; the program's stdin, stdout and stderr are
the terminal's. Changes are found by polling every 300 ms — the program
file by content, the library trees by modification time and size — with
no kernel watcher to set up. The loop needs a program *file*: `-e` code
and a stdin program have nothing to watch. `^C` stops it, and so does the
program file disappearing, which is reported.

## Looking things up: `rakupp doc`

`rakupp doc SYMBOL` looks a builtin, method, operator or syntax form up in
the language reference, offline — `go doc`, `perldoc -f`, `pydoc`:

```
$ rakupp doc trim
REFERENCE.md › 6. Methods by receiver › On `Str`
    say '  hi  '.trim;               # → hi

REFERENCE.md › Appendix B — all methods
    to to-json to-posix today toggle total trans tree trim trim-leading
    …
```

The content is [REFERENCE.md](REFERENCE.md), every example of which was
executed on rakupp and shows its real output, with [FEATURES.md](FEATURES.md)
as the second source; each hit is shown under its heading trail. Table
rows come first (they carry the meaning column), then code examples, then
prose. `.trim` and `trim` are the same question; a symbol made of
punctuation (`<=>`, `»`, `Z`) is matched as a substring, a name as a whole
word. `--code` keeps only the examples, `--all` shows every hit instead of
the first dozen; several symbols at once are answered one after another.
An unknown symbol exits 1. Like the installer, the command is a Raku program
(`tools/doc.raku`) that the binary carries and dispatches to — and it carries
the two guides with it, so `rakupp doc` answers from an executable with
nothing beside it. `RAKUPP_DOCS=DIR` reads them from that directory instead,
which is how you check an edit to `REFERENCE.md` before rebuilding.

## Shell completion

`--completions=bash`, `=zsh` or `=fish` prints a completion script for the
shell, generated from the binary's own flag table — so it travels with the
binary rather than with a copy in this page. That table is maintained beside
the option parser rather than derived from it, so an accepted alias can be
missing from it: `-m`, `--colour`, `--terminal` and `--emit-cpp` all work and
are deliberately not offered. Flags complete with their descriptions (zsh,
fish), a `=` option completes its values (`--color=` offers `auto`, `always`,
`never`), the first word completes to a file or a subcommand (`install`,
`uninstall`, `reinstall`, `test`, `doc`), and everything else completes to
files.

```bash
eval "$(rakupp --completions=bash)"                     # bash, in ~/.bashrc
eval "$(rakupp --completions=zsh)"                      # zsh, in ~/.zshrc (after compinit)
rakupp --completions=fish > ~/.config/fish/completions/rakupp.fish
```

## Serving

`--mcp` turns the process into a [Model Context Protocol](MCP.md) server on
stdio, so AI agent clients get `raku` (a persistent session) and
`raku-parse` (grammars) as tools. `--timeout=SECS` bounds a stuck call
(default 120, `0` = never), `-M` preloads modules into the session, and
`RAKULIB` — not `-I` — adds module directories. The whole story is
[MCP.md](MCP.md).

`--jupyter FILE` runs the process as a [Jupyter kernel](JUPYTER.md) against
the connection file the frontend passes as `{connection_file}`, and
`--jupyter-install` writes the kernelspec that makes `jupyter lab` and
`jupyter console --kernel raku` able to launch it (`--name=NAME` for a second
build, `--prefix=DIR` for another location). `-M` preloads modules into the
notebook's session. No ZeroMQ is needed: the binary speaks the wire protocol
itself. The whole story is [JUPYTER.md](JUPYTER.md).

## Transpiling to JavaScript

`--target=js` emits a JavaScript program (to stdout, or `-o prog.js` with the
runtime `rakupp-rt.js` written beside it; `--standalone` inlines it) that
runs under Node, Bun, Deno or a browser. `--verify` runs the program under
the interpreter and under the JavaScript host and emits only when they agree
byte for byte; a program outside the JavaScript core is refused with the
construct and line, or accepted with `--fallback=wasm` as a wrapper around
the WebAssembly engine. See [JS.md](JS.md).

## Compiling

`--bundle`, `--aot` and `--exe` produce standalone binaries — see
[COMPILERS.md](COMPILERS.md) and [NATIVE.md](NATIVE.md). Their flags
(`-o OUT`, `-O[level]`, `-I`, `--slim[=SPEC]`) compose in any order with the
mode, before or after the source file. `-q` drops the `Compiled …` line and
the list of embedded modules; a module that could *not* be embedded is still
reported, because the binary will need the disk for it.

### `--slim` — how much of itself a compiled binary keeps

Compiled binaries are **dead-stripped and symbol-stripped by default** (level
`safe`): the linker drops unreferenced sections and local symbols stay out of
the symbol table. That removes no Raku feature and runs no analysis — `say
"Hello"` goes from 12.4 MB to 10.1 MB and behaves byte-identically. Two escapes:

```
rakupp --exe --slim=none      prog.raku   # the old output, bit for bit
rakupp --exe --slim=+symbols  prog.raku   # dead-strip, but keep the symbol
                                          # table (a crash report worth reading)
```

The practical cost of `safe` is exactly that second case: a C++-level crash in
a shipped binary reports addresses instead of names. If you are debugging a
binary, build it `--slim=safe,+symbols`.

**The levels** — a ladder, at most one per SPEC:

| level | what it does |
|---|---|
| `none` | nothing at all: no dead-strip, symbols kept. For debugging a compiled binary. |
| `safe` | **the default with no flag.** Dead-strip + symbol strip. No Raku feature removed, no analysis run. |
| `auto` | **what bare `--slim` means.** `safe`, plus every feature the scan *proves* no site in the program — or in any embedded module — can reach. Anything the scan cannot decide keeps the feature; any force-full trigger (below) keeps everything. Sound. |
| `max` | `auto`, but ignoring the force-full triggers. Unsound by design: code the scan never saw may need a cut feature at run time, and then it throws `X::Feature::NotBuilt` — never a crash, never a wrong answer. |

A SPEC that names no level means `auto` — so `--slim=+eval` is "automatic
pruning, but keep eval".

`say "Hello"` under bare `--slim`: 10.1 → 6.8 MB, all four features cut,
because hello provably uses none of them.

**The force-full triggers.** Under `auto`, any of these means the program can
run code the scan never saw, so everything is kept — and stderr says so, with
the construct named: `EVAL`/`EVALFILE`/`require`; a symbolic reference
(`::($name)`); an indirect method call (`."$name"()`) or metamodel lookup
(`.^lookup`); a regex interpolating a subregex (`<$var>`/`<{…}>`); a `use`d
module that could not be embedded alongside the program. Literal regex code
blocks (`{…}`) are NOT triggers — their source is visible, so the scan parses
and walks them like any other code.

**Explicit feature cuts.** The same four features can be cut (or kept) by
name, overriding whatever the level concluded — each cut drops its data (or
the parser) from the binary and replaces it with a stub that throws
`X::Feature::NotBuilt`, a typed, catchable exception naming the feature and
the rebuild flag:

| feature             | what leaves the binary        | what then throws                          |
|---------------------|-------------------------------|-------------------------------------------|
| `unicode-names`     | the Unicode name/numeric tables | `uniname`, `uniparse`, `unival`         |
| `unicode-collation` | the DUCET tables              | `unicmp`, `coll`, `.collate`              |
| `unicode-props`     | Script/Block/Bidi_Class ranges | `uniprop('Script')`, `<:Script<…>>`, …   |
| `eval`              | the lexer and parser          | `EVAL`, `require`, runtime-compiled regexes |

```
rakupp --exe --slim                     prog.raku   # the button: sound automatic pruning
rakupp --exe --slim=max,+unicode        prog.raku   # smallest, the Unicode features intact
rakupp --exe --slim=safe,-eval          prog.raku   # one deliberate cut, no scan
rakupp --exe --slim=-all,+unicode-names prog.raku   # auto is implied; a named feature
                                                    # beats a group: cut three, keep names
```

The groups are `unicode` (the three Unicode features) and `all`; a named
feature beats `unicode` beats `all`. Whatever ends up cut, using it throws
the named exception at the point of use — never a crash, never a quiet wrong
answer. Every conflict (two levels, `+x` with `-x`, an unknown name, `none`
with any override) is an error listing what exists.

Two modes decline the scan, loudly: `--bundle` embeds source and parses it at
run time, so nothing can be proven unused (and `--slim=-eval` is refused there
outright — bundling *is* the eval feature); `--aot` keeps every feature until
the scan is wired for it (SLIM-PLAN P7). Explicit `±feature` still applies in
both.

Every compiled binary embeds a one-line build manifest; `rakupp --exe-info
BIN` prints it (version, mode, slim level, cut list). It survives symbol
stripping — the reader scans bytes, not symbol tables — so `strings BIN |
grep RAKUPP-EXE` finds it too.

**The directives** — one per SPEC, and the key documents itself:

| directive | |
|---|---|
| `help` | the grammar, the feature table with the real archive sizes beside this rakupp, and examples. Stands alone: `rakupp --slim=help`. |
| `list` | for *this* program: keep/cut per feature with the reason (`used: uniname (line 3)`, `proven unused`, the trigger list) and the bytes. Analyses only — does not compile. |
| `why:FEAT` | every site — program or module, with the line — that forces FEAT to be kept; or the honest `no use anywhere`. |
| `verify` | build the slim binary AND a full reference, run both, and emit the slim one only if stdout, stderr and exit status agree. A nondeterministic program cannot agree with anything, so `verify` refuses it too. |

`list` and `why:` compose with a level (`--slim=max,list` shows what `max`
would decide); `verify` verifies whatever the rest of the SPEC asks for —
including an explicit cut, which is exactly when you want the proof.

`--slim` shapes the link, so it applies to the compile modes only — the
interpreter never slims.

## Installing modules

`rakupp install` is the ecosystem installer — a Raku program shipped with
the release and dispatched by the binary. It writes the same CURI store
zef and Rakudo share (`~/.raku` by default), so an installed module is
loadable by either engine.

```console
rakupp install Foo::Bar          # newest satisfying, plus dependencies
rakupp install Foo:ver<1.2.3>    # a specific version (installs are additive)
rakupp install .                 # this directory's dist; deps from the index
rakupp install ./my-dist         # any path — an argument starting with . or /
rakupp test Foo                  # build + run Foo's own suite; installs its
                                 # deps, never Foo — the measurement command
rakupp uninstall Foo             # remove what THIS installer put there —
                                 # every installed version behind the name
rakupp reinstall Foo             # uninstall + install fresh, one command
rakupp install --list            # what is installed: identity, installer,
                                 # module files, bin wrappers (-q: identities)
rakupp install --check           # store integrity report; fixes nothing
rakupp install --gc              # remove the blobs --check calls unreferenced
                                 # (--dry-run lists them and removes nothing)
rakupp install --refresh         # refetch the cached ecosystem index(es)
rakupp install --to=PATH Foo     # another store prefix (default ~/.raku)
rakupp install -q Foo            # only warnings and failures; nothing on
                                 # success (-q goes with every command here)
```

`--list` prints one identity line per installed distribution and, under
it, who installed the dist — `rakupp`, or `zef` for one this installer did
not put there — then the store path of every module file it provides and
the wrapper path of every command it put in `bin/`. Paths are absolute
(shortened here):

```console
$ rakupp install --list
HTTP::Tiny:ver<0.2.6>:auth<zef:jjatria>  (HTTP::Tiny)
    installed by: rakupp
    HTTP::Tiny  ~/.raku/sources/B8D9978ECCC705A8781DD91A00AAF3500916C95C
    bin/rakurl  ~/.raku/bin/rakurl
```

A blob or wrapper the record names but the disk lacks is flagged beside its
path; `--check` is the full audit. With `-q` only the identity lines print.

`--check` reports a blob that is present but no longer holds what it should,
too. The store is content-addressed — a blob's file name is the SHA-1 of its
content — so a truncated or overwritten copy is provable, and it is the damage
worth naming: an absent blob fails `use` loudly, while an empty one compiles to
a module that exports nothing and leaves the program silently doing less than
it should. Only distributions this installer wrote are checked that way (zef
names its blobs by something other than the content); the summary says how many
were checked for presence alone. `rakupp install <name>` repairs either kind
rather than answering "already installed".

`--check` also counts blobs that nothing references — an orphan left by an
interrupted install, or by a file some later version replaced. They are wasted
disk rather than damage, so they do not fail the check, and `--gc` is what
removes them:

```console
$ rakupp install --gc --dry-run
store: ~/.raku
  sources/9426E1FB2DABFEF01CCE4403DB08A6A816F32B87  2.9 KB
  resources/libB2AECA0D0203E5AB9680EA5F9EA463F0B5B458B1.dylib  49.1 KB
store gc: 5 blobs, 144.8 KB would be reclaimed (--dry-run: nothing removed)
```

Without `--dry-run` it removes them and reports what it recovered. The
collector and the checker compute the live set with the same routine, so a
blob one calls live is never a blob the other deletes; it takes the store lock
while it works, and a store it cannot read in full — an unreadable `dist/`
record — makes it refuse and remove nothing, since an incomplete live set is
how a collector would eat an installation.

`--list`, `--check` and `--gc` each answer on their own: a run that asks for
two is refused rather than silently answering as one of them, and all three
belong to `rakupp install` — asking `uninstall`, `reinstall` or `test` for one
is refused the same way, naming where it belongs.

A distribution that ships commands in `bin/` gets a named, executable
wrapper per script — `~/.raku/bin/s6` for Sparrow6's `s6` — the same
dispatch stub Rakudo writes, so the command runs by name under either
engine once the store's `bin/` is on `PATH`. The wrapper's shebang names
`raku` — the language, not an engine — and on a machine where nothing
answers to that name, the install links `~/.raku/bin/raku` to this engine
and says so, so the one `PATH` entry above also resolves the shebangs; a
machine with a Rakudo keeps its Rakudo, since the link is made only when
the name resolves nowhere. `uninstall` removes the wrapper unless another
installed dist still provides a script of that name. (Installs from before
this existed have no wrappers; a `reinstall` of the dist writes them.)

A distribution with a `Build.rakumod` (zef's build protocol — OpenSSL
generates its `resources/libraries.json` in one) gets it run before its
tests, with `build-depends` and `test-depends` installed like runtime deps.
Test and build children see the target store as `-I inst#<prefix>`, so the
suite imports exactly what the plan installed, wherever `--to` pointed.

Resolution is zef-index-first with the community's REA archive
(github.com/Raku/REA) as the fallback, the same order zef itself uses —
names and exact `:ver`/`:auth` pins the live index no longer carries still
resolve. A pin neither index answers is put to the STORE next: a dependency
installed from a checkout is in no index at all, and an already-installed
version that satisfies the pin satisfies it. Only then does the pin loosen,
and loosening drops `:auth` before `:ver` and never drops a `ver<X+>` floor —
answering "at least X" with a release older than X is the one answer that
cannot work, so an unsatisfiable floor is reported instead.
A distribution's own test suite runs under rakupp before it is
marked installed (`--no-test` skips; `--dry-run` prints the plan and writes
nothing). Each command alone prints its full usage.

Installs are additive, and a repeat is answered from the store: every plan
entry the store already holds is marked `(already installed)` and skipped
before anything is fetched, built or tested, so re-running an install you
already did costs a plan, not a download. `--force` (or `reinstall`) is how
you mean it anyway. One case still pays full price: a distribution whose
index identity disagrees with its own `META6.json` — a different `:auth`,
say — cannot be recognized until its archive is open, and the engine refuses
it at the end.

With `-q` that re-run prints nothing at all: the plan, the progress lines,
`already installed:` and `done:` are narration, and narration is what
`-q` removes ([Quiet mode](#quiet-mode)). Warnings, refusals, failures and
the `--check` and `--dry-run` reports stay, as do `--list`'s identity lines
(only the detail under each goes).

An argument that starts with `.` or `/` is a PATH — zef's own rule,
adopted verbatim — naming a directory whose `META6.json` is the dist. It
installs from disk: no fetch and no checksum (the directory is the source
of truth), while the build hook and the test gate stand unchanged, and its
dependencies still resolve from the ecosystem and install first.
`rakupp install .` is the development loop; `rakupp test .` measures the
suite without installing; `uninstall` and `reinstall` accept the same
spelling and act on whatever dist the directory names. A path install
whose dist has no ecosystem dependencies touches no network at all.

Every run appends a step-by-step account of itself — engine build, OS,
arguments, resolution, fetches, checksums, hook and suite verdicts, store
writes down to each bin wrapper — to `~/.raku/rakupp-install/trace.log`
(at 512 KB it rotates once, to `trace.log.1`). A failed run prints the
file's path. When an install misbehaves on a machine you cannot see,
that one attachable file answers which build, which OS and what happened,
in order — ask for it before asking anything else.

## MAIN: how a program's own arguments parse

A program with a `sub MAIN` gets Rakudo-compatible argument parsing —
byte-identical on a 46-case oracle matrix
(`t/regression/main-args-conventions.raku`, which passes under both
engines). The generated **usage text** follows Rakudo's with two known
differences: named parameters are listed in declaration order where Rakudo
hoists the required ones to the front, and a named parameter whose type is not
`Str` renders as `-n=<Int>` where Rakudo writes `-n[=Int]`. Do not diff a usage
line against Rakudo's in a golden-file test. The conventions, which are also
the ordinary Unix ones:

- **`--key=value` and `--key value` both work — the space form for
  `Str`-typed named parameters.** `sub MAIN(Str :$foo, Bool :$verbose)`
  accepts `prog --foo abc --verbose`. Only `Str`-typed named params pair
  this way (Rakudo's rule, exactly: with `Int :$n`, `--n 42` fails — spell
  it `--n=42`), and the next token is consumed unconditionally:
  `--foo --verbose` makes `$foo eq "--verbose"`. Decided per multi
  candidate.
- **Options end at the first positional argument** (POSIX). After
  `prog xx`, a later `--foo=abc` is the literal string `"--foo=abc"`. A
  bare `--` is consumed and ends options — which is how you pass a
  positional that starts with a dash: `prog -- -5`. Setting
  `my %*SUB-MAIN-OPTS = :named-anywhere;` in the mainline lifts the
  boundary — options (and space-form pairing) bind wherever they appear,
  as in `prog URL --verbose` — while a bare `--` still ends them.
- **Single-dash spellings are named options too**: `-v` is `:v`, `-n=3`
  binds `:n(3)`, `-foo=bar` binds `:foo<bar>`. The whole rest of the token
  is the name (`-xyz` is `:xyz`, not a `-x -y -z` cluster), and `--/key`
  passes `False`.
- **A repeated option collects every value**: `--x=a --x=b` is
  `:x(["a","b"])`, which binds `:@x` whole — and fails to bind a scalar
  `Str :$x`, exactly as under Rakudo, instead of silently keeping the
  last value.

### The usage text

The auto-generated usage: `--help` prints it to stdout (exit 0); a failed
dispatch prints it to stderr (exit 2). `#|` docs the routine (the ` -- …`
suffix on the usage line), a parameter's trailing `#=` docs that parameter
in the option list (and answers its `.WHY`), and a required named parameter
prints without the optionality brackets. The text is byte-identical to
Rakudo's.

### Compiled binaries follow the same protocol

Everything above holds for a `--exe` binary too. A natively-compiled program
embeds its MAIN signatures (bodies detached, written by the same serializer
as the module cache) and dispatches through the interpreter's own protocol —
pairing, candidate scoring, alias keys like `:r(:$string)`, the usage text,
`--help`, exit codes. `--slim` binaries included: the metadata is data, not
source, so none of it needs the parser. One deliberate boundary: a `where`
clause on a MAIN parameter is not scored in a native binary — it may close
over lexicals that compile to C++ globals the runtime env cannot see, and
refusing a good command line would be worse than accepting a bad one.
`t/regression/main-args-exe.raku` compiles three probes and holds the
compiled answers to the oracle matrix; it used to be that a compiled binary
silently ran on argument lines the same program would refuse under the
interpreter.
