# `--sandbox`: running code you did not write

`rakupp --sandbox` runs a program that may compute, print and read its
standard input, and may not touch anything else: no files, no processes, no
network, no native code, no environment.

```bash
rakupp --sandbox submission.raku < input.txt
```

```
$ rakupp --sandbox -e 'say (1..10).grep(*.is-prime).sum; say slurp "/etc/passwd"'
17
slurp is not allowed in the sandbox: it needs read access
  (X::SecurityPolicy::Sandbox)
  in block <unit> at -e line 1
      1 | say (1..10).grep(*.is-prime).sum; say slurp "/etc/passwd"
```

It is meant for whoever runs other people's code: a grader or a judge, a
playground, an AI agent that should only calculate (`rakupp --mcp --sandbox`),
or a program that embeds Raku++ and lets its users script it
(`RkConfig.sandbox`). The flag is rakupp's own; Rakudo has no counterpart.

Two layers do the confining: the interpreter checks every operation before it
reaches the operating system, and under those checks the kernel confines the
whole process, on macOS and on Linux ([Two layers](#two-layers)).

## What a sandboxed program can do

Everything that stays inside the process:

- compute, with the whole language: classes, grammars, `EVAL`, exact
  arithmetic, Unicode;
- `say`, `print`, `put`, `note`, `printf`, `dd`, and `$*OUT` / `$*ERR`;
- read standard input: `get`, `lines()`, `slurp()`, `prompt`, `$*IN`, and the
  `-n` / `-p` loops over standard input;
- threads, `start`, Promises, Supplies, `react`, Channels, `sleep`, timers,
  signals;
- use modules from the search path the host gave it: the built-in ones
  (`Test` among them), `-I`, `-M`, `RAKULIB` and installed distributions;
- work with paths as text: `.IO`, `.basename`, `.extension`, `.parent`,
  `.child`, `.add`, `.absolute`, `.relative`, `.cleanup`, `.parts`;
- `exit` with any code, and `die`.

```bash
printf 'b\na\nc\n' | rakupp --sandbox -e 'say lines().sort.join(",")'     # a,b,c
printf 'one\ntwo\n' | rakupp --sandbox -ne '.uc.say'                     # ONE TWO
```

## What it cannot do

| needs | refused |
|---|---|
| `read` | `open`, `slurp` and `.IO.slurp` / `.lines` / `.words` of a file; `dir`, `.dir`; file tests (`.e`, `.f`, `.d`, `.s`, `.modified`, `.mode`, `~~ :e`, …); `.resolve`, `.readlink`, `.watch`; `chdir`, `indir`; `$*ARGFILES` over files named in `@*ARGS` (so `lines()` with file arguments); `EVALFILE`; `use lib`; `Grammar.parsefile`; NQP's file ops |
| `write` | `spurt`, `open(:w)` and the other writing modes, `mkdir`, `rmdir`, `unlink`, `rename`, `move`, `copy`, `chmod`, `link`, `symlink`, temporary files, installing or uninstalling a distribution |
| `run` | `run`, `shell`, `qx` / `qqx`, `Proc::Async`, `Proc.spawn` |
| `net` | `IO::Socket::INET` and `IO::Socket::Async` (TCP and UDP), listening or connecting |
| `ffi` | `is native` routines (NativeCall), native extension modules, `nativecast`, `cglobal`, a `Pointer` made from an address or dereferenced, `Rakupp::Internals::Blob` |

The environment is hidden rather than refused: `%*ENV` starts empty, and a
program may still set keys in its own copy, which reaches nothing outside it.
`$*HOME` is therefore undefined, and `$*TMPDIR` is the platform default.

```
$ rakupp --sandbox -e 'say %*ENV.elems; %*ENV<GREETING> = "hi"; say %*ENV<GREETING>'
0
hi
```

## The refusal

A refused operation throws `X::SecurityPolicy::Sandbox`. It sits under
`X::SecurityPolicy`, the class both engines already have for refusing an
`EVAL`, so a `CATCH { when X::SecurityPolicy { … } }` catches it. It carries two
attributes:

- `.operation` — what the program asked for: `open`, `slurp`, `IO::Path.e`,
  `run`, `IO::Socket::INET.new`, `getpid (is native)`, …
- `.capability` — what that needed: `read`, `write`, `run`, `net` or `ffi`.

```
$ rakupp --sandbox -e 'try dir("."); say $!.message; say $!.capability'
dir is not allowed in the sandbox: it needs read access
read
```

A refusal throws even where the operation would otherwise `fail` (as `open` of
a missing file does), so a program learns at once that it is sandboxed instead
of reading the Failure as "that file is not there". Catching the exception
grants nothing; the program just carries on without the file.

## Modules

A sandboxed program loads modules from wherever the host said modules live:
`-I DIR` and `-M Module` on the command line, `RAKULIB`, the install store.
The current directory is not among them: the `lib/` and `rakulib/` beside the
program, which an ordinary run searches, are searched only when the host
passes them (`-I lib`). `use lib` inside the program is refused, because
pointing the loader at a directory is a way of reading it. The precompilation cache is neither read nor
written, so a sandboxed run leaves no files behind. Code in a module runs under
the same rules as the program that loaded it, so a module that reads a data
file of its own (`%?RESOURCES<table.txt>.slurp`) or calls native code while it
loads is refused too.

## Standard input, not files

`lines()`, `get` and `slurp()` read `$*ARGFILES`, which is standard input when
`@*ARGS` names no files. When it does name some, reading them is refused, so
hand the program its data on standard input:

```bash
rakupp --sandbox count.raku < data.txt      # yes
rakupp --sandbox count.raku data.txt        # refused when count.raku reads lines()
```

`'-'.IO.slurp` reads standard input as well.

## With `--mcp`

`rakupp --mcp --sandbox` serves the `raku` and `raku-parse` tools
([MCP.md](integrations/MCP.md)) with every call sandboxed. An agent can compute,
define routines that persist, and parse with grammars; a call that reaches for
a file comes back as a tool error, and the session carries on.

## Embedding

A host sets `RkConfig.sandbox` ([EMBEDDING.md](EMBEDDING.md)). The Raku code it
evaluates follows the same rules as under `--sandbox`, enforced by the
interpreter's checks alone. There is no OS layer, because the process belongs
to the host, which still needs its own files and sockets.

```c
RkConfig cfg = {0};
cfg.size = sizeof cfg;
cfg.sandbox = 1;
RkInterp rk = rk_new(&cfg);
```

The switch is process-wide and one-way. Once an interpreter is created with it,
every later interpreter in that process is sandboxed too.
`tools/embed/embed-sandbox.c` is a complete host, run by
`tools/embed-smoke.raku`.

## Two layers

Every refusal above is the interpreter's. Under those checks the operating
system confines the whole process, so a bug in the interpreter, a memory bug in
its C++ included, cannot turn into file, process or network access either:

| platform | the OS layer |
|---|---|
| macOS | Seatbelt, with a deny-by-default profile |
| Linux 5.13 or later, with Landlock enabled | Landlock for files; seccomp for processes, sockets, signals, `ptrace`, a file's mode and owner, and the kernel's administrative calls |
| anything else (Windows, OpenBSD, an older Linux) | none |

`rakupp -V` says what this machine has:

```
$ rakupp -V | grep Sandbox
Sandbox built-in checks + Seatbelt
```

**Where the OS layer is missing or cannot be applied, `--sandbox` refuses to
run** — exit 4, with the reason, before a line of the program is read:

```
$ rakupp --sandbox prog.raku
rakupp: --sandbox: this kernel (5.10.76) has no Landlock; it needs Linux 5.13 or later.
  --sandbox=language runs with the interpreter's own checks only.
```

That happens on an older kernel, on Windows, and in a container whose own
seccomp profile hides Landlock. **`--sandbox=language`** asks for the checks
alone, on any platform: the choice where the process is already confined some
other way (a container, a VM, a user of its own), and the only one where the
platform offers nothing.

What the OS layer still allows the process: reading the program file, the
module search path and the system's libraries and time zones; writing to the
standard handles it started with; threads; signals to itself; and, for the
REPL, reading and setting its terminal's mode and size. It also allows file
**metadata** anywhere, because finding the current directory needs it: a stat
can tell a program that a path exists and how big it is, never what is in it.
The interpreter's checks refuse those stats in Raku code, so only a program
that had already got past them could make one.

## Where it stops

- **Without the OS layer, the checks stand alone.** Under
  `--sandbox=language`, and in an embedding host, a bug in the interpreter
  itself could get past them. For code that may be hostile there, confine the
  process another way as well.
- **No limits yet.** A program can still loop forever, allocate until the
  machine swaps, or print without end. Bound it from outside (`ulimit`,
  `timeout`, cgroups); `--mcp` has its own `--timeout=SECS`.
- **What a program can still learn:** its own process id, the time, `$*USER`,
  `$*KERNEL.hostname`, the platform names in `$*KERNEL` and `$*DISTRO`, and the
  paths in `$*CWD`, `$*PROGRAM` and `$*EXECUTABLE`.
- **One file is readable:** `nqp::open` may read `/dev/urandom` (or
  `/dev/random`), the entropy that crypto modules draw on.
- **It confines code that runs.** It goes with a program, `-e`, the REPL and
  `--mcp`. The compilers and source tools run no code, and `--jupyter` is not
  confined yet, so `--sandbox` there is an illegal option. `-i`, `--jit` and
  `--profile=FILE` are refused alongside it, because each writes files
  (`--profile` alone prints to stderr and is fine). `--watch` reruns the
  program with `--sandbox` each time.
