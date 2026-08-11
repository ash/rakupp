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
| `-c` | syntax-check only, print `Syntax OK` (parse only — BEGIN does not run, unlike Rakudo) |
| `--lint` | static analysis; `-q` drops the summary (see [LINT.md](LINT.md)) |
| `--ast` | print the parsed AST (`--dump-ast`, `--target=ast` are aliases) |
| `--target=parse` | Rakudo-compatible alias of `-c` |
| `--ast-roundtrip` | prove the AST survives the precomp cache format |
| `--highlight` | syntax-highlight to HTML (`--ansi` for terminals) |
| `--precomp-*` | the parsed-module cache (see [CACHING.md](CACHING.md)) |
| `--ffi-info` | which FFI backend NativeCall will use (see [FFI.md](FFI.md)) |
| `--exe-info BIN` | a compiled binary's embedded build manifest (version, mode, `--slim` cuts) |

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
binary, build it `--slim=+symbols`.

**Explicit feature cuts.** Four features can be cut by name — each drops its
data (or the parser) from the binary and replaces it with a stub that throws
`X::Feature::NotBuilt`, a typed, catchable exception naming the feature and
the rebuild flag:

| feature             | what leaves the binary        | what then throws                          |
|---------------------|-------------------------------|-------------------------------------------|
| `unicode-names`     | the Unicode name/numeric tables | `uniname`, `uniparse`, `unival`         |
| `unicode-collation` | the DUCET tables              | `unicmp`, `coll`, `.collate`              |
| `unicode-props`     | Script/Block/Bidi_Class ranges | `uniprop('Script')`, `<:Script<…>>`, …   |
| `eval`              | the lexer and parser          | `EVAL`, `require`, runtime-compiled regexes |

```
rakupp --exe --slim=-eval               prog.raku   # cut one
rakupp --exe --slim=-all                prog.raku   # cut all four: 8.1 → 4.6 MB
rakupp --exe --slim=-all,+unicode-names prog.raku   # a named feature beats the
                                                    # group: cut three, keep names
```

Cutting is explicit — nothing scans your program or guesses. Everything the
program does NOT use is free to cut; whatever it does use will throw the named
exception at the point of use, never crash or quietly misbehave. Every
conflict (two levels, `+x` with `-x`, an unknown name, `none` with any
override) is an error listing what exists. One mode refuses composition:
`--bundle --slim=-eval`, because a bundled binary parses its embedded source
at run time — bundling *is* the eval feature.

Every compiled binary embeds a one-line build manifest; `rakupp --exe-info
BIN` prints it (version, mode, slim level, cut list). It survives symbol
stripping — the reader scans bytes, not symbol tables — so `strings BIN |
grep RAKUPP-EXE` finds it too.

The levels above `safe` — `auto` and `max`, which cut what a program provably
does not use, without you naming it — arrive with the feature scan
([SLIM-PLAN](../dev/plans/SLIM-PLAN.md)); asking for one today errors with the
list of what exists rather than quietly meaning something weaker. `--slim`
shapes the link, so it applies to the compile modes only — the interpreter
never slims.

## MAIN: how a program's own arguments parse

A program with a `sub MAIN` gets Rakudo-compatible argument parsing —
byte-identical on a 30-case oracle matrix
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
  positional that starts with a dash: `prog -- -5`.
- **Single-dash spellings are named options too**: `-v` is `:v`, `-n=3`
  binds `:n(3)`, `-foo=bar` binds `:foo<bar>`. The whole rest of the token
  is the name (`-xyz` is `:xyz`, not a `-x -y -z` cluster), and `--/key`
  passes `False`.

### The usage text

The auto-generated usage: `--help` prints it to stdout (exit 0); a failed
dispatch prints it to stderr (exit 2). `#|` docs the routine (the ` -- …`
suffix on the usage line), a parameter's trailing `#=` docs that parameter
in the option list (and answers its `.WHY`), and a required named parameter
prints without the optionality brackets. The text is byte-identical to
Rakudo's.
