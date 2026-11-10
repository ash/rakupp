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
| `-O` | run the codegen optimizer. Any suffix (`-O2`, `-Ofast`, `-Os`, …) turns it on too and is forwarded to the C++ compiler; there is one flag here, not five |

## Inspection and tooling flags

| Flag | Effect |
|---|---|
| `--ast`, `--dump-ast` | print the parsed tree as an indented outline |
| `--ast-roundtrip` | serialise and deserialise the tree, then run it — the cache format's own test |
| `-c`, `--target=parse` | compile-check only: parse and check every variable is declared |
| `--lint` | static analysis; does not run the program |
| `--doc-links` | link every construct of a program to its page on docs.raku.org; a list, or the program annotated with `--html` or `--ansi`, or `--json` |
| `--highlight` | syntax-highlight the source |
| `--html`, `--ansi`, `--terminal` | pick the highlighter's renderer |
| `--doc` | run `DOC` phasers and print rendered Pod |
| `--profile[=dest]` | the routine-level wall-time profiler |
| `--mcp` | serve the interpreter over the Model Context Protocol on stdio |
| `--timeout=SECS` | `--mcp` only: the watchdog's limit; `0` disables it |
| `--jupyter FILE` | run as a Jupyter kernel against Jupyter's connection file |
| `--jupyter-install` | write the kernelspec that lets Jupyter launch this binary |
| `--ffi-info` | which FFI backend is live, or why none is |
| `--fmt` | format Raku source to stdout (`--target=raku`); `--check` lists the files that would change, `--diff` shows what |
| `--rakuast` | print the RakuAST *view* of the program beside the Raku each node renders back to (`--target=rakuast`); `=tree`, `=attrs`, `=compunit` vary it |
| `--jit-info` | what is in the tier-up kernel cache (Chapter 44) |
| `--jit-clean` | empty it |
| `--precomp-info` | what the parse cache holds |
| `--precomp-clean` | empty it |
| `--precomp-modules=on\|off`, `--precomp-files=on\|off` | the two cache switches |
| `--version`, `-v` | the release, build and platform, on one line |
| `-V` | the full report — the split is Rakudo's, but the report is a readable block, not Rakudo's `key=value` config dump |
| `--info`, `--version-full` | ours; Rakudo spells this `-V` and nothing else |
| `--help`, `-h`, `--quiet`, `-q` | as expected |
| `-x` | skip everything before the `#!` line |
| `--json` | machine-readable `-c` and `--lint` findings |
| `--ll-exception` | every frame of an uncaught error |
| `--exe-info FILE` | the build manifest embedded in a compiled binary |
| `--stagestats` | phase timings and module loads on stderr |
| `--trace` | print each statement as it runs |
| `--hints` | performance advice on stderr, each hint once per source line; the same as `RAKUPP_HINTS=1` |
| `--detect-races` | watch the threads' reads and writes and report data races when the program ends; a run that raced exits 66 |
| `--repl-after` | run the program, then open a session on its state |
| `--completions=bash\|zsh\|fish` | print a shell completion script |
| `--lsp` | run the Language Server: diagnostics, hover, completion and go-to-definition (Chapter 39) |

## Flags that shape a build

| Flag | Effect |
|---|---|
| `-o FILE` | output file (compile modes, `--target=js`) |
| `-I DIR` | add a module search directory |
| `--slim[=MODE]` | cut unused runtime subsystems from the binary; MODE is `safe`, `auto`, `max`, `none`, `help`, `list` or `verify` |
| `--standalone` | a module that cannot be embedded is a build error |
| `--static` | link the C++ runtime into the compiled binary (Linux, MinGW) |
| `--target=js` | transpile to JavaScript (Chapter 31) |
| `--target=arduino` | transpile to an Arduino sketch |
| `--sim` | `--target=arduino` only: build the sketch for this computer, on a simulated board |
| `--verify` | emit JavaScript only if it agrees with the interpreter |
| `--module` | JavaScript: export the subs, classes and `MAIN` instead of running |
| `--runtime` | write just the JavaScript runtime |
| `--fallback=wasm` | accept a program outside the JavaScript core, on the WebAssembly engine |

## Flags that change how a run behaves

| Flag | Effect |
|---|---|
| `--jit[=SPEC]` | **work in progress**: compile hot loops with the machine's C++ compiler while the program runs, off by default. SPEC words: `off`, `on`, `sync`, `verbose`, `stats`, `nocache`, `pch`, `threshold=N` (Chapter 44) |
| `--cnp[=SPEC]` | **work in progress**: the same by copy-and-patch — no compiler and no cache. SPEC words: `off`, `on`, `verbose`, `stats`, `threshold=N` (Chapter 44) |
| `--seed` | pin the random generator |
| `--auto` | install the modules the program uses that are missing, then run it; with a compile mode, install and then embed them |
| `--sandbox[=language]` | the program may compute, print and read standard input, and nothing else: no files, processes, sockets, native code or environment. Enforced by the interpreter and by the operating system under it; `=language` is the interpreter's checks alone |
| `--stack-size` | the stack of the program thread, and so the recursion ceiling |
| `--env-file FILE` | load `KEY=VALUE` lines into the environment |
| `--color=auto\|always\|never` | ANSI colour on stderr and in the REPL |
| `--watch` | re-run the program whenever it or a library file changes |
| `-l` | accepted; lines already arrive chomped |
| `-0` | NUL-separated records |
| `--name`, `--prefix DIR` | kernel name and location for `--jupyter-install` |

Flags are position-independent, and the perl-compatible one-liner family
(`-n`, `-p`, `-a`, `-F`, `-i`, `-0777`, `-M`) is documented in the CLI guide.

This list is checked against the binary rather than maintained by hand:
`tools/check-book-appendix.raku` reads `kFlagDocs` out of `src/main.cpp` — the
same table `--help` prints from — and fails if a flag is in one and not the
other. It was written because this appendix was missing twenty-seven of the
sixty-six flags the binary accepts, including a whole run mode. The table has
81 flags now.

## Subcommands

A first argument that is one of these words is a command, not a program file.

| Command | Effect |
|---|---|
| `install` | fetch, test and install modules into the store (Chapter 34) |
| `uninstall` | remove distributions this installer put there |
| `reinstall` | uninstall and install fresh |
| `test` | run a module's test suite against the store, installing nothing but its dependencies |
| `doc` | look a builtin, method or operator up in the language reference, offline |
| `upgrade` | replace this binary with a newer release |

## Environment variables

### Search and loading

| Variable | Effect |
|---|---|
| `RAKULIB` | extra module search paths; **both** `,` and `:` separate |
| `ROAST` | adds the specification suite's test-helper library |
| `RAKUPP_HOME` | where the binary considers itself installed |
| `RAKUPP_CONFIG` | override the settings file location |
| `RAKUPP_VM_NAME` | what `$*VM.name` answers instead of `cpp`, for modules that branch on it |
| `RAKUPP_NO_CWD_LIB=1` | do not search `lib` and `rakulib` under the current directory |
| `RAKUPP_OPT` | options prepended to every command line; options only, never a program |
| `RAKUPP_DEPS=0` | make `--auto` do nothing, for a harness whose children inherit it through `RAKUPP_OPT` |
| `XDG_CONFIG_HOME`, `XDG_CACHE_HOME` | the settings and cache directories |

### The parse cache

| Variable | Effect |
|---|---|
| `RAKUPP_PRECOMP_MODULES` | override the module-cache switch for one run |
| `RAKUPP_PRECOMP_FILES` | override the file-cache switch |
| `RAKUPP_NO_PRECOMP=1` | force both off |
| `RAKUPP_PRECOMP_DIR` | where cache entries live |

### The tier-up compilers

| Variable | Effect |
|---|---|
| `RAKUPP_JIT_THRESHOLD` | iterations before a loop counts as hot, for whichever back end is on |
| `RAKUPP_JIT_VERBOSE` | narrate every tier-up decision to stderr |
| `RAKUPP_JIT_DIR` | where `--jit` keeps compiled kernels |
| `RAKUPP_CNP` | a bundled binary only, which has no option surface of its own: `0` or `off` turns the baked-in back end off for one run, `1` on, anything else is read as a `--cnp` spec |
| `RAKUPP_CNP_X86=1` | run the x86-64 patcher on Linux, where it is refused at startup until it has been verified on ELF (it runs on macOS x86-64 without this) |

### Kernels and native bodies

| Variable | Effect |
|---|---|
| `RAKUPP_NO_KERNELS=1` | run subs and loops over plain Ints, Nums and Strs the ordinary way, not as kernels on machine values; under `--exe`, no integer sub twins either. The kernels' oracle is the same build with this set |
| `RAKUPP_KERNEL_TRACE=1` | name each sub and loop as its kernel is decided |
| `RAKUPP_HYPER_THREADS=N` | cap the threads the hyper-operator kernels split a long operand over; `1` is serial |
| `RAKUPP_NO_SELFPACK=1` | do not pack a fresh untyped Array of plain Nums into machine words; `RAKUPP_SELFPACK_MIN=n` is the fewest elements packed |
| `RAKUPP_NO_AOT=1` | `--exe`: keep the embedded modules' routines interpreted (`RAKUPP_AOT_VERBOSE=1` says why one stays interpreted) |
| `RAKUPP_AOT_REPORT=FILE` | `--exe`: write which module routines got native bodies and why the others did not |
| `RAKUPP_AOT_RUNLOG=FILE` | in a compiled binary: at exit, write how often each native module body was entered or declined |
| `RAKUPP_NO_PCH=1`, `RAKUPP_PCH_DIR` | `--exe`: compile without the cached precompiled header; where that cache is kept |

### The foreign-function interface

| Variable | Effect |
|---|---|
| `RAKUPP_FFI=0` | force the no-libffi fallback path |
| `RAKUPP_FFI=/path` | use *only* that library; if it fails to load, report and fall back rather than silently substituting the system copy |
| `RAKUPP_FFI_TRACE=1` | log every crossing, with marshalled arguments and raw returns |

### Memory

| Variable | Effect |
|---|---|
| `RAKUPP_CYCLE=0` | no cycle collection: reference counting alone (Chapter 12) |
| `RAKUPP_CYCLE=dry` | find cyclic garbage and count it, free nothing |
| `RAKUPP_CYCLE_STATS=1` | at exit, report collections, candidates, walks and what was freed |
| `RAKUPP_CYCLE_POISON=1` | give a collected object a class whose every method dies, instead of clearing it |
| `RAKUPP_CYCLE_TRIGGER=N` | collect whenever N candidates wait; `1` collects at nearly every scope exit |

### Concurrency

| Variable | Effect |
|---|---|
| `RAKUPP_GIL=1` | coordinate threads under one global lock. Since v3 the default is the other way: `start` blocks and workers run on all cores |
| `RAKUPP_PARALLEL=0` | a synonym for `RAKUPP_GIL=1`, kept from when parallel was the opt-in |
| `RAKUPP_PRIVATE_SLOTS=0` | take the stripe lock for every variable slot, including the ones proved private to one thread; an A/B instrument |
| `RAKUPP_STRIPE_STATS=1` | at exit, how often each stripe was found already held |
| `RAKUPP_RACE=1` | what `--detect-races` sets; `RAKUPP_RACE_TRACE=edges` or `=accesses` narrates what the detector sees |
| `RAKUPP_MAIN_THREAD=1` | run the program on the process's main thread, not the spawned 1 GiB one — for Cocoa, which refuses to create windows anywhere else |
| `RAKUPP_FREEZE_TRACE=1` | report any symbol-table mutation after concurrency engaged, and which thread did it |

### The regex engine

| Variable | Effect |
|---|---|
| `RAKUPP_LTM=0` | rank longest-token alternatives with the legacy greedy probe. The NFA ranker is the default, where it is gap-free |
| `RAKUPP_LTM_DEBUG=1` | rank both ways and print every disagreement — works without `RAKUPP_LTM` |
| `RAKUPP_LTM_RANKDUMP=1` | print each decided alternation's ranking |

### Diagnostics

| Variable | Effect |
|---|---|
| `RAKUPP_TRACE=1` | the module search path and every resolution |
| `RAKUPP_DUMPTOKENS=1` | the token stream |
| `RAKUPP_NO_DECLCHECK=1` | skip the undeclared-variable gate (Chapter 39) |
| `RAKUPP_NO_CALLCHECK=1` | skip the will-never-work call gate (Chapter 39) |
| `RAKUPP_BACKTRACE=0\|short\|full` | how much of an uncaught error's backtrace prints |
| `RAKUPP_HINTS=1` | performance advice on stderr, as `--hints` |
| `NO_COLOR`, `RAKUPP_COLOR=0\|1` | colour off by convention; forced off or on |
| `RAKUPP_UNICODE=0\|1` | force the non-ASCII glyphs in rakupp's own output off or on |
| `RAKUPP_ACTTRACE=1` | grammar action firing |
| `RAKUPP_TAP_TRACE=1` | the test harness's own emission |
| `RAKUPP_KEEPGEN=1` | keep the generated C++ from a compiling mode |
| `RAKUPP_DEBUG_MAKE` | grammar `make` diagnostics |
| `RAKUPP_NFC_CHECK=1`, `RAKUPP_DYNCACHE_CHECK=1`, `RAKUPP_ATTR_LAYOUT_CHECK` | each checks one cache against the slow path it replaces — the NFC flag on a string body, the dynamic-variable memo, the attribute layout — and reports a mismatch |

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
| `RAKUPP_STR_CENSUS` | count the strings a `Value` makes, copies and writes, by length — the measurement that picked VALUE32-PLAN's design C, a 15-byte inline string, over B, a body for every string (tools/str-census.md) |
| `_GLIBCXX_USE_CXX11_ABI` | relevant only because it is the ABI break copy-on-write strings caused (Chapter 9) |

The pointer census is a good example of the project's habit: before collapsing
`Value`'s pointers into tag-dispatched slots, a special build **counts** which
sets are live simultaneously, rather than reasoning about what the type tags
suggest.

## A note on flags that change behaviour

Two of the variables above change *results* rather than speed: `RAKUPP_LTM` and
`RAKUPP_GIL`. The policy for both is the same: the old path stays available
for at least one release after the default changes, so a regression can be
bisected to the switch rather than to the release. Both defaults have changed.
The NFA ranker and parallel workers are what runs, and the two variables are
now the way back.

`RAKUPP_VM_NAME` is a third, and a different kind. It changes one answer —
`$*VM.name`, and the `$*RAKU.VMnames` list it has to stay a member of — so that
a module which branches on the name and dies on the `else` takes its `moar`
branch instead. Nothing sets it on your behalf, and no measurement this project
publishes runs with it set: the default is what this engine is, and asserting a
dialect is the caller's statement, not the engine's. `$*VM.config` is untouched
either way, because the toolchain values in it were already real.

Everything else here either reports something or selects a code path that is
required to produce identical output.
