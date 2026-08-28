# The command line

Everything `rakupp` accepts on the command line: running programs, the
perl-style one-liner family (including in-place editing), module preloading,
the profiler, and the inspection/compile modes. Flags are
position-independent and composable — `rakupp -I lib --lint prog.raku` and
`rakupp --lint -I lib prog.raku` are the same command — and everything after
the program token belongs to the program:

```
rakupp prog.raku --lint      # --lint is in prog.raku's @*ARGS, not ours
```

A bare `--` ends option parsing early (`rakupp -- -strange-name.raku`).
Unknown options print the same banner Rakudo prints, and exit 0, for
compatibility.

## Running programs

| Form | Meaning |
|---|---|
| `rakupp FILE ARGS…` | run a file; `ARGS` land in `@*ARGS` |
| `rakupp -e 'CODE' ARGS…` | one-liner (`-e'CODE'` glued also works) |
| `rakupp - ARGS…` | program text from stdin |
| `… \| rakupp` | same, implicit |
| `rakupp` | the REPL, at a terminal |

- `-I <path>` — add a module search directory (repeatable, `-Ipath` works).
- `-M <module>` — load a module before the program runs (repeatable;
  `-MFoo` glued and `-m` both work — `-m` is a Perl-ism Rakudo rejects).
  The program behaves as if it began with `use Foo;` *on its own first
  line*, so error line numbers do not shift.
- `--doc` — after the run, render the program's POD to stdout.
- `-v` / `-V` / `--version`, `-h` / `--help` — from any position.
  `--version` reports the release, the Raku version implemented, and the
  build's own identity: the `git describe` commit it came from, the build
  date, the platform it targets and the compiler that made it. Quote it
  whole in a bug report.

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
  reported, skipped — and the exit code is 1 (perl exits 0).

## `--profile`

`--profile` prints a routine-level wall-time profile to stderr after the
run; `--profile=FILE` writes it to a file, and a `.json` extension switches
to the machine-readable form.

```
$ rakupp --profile tools/bench/fib.raku
514229
Profile — wall time; builtins are attributed to their caller
  excl(ms)   incl(ms)      calls  routine
   777.582    777.582    1664079  fib (fib.raku)
```

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
| `-c` | compile-check only, print `Syntax OK` (parse + the undeclared-variable check — BEGIN does not run, unlike Rakudo) |
| `--lint` | static analysis; `-q` drops the summary (see [LINT.md](LINT.md)) |
| `--ast` | print the parsed AST (`--dump-ast`, `--target=ast` are aliases) |
| `--target=parse` | Rakudo-compatible alias of `-c` |
| `--ast-roundtrip` | prove the AST survives the precomp cache format |
| `--highlight` | syntax-highlight to HTML (`--ansi` for terminals) |
| `--precomp-*` | the parsed-module cache (see [CACHING.md](CACHING.md)) |

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
| `--ffi-info` | which FFI backend NativeCall will use (see [FFI.md](FFI.md)) |
| `--exe-info BIN` | a compiled binary's embedded build manifest (version, mode, `--slim` cuts) |

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

## Compiling

`--bundle`, `--aot` and `--exe` produce standalone binaries — see
[COMPILERS.md](COMPILERS.md) and [NATIVE.md](NATIVE.md). Their flags
(`-o OUT`, `-O[level]`, `-I`, `--slim[=SPEC]`) compose in any order with the
mode, before or after the source file.

### `--slim` — how much of itself a compiled binary keeps

Compiled binaries are **dead-stripped and symbol-stripped by default** (level
`safe`): the linker drops unreferenced sections and local symbols stay out of
the symbol table. That removes no Raku feature and runs no analysis — `say
"Hello"` goes from 9.9 MB to 8.1 MB and behaves byte-identically. Two escapes:

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

`say "Hello"` under bare `--slim`: 8.1 → 4.6 MB, all four features cut,
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
rakupp install --list            # what is installed in the target store
rakupp install --check           # store integrity report; fixes nothing
rakupp install --refresh         # refetch the cached ecosystem index(es)
rakupp install --to=PATH Foo     # another store prefix (default ~/.raku)
```

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
byte-identical on a 36-case oracle matrix
(`t/regression/main-args-conventions.raku`, which passes under both
engines). The conventions, which are also the ordinary Unix ones:

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
