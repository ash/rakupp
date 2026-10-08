# `--sandbox` — plan

*Written 2026-10-08 at `e9e11f91`, before any code. Goal: `rakupp --sandbox
prog.raku` runs a program somebody else wrote with no way to reach outside the
process except through its standard handles. It may not read or write files,
start processes, open sockets, call native code or see the environment, and
everything else in the language keeps working. The idea was "6. A capability
sandbox" in [V5-IDEAS.md](V5-IDEAS.md), from
[CLI-BORROW-PLAN.md](CLI-BORROW-PLAN.md)'s Deno-style `--allow-*` entry; this
file answers that entry's two open questions.*

**The number a stranger can re-measure:**

```bash
rakupp t/sandbox/run.raku                   # every probe, both ways
rakupp --sandbox -e 'say slurp "/etc/hosts"'
```

Every probe in `t/sandbox/` must be refused under `--sandbox` and must work
without it. The second half is what proves a probe can fail: a probe that is
refused either way tests nothing.

## Who runs other people's code

- **Graders, judges, course runners.** A new process per submission and per
  test. Startup is about 3 ms and the empty program takes about 7 MB, so the
  sandbox has to cost nothing measurable at startup either.
- **`rakupp --mcp`** ([MCP.md](../../guide/integrations/MCP.md)) gives an agent
  "exactly the trust that giving it a shell does". `--mcp --sandbox` is the
  variant that can be offered to an agent which should only compute.
- **raku.online's live runner** and any hosted playground.
- **`rakupp install` / `rakupp test`**, which run distribution code fetched
  from the network.
- **Embedding hosts** (`rakupp.h`): a plugin system, or a trusted `plraku`
  ([V5-IDEAS.md](V5-IDEAS.md), "11. Raku inside the database"), needs the same
  switch through `RkConfig`.

## The contract

Under `--sandbox` a program **can** compute; use threads, `start`, Promises,
Supplies, `react`, `sleep` and timers; read `$*IN` and write `$*OUT` and
`$*ERR` (`say`, `print`, `note`, `get`, `lines()` from standard input,
`prompt`); `EVAL`; `exit`; and load modules from the search path the HOST gave
it (the built-in modules, `-I`, `RAKULIB`, the install store).

It **cannot**:

| capability | what is refused |
|---|---|
| read | opening, slurping or reading lines from a file; listing a directory; testing a path (`.e`, `.f`, `.d`, `.s`, `.modified`, …); `chdir` / `indir` (each tests a directory); `$*ARGFILES` over named files; `EVALFILE`; `use lib` |
| write | `spurt`, writing opens, `mkdir`, `rmdir`, `unlink`, `rename`, `move`, `copy`, `chmod`, `link`, `symlink`, `-i` |
| run | `run`, `shell`, `qx` / `qqx`, `Proc::Async`, `Proc` |
| net | `IO::Socket::INET`, `IO::Socket::Async` (TCP and UDP), name lookups |
| ffi | `is native` routines, NativeCall, native extension modules |
| env | `%*ENV` starts empty; a program may still set keys in its own copy |

Pure path arithmetic stays: `.IO`, `.basename`, `.extension`, `.parent`,
`.child`, `.add`, `.absolute`, `.relative`, `.cleanup`, `.parts`, `.Str`.

**A refusal is an exception, not a crash:** `X::SecurityPolicy::Sandbox`, a
rakupp-only class under Rakudo's existing `X::SecurityPolicy` (whose
`X::SecurityPolicy::Eval` both engines already have), so `CATCH { when
X::SecurityPolicy { … } }` written for either engine catches it. It carries
`.operation` (`open`, `run`, `IO::Socket::INET.new`, …) and `.capability`
(`read`, `write`, `run`, `net`, `ffi`). The environment is hidden rather than
refused, so it has no capability of its own until S4 grants one. A refusal
throws even where the operation itself would `fail` (as `open` does on a
missing file): a program finds out at once that it is sandboxed, instead of
carrying a Failure into code that reads it as "the file is not there". A
program may catch it and carry on; nothing it catches grants anything.

**The engine itself does not write either:** the precompilation cache is
neither read nor written under `--sandbox`, so a sandboxed run leaves no files
behind.

The two questions CLI-BORROW-PLAN left open:

- **Is NativeCall simply off?** Yes. Native code can do anything the process
  can, so a sandbox with FFI granted is not a sandbox. If `--allow-ffi` ever
  exists (S4), its help text says it turns the sandbox off.
- **Is `--allow-read` prefix-based?** By path components of the resolved path,
  never by string: `--allow-read=/data` grants `/data/x` and refuses
  `/data2/x`, and a symlink inside `/data` that points outside is refused. Where
  the OS layer exists it enforces the same list in the kernel, which closes the
  race between checking a path and opening it.

## Two layers

1. **The language layer**, on every platform, embedded hosts and Raku.js
   included. Each operation in the table checks the policy before it touches
   the OS. This is what gives a program a typed exception it can read, and it is
   the only layer an embedding host gets, because a database backend cannot
   confine its own process.
2. **The OS layer**, where the kernel offers one to an unprivileged process.
   This is what makes a bug in the interpreter, a memory-safety bug in C++
   included, unable to turn into file or network access:
   - macOS: Seatbelt (`sandbox_init` with a deny-by-default profile).
     Prototyped 2026-10-08 on Darwin 27: reading outside the allowed subpaths,
     writing, `connect`, `posix_spawn` and `fork` all fail with EPERM; threads,
     `getentropy`, `malloc` and the standard handles work.
   - Linux: seccomp-bpf (no `execve`, no process `clone`, no `socket` of any
     family, no `ptrace`) plus Landlock for paths (kernel 5.13+; TCP needs
     ABI 4, 6.7+).
   - OpenBSD: `pledge("stdio rpath")` and `unveil` on the module search path.
   - Windows: none in this plan. The language layer only, and the docs say so.

   The layer is entered after the engine has read the program, its own search
   path and the time zone, and before the first token is parsed. Module loading
   happens during parsing, so the module search path stays readable, read-only.

**Decided (the maintainer's call, 2026-10-08):** `--sandbox` fails closed
where the platform has no OS layer or it cannot be applied (an old kernel, a
container that forbids seccomp, Windows): exit 4 with the reason, and no
warning-and-continue. `--sandbox=language` runs with the language layer alone,
explicitly and silently.

## Phases

### S0 — the gate first

`t/sandbox/run.raku`: one probe per row of the table, each run as a child
`rakupp --sandbox -e …`, and each must (a) exit with
`X::SecurityPolicy::Sandbox` in its output and (b) leave no side effect (the
file it tried to create does not exist, the listener it tried to reach saw no
connection). Each probe also runs WITHOUT `--sandbox` and must succeed. The
allowed list (threads, stdin, `EVAL`, a module from `-I`, …) must work under
`--sandbox`.

### S1 — the language layer

- `--sandbox` on the command line, accepted where code runs (a program, `-e`,
  the REPL, `--mcp`); refused by the source tools and the compilers.
- `RkConfig.sandbox` for embedding hosts. It is one-way: nothing turns it off
  in that process.
- One process-global policy (`src/Sandbox.cpp`), set before the interpreter is
  built and read-only after, so threads read it without a lock.
- `X::SecurityPolicy::Sandbox` with its two attributes.
- The checks, at the fewest points that cover each row (listed in "Where it
  stands" once found).
- `%*ENV` empty; precomp off; `use lib` and `EVALFILE` refused.
- Docs: `docs/guide/SANDBOX.md`, the flag in CLI.md, and the "No sandbox"
  lines in EMBEDDING.md and MCP.md updated to what is true.

### S2 — the OS layer

Seatbelt, seccomp plus Landlock, pledge plus unveil, as above;
`--sandbox=language`; the full build report (`-V`) names the layer this binary
has.

How it went (2026-10-08, `src/SandboxOs.cpp`, CLI-only, entered in `main()`
before any thread):

- **macOS** — Seatbelt, deny by default. Allowed: `file-read*` beneath the
  program file and the module search path (canonical paths), the time-zone
  files and `/dev/{null,zero,random,urandom}`; `file-read-metadata` anywhere
  (getcwd and realpath walk every ancestor); `sysctl-read`; `process-info*`
  and `signal` on itself; `file-ioctl` for TIOCGETA, TIOCSETA, TIOCSETAW,
  TIOCSETAF and TIOCGWINSZ only, which is what the REPL's raw mode and width
  need and not TIOCSTI. Without that ioctl rule the REPL fell back to plain
  lines. `<sandbox.h>` is declared rather than included: on a case-insensitive
  disk `-Isrc` makes the include find `src/Sandbox.h`.
- **Linux** — Landlock (every access right the ABI knows handled; read-only
  rules for the same list plus the system library dirs and `ld.so.cache`; TCP
  rules from ABI 4, signal and abstract-socket scoping from ABI 6) and a
  seccomp-bpf deny list: `execve`/`execveat`, `fork`/`vfork`, `clone` without
  CLONE_THREAD, `clone3` as ENOSYS (the C library falls back to `clone` for
  its threads), every `socket`/`connect`/`bind`/`listen`/`accept`, signals
  to any pid but our own, `tkill`, ioctl TIOCSTI/TIOCLINUX, `ptrace`,
  `process_vm_*`, `pidfd_getfd`, io_uring, mount and namespace calls, module
  and kexec calls, `bpf`, `perf_event_open`, `userfaultfd`, keyrings — and
  the chmod/chown/xattr/utimes family, because **Landlock has no right for a
  file's metadata**: the first Linux gate run had `chmod` succeed under the
  kernel alone. The structs and syscall numbers are declared, not taken from
  `<linux/landlock.h>`, which the manylinux 2.28 headers lack.
- **The self-test**: `RAKUPP_SANDBOX_SELFTEST=os-only` turns the interpreter's
  checks off (`g_sandboxChecks`; the refusals test that flag, the hiding tests
  `g_sandboxed`). main() honours it only after the OS layer is in force, so it
  can never produce an unconfined run. It is what lets the gate see the kernel
  alone.
- **Search path**: under `--sandbox` the cwd's implicit `lib`, `.` and
  `rakulib` are dropped (`defaultCwdLibPaths`, `precompSearchPath`); a host
  passes `-I lib`. Otherwise the whole working tree would be readable.
- **Refused alongside**: `--profile=FILE`. The REPL keeps no history file.
- **OpenBSD** (pledge + unveil) is not written: no machine to test it on, so
  it reports "none" and refuses as Windows does.

### S3 — limits

`--limit-cpu=SECONDS`, `--limit-memory=SIZE`, `--limit-output=SIZE`, a wall
clock. A sandbox stops a program reaching out; a limit stops it eating the
host. Both are wanted by every consumer above, but they are separate switches.

### S4 — grants

`--allow-read[=PATH,…]`, `--allow-write[=PATH,…]`, `--allow-net[=HOST[:PORT],…]`,
`--allow-run[=PROG,…]`, `--allow-env[=NAME,…]`. Any `--allow-*` turns the
sandbox on: it means "only this".

### S5 — consumers

`rakupp test --sandbox` over the ecosystem sweep (V5-IDEAS's number: a planted
exfiltration in a test file fails closed, gated); the raku.online runner; a
hosted `--mcp --sandbox`; a trusted `plraku`.

## Gates, every batch

- `t/sandbox/run.raku`, every lane, on macOS AND on a Linux with Landlock.
- `tools/run-roast.raku`: the sandbox is off by default, so Roast must not move.
- `t/run.raku`'s CLI section, for the new flag and its refusals.
- A MinGW `-fsyntax-only` pass over every touched file: `SandboxOs.cpp`
  compiles everywhere, and its Windows branch is the one no local build sees.

## Where it stands

*2026-10-08, committed on top of `425d70df`:* S0, S1 and S2 are built.

**S2:**
- `t/sandbox/run.raku` now runs each probe in four lanes (without, `--sandbox`,
  `--sandbox=language`, the kernel alone): **341/341 on macOS** (Seatbelt) and
  **341/341 on Linux 6.17** (Landlock ABI 7 + seccomp, x86_64). A control
  build whose `sandboxOsEnter` returned success without applying anything
  failed 46, every kernel-alone check, so that lane can fail. With Landlock
  hidden by a seccomp wrapper, `-V` says why, `--sandbox` exits 4 and the gate
  passes its no-OS-layer form (201/201).
- Native code under the kernel alone on Linux: `kill(getppid(), 0)`,
  `socket(AF_UNIX)`, `socket(AF_INET)` and `fork()` all answer -1.
- The REPL under Seatbelt, driven by `expect`: raw mode on, answers, refuses.
- riscv64 compiles `SandboxOs.cpp` clean (`-Wall -Wextra`, Docker); aarch64
  shares its syscall table and was not compiled separately. Every touched file
  passes MinGW `-fsyntax-only`.
- Roast at `aa7b2ab9`: 219,207/219,207 without skip/todo and 1,425 files,
  base and sandbox build alike, lists identical.
- Cost to a run without `--sandbox`: no new code runs. Startup touches ~9 more
  pages (593 against ~584: the binary is 30 KB bigger and laid out
  differently), about 0.7% of startup instructions, counted by
  `/usr/bin/time -l` with the kernel's fault handling in them.

**S0 and S1:**

- **The gate:** `t/sandbox/run.raku`, 198 checks, PASS. The same file run by
  the `e9e11f91` binary fails 77, which is every sandboxed probe, while all 57
  "works without `--sandbox`" checks pass there too.
  `tools/embed-smoke.raku` gained check 1c, `tools/embed/embed-sandbox.c`:
  a C host with `RkConfig.sandbox` whose secret environment variable the Raku
  code cannot see.
- **Where the checks sit** (`src/Sandbox.h` / `.cpp` hold the switch, the
  exception and the name tables):
  - builtin subs: each table entry is re-wrapped in the constructor, before
    anything resolves one (`sandboxWrapBuiltin`); `open('-')` and `slurp()`
    of standard input pass;
  - IO::Path, Str and IO::Handle methods: one check at the head of their
    arms in `methodCallPart3` (`sandboxMethodGate`), by method name;
  - `Interpreter::ioFsPath`, which nearly every file operation turns its path
    through, refuses as a backstop;
  - processes: `Proc::Async`, plus backstops in `spawnChildStart` /
    `spawnWithInput`; `$*DISTRO` on macOS skips its `sw_vers`;
  - sockets: `IO::Socket::INET.new`, `IO::Socket::Async`;
  - native: `callNative`, `Pointer` from an address and `.deref`,
    `Rakupp::Internals::Blob`, the `nativecast`/`cglobal`/`rakupp-ext-load`
    subs;
  - files reached by other doors: `$*ARGFILES`, `use lib` (refused at run
    time and skipped by the parser's harvest), `CompUnit::Loader
    .load-source-file`, `Distribution::Path.new`, `Grammar.parsefile`,
    repository `install`/`uninstall`, the NQP file ops (except a read-only
    `nqp::open` of `/dev/urandom`);
  - environment: `%*ENV` empty, `syncEnvToProcess` a no-op, `$*TMPDIR` and
    `$*VM.config` not read from it;
  - precomp off (`precompHalf`); `-i` and `--jit` refused with `--sandbox`.
- **Open:** `--jupyter --sandbox` (refused for now); the bindings do not
  expose `RkConfig.sandbox` yet; OpenBSD's pledge + unveil; `open('-')` and
  `'-'.IO.lines` are not standard input on rakupp at all (an existing
  divergence from Rakudo, filed separately), so the sandbox only passes them
  through. Next: S3, limits.
