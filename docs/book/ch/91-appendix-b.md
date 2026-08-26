# Flags and Environment Variables

Everything that changes what the compiler does from the outside. This is a
reference for the internals; the user-facing command line is documented in
`docs/guide/CLI.md`.

## Command-line flags that select a mode

| Flag | Effect |
|---|---|
| *(none)* | lex, parse, interpret |
| `-e 'code'` | interpret the argument |
| `--bundle` | embed the source bytes in a standalone binary |
| `--aot` | parse at build time, emit C++ that rebuilds the AST |
| `--exe` | transpile to C++ and compile natively |
| `--cpp`, `--emit-cpp` | print the generated C++ instead of compiling it |
| `-O`, `-O2`, `-O3`, `-Os`, `-O0` | run the codegen optimizer; the level is forwarded to the C++ compiler |

## Inspection and tooling flags

| Flag | Effect |
|---|---|
| `--ast`, `--dump-ast` | print the parsed tree as an indented outline |
| `--ast-roundtrip` | serialise and deserialise the tree, then run it — the cache format's own test |
| `-c`, `--target=parse` | compile-check only: parse and check every variable is declared |
| `--lint` | static analysis; does not run the program |
| `--highlight` | syntax-highlight the source |
| `--html`, `--ansi`, `--terminal` | pick the highlighter's renderer |
| `--doc` | run `DOC` phasers and print rendered Pod |
| `--profile[=dest]` | the routine-level wall-time profiler |
| `--ffi-info` | which FFI backend is live, or why none is |
| `--precomp-info` | what the parse cache holds |
| `--precomp-clean` | empty it |
| `--precomp-modules=on\|off`, `--precomp-files=on\|off` | the two cache switches |
| `--version`, `--help`, `--quiet` | as expected |

Flags are position-independent, and the perl-compatible one-liner family
(`-n`, `-p`, `-a`, `-F`, `-i`, `-0777`, `-M`) is documented in the CLI guide.

## Environment variables

### Search and loading

| Variable | Effect |
|---|---|
| `RAKULIB` | extra module search paths; **both** `,` and `:` separate |
| `ROAST` | adds the specification suite's test-helper library |
| `RAKUPP_HOME` | where the binary considers itself installed |
| `RAKUPP_CONFIG` | override the settings file location |
| `XDG_CONFIG_HOME`, `XDG_CACHE_HOME` | the settings and cache directories |

### The parse cache

| Variable | Effect |
|---|---|
| `RAKUPP_PRECOMP_MODULES` | override the module-cache switch for one run |
| `RAKUPP_PRECOMP_FILES` | override the file-cache switch |
| `RAKUPP_NO_PRECOMP=1` | force both off |
| `RAKUPP_PRECOMP_DIR` | where cache entries live |

### The foreign-function interface

| Variable | Effect |
|---|---|
| `RAKUPP_FFI=0` | force the no-libffi fallback path — the second CI leg |
| `RAKUPP_FFI=/path` | use *only* that library; if it fails to load, report and fall back rather than silently substituting the system copy |
| `RAKUPP_FFI_TRACE=1` | log every crossing, with marshalled arguments and raw returns |

### Concurrency

| Variable | Effect |
|---|---|
| `RAKUPP_PARALLEL=1` | true parallelism: workers run interpreter compute concurrently instead of serialising on the GIL |
| `RAKUPP_GIL` | control the lock explicitly |
| `RAKUPP_FREEZE_TRACE=1` | report any symbol-table mutation after concurrency engaged, and which thread did it |

### The regex engine

| Variable | Effect |
|---|---|
| `RAKUPP_LTM=1` | use the NFA ranker for longest-token matching where it is gap-free |
| `RAKUPP_LTM_DEBUG=1` | rank both ways and print every disagreement — works without `RAKUPP_LTM` |
| `RAKUPP_LTM_RANKDUMP=1` | print each decided alternation's ranking |

### Diagnostics

| Variable | Effect |
|---|---|
| `RAKUPP_TRACE=1` | the module search path and every resolution |
| `RAKUPP_DUMPTOKENS=1` | the token stream |
| `RAKUPP_NO_DECLCHECK=1` | skip the undeclared-variable gate (Chapter 38) |
| `RAKUPP_ACTTRACE=1` | grammar action firing |
| `RAKUPP_TAP_TRACE=1` | the test harness's own emission |
| `RAKUPP_KEEPGEN=1` | keep the generated C++ from a compiling mode |
| `RAKUPP_DEBUG_MAKE`, `RAKUPP_DEBUG_REPLAY` | grammar `make` and replay diagnostics |

### The interactive session

| Variable | Effect |
|---|---|
| `RAKUPP_REPL=1` | force a session even when standard input is not a terminal |
| `RAKUPP_HISTORY` | the history file |

### Test-harness variables

`RAKU_TEST_DIE_ON_FAIL` stops a suite after a real failure;
`RAKU_EXCEPTIONS_HANDLER=JSON` serialises uncaught exceptions as JSON. Both
follow Rakudo's spelling because test harnesses set them.

## Build-time switches

| Define | Effect |
|---|---|
| `RAKUPP_PTR_CENSUS` | count which combinations of `Value`'s eleven pointers are actually live together — the empirical input to shrinking the struct |
| `_GLIBCXX_USE_CXX11_ABI` | relevant only because it is the ABI break copy-on-write strings caused (Chapter 9) |

The pointer census is a good example of the project's habit: before collapsing
`Value`'s pointers into tag-dispatched slots, a special build **counts** which
sets are live simultaneously, rather than reasoning about what the type tags
suggest.

## A note on flags that change behaviour

Two of the variables above change *results* rather than speed: `RAKUPP_LTM` and
`RAKUPP_PARALLEL`. Both are off by default, and the policy for both is the same:
the old path stays available for at least one release after the default changes,
so a regression can be bisected to the switch rather than to the release.

Everything else here either reports something or selects a code path that is
required to produce identical output.
