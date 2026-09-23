# Proc and Proc::Async — semantics sheet

Provenance: Rakudo tag `2026.08`, files `src/core.c/Proc.rakumod` (255
lines) and `src/core.c/Proc/Async.rakumod` (583) read in full on
2026-09-23; `IO/Pipe.rakumod` (84) for the pipe objects a Proc hands out;
`Exception.rakumod` for `X::Proc::Unsuccessful` and `X::OS`;
`Kernel.rakumod` for the signal-name rule behind `.kill(Str)`;
`Process.rakumod` (102) read and found to hold nothing `run` depends on.
Out of scope: `:pty` (the two exception types its stderr paths name,
`X::Proc::Async::PtyOnlyStdOut` and `PtyOrStdErr`, are not defined
anywhere at this tag), Windows argument quoting (`:win-verbatim-args`,
`:arg0` on Windows), the JVM and JS backends, and the `:scheduler`
argument of `start`. Oracle: Homebrew Rakudo v2026.08 on macOS. Compared
against Raku++ 4.0.1-84-ga4291988 (build-arm64, 2026-09-23). Format and
legend: [README.md](README.md).

Where this sits against the declared spec: Rakudo passes all ten
`S17-procasync` files and `S32-io/pipe.t` on this machine, and not
`S29-os/system.t`; Raku++ passes three of the ten
(`many-processes-no-close-stdin.t`, `no-runaway-file-limit.t`, `print.t`)
and neither of the other two. The rules below are what those files and
real programs lean on: what a `run` returns for every kind of outcome,
when a Proc throws, how the three streams are captured, fed, merged,
bound, decoded and split, when the parent blocks, and the Proc::Async
protocol of Supplies, Promises and exceptions. 9 of the 37 items are
neither fully documented nor fully asserted by Roast. Two Rakudo
behaviours are recorded as bugs and ten as quirks; step two should not
imitate the bugs, and one of the quirks (the two-pipe deadlock, PA-06)
is something Raku++ already does better.

Every probe ran with `alarm 20`, standard input from `/dev/null`, in a
fresh sandbox directory per engine and per line (so a file or directory
made by one probe does not exist for the next); every wait on a process
is bounded by a `Promise.in` timeout, and the items whose statement says
"timing-dependent" use three-second margins. No output contains the
sandbox path. A child that inherits the parent's standard output
interleaves unpredictably with the parent's buffered output, so the
probes let such a child run only before the parent prints anything.

## A. `run`, `shell` and the Proc they return

### PA-01  run and shell return a Proc                                  D:yes R:yes V:spec
`run(*@args)` spawns the first positional as the program with the rest as
its arguments, with no shell in between; `shell($cmd)` runs `/bin/sh -c
$cmd` (`%*ENV<ComSpec> /c` on Windows). Both return a `Proc` after the
process has ended, unless a pipe was requested (PA-05). `.exitcode` is
the exit status (`exit 256` reads as 0: eight bits), `.signal` 0 for a
normal exit, `.pid` the child's Int process id, `.command` a List of the
positionals as given — for `shell` a one-element List holding the command
string — `.Bool` True only when exit code and signal are both 0,
`.Numeric` the exit code. `.in`, `.out`, `.err` are the `IO::Pipe` type
object for a stream that was not requested; `.os-error` is the `Str` type
object unless the spawn itself failed (PA-02). A `shell` command sees
`$0` as `/bin/sh`, inherits the environment, and takes `:in`, `:out`,
`:err`, `:merge` exactly like `run`.
```
my $p = run("sh", "-c", "exit 3"); say $p.^name, " ", $p.exitcode, " ", $p.signal, " ", $p.pid.^name, " ", ($p.pid > 0), " ", $p.command.raku, " ", ?$p, " ", +$p, " ", $p.defined, " ", run("true").exitcode, " ", run("true").signal, " ", ?run("true"), " ", run(<sh -c>, "exit 255").exitcode, " ", run("sh", "-c", "exit 256").exitcode, " ", run("false").exitcode, " ", $p.out.raku, " ", $p.in.raku, " ", $p.err.raku, " ", (try $p.os-error.raku) // $!.^name
# rakudo 2026.08: Proc 3 0 Int True ("sh", "-c", "exit 3") False 3 True 0 0 True 255 0 1 IO::Pipe IO::Pipe IO::Pipe Str
my $p = shell("exit 4"); say $p.^name, " ", $p.exitcode, " ", $p.signal, " ", $p.command.raku, " ", shell("printf hi", :out).out.slurp(:close).raku, " ", shell("printf \$0", :out).out.slurp(:close).raku, " ", shell("printf x; exit 2", :out).exitcode, " ", shell("printf x; exit 2", :out).out.slurp(:close).raku, " ", shell("printf \$PATH", :out).out.slurp(:close).chars > 0, " ", do { my $s = shell("cat", :in, :out); $s.in.print("sh-in"); $s.in.close; $s.out.slurp(:close).raku }, " ", shell("echo o; echo e >&2", :merge).out.slurp(:close).lines.sort.raku
# rakudo 2026.08: Proc 4 0 ("exit 4",) "hi" "/bin/sh" 2 "x" True "sh-in" ("e", "o").Seq
```
rakupp 4.0.1-84: differs — the second line matches; in the first, the three unrequested pipes print as hash dumps of the process record rather than the `IO::Pipe` type object, and `.os-error` is `X::Method::NotFound`.

### PA-02  A command that cannot be started                             D:partial R:yes V:quirk
When the program cannot be spawned (not found, an empty name, an unusable
`:cwd`), `run` still returns a Proc and does not throw: `.exitcode` is
-1, `.signal` 254, `.pid` Nil, the Proc False, `.os-error` a non-empty
Str, `.command` as given. Requested pipes exist and read as empty
(`.out.slurp(:close)` is `""`, `.lines` an empty Seq, `.get` Nil), and a
`print` to a requested `.in` returns True. `shell` of a missing command
spawns the shell itself successfully, so that is an ordinary exit 127 with
a pid and no os-error. The 254 is the low byte of the spawn error code
(-2, PA-20) and carries no meaning; the docs promise the -1 and say
nothing of the signal, and Roast asserts only the -1 (in the sink message,
PA-03). Implement the -1, the Nil pid and the os-error; the 254 is
recorded so nobody reads meaning into it.
```
my $p = run("nonexistent-cmd-zz"); say $p.^name, " ", $p.exitcode, " ", $p.signal, " ", ?$p, " ", $p.pid.raku, " ", $p.command.raku, " ", run("nonexistent-cmd-zz", "a", :out).out.slurp(:close).raku, " ", run("nonexistent-cmd-zz", :err).err.slurp(:close).raku, " ", run("nonexistent-cmd-zz", :merge).out.slurp(:close).raku, " ", shell("nonexistent-cmd-zz 2>/dev/null").exitcode, " ", shell("nonexistent-cmd-zz 2>/dev/null").signal, " ", shell("nonexistent-cmd-zz 2>/dev/null").pid.^name, " ", run("nonexistent-cmd-zz", :out).out.lines.raku, " ", run("nonexistent-cmd-zz", :out).out.get.raku, " ", run("nonexistent-cmd-zz", :in).in.print("x").raku, " ", run("", :out).exitcode, " ", run("", :out).pid.raku, " ", (try $p.os-error.^name) // $!.^name, " ", (try ($p.os-error.chars > 0)) // $!.^name, " ", (try run("true").os-error.raku) // $!.^name
# rakudo 2026.08: Proc -1 254 False Nil ("nonexistent-cmd-zz",) "" "" "" 127 0 Int ().Seq Nil Bool::True -1 Nil Str True Str
```
rakupp 4.0.1-84: differs — a program that cannot start is reported as exit 127, signal 0, with a pid, for `run("nonexistent-cmd-zz")` and `run("")` alike; `.out.lines` is a List; `.os-error` does not exist.

### PA-03  Sinking a Proc throws X::Proc::Unsuccessful                  D:yes R:yes V:spec
A Proc in sink context — a bare `run`/`shell` statement, `sink $proc`,
`$proc.sink`, or a sunk `.out.close`/`.err.close`/`.in.close` (they return
the Proc) — throws `X::Proc::Unsuccessful` when the exit code is not 0 or
the process died by a signal, and yields Nil otherwise. Nothing is thrown
when the Proc is assigned, bound, tested with `.so`, assigned to `$ =`, or
when only `.slurp(:close)` is used (the close inside `slurp` is not
sunk). A spawn failure (PA-02) throws when sunk like any failure. The
exception's `.proc` is the Proc; its one-line message names the first
word of the command and says `exit code: N, signal: N`, and a second line
`(OS error = …)` is added when `.os-error` is set — Roast asserts the
`exit code: -1` and `OS error = ` fragments. `qx` never sinks its Proc
(PA-37).
```
sub t(&c) { (try { c(); "ok" }) // $!.^name }; say t({ run("sh", "-c", "exit 3") }), " ", t({ run("true") }), " ", t({ my $p = run("sh", "-c", "exit 3"); 1 }), " ", t({ $ = run("sh", "-c", "exit 3") }), " ", t({ run("sh", "-c", "exit 3").so }), " ", t({ sink run("sh", "-c", "exit 3") }), " ", t({ sink run("true") }), " ", t({ run("nonexistent-cmd-zz") }), " ", t({ shell("exit 2") }), " ", t({ run("sh", "-c", "exit 3", :out).out.close }), " ", t({ run("sh", "-c", "exit 3", :out).out.slurp(:close) }), " ", t({ run("sh", "-c", "exit 3", :out).out.close; 1 }), " ", t({ run("sh", "-c", "exit 3").sink }), " ", run("true").sink.raku, " ", t({ run("sh", "-c", "kill -TERM \$\$") }), " ", t({ qx{exit 3} }), " ", (X::Proc::Unsuccessful ~~ Exception), " ", (try { run("sh", "-c", "exit 3"); 1 }) // (($!.message ~~ /"exit code: 3, signal: 0"/).so ~ ":" ~ $!.message.lines.elems), " ", (try { run("nonexistent-cmd-zz"); 1 }) // (($!.message ~~ /"exit code: -1"/).so ~ ":" ~ ($!.message ~~ /"OS error = "/).so ~ ":" ~ $!.message.lines.elems)
# rakudo 2026.08: X::Proc::Unsuccessful ok ok ok ok X::Proc::Unsuccessful ok X::Proc::Unsuccessful X::Proc::Unsuccessful X::Proc::Unsuccessful ok X::Proc::Unsuccessful X::Proc::Unsuccessful Nil X::Proc::Unsuccessful ok True True:1 True:True:2
say (try { run("sh", "-c", "exit 3"); 1 }) // ($!.proc.^name ~ ":" ~ $!.proc.exitcode ~ ":" ~ $!.proc.signal ~ ":" ~ $!.proc.command.raku), " ", (try { run("sh", "-c", "kill -TERM \$\$"); 1 }) // ($!.proc.exitcode ~ ":" ~ $!.proc.signal ~ ":" ~ ($!.message ~~ /"signal: 15"/).so)
# rakudo 2026.08: Proc:3:0:("sh", "-c", "exit 3") 0:15:True
```
rakupp 4.0.1-84: differs — a bare `run` statement throws, but `sink run(…)` does not, a signal-terminated process never throws, `run("true").sink` returns the process record instead of Nil, the spawn-failure message has neither the `-1` nor an `OS error` line, and the exception has no `.proc` (the second line dies at its first field).

### PA-04  Arguments and adverbs                                        D:partial R:partial V:spec
Every positional is stringified (`42`, `1.5`, an IO::Path as its path,
`Any` as `""` with the usual uninitialized-value warning); a List or Array
positional is flattened, so `run(<printf %s x>)` and
`run(("printf", "%s"), "lst")` work; named arguments may sit anywhere.
No shell is involved: `$HOME`, `*` and `"a b"` reach the program verbatim
as single arguments. `run()` with no positionals is `X::Multi::NoMatch`;
`run("")` spawns nothing and is a failed Proc (PA-02); `shell("")` runs an
empty script and exits 0; `shell()` and `shell($cmd, $extra)` are
compile-time errors (`X::TypeCheck::Argument`).
```
say run("printf", "%s-%s", 42, "x".IO, :out).out.slurp(:close).raku, " ", run("printf", "%s", 1.5, :out).out.slurp(:close).raku, " ", run("printf".IO, "io", :out).out.slurp(:close).raku, " ", run(("printf", "%s"), "lst", :out).out.slurp(:close).raku, " ", run(<printf %s x>, :out).out.slurp(:close).raku, " ", run("printf", "%s", <a b>, :out).out.slurp(:close).raku, " ", run("printf", "%s", (1, 2), :out).out.slurp(:close).raku, " ", run("printf", "%s", :out, "named-after").out.slurp(:close).raku, " ", run("printf", "%s", "", :out).out.slurp(:close).raku, " ", run("printf", "a b", :out).out.slurp(:close).raku, " ", run("printf", "%s", "\$HOME", :out).out.slurp(:close).raku, " ", run("printf", "%s", "*", :out).out.slurp(:close).raku, " ", run("printf", "%s", "é", :out).out.slurp(:close).raku, " ", (quietly run("printf", "%s", Any, :out).out.slurp(:close).raku), " ", run("").exitcode, " ", shell("", :out).exitcode, " ", (try run()) // $!.^name, " ", (try EVAL('shell()')) // $!.^name, " ", (try EVAL('shell("exit 1", "extra")')) // $!.^name
# rakudo 2026.08: "42-x" "1.5" "io" "lst" "x" "ab" "12" "named-after" "" "a b" "\$HOME" "*" "é" "" -1 0 X::Multi::NoMatch X::TypeCheck::Argument+{X::Comp} X::TypeCheck::Argument+{X::Comp}
```
rakupp 4.0.1-84: differs — `run()`, `shell()` and `shell($cmd, $extra)` are accepted and return Procs, and `run("")` is exit 127; the stringification and flattening fields match.

### PA-05  exitcode, Bool and slurp block until the process ends        D:partial R:no V:spec
`.exitcode`, `.signal`, `.Bool`, `.Numeric` and `.sink` wait for the
process to end. With `:in` that cannot happen before `.in` is closed, so
`run("cat", :in).exitcode` blocks until `$p.in.close` — forever if the
program waits for its input. `.out.slurp` blocks until the child closes
its stdout, and data written to `.in` before the close is delivered.
Once a pipe is being drained (PA-06) its data is buffered on the parent
side without bound, so `.exitcode` before reading `.out` returns for
output of any size and the output is still there afterwards.
Timing-dependent probe.
```
my $p = run("cat", :in); my $t = start { $p.exitcode }; await Promise.anyof($t, Promise.in(3)); say $t.status; $p.in.close; await Promise.anyof($t, Promise.in(5)); say $t.status, " ", $t.result; my $q = run("cat", :in, :out); my $u = start { $q.out.slurp(:close) }; await Promise.anyof($u, Promise.in(3)); say $u.status; $q.in.print("late"); $q.in.close; await Promise.anyof($u, Promise.in(5)); say $u.status, " ", $u.result.raku, " ", $q.exitcode; my $r = run("sh", "-c", "printf x; exit 5", :out); say $r.exitcode, " ", $r.out.slurp(:close).raku, " ", $r.exitcode; my $s = run("sh", "-c", "yes | head -c 200000; exit 6", :out); say $s.exitcode, " ", $s.out.slurp(:close).chars, " ", $s.out.close.exitcode
# rakudo 2026.08: Planned|Kept 0|Planned|Kept "late" 0|5 "x" 5|6 200000 6
```
rakupp 4.0.1-84: differs — `.exitcode` with an open `:in` returns at once (0) and `.out.slurp(:close)` returns `""` without waiting, so the `"late"` write is lost; `yes | head` also prints `yes: stdout: Broken pipe` on the parent's stderr, because SIGPIPE is ignored process-wide and inherited.

### PA-06  Two captured pipes can deadlock                              D:yes R:no V:quirk
A requested `.out` or `.err` is drained from the moment it is first read
or closed, not before; until then the child's writes fill the OS pipe
(64 KiB here) and block it. So with `:out, :err` a child that writes more
than that to stderr blocks before the parent, still reading `.out` to
end of file, ever reaches `.err`, and the parent blocks too; reading
`.err` first is fine when stdout stays small, `:merge` has one pipe and
never deadlocks, and closing an unread pipe drains and discards its
data. The docs describe this hazard under "Potential Deadlocks"; step two
need not reproduce it, and Raku++ does not. Timing-dependent probe.
```
my $r = run("sh", "-c", "i=0; while [ \$i -lt 3000 ]; do echo 0123456789012345678901234567890123456789 >&2; i=\$((i+1)); done; echo done", :out, :err); my $t = start { $r.out.slurp(:close) }; await Promise.anyof($t, Promise.in(3)); say $t.status; my $e = start { $r.err.slurp(:close) }; await Promise.anyof(Promise.allof($t, $e), Promise.in(6)); say $t.status, " ", $t.result.raku, " ", $e.result.lines.elems, " ", $r.exitcode; say run("sh", "-c", "i=0; while [ \$i -lt 3000 ]; do echo 0123456789012345678901234567890123456789 >&2; i=\$((i+1)); done; echo done", :merge).out.slurp(:close).lines.elems; my $s = run("sh", "-c", "i=0; while [ \$i -lt 3000 ]; do echo 0123456789012345678901234567890123456789 >&2; i=\$((i+1)); done; echo done", :out, :err); say $s.err.slurp(:close).lines.elems, " ", $s.out.slurp(:close).raku; my $u = run("sh", "-c", "i=0; while [ \$i -lt 3000 ]; do echo 0123456789012345678901234567890123456789; i=\$((i+1)); done", :out, :err); say $u.out.close.exitcode, " ", $u.err.slurp(:close).raku
# rakudo 2026.08: Planned|Kept "done\n" 3000 0|3001|3000 "done\n"|0 ""
```
rakupp 4.0.1-84: differs, for the better — the first `.out.slurp` completes at once (`Kept`) because both pipes are drained from the spawn; every other field matches.

### PA-07  Proc.new, spawn and shell as methods                         D:yes R:yes V:spec
`Proc.new` takes `:in`, `:out`, `:err`, `:bin`, `:chomp`, `:merge`,
`:enc`, `:nl` as `run` does, plus `:exitcode`, `:signal` and `:command`
to build an already-finished Proc (the kind a `Proc::Async` start
Promise delivers). Fresh, `.pid` is Nil, `.command` an empty Array, and
requested pipes already exist. `.spawn(*@args, :cwd, :env, :arg0)` and
`.shell($cmd, :cwd, :env)` return True when the process could be started,
whatever it later exits with, and False when it could not — `.exitcode`
is then -1 and `.pid` keeps its old value, while a `shell` of a missing
command still gets the shell's pid and its exit 127. `.command` is set by
the spawn. `Proc.new(:exitcode(3), :signal(0))` and
`Proc.new(:exitcode(0), :signal(9))` throw when sunk, `:exitcode(0),
:signal(0)` sinks to Nil, and `.signal` is Any when not given. On the type
object `spawn` and `exitcode` are `X::AdHoc`.
```
my $p = Proc.new(:out); say $p.^name, " ", $p.pid.raku, " ", $p.command.raku, " ", $p.out.^name, " ", $p.spawn("printf", "s14"), " ", $p.out.slurp(:close).raku, " ", $p.exitcode, " ", $p.command.raku, " ", $p.pid.^name; my $q = Proc.new; say $q.shell("exit 5"), " ", $q.exitcode, " ", $q.command.raku, " ", ?$q; my $r = Proc.new(:err); say $r.spawn("nonexistent-cmd-zz"), " ", $r.exitcode, " ", $r.pid.raku, " ", $r.err.slurp(:close).raku; my $s = Proc.new(:err); say $s.shell("nonexistent-cmd-zz"), " ", $s.exitcode, " ", $s.pid.^name, " ", $s.err.slurp(:close).chars > 0; my $u = Proc.new(:out); say $u.spawn(<printf u14>), " ", $u.out.slurp(:close).raku, " ", (try Proc.spawn("true")) // $!.^name, " ", (try Proc.exitcode) // $!.^name
# rakudo 2026.08: Proc Nil [] IO::Pipe True "s14" 0 ("printf", "s14") Int|True 5 ("exit 5",) False|False -1 Nil ""|True 127 Int True|True "u14" X::AdHoc X::AdHoc
say Proc.new(:exitcode(3), :signal(2), :command(<a b>)).exitcode, " ", Proc.new(:exitcode(3), :signal(2)).signal, " ", Proc.new(:exitcode(0), :signal(0)).so, " ", Proc.new(:exitcode(3), :signal(0), :command(<a b>)).command.raku, " ", (try { sink Proc.new(:exitcode(3), :signal(0), :command(<a b>)); "ok" }) // $!.^name, " ", (try { sink Proc.new(:exitcode(0), :signal(9), :command(<a b>)); "ok" }) // $!.^name, " ", Proc.new(:exitcode(0), :signal(0)).exitcode, " ", Proc.new(:exitcode(0), :signal(0)).sink.raku, " ", Proc.new(:exitcode(3)).signal.raku, " ", Proc.new(:command("x")).command.raku, " ", Proc.new(:in).in.^name, " ", Proc.new(:out).out.^name, " ", Proc.new(:err).err.^name, " ", Proc.new(:merge).out.^name, " ", Proc.new(:merge).err.raku, " ", Proc.new.out.raku
# rakudo 2026.08: 3 2 True ("a", "b") X::Proc::Unsuccessful X::Proc::Unsuccessful 0 Nil Any ("x",) IO::Pipe IO::Pipe IO::Pipe IO::Pipe IO::Pipe IO::Pipe
```
rakupp 4.0.1-84: differs — `Proc.new.pid` is 0, `.shell` returns False for a successful spawn, a failed spawn reports 127 with a pid, the type-object calls are `X::Method::NotFound`; on the second line `:exitcode`/`:signal`/`:command` are ignored (-1, 0, `[]`), sinking never throws, `.in` is a `ProcIn` and `.out`/`.err` are handle objects rather than the `IO::Pipe` type object.

### PA-08  Asking the status before a spawn poisons the Proc            D:no R:no V:bug
`Proc.new.exitcode` (or `.signal`, `.Bool`) before any spawn answers 1 and
0 instead of the documented -1, and fixes the status: a spawn afterwards
runs the program but `.exitcode` stays 1. Spawning or shelling a second
time on a Proc that has already run replaces `.command` but keeps the
first status: after `exit 7` then `exit 8` it still answers 7, after
`run("true")` then `.shell("exit 9")` still 0. Do not imitate: answer -1
before any spawn and record every spawn's status.
```
my $p = Proc.new; say $p.exitcode.raku, " ", $p.signal.raku; $p.spawn("sh", "-c", "exit 7"); say $p.exitcode, " ", ?$p; my $q = Proc.new; $q.spawn("sh", "-c", "exit 7"); say $q.exitcode; $q.spawn("sh", "-c", "exit 8"); say $q.exitcode, " ", $q.command.raku; my $r = run("true"); $r.shell("exit 9"); say $r.exitcode, " ", $r.command.raku, " ", ?$r; my $s = run("sh", "-c", "exit 2"); $s.spawn("true"); say $s.exitcode
# rakudo 2026.08: 1 0|1 False|7|7 ("sh", "-c", "exit 8")|0 ("exit 9",) True|2
```
rakupp 4.0.1-84: differs, for the better on the bug — `Proc.new.exitcode` is -1 and the spawn's real status (7) is recorded; but a second `spawn`, or `.shell` on a Proc returned by `run`, is `X::Method::NotFound`.

### PA-09  pid, identity, printing                                      D:partial R:partial V:spec
`.pid` is a positive Int for every process that was actually spawned,
including a `shell` whose command is missing, and Nil for a failed spawn
or an unspawned Proc. A Proc is a plain object: `.gist` and `.raku` are
the default `Proc.new(…)` dump, `.Str` the default `Proc<…>`, `WHICH` an
ObjAt, two Procs are never `eqv`; the MROs are `Proc, Any, Mu` and
`Proc::Async, Any, Mu`, neither a subtype of the other.
```
say run("true").pid.^name, " ", (run("true").pid > 0), " ", shell("true").pid.^name, " ", run("nonexistent-cmd-zz").pid.raku, " ", (shell("nonexistent-cmd-zz 2>/dev/null").pid > 0), " ", Proc.new.pid.raku, " ", run("true").gist.substr(0, 9), " ", run("true").raku.substr(0, 9), " ", run("true").Str.substr(0, 5), " ", Proc.^mro.map(*.^name).raku, " ", Proc::Async.^mro.map(*.^name).raku, " ", (run("true") eqv run("true")), " ", (run("true") ~~ Proc), " ", run("true").WHICH.^name, " ", (run("true").pid != run("true").pid), " ", do { my $p = run("true"); $p.pid == $p.pid }, " ", Proc::Async.new("true").gist.substr(0, 16), " ", (Proc::Async.new("true") ~~ Proc), " ", (run("true") ~~ Proc::Async)
# rakudo 2026.08: Int True Int Nil True Nil Proc.new( Proc.new( Proc< ("Proc", "Any", "Mu").Seq ("Proc::Async", "Any", "Mu").Seq False True ObjAt True True Proc::Async.new( False False
```
rakupp 4.0.1-84: differs — a failed spawn has a pid and `Proc.new.pid` is 0, `.raku`/`.Str` are hash dumps, `WHICH` is a ValueObjAt, and both MROs lack the class itself (`Any, Mu`).

## B. The pipes: `:out`, `:err`, `:merge`, `:in`, handles

### PA-10  The :out pipe                                                D:yes R:yes V:spec
With `:out`, `.out` is an `IO::Pipe` (an IO::Handle), opened, utf8, with
`nl-in` exactly `"\n"` (not the handle default `["\n", "\r\n"]`), `chomp`
True, `.path` and `.IO` the `IO::Path` type object, `.t` False, `.proc`
the Proc. `.get` returns the next line chomped and then Nil, `.lines` the
remaining lines as a Seq, `.eof` False on a fresh pipe and True once the
end has been seen, `.words` reads the pipe; `.slurp` reads to the end and
with `:close` also closes; `.close` returns the Proc, after which
`.opened` is False; `.print` on it is `X::AdHoc` (opened for reading).
`.exitcode` may be asked before reading (PA-05).
```
my $p = run("printf", "a\\nb\\n", :out); say $p.out.^name, " ", (try $p.out.opened) // $!.^name, " ", $p.exitcode, " ", $p.out.get.raku, " ", $p.out.lines.raku, " ", $p.out.get.raku, " ", $p.out.eof, " ", $p.out.close.^name, " ", ($p.out.close === $p), " ", run("printf", "x\\ny", :out).out.slurp(:close).raku, " ", run("printf", "x\\ny", :out).out.slurp.raku, " ", run("printf", "", :out).out.slurp(:close).raku, " ", run("printf", "x", :out).out.encoding, " ", run("printf", "x", :out).out.nl-in.raku, " ", run("printf", "x", :out).out.chomp, " ", run("printf", "x", :out).out.path.raku, " ", run("printf","x",:out).err.raku, " ", run("printf","x",:out).in.raku, " ", run("printf", "a b", :out).out.words.raku, " ", (run("printf","x",:out).out ~~ IO::Pipe), " ", (run("printf","x",:out).out ~~ IO::Handle), " ", run("printf", "x", :out).out.t, " ", run("printf", "x", :out).out.eof, " ", (try run("printf", "x", :out).out.print("y")) // $!.^name, " ", run("printf", "x", :out).out.proc.^name, " ", $p.out.opened
# rakudo 2026.08: IO::Pipe True 0 "a" ("b",).Seq Nil True Proc True "x\ny" "x\ny" "" utf8 "\n" True IO::Path IO::Pipe IO::Pipe ("a", "b").Seq True True False False X::AdHoc Proc False
```
rakupp 4.0.1-84: differs — the line dies at `.proc`; measured without it: `.out` is a `FileHandle` (an IO::Handle but not an `IO::Pipe`) with no `.opened` or `.proc`, `.lines` is a List, a `.get` after `.lines` still answers `"a"`, `.eof` stays False, `nl-in` is `["\n", "\r\n"]`, and `.print` on it does not throw.

### PA-11  :err and :merge                                              D:yes R:yes V:spec
`:err` gives `.err`, an IO::Pipe that behaves as `.out` does. `:merge`
sends both streams into `.out` and leaves `.err` the type object, whatever
`:out` and `:err` say (`:merge, :!out` and `:merge, :err` still capture
both into `.out`); the interleaving inside is the child's write order,
not a guarantee. A stream neither requested nor merged is inherited from
the parent, so `:out` alone lets stderr through to the parent's stderr;
`:!err`/`:err(False)` discards it (PA-13). `.out.close` on a merged pipe
returns the Proc with its exit code.
```
my $p = run("sh", "-c", "echo out; echo err >&2", :out, :err); say $p.out.slurp(:close).raku, " ", $p.err.slurp(:close).raku, " ", $p.exitcode, " ", run("sh", "-c", "echo out; echo err >&2", :merge).out.slurp(:close).lines.sort.raku, " ", run("sh", "-c", "echo e >&2", :err).err.lines.raku, " ", run("sh", "-c", "echo e >&2", :out, :!err).out.slurp(:close).raku, " ", run("sh", "-c", "echo o", :err, :!out).err.slurp(:close).raku, " ", run("sh", "-c", "echo o; echo e >&2", :merge, :err).out.slurp(:close).lines.sort.raku, " ", run("sh", "-c", "echo o; echo e >&2", :merge, :!out).out.slurp(:close).lines.sort.raku, " ", run("sh", "-c", "echo o; echo e >&2", :merge, :out).out.slurp(:close).lines.sort.raku, " ", run("sh", "-c", "echo o; echo e >&2", :merge, :err).err.raku, " ", run("sh", "-c", "echo out; echo err >&2", :merge).err.raku, " ", run("sh", "-c", "echo out; echo err >&2", :merge).out.^name, " ", run("sh", "-c", "echo o; exit 4", :merge).out.close.exitcode
# rakudo 2026.08: "out\n" "err\n" 0 ("err", "out").Seq ("e",).Seq "" "" ("e", "o").Seq ("e", "o").Seq ("e", "o").Seq IO::Pipe IO::Pipe IO::Pipe 4
```
rakupp 4.0.1-84: differs — `:!err` still lets the child's stderr through to the parent's, `:merge, :!out` captures nothing, `.err` under `:merge` is a handle dump rather than the type object, and `.lines` is a List.

### PA-12  The :in pipe                                                 D:yes R:yes V:spec
With `:in`, `.in` is a write-only IO::Pipe: `print`, `say`, `put`, `write`
(a Blob) and `flush` return True, `spurt($s, :close)` writes and closes,
`nl-out` is the `:nl` value (`"\n"`), encoding utf8, `.t` False, `.path`
the IO::Path type object; `.close` returns the Proc and `.opened` is then
False; `.get` and `.slurp` on it are `X::AdHoc` (opened for writing).
`:!in` or `:in(False)` closes the child's stdin at once, so `cat` sees an
empty input, and `.in` stays the type object. Without `:in` the child
inherits the parent's stdin.
```
my $p = run("cat", :in, :out); say $p.in.^name, " ", $p.in.print("ab").raku, " ", $p.in.say("c").raku, " ", $p.in.put("d").raku, " ", $p.in.write("e".encode).raku, " ", $p.in.flush.raku, " ", $p.in.close.^name, " ", ($p.in.close === $p), " ", $p.out.slurp(:close).raku, " ", $p.exitcode; my $q = run("cat", :in, :out); $q.in.spurt("sp", :close); say $q.out.slurp(:close).raku, " ", run("cat", :!in, :out).out.slurp(:close).raku, " ", run("cat", :in(False), :out).out.slurp(:close).raku, " ", run("cat", :!in, :out).in.raku, " ", run("cat", :in, :out).in.nl-out.raku, " ", run("cat", :in, :out).in.encoding, " ", run("cat", :in, :out).in.t, " ", run("cat", :in, :out).in.path.raku, " ", (try run("cat", :in, :out).in.get) // $!.^name, " ", (try run("cat", :in, :out).in.slurp) // $!.^name, " ", $p.in.opened
# rakudo 2026.08: IO::Pipe Bool::True Bool::True Bool::True Bool::True Bool::True Proc True "abc\nd\ne" 0|"sp" "" "" IO::Pipe "\n" utf8 False IO::Path X::AdHoc X::AdHoc False
```
rakupp 4.0.1-84: differs — `.in` is a `ProcIn` whose `print`/`say`/`put`/`write` answer a dump of the process record instead of True (the line's output is that dump, cut at six lines), and it has no `.opened`.

### PA-13  Handles and other Procs as streams; discarding a stream      D:yes R:yes V:spec
`:in($handle)` feeds the child from an opened IO::Handle or from another
Proc's `.out`/`.err`; a pipe used that way is closed when the consuming
child exits (`$a.out.opened` is then False), a plain file handle is left
open. `:out($handle)` and `:err($handle)` write into an opened handle (the
same one may serve both), and `:merge` with `:out($handle)` sends both
streams there; `.out`/`.err` are then the type object. `:!out`,
`:out(False)`, `:!err` and `:err(False)` discard the stream. An unopened
handle is `X::AdHoc` at spawn time.
```
my $a = run("printf", "x\\ny\\n", :out); my $b = run("cat", "-n", :in($a.out), :out); say $b.out.slurp(:close).lines.elems, " ", $b.exitcode, " ", $a.exitcode; spurt("in9", "from-file\n"); my $c = run("cat", :in(open("in9")), :out); say $c.out.slurp(:close).raku, " ", $c.exitcode; my $d = run("printf", "z", :out); my $e = run("cat", :in($d.out), :out); say $e.out.slurp(:close).raku, " ", $e.exitcode, " ", $d.exitcode, " ", $d.out.opened, " ", $a.out.opened
# rakudo 2026.08: 2 0 0|"from-file\n" 0|"z" 0 0 False False
my $h = open("o10", :w); my $p = run("sh", "-c", "echo A; echo B >&2", :out($h), :err($h)); $h.close; say $p.exitcode, " ", $p.out.raku, " ", $p.err.raku, " ", slurp("o10").lines.sort.raku; my $g = open("o10b", :w); run("sh", "-c", "echo A; echo B >&2", :out($g), :merge); $g.close; say slurp("o10b").lines.sort.raku, " ", run("printf", "zz", :!out).out.raku, " ", run("printf", "zz", :out(False)).exitcode, " ", run("sh", "-c", "echo zz >&2", :!err).exitcode, " ", run("sh", "-c", "echo zz >&2", :err(False), :out).out.slurp(:close).raku, " ", run("sh", "-c", "echo zz >&2", :err(False), :out).err.raku; my $k = open("o10c", :w); my $q = run("sh", "-c", "echo A; echo B >&2", :out($k), :err); say $q.err.slurp(:close).raku; $k.close; say slurp("o10c").raku, " ", (try run("printf", "x", :out(IO::Handle.new))) // $!.^name
# rakudo 2026.08: 0 IO::Pipe IO::Pipe ("A", "B").Seq|("A", "B").Seq IO::Pipe 0 0 "" IO::Pipe|"B\n"|"A\n" X::AdHoc
```
rakupp 4.0.1-84: differs — the chaining fields of the first line match and it dies at `.opened`; in the second line the bound streams work (both files receive their lines, discarding works) but `.out`/`.err` print as handle dumps instead of the type object, and `:out(IO::Handle.new)` spawns instead of throwing.

### PA-14  :bin and :enc                                                D:partial R:yes V:quirk
`:bin` makes every pipe binary: `.out.slurp` gives a `Buf[uint8]`,
`.encoding` is Nil, `.in.write(Blob)` sends bytes; `get`, `lines` and
`.in.print` on a binary pipe throw `X::IO::Closed` although nothing is
closed (the pipe has no decoder or encoder; `X::IO::BinaryMode` would be
the honest type — quirk), and `:bin` together with `:enc` is
`X::IO::BinaryAndEncoding`. `:enc` sets both directions: `latin1` reads
byte 0xE9 as "é" and writes "é" as one byte; `.encoding` reports the
canonical name (`iso-8859-1`, `utf8` for `UTF-8`); an unknown name is
`X::Encoding::Unknown` at spawn. A text pipe's `.slurp(:bin)` gives the
raw bytes. Malformed UTF-8 throws `X::AdHoc` from `slurp` and from `get`.
```
my $p = run("printf", "\\303\\251", :out, :bin); say $p.out.slurp(:close).raku, " ", run("printf", "\\303\\251", :out).out.slurp(:close).raku, " ", run("printf", "\\351", :out, :enc<latin1>).out.slurp(:close).ords.raku, " ", run("printf", "\\351", :out, :enc<latin1>).out.encoding, " ", run("printf", "ab", :out, :bin).out.encoding.raku, " ", run("printf","ab",:out,:bin).out.slurp(:close).^name, " ", run("printf", "ab", :out).out.slurp(:close, :bin).raku, " ", do { my $q = run("cat", :in, :out, :bin); $q.in.write(Blob.new(0xc3, 0xa9)); $q.in.close; $q.out.slurp(:close).raku }, " ", do { my $q = run("cat", :in, :out, :enc<latin1>); $q.in.print("é"); $q.in.close; $q.out.slurp(:close, :bin).raku }, " ", do { my $q = run("cat", :in, :out, :bin); my $r = (try { $q.in.print("é"); "printed" }) // $!.^name; $q.in.close; $r ~ ":" ~ $q.out.slurp(:close).raku }, " ", (try run("printf", "ab", :out, :bin).out.lines.eager) // $!.^name, " ", (try run("printf", "ab", :out, :bin).out.get) // $!.^name, " ", (try run("printf", "ab", :out, :bin, :enc<utf8>)) // $!.^name, " ", (try run("printf", "\\377", :out).out.slurp(:close).raku) // $!.^name, " ", (try run("printf", "\\377", :out).out.get.raku) // $!.^name, " ", (try run("printf", "ab", :out, :enc<nope>)) // $!.^name, " ", run("printf", "ab", :out, :enc<UTF-8>).out.encoding
# rakudo 2026.08: Buf[uint8].new(195,169) "é" (233,).Seq iso-8859-1 Nil Buf[uint8] Buf[uint8].new(97,98) Buf[uint8].new(195,169) Buf[uint8].new(233) X::IO::Closed:Buf[uint8].new() X::IO::Closed X::IO::Closed X::IO::BinaryAndEncoding X::AdHoc X::AdHoc X::Encoding::Unknown utf8
```
rakupp 4.0.1-84: differs — `:bin` is ignored (`slurp` gives a Str, `.encoding` "utf8", `get`/`lines`/`print` work as text), `:enc<latin1>` decodes 0xE9 as U+0009, `:bin` with `:enc` and `:enc<nope>` are accepted, and malformed UTF-8 decodes to "ÿ" instead of throwing.

### PA-15  read(n) on a pipe ignores n                                  D:no R:no V:bug
`.read($n)` on a binary pipe returns the whole next chunk the child wrote,
however small `$n` is: `read(1)` after `printf abcdef` is six bytes, the
next `read` is empty and `.eof` True; with a pause between two writes each
`read` returns one write. `readchars($n)` and `getc` on a text pipe honour
their count, and a `read` after them continues from the decoder's
position. The docs say `read` returns at most `$n` bytes; do not imitate.
Chunk boundaries are timing-dependent.
```
my $p = run("printf", "abcdef", :out, :bin); say $p.out.read(1).raku, " ", $p.out.read(2).raku, " ", $p.out.read(100).raku, " ", $p.out.read(1).raku, " ", $p.out.eof, " ", $p.out.close.exitcode; my $q = run("printf", "abcdef", :out); say $q.out.readchars(2).raku, " ", $q.out.readchars(1).raku, " ", $q.out.getc.raku, " ", $q.out.read(2).raku, " ", $q.out.slurp(:close).raku; my $r = run("sh", "-c", "printf abc; sleep 0.3; printf def", :out, :bin); say $r.out.read(1).raku, " ", $r.out.read(100).raku, " ", $r.out.read(100).raku, " ", $r.out.eof
# rakudo 2026.08: Buf[uint8].new(97,98,99,100,101,102) Buf[uint8].new() Buf[uint8].new() Buf[uint8].new() True 0|"ab" "c" "d" Buf[uint8].new(101,102) ""|Buf[uint8].new(97,98,99) Buf[uint8].new(100,101,102) Buf[uint8].new() True
```
rakupp 4.0.1-84: differs — `read` always returns an empty Buf, `readchars` `""` and `getc` Nil; only `slurp` reads a pipe.

### PA-16  :nl and :chomp                                               D:partial R:no V:quirk
`:nl($s)` is the line separator for `.lines`/`.get` on `.out` and `.err`
(their `nl-in` is exactly `[$s]`, so `"\n"` no longer splits under
`:nl("!")`) and the terminator `.in.say` appends (`nl-out`); `.slurp` is
untouched. `:!chomp` keeps the separator on each line and sets `.chomp`
False. Independently of `:nl`, a `\r\n` arriving on a text pipe is always
turned into `\n` — so `:nl("\r\n")` never matches and yields one unchomped
line — while a lone `\r` is kept as data; and `:nl("\r")` never splits on
a pipe although `lines(:nl-in("\r"))` on a file does (quirk). A binary
pipe, and `.slurp(:bin)`/`.read` on a text pipe, see the raw `\r\n`.
```
say run("printf", "a!b!c", :out, :nl("!")).out.lines.raku, " ", run("printf", "a!b!c", :out, :nl("!")).out.get.raku, " ", run("printf", "a\\nb\\n", :out, :!chomp).out.lines.raku, " ", run("printf", "a\\nb\\n", :out, :!chomp).out.get.raku, " ", run("printf", "a\\r\\nb", :out).out.lines.raku, " ", run("printf", "a\\r\\nb", :out).out.slurp(:close).raku, " ", run("printf", "a\\nb\\n", :out, :nl("!")).out.slurp(:close).raku, " ", run("printf", "a\\nb\\n", :out, :nl("!")).out.nl-in.raku, " ", run("printf", "a\\nb\\n", :out, :nl("!")).out.lines.raku, " ", do { my $q = run("cat", :in, :out, :nl("!")); $q.in.say("x"); $q.in.close; $q.out.slurp(:close).raku }, " ", run("cat", :in, :out, :nl("!")).in.nl-out.raku, " ", run("printf", "a!b", :out, :nl("!"), :!chomp).out.lines.raku, " ", run("printf", "a\\nb", :out, :chomp).out.chomp, " ", run("printf", "a\\nb", :out, :!chomp).out.chomp
# rakudo 2026.08: ("a", "b", "c").Seq "a" ("a\n", "b\n").Seq "a\n" ("a", "b").Seq "a\nb" "a\nb\n" "!" ("a\nb\n",).Seq "x!" "!" ("a!", "b").Seq True False
say run("printf", "a\\rb\\n", :out).out.slurp(:close).ords.raku, " ", run("printf", "a\\rb", :out).out.get.ords.raku, " ", run("printf", "a\\rb\\r", :out, :nl("\\r")).out.lines.elems, " ", run("printf", "a\\rb\\n", :out, :nl("\\r")).out.lines.raku, " ", run("printf", "a\\r\\nb\\r\\n", :out, :nl("\\r\\n")).out.lines.raku, " ", run("printf", "a\\r\\nb\\r\\n", :out).out.lines.raku, " ", run("printf", "a\\nb\\n", :out, :nl("\\n")).out.lines.raku, " ", run("printf", "a\\r\\nb", :out, :bin).out.slurp(:close).raku, " ", run("printf", "a\\r\\nb", :out).out.slurp(:close, :bin).raku, " ", run("printf", "a\\r\\nb", :out).out.read(10).raku, " ", run("printf", "a\\r\\nb\\r\\n", :out, :nl("\\r\\n")).out.slurp(:close).raku, " ", run("printf", "a\\r\\r\\nb", :out).out.slurp(:close).ords.raku, " ", do { spurt("cr", "a\rb\rc"); "cr".IO.lines(:nl-in("\r")).raku }, " ", run("printf", "a\\rb\\rc", :out, :nl("\\r")).out.lines.raku
# rakudo 2026.08: (97, 13, 98, 10).Seq (97, 13, 98).Seq 1 ("a\rb\n",).Seq ("a\nb\n",).Seq ("a", "b").Seq ("a\nb\n",).Seq Buf[uint8].new(97,13,10,98) Buf[uint8].new(97,13,10,98) Buf[uint8].new(97,13,10,98) "a\nb\n" (97, 13, 10, 98).Seq ("a", "b", "c").Seq ("a\rb\rc",).Seq
```
rakupp 4.0.1-84: differs — `:nl` is ignored (lines split on `\n` only, `nl-in` stays the default, `.in.say` appends `\n`), `:!chomp` is ignored, `\r\n` is translated but `.slurp(:bin)` and `.read` on a text pipe give a Str and an empty Buf, and a file with `:nl-in("\r")` does not split either.

### PA-17  :cwd and :env                                                D:yes R:yes V:quirk
`:cwd` (a Str or IO::Path, relative to `$*CWD`) is the child's working
directory; a missing directory or a file there makes the spawn fail as in
PA-02 (-1, 254, Nil). `:env` (a Hash) is the child's entire environment —
`HOME` is empty when not passed — and defaults to `%*ENV`, so a
`temp %*ENV<X>` reaches the child. The program is still found through
the parent's `PATH` even under `:env({})` (quirk: the lookup ignores the
child's environment). `shell` takes the same two adverbs.
```
"d13".IO.mkdir; say run("sh", "-c", "basename \$PWD", :out, :cwd("d13")).out.slurp(:close).raku, " ", run("sh", "-c", "basename \$PWD", :out, :cwd("d13".IO)).out.slurp(:close).raku, " ", run("sh", "-c", "echo \$PA13", :out, :env({ PA13 => "v", PATH => %*ENV<PATH> })).out.slurp(:close).raku, " ", run("sh", "-c", "echo \$HOME", :out, :env({ PATH => %*ENV<PATH> })).out.slurp(:close).raku, " ", do { temp %*ENV<PA13B> = "w"; run("sh", "-c", "echo \$PA13B", :out).out.slurp(:close).raku }, " ", run("printf", "ok", :out, :env({})).out.slurp(:close).raku, " ", run("sh", "-c", "basename \$PWD", :out, :cwd("nope13")).exitcode, " ", run("sh", "-c", "true", :cwd("nope13")).signal, " ", run("sh", "-c", "true", :cwd("nope13")).pid.raku, " ", shell("basename \$PWD", :out, :cwd("d13")).out.slurp(:close).raku, " ", shell("echo \$PA13", :out, :env({ PA13 => "v", PATH => %*ENV<PATH> })).out.slurp(:close).raku, " ", run("sh", "-c", "echo \$PA13", :out, :env(%(PA13 => "p"))).out.slurp(:close).raku, " ", run("sh", "-c", "true", :cwd("in9")).exitcode, " ", (try run("sh", "-c", "basename \$PWD", :out, :cwd("nope13")).os-error.^name) // $!.^name
# rakudo 2026.08: "d13\n" "d13\n" "v\n" "\n" "w\n" "ok" -1 254 Nil "d13\n" "v\n" "p\n" -1 Str
```
rakupp 4.0.1-84: differs — an unusable `:cwd` gives exit 126 with a pid, and `.os-error` does not exist; `:cwd`, `:env` and the inherited `temp` variable all work.

## C. Proc::Async: construction and lifecycle

### PA-18  Proc::Async.new                                              D:yes R:yes V:quirk
`.new(*@args, :w, :enc, :translate-nl, :arg0, :win-verbatim-args, :pty)`
takes the program and its arguments as positionals; a single Iterable
positional is flattened (`.new(<printf x y>)`, `.new(("printf", "x"),
"y")`); non-Str positionals are kept as given in `.command` and
stringified at start. `.command` is the List of all positionals; the
deprecated `.path` is the first and `.args` an Array of `arg0` (the
`:arg0` value or the program) followed by the rest, so `:arg0<meow>`
shows only in `.args`. `.enc` is as given (`latin1` is not canonicalised;
default `utf8`) and an unknown name throws `X::Encoding::Unknown` in
`.new`; `.translate-nl` defaults True; `.w` is the Any type object unless
`:w` (then True); `.started`, `.pty` and `.win-verbatim-args` are False.
Unknown nameds (`:bin`, `:out`, `:r`) are silently ignored. No positional
is `X::Multi::NoMatch`, and so is the documented `.new(:path, :args)` form,
which does not exist. `:pty` without both `:cols` and `:rows` is
`X::Proc::Async::MissingColsRows`. `.ready` and `.pid` are a Planned
Promise before start. `:started` is a public attribute, so
`.new("cat", :started)` reports started and can never be started
(`X::Proc::Async::AlreadyStarted`, PA-19) — the quirk.
```
my $p = Proc::Async.new("printf", "x"); say $p.command.raku, " ", $p.enc, " ", $p.translate-nl, " ", $p.arg0.raku, " ", $p.win-verbatim-args, " ", $p.pty, " ", Proc::Async.new("cat", :arg0<meow>).command.raku, " ", Proc::Async.new(<printf x y>).command.raku, " ", Proc::Async.new(("printf", "x"), "y").command.raku, " ", Proc::Async.new("cat", :enc<latin1>).enc, " ", Proc::Async.new("cat", :!translate-nl).translate-nl, " ", Proc::Async.new("cat", :bin, :out, :r, :win-verbatim-args).win-verbatim-args, " ", (try Proc::Async.new()) // $!.^name, " ", Proc::Async.new("printf".IO, 1).command.map(*.^name).raku, " ", (try Proc::Async.new("cat", :enc<nope>)) // $!.^name, " ", (try Proc::Async.new("cat", :pty)) // $!.^name, " ", (try Proc::Async.new("cat", :pty(:cols(1)))) // $!.^name, " ", (try Proc::Async.new(:path<cat>, :args<a b>).command.raku) // $!.^name, " ", Proc::Async.new("cat", "a").command.^name, " ", Proc::Async.new("true").ready.status, " ", Proc::Async.new("true").pid.^name, " ", $p.w.raku, " ", Proc::Async.new(:w, "cat").w.raku, " ", $p.started, " ", Proc::Async.new("cat", :started).started, " ", $p.path.raku, " ", $p.args.raku, " ", Proc::Async.new("cat", :arg0<meow>).args.raku, " ", Proc::Async.new("cat", :arg0<meow>).path.raku
# rakudo 2026.08: ("printf", "x") utf8 True "printf" False False ("cat",) ("printf", "x", "y") ("printf", "x", "y") latin1 False True X::Multi::NoMatch ("IO::Path", "Int").Seq X::Encoding::Unknown X::Proc::Async::MissingColsRows X::Proc::Async::MissingColsRows X::Multi::NoMatch List Planned Promise Any Bool::True False True "printf" ["printf", "x"] ["meow"] "cat"
```
rakupp 4.0.1-84: differs — the line dies at `.enc`; measured without it: `.command` and its flattening match, `.new()` and `.new(:path, :args)` return objects instead of `X::Multi::NoMatch`, `:enc<nope>` and `:pty` are accepted, and `.w`, `.started`, `.enc`, `.translate-nl`, `.arg0`, `.pty`, `.path`, `.args`, `.pid` are all `X::Method::NotFound`.

### PA-19  start, the Promise and the Proc it delivers; ready and pid   D:yes R:yes V:spec
`.start(:cwd, :ENV, :scheduler)` returns a Promise and sets `.started`; a
second `.start` throws `X::Proc::Async::AlreadyStarted` (`.proc` the
object). When the process ends the Promise is kept with a `Proc` carrying
`.exitcode`, `.signal` and `.command` (the same List) and nothing else:
`.pid` Nil, pipes the type object, `.os-error` Str. That Proc is True only
for exit 0 and, sunk, throws `X::Proc::Unsuccessful` — so `await $p;` as a
statement throws for a non-zero exit while `$ = await $p` does not.
`.ready` and `.pid` return one and the same Promise, kept with the
child's pid (an Int above 0) once it runs, and kept before the start
Promise is.
```
my $p = Proc::Async.new("sh", "-c", "exit 6"); my $pr = $p.start; say $pr.^name, " ", (try $p.start) // $!.^name; await Promise.anyof($pr, Promise.in(5)); say $pr.status, " ", $pr.result.^name, " ", $pr.result.exitcode, " ", $pr.result.signal, " ", ?$pr.result, " ", +$pr.result, " ", $pr.result.command.raku, " ", $pr.result.pid.raku, " ", $pr.result.out.raku, " ", $pr.result.in.raku, " ", $p.ready.status, " ", $p.ready.result.^name, " ", ($p.ready.result > 0), " ", (try { sink $pr.result; "no-throw" }) // $!.^name, " ", (try { $ = await $pr; "ok" }) // $!.^name, " ", (try { await $pr; "sunk" }) // $!.^name, " ", (try { my $x = $p.start }) // $!.^name, " ", ($p.pid === $p.ready), " ", $p.started, " ", (try { await $pr; 1 }) // $!.proc.exitcode, " ", (try { my $x = $p.start }) // $!.proc.^name, " ", $pr.result.os-error.raku
# rakudo 2026.08: Promise X::Proc::Async::AlreadyStarted|Kept Proc 6 0 False 6 ("sh", "-c", "exit 6") Nil IO::Pipe IO::Pipe Kept Int True X::Proc::Unsuccessful ok X::Proc::Unsuccessful X::Proc::Async::AlreadyStarted True True 6 Proc::Async Str
my $p = Proc::Async.new("true"); my $pr = $p.start; await Promise.anyof($pr, Promise.in(5)); say $pr.status, " ", $pr.result.exitcode, " ", $pr.result.signal, " ", ?$pr.result, " ", (try { await $pr; "sunk-ok" }) // $!.^name, " ", $p.ready.status, " ", $p.ready.result.^name, " ", ($p.ready.result > 0), " ", ((await $p.pid) == $p.ready.result), " ", $p.started
# rakudo 2026.08: Kept 0 0 True sunk-ok Kept Int True True True
```
rakupp 4.0.1-84: differs — both lines die at `.pid`; measured without it: `AlreadyStarted` is thrown, the start Promise is kept with a Proc whose exitcode, signal and command are right (the command an Array, `.pid` the real pid), sinking that Proc does not throw, and `.ready` stays Planned forever (its result is the `Proc` type object).

### PA-20  A process that cannot be started                             D:partial R:yes V:spec
When the program is missing (an empty name included) or `:cwd` is
unusable, the start Promise is broken with an `X::OS` whose `.os-error` is
a Str and `.error-code` -2 for a missing program or directory and -20 for
a `:cwd` that is a file; `.ready`/`.pid` is broken with the same exception
object; `.started` is True. A `.write` or `.print` Promise is broken with
`X::AdHoc`; `.close-stdin` returns True; every stdout, stderr, `:bin` or
merged Supply obtained before start quits with `X::AdHoc` (a `react` on it
dies); `.kill` does nothing and does not throw.
```
my $p = Proc::Async.new(:w, "nonexistent-cmd-zz"); my $so = $p.stdout; my $se = $p.stderr; my $pr = $p.start; await Promise.anyof($pr, Promise.in(5)); say $pr.status, " ", $pr.cause.^name, " ", ($pr.cause ~~ X::OS), " ", $pr.cause.os-error.^name, " ", $pr.cause.error-code, " ", $p.ready.status, " ", ($p.ready.cause === $pr.cause), " ", do { my $w = $p.write("x".encode); await Promise.anyof($w, Promise.in(5)); $w.status ~ ":" ~ $w.cause.^name }, " ", do { my $w = $p.print("x"); await Promise.anyof($w, Promise.in(5)); $w.status }, " ", $p.close-stdin, " ", do { my $q = ""; react { whenever $so { QUIT { default { $q = .^name } } } }; $q }, " ", do { my $q = ""; react { whenever $se { QUIT { default { $q = .^name } } } }; $q }, " ", (try { await $p.ready; "ok" }) // $!.^name, " ", (X::OS ~~ Exception), " ", (try { $p.kill; "killed" }) // $!.^name, " ", $p.started
# rakudo 2026.08: Broken X::OS True Str -2 Broken True Broken:X::AdHoc Broken True X::AdHoc X::AdHoc X::OS+{X::Await::Died} True killed True
my $p = Proc::Async.new("nonexistent-cmd-zz"); my $q = ""; $p.stdout.tap(-> $ {}, quit => { $q ~= "O:" ~ .^name }); $p.stderr.tap(-> $ {}, quit => { $q ~= " E:" ~ .^name }); my $pr = $p.start; await Promise.anyof($pr, Promise.in(5)); say $q, " ", $pr.status; my $r = Proc::Async.new("nonexistent-cmd-zz"); my $rr = $r.start; await Promise.anyof($rr, Promise.in(5)); say $rr.status, " ", $rr.cause.^name, " ", $r.ready.status; my $s = Proc::Async.new("nonexistent-cmd-zz"); my $sq = ""; $s.Supply.tap(-> $ {}, quit => { $sq = .^name }); my $sr = $s.start; await Promise.anyof($sr, Promise.in(5)); say $sq, " ", $sr.status; my $t = Proc::Async.new("nonexistent-cmd-zz"); my $tq = ""; $t.stdout(:bin).tap(-> $ {}, quit => { $tq = .^name }); my $tr = $t.start; await Promise.anyof($tr, Promise.in(5)); say $tq, " ", $tr.status; my $u = Proc::Async.new(""); my $ur = $u.start; await Promise.anyof($ur, Promise.in(5)); say $ur.status, " ", $ur.cause.^name; my $v = Proc::Async.new("true"); my $vr = $v.start(:cwd("nope21")); await Promise.anyof($vr, Promise.in(5)); spurt("f21", "x"); my $w = Proc::Async.new("true"); my $wr = $w.start(:cwd("f21")); await Promise.anyof($wr, Promise.in(5)); say $vr.status, " ", $vr.cause.^name, " ", $vr.cause.error-code, " ", $v.ready.status, " ", $wr.status, " ", $wr.cause.error-code
# rakudo 2026.08: O:X::AdHoc E:X::AdHoc Broken|Broken X::OS Broken|X::AdHoc Broken|X::AdHoc Broken|Broken X::OS|Broken X::OS -2 Broken Broken -20
```
rakupp 4.0.1-84: differs — the start Promise is kept (with Nil) for a missing program, `""` and a bad `:cwd` alike, `.ready` stays Planned, no Supply quits, and `.started` is `X::Method::NotFound` (the first line dies there, at its last field).

### PA-21  :cwd and :ENV on start                                       D:yes R:no V:spec
`:cwd` (Str or IO::Path, relative to `$*CWD` or absolute) is the child's
directory; `:ENV` (a Hash) is its whole environment and defaults to
`%*ENV` including `temp` changes. As with `run`, the program is looked up
through the parent's `PATH` even under `:ENV({})`.
```
"d27".IO.mkdir; my $o = ""; my $p = Proc::Async.new("sh", "-c", "basename \$PWD; echo \$PA27"); $p.stdout.tap({ $o ~= $_ }); await Promise.anyof($p.start(:cwd("d27"), :ENV({ PA27 => "v", PATH => %*ENV<PATH> })), Promise.in(5)); print $o.raku, " "; my $q = Proc::Async.new("sh", "-c", "echo \$PA27"); my $o2 = ""; $q.stdout.tap({ $o2 ~= $_ }); await Promise.anyof($q.start(:ENV({})), Promise.in(5)); print $o2.raku, " "; my $r = Proc::Async.new("printf", "x"); my $o3 = ""; $r.stdout.tap({ $o3 ~= $_ }); my $rr = $r.start(:ENV({})); await Promise.anyof($rr, Promise.in(5)); print $rr.status, " ", $o3.raku, " "; my $t = Proc::Async.new("sh", "-c", "basename \$PWD"); my $o4 = ""; $t.stdout.tap({ $o4 ~= $_ }); await Promise.anyof($t.start(:cwd("d27".IO)), Promise.in(5)); print $o4.raku, " "; my $u = Proc::Async.new("sh", "-c", "echo \$PA27U"); my $o5 = ""; $u.stdout.tap({ $o5 ~= $_ }); temp %*ENV<PA27U> = "inherited"; await Promise.anyof($u.start, Promise.in(5)); print $o5.raku, " "; my $v = Proc::Async.new("sh", "-c", "echo \$PA27V"); my $o6 = ""; $v.stdout.tap({ $o6 ~= $_ }); await Promise.anyof($v.start(:ENV(%(PA27V => "pct"))), Promise.in(5)); print $o6.raku, " "; my $w = Proc::Async.new("sh", "-c", "basename \$PWD"); my $o7 = ""; $w.stdout.tap({ $o7 ~= $_ }); await Promise.anyof($w.start(:cwd("d27".IO.absolute)), Promise.in(5)); say $o7.raku
# rakudo 2026.08: "d27\nv\n" "\n" Kept "x" "d27\n" "inherited\n" "pct\n" "d27\n"
```
rakupp 4.0.1-84: differs — `:ENV` is ignored and a `temp %*ENV` change is not passed on (the child sees the parent's original environment); `:cwd` works in all three spellings.

### PA-22  A stdout/stderr Supply is read only once tapped              D:no R:no V:quirk
Calling `.stdout` or `.stderr` (either mode) makes the parent capture
that stream, but the pipe is read only once the Supply has a tap; until
then the start Promise stays Planned even after the child has exited
(`.ready` is Kept), and a child that writes more than the pipe buffer
blocks. Tapping later, at any time and on any of the Supply objects the
method returned, releases it, and every byte written before the tap is
delivered. The merged `.Supply` is read from the start whether tapped or
not, and a first tap after the process has ended still receives
everything, synchronously. Asking `.native-descriptor` (PA-30) counts as
consuming the stream. Timing-dependent probe.
```
my $p = Proc::Async.new("printf", "u"); my $so = $p.stdout; my $pr = $p.start; await Promise.anyof($pr, Promise.in(3)); say $pr.status, " ", $p.ready.status; my $o = ""; $so.tap({ $o ~= $_ }); await Promise.anyof($pr, Promise.in(5)); say $pr.status, " ", $o.raku, " ", $pr.result.exitcode; my $q = Proc::Async.new("sh", "-c", "printf q >&2"); my $qe = $q.stderr; my $qr = $q.start; await Promise.anyof($qr, Promise.in(3)); say $qr.status; my $e = ""; $qe.tap({ $e ~= $_ }); await Promise.anyof($qr, Promise.in(5)); say $qr.status, " ", $e.raku; my $r = Proc::Async.new("printf", "m"); my $rm = $r.Supply; my $rr = $r.start; await Promise.anyof($rr, Promise.in(3)); my $m = ""; $rm.tap({ $m ~= $_ }); say $rr.status, " ", $m.raku; my $s = Proc::Async.new("printf", "n"); my $ss = $s.stdout(:bin); my $nd = $ss.native-descriptor; my $sr = $s.start; await Promise.anyof($sr, Promise.in(3)); say $sr.status, " ", $nd.status
# rakudo 2026.08: Planned Kept|Kept "u" 0|Planned|Kept "q"|Kept "m"|Kept Kept
```
rakupp 4.0.1-84: differs — an untapped stdout/stderr Supply lets the output through to the parent's own stream, the start Promise is kept at once, `.ready` stays Planned, and a tap added afterwards receives nothing (also for the merged Supply); `native-descriptor` does not exist.

### PA-23  Ordering guarantees                                          D:no R:partial V:spec
For a tapped stdout or stderr Supply, every emitted chunk precedes that
Supply's `done`; both `done`s (and the merged Supply's single `done`)
precede the keeping of the start Promise, so a `.then` on it, or a
`whenever $proc.start`, sees all output already delivered; `.ready` is
Kept before the start Promise is. A process that writes nothing emits
nothing — no empty chunk.
```
my $l = Lock.new; my @ev; sub ev($x) { $l.protect({ @ev.push($x) }) }; my $p = Proc::Async.new("sh", "-c", "printf a; printf b >&2"); $p.stdout.tap({ ev("o") }, done => { ev("od") }); $p.stderr.tap({ ev("e") }, done => { ev("ed") }); my $pr = $p.start; my $t = $pr.then({ ev("exit:" ~ .result.exitcode) }); await Promise.anyof($t, Promise.in(5)); say @ev.grep(* eq "od").elems, " ", @ev.grep(* eq "ed").elems, " ", @ev.grep(* eq "o").elems > 0, " ", (@ev.first(* eq "od", :k) < @ev.first(* eq "exit:0", :k)), " ", (@ev.first(* eq "ed", :k) < @ev.first(* eq "exit:0", :k)), " ", @ev.tail, " ", (@ev.first(* eq "o", :k) < @ev.first(* eq "od", :k)), " ", (@ev.first(* eq "e", :k) < @ev.first(* eq "ed", :k)), " ", $p.ready.status, " ", $pr.status; my $q = Proc::Async.new("sh", "-c", "printf a; printf b >&2"); my @e2; my $l2 = Lock.new; $q.Supply.tap({ $l2.protect({ @e2.push("v") }) }, done => { $l2.protect({ @e2.push("d") }) }); my $qr = $q.start; my $qt = $qr.then({ $l2.protect({ @e2.push("x") }) }); await Promise.anyof($qt, Promise.in(5)); say @e2.grep(* eq "d").elems, " ", @e2.tail(2).raku; my $r = Proc::Async.new("true"); my $n = 0; $r.stdout.tap({ $n++ }); $r.stderr.tap({ $n++ }); await Promise.anyof($r.start, Promise.in(5)); say $n
# rakudo 2026.08: 1 1 True True True exit:0 True True Kept Kept|1 ("d", "x").Seq|0
```
rakupp 4.0.1-84: differs — the chunk/done/exit order holds for stdout, stderr and the merged Supply, but `.ready` is still Planned after the exit, and the merged Supply carries stdout only (the `b` on stderr leaks to the parent's stderr).

### PA-24  Uncaptured output is inherited                               D:yes R:no V:spec
A stream for which no Supply was obtained and no handle bound goes
straight to the parent's own stdout or stderr (ahead of the parent's
buffered output, so the interleaving is not deterministic); a stream
whose Supply was obtained but never tapped is captured and lost (PA-22).
A child not made with `:w` and with nothing bound inherits the parent's
stdin. Timing-dependent probe (the untapped case waits three seconds).
```
my $p = Proc::Async.new("printf", "inherited30"); await Promise.anyof($p.start, Promise.in(5)); print " "; my $q = Proc::Async.new("printf", "lost30"); $q.stdout; await Promise.anyof($q.start, Promise.in(3)); my $r = Proc::Async.new("sh", "-c", "printf inh-err >&2"); await Promise.anyof($r.start, Promise.in(5)); print " "; my $s = Proc::Async.new("cat"); my $so = ""; $s.stdout.tap({ $so ~= $_ }); my $sr = $s.start; await Promise.anyof($sr, Promise.in(5)); say "after ", $sr.status, " ", $so.raku, " ", $sr.result.exitcode
# rakudo 2026.08: inherited30 inh-err after Kept "" 0
```
rakupp 4.0.1-84: differs — the captured-but-untapped `lost30` is printed to the parent's stdout.

## D. Proc::Async: the output Supplies

### PA-25  Obtaining stdout, stderr and the merged Supply               D:yes R:yes V:spec
`.stdout`, `.stderr` and `.Supply` must be called before `.start`;
afterwards they throw `X::Proc::Async::TapBeforeSpawn` with `.handle`
`stdout`, `stderr` or `merge` and `.proc` the object — also for a stream
already obtained. Each call returns a new `Proc::Async::Pipe` (a Supply
subclass; `===` is False between two calls) over the same stream, and any
of them may be tapped. A stream is either characters or bytes: after
`.stdout`, `.stdout(:bin)` is `X::Proc::Async::CharsOrBytes` (`.handle`)
and vice versa; stdout and stderr are independent (`.stdout` beside
`.stderr(:bin)` is fine). The merged `.Supply` excludes `.stdout`/`.stderr`
and they exclude it, in either order: `X::Proc::Async::SupplyOrStd`. A
bound handle (PA-31) excludes the Supply: `X::Proc::Async::BindOrUse`.
```
my $p = Proc::Async.new("sh", "-c", "printf o; printf e >&2"); my $so = $p.stdout; my $so2 = $p.stdout; say ($so === $so2), " ", $so.^name, " ", ($so ~~ Supply), " ", (try $p.stdout(:bin)) // $!.^name ~ ":" ~ $!.handle, " ", (try $p.Supply) // $!.^name, " ", (try $p.stderr(:bin)).^name; my $o = ""; $so.tap({ $o ~= $_ }); my $eb = Buf.new; $p.stderr(:bin).tap({ $eb.append($_) }); my $pr = $p.start; say (try $p.stderr) // $!.^name ~ ":" ~ $!.handle ~ ":" ~ $!.proc.^name, " ", (try $p.stdout) // $!.^name ~ ":" ~ $!.handle, " ", (try $p.stdout(:bin)) // $!.^name, " ", (try $p.Supply) // $!.^name; await Promise.anyof($pr, Promise.in(5)); say $o.raku, " ", $eb.decode.raku, " ", $pr.status, " ", $pr.result.exitcode
# rakudo 2026.08: False Proc::Async::Pipe True X::Proc::Async::CharsOrBytes:stdout X::Proc::Async::SupplyOrStd Proc::Async::Pipe|X::Proc::Async::TapBeforeSpawn:stderr:Proc::Async X::Proc::Async::TapBeforeSpawn:stdout X::Proc::Async::TapBeforeSpawn X::Proc::Async::SupplyOrStd|"o" "e" Kept 0
```
rakupp 4.0.1-84: differs — `.stdout` returns a plain `Supply`; `.stdout(:bin)` after `.stdout`, `.Supply` after `.stdout`, and `.stdout`/`.stderr` after `.start` are all accepted, so none of the three exception types is thrown.

### PA-26  Late taps and replay                                         D:no R:partial V:quirk
The first tap on a stream, whenever it comes, receives every chunk from
the start and then `done`; a second tap on the same stream — on the same
or another `Proc::Async::Pipe` object for it — receives nothing, not even
`done` after the process has ended, so `.list` on it would block forever.
`.list` on the first tap blocks until the stream is closed and returns
the chunks. The merged Supply behaves the same. Timing-dependent probe.
```
my $p = Proc::Async.new("sh", "-c", "printf o; printf e >&2"); my $so = $p.stdout; my $se = $p.stderr; my $pr = $p.start; await Promise.anyof($pr, Promise.in(3)); print $pr.status, " "; my ($o, $e) = "", ""; $so.tap({ $o ~= $_ }); $se.tap({ $e ~= $_ }); await Promise.anyof($pr, Promise.in(5)); print $pr.status, " ", $o.raku, " ", $e.raku, " "; my ($o2, $e2) = "", ""; my $d2 = ""; $so.tap({ $o2 ~= $_ }, done => { $d2 = "done" }); $se.tap({ $e2 ~= $_ }); sleep 0.5; print $o2.raku, " ", $e2.raku, " ", $d2.raku, " "; my $q = Proc::Async.new("printf", "big"); my $qs = $q.stdout; my $qr = $q.start; my $got = ""; $qs.tap({ $got ~= $_ }); await Promise.anyof($qr, Promise.in(5)); my $done = ""; my $late = ""; $qs.tap({ $late ~= $_ }, done => { $done = "done" }); sleep 0.5; print $got.raku, " ", $late.raku, " ", $done.raku, " ", $qr.result.exitcode, " "; my $r = Proc::Async.new("printf", "z"); my $rs = $r.stdout; my $rr = $r.start; my @l1 = $rs.list; say @l1.join.raku, " ", $rr.status, " ", do { my $rm = Proc::Async.new("printf", "mm"); my $ms = $rm.Supply; my $mr = $rm.start; await Promise.anyof($mr, Promise.in(5)); my $m1 = ""; $ms.tap({ $m1 ~= $_ }); my $m2 = ""; $ms.tap({ $m2 ~= $_ }); sleep 0.5; $m1.raku ~ " " ~ $m2.raku }
# rakudo 2026.08: Planned Kept "o" "e" "" "" "" "big" "" "" 0 "z" Planned "mm" ""
```
rakupp 4.0.1-84: differs — a late first tap receives nothing (the output went to the parent's stdout), `.list` returns an empty list while the start Promise is still Planned, and the merged Supply's late taps are empty.

### PA-27  Decoding: chunks, encodings, newline translation             D:yes R:yes V:spec
A character Supply emits Str chunks of whatever size arrived, never lines
(`.lines` on the Supply reassembles them), decoded with the `:enc` of
`.new` (default utf8) unless `.stdout(:enc)`, `.stderr(:enc)` or
`.Supply(:enc)` overrides it for that stream; `\r\n` becomes `\n` unless
`:translate-nl` is False on `.new` or on the call; a lone `\r` is kept. A
`:bin` Supply emits `Buf[uint8]` chunks untouched. `.stderr` and the
merged `.Supply` follow the same rules.
```
my $p = Proc::Async.new("printf", "\\303\\251\\r\\n"); my $b = Buf.new; my $k; $p.stdout(:bin).tap({ $b.append($_); $k = .^name }); await Promise.anyof($p.start, Promise.in(5)); print $b.raku, " ", $k, " "; my $q = Proc::Async.new("printf", "\\303\\251\\r\\nx"); my $s = ""; $q.stdout.tap({ $s ~= $_ }); await Promise.anyof($q.start, Promise.in(5)); print $s.ords.raku, " "; my $r = Proc::Async.new("printf", "\\351\\r\\n", :enc<latin1>); my $t = ""; $r.stdout.tap({ $t ~= $_ }); await Promise.anyof($r.start, Promise.in(5)); print $t.ords.raku, " "; my $u = Proc::Async.new("printf", "\\351\\r\\n"); my $v = ""; $u.stdout(:enc<latin1>, :!translate-nl).tap({ $v ~= $_ }); await Promise.anyof($u.start, Promise.in(5)); print $v.ords.raku, " "; my $w = Proc::Async.new("printf", "\\351\\r\\n", :!translate-nl); my $x = ""; $w.stdout(:enc<latin1>).tap({ $x ~= $_ }); await Promise.anyof($w.start, Promise.in(5)); print $x.ords.raku, " "; my $y = Proc::Async.new("printf", "a\\rb"); my $z = ""; $y.stdout.tap({ $z ~= $_ }); await Promise.anyof($y.start, Promise.in(5)); say $z.ords.raku
# rakudo 2026.08: Buf.new(195,169,13,10) Buf[uint8] (233, 10, 120).Seq (233, 10).Seq (233, 13, 10).Seq (233, 13, 10).Seq (97, 13, 98).Seq
my $y = Proc::Async.new("sh", "-c", "printf \\\\351 >&2", :enc<latin1>); my $z = ""; $y.stderr.tap({ $z ~= $_ }); await Promise.anyof($y.start, Promise.in(5)); print $z.ords.raku, " "; my $m = Proc::Async.new("sh", "-c", "printf \\\\351 >&2"); my $n = ""; $m.stderr(:enc<latin1>).tap({ $n ~= $_ }); await Promise.anyof($m.start, Promise.in(5)); print $n.ords.raku, " "; my $a = Proc::Async.new("sh", "-c", "printf \\\\303\\\\251 >&2"); my $ab = Buf.new; $a.stderr(:bin).tap({ $ab.append($_) }); await Promise.anyof($a.start, Promise.in(5)); print $ab.raku, " "; my $c = Proc::Async.new("sh", "-c", "printf o; printf \\\\351 >&2", :enc<latin1>); my $cm = ""; $c.Supply.tap({ $cm ~= $_ }); await Promise.anyof($c.start, Promise.in(5)); print $cm.ords.sort.raku, " "; my $d = Proc::Async.new("sh", "-c", "printf \\\\351\\\\r\\\\n"); my $dm = ""; $d.Supply(:enc<latin1>, :!translate-nl).tap({ $dm ~= $_ }); await Promise.anyof($d.start, Promise.in(5)); say $dm.ords.raku, " ", (try Proc::Async.new("cat").stdout(:enc<nope>).^name) // $!.^name, " ", (try Proc::Async.new("cat").Supply(:enc<nope>).^name) // $!.^name
# rakudo 2026.08: (233,).Seq (233,).Seq Buf.new(195,169) (111, 233).Seq (233, 13, 10).Seq Proc::Async::Pipe Supply
```
rakupp 4.0.1-84: differs — binary chunks are `Blob`, `\r\n` is never translated, `latin1` decodes 0xE9 as U+934A on stdout and U+0009 on stderr, the merged Supply drops stderr, and `.stdout(:enc<nope>)` returns a plain Supply.

### PA-28  Malformed input and an unknown encoding quit the Supply      D:no R:yes V:spec
A byte sequence invalid in the stream's encoding makes that character
Supply quit with `X::AdHoc` without emitting the valid prefix, while the
other stream and the start Promise are unaffected (kept, exit 0); the
same bytes on a `:bin` Supply are delivered whole. `.stdout.lines` quits
the same way, having emitted nothing. An unknown `:enc` given to
`.stdout`/`.stderr`/`.Supply` is accepted at call time and quits that
Supply with `X::Encoding::Unknown` once the process starts; on `.new` it
throws at once (PA-18).
```
my $p = Proc::Async.new("printf", "a\\377b"); my ($o, $q, $e) = "", "", ""; $p.stdout.tap({ $o ~= $_ }, quit => { $q = .^name }); $p.stderr.tap(-> $ {}, quit => { $e = "quit" }); my $pr = $p.start; await Promise.anyof($pr, Promise.in(5)); say $q, " ", $e.raku, " ", $pr.status, " ", $pr.result.exitcode, " ", $o.raku; my $r = Proc::Async.new("sh", "-c", "printf a\\\\377b >&2"); my $qq = ""; $r.stdout.tap(-> $ {}, quit => { $qq ~= "O" }); $r.stderr.tap(-> $ {}, quit => { $qq ~= "E:" ~ .^name }); my $rr = $r.start; await Promise.anyof($rr, Promise.in(5)); say $qq, " ", $rr.status; my $s = Proc::Async.new("printf", "a\\377b"); my $sb = Buf.new; my $sq = ""; $s.stdout(:bin).tap({ $sb.append($_) }, quit => { $sq = "quit" }); await Promise.anyof($s.start, Promise.in(5)); say $sb.raku, " ", $sq.raku; my $t = Proc::Async.new("printf", "a\\377b"); my $tq = ""; $t.Supply.tap(-> $ {}, quit => { $tq = .^name }); my $tr = $t.start; await Promise.anyof($tr, Promise.in(5)); say $tq, " ", $tr.status; my $u = Proc::Async.new("printf", "a\\377b"); my @l; my $uq = ""; $u.stdout.lines.tap({ @l.push($_) }, quit => { $uq = .^name }); my $ur = $u.start; await Promise.anyof($ur, Promise.in(5)); say @l.raku, " ", $uq, " ", $ur.status; my $v = Proc::Async.new("printf", "x"); my $vs = $v.stdout(:enc<nope>); my $vq = ""; $vs.tap(-> $ {}, quit => { $vq = .^name }); my $vr = $v.start; await Promise.anyof($vr, Promise.in(5)); say $vq, " ", $vr.status
# rakudo 2026.08: X::AdHoc "" Kept 0 ""|E:X::AdHoc Kept|Buf.new(97,255,98) ""|X::AdHoc Kept|[] X::AdHoc Kept|X::Encoding::Unknown Kept
```
rakupp 4.0.1-84: differs — nothing ever quits: malformed bytes decode leniently to `"aÿb"` and the unknown encoding is ignored.

### PA-29  The merged Supply                                            D:yes R:yes V:spec
`.Supply` (Str chunks) or `.Supply(:bin)` (`Buf[uint8]`) carries stdout
and stderr in the order the parent receives them; chars and bytes exclude
each other (`X::Proc::Async::CharsOrBytes`, `.handle` `merge`), and a
bound stdout or stderr excludes it (`X::Proc::Async::BindOrUse`);
`whenever $proc { }` in a react block taps it, since a Proc::Async coerces
to its merged Supply; `.Supply` after `.start` is
`X::Proc::Async::TapBeforeSpawn` with `.handle` `merge`.
```
sub h($e) { $e.^name ~ ":" ~ ($e.?handle // "-") }; my $p = Proc::Async.new("sh", "-c", "printf o; printf e >&2"); my $m = ""; my $sup = $p.Supply; say $sup.^name, " ", (try $p.stdout) // $!.^name, " ", (try $p.stderr) // $!.^name, " ", (try $p.Supply(:bin)) // h($!), " ", ($p.Supply === $sup), " ", (try $p.stdout(:bin)) // $!.^name, " ", (try $p.bind-stdout($*OUT)) // h($!); $sup.tap({ $m ~= $_ }); my $pr = $p.start; say (try $p.Supply) // h($!); await Promise.anyof($pr, Promise.in(5)); say $m.comb.sort.join, " ", $pr.status, " ", $pr.result.exitcode; my $q = Proc::Async.new("sh", "-c", "printf o; printf e >&2"); my $b = Buf.new; my $k; $q.Supply(:bin).tap({ $b.append($_); $k = .^name }); await Promise.anyof($q.start, Promise.in(5)); say $b.decode.comb.sort.join, " ", $k; my $r = Proc::Async.new("true"); $r.stdout; say (try $r.Supply) // $!.^name, " ", do { my $s = Proc::Async.new("true"); $s.stderr; (try $s.Supply) // $!.^name }, " ", do { my $s = Proc::Async.new("true"); $s.stderr(:bin); (try $s.Supply(:bin)) // $!.^name }; my $u = Proc::Async.new("sh", "-c", "printf o; printf e >&2"); my $um = ""; react { whenever $u { $um ~= $_ }; whenever $u.start { done }; whenever Promise.in(5) { done } }; say $um.comb.sort.join
# rakudo 2026.08: Supply X::Proc::Async::SupplyOrStd X::Proc::Async::SupplyOrStd X::Proc::Async::CharsOrBytes:merge False X::Proc::Async::SupplyOrStd X::Proc::Async::BindOrUse:stdout|X::Proc::Async::TapBeforeSpawn:merge|eo Kept 0|eo Buf[uint8]|X::Proc::Async::SupplyOrStd X::Proc::Async::SupplyOrStd X::Proc::Async::SupplyOrStd|eo
```
rakupp 4.0.1-84: differs — `.stdout`/`.stderr` after `.Supply` are accepted (they print as dumps), and the line's output is cut by those dumps; measured elsewhere (PA-23, PA-27), the merged Supply carries stdout only.

### PA-30  native-descriptor                                            D:no R:yes V:spec
A `Proc::Async::Pipe` from `.stdout` or `.stderr` (either mode) has
`.native-descriptor`, a Promise that is Planned before `.start` and kept
with the Int file descriptor (above 0) of the read end of that pipe once
the process runs; using it hands the stream over, so the process can
finish without a tap (PA-22). The merged Supply has no such method.
```
my $e = Proc::Async.new("true"); my $so = $e.stdout; my $nd = $so.native-descriptor; say $nd.^name, " ", $nd.status; my $er = $e.start; await Promise.anyof($nd, Promise.in(5)); say $nd.status, " ", ($nd.result > 0), " ", $nd.result.^name; await Promise.anyof($er, Promise.in(5)); say $er.status; my $f = Proc::Async.new("true"); my $fe = $f.stderr(:bin); my $fn = $fe.native-descriptor; my $fr = $f.start; await Promise.anyof($fn, Promise.in(5)); say $fn.status, " ", ($fn.result > 0); await Promise.anyof($fr, Promise.in(5)); say $fr.status, " ", (try Proc::Async.new("true").Supply.native-descriptor) // $!.^name, " ", (try Proc::Async.new("true").stdout.native-descriptor.^name) // $!.^name
# rakudo 2026.08: Promise Planned|Kept True Int|Kept|Kept True|Kept X::Method::NotFound Promise
```
rakupp 4.0.1-84: differs — `native-descriptor` is `X::Method::NotFound` on every Supply.

## E. Proc::Async: binding, writing, killing, exceptions

### PA-31  bind-stdout and bind-stderr                                  D:yes R:yes V:spec
`.bind-stdout($handle)` and `.bind-stderr($handle)` take an opened
`IO::Handle` (anything else is `X::TypeCheck::Binding::Parameter`, an
unopened handle `X::AdHoc`), return Nil and send the stream into it;
binding twice keeps the last handle; the handle is not closed afterwards.
Binding a stream that already has a Supply, obtaining a Supply for a
bound stream, or `.Supply` when either stream is bound, is
`X::Proc::Async::BindOrUse` with `.handle` `stdout`/`stderr` and `.use` a
Str naming the other use; the two streams are independent.
```
sub h($e) { $e.^name ~ ":" ~ ($e.?handle // "-") }; my $h = open("o22", :w); my $e = open("e22", :w); my $p = Proc::Async.new("sh", "-c", "echo A; echo B >&2"); say $p.bind-stdout($h).raku, " ", $p.bind-stderr($e).raku, " ", (try $p.stdout) // h($!), " ", (try $p.stderr(:bin)) // h($!), " ", (try $p.Supply) // h($!); my $pr = $p.start; await Promise.anyof($pr, Promise.in(5)); $h.close; $e.close; say slurp("o22").raku, " ", slurp("e22").raku, " ", $pr.result.exitcode, " ", $h.opened; my $h2 = open("o22b", :w); my $h3 = open("o22c", :w); my $p2 = Proc::Async.new("printf", "second"); $p2.bind-stdout($h2); $p2.bind-stdout($h3); await Promise.anyof($p2.start, Promise.in(5)); $h2.close; $h3.close; say slurp("o22b").raku, " ", slurp("o22c").raku
# rakudo 2026.08: Nil Nil X::Proc::Async::BindOrUse:stdout X::Proc::Async::BindOrUse:stderr X::Proc::Async::BindOrUse:stdout|"A\n" "B\n" 0 False|"" "second"
sub h($e) { $e.^name ~ ":" ~ ($e.?handle // "-") ~ ":" ~ ($e.?use // "-") }; my $q = Proc::Async.new("true"); $q.stdout; say (try { $q.bind-stdout($*OUT); "bound" }) // h($!), " ", do { my $r = Proc::Async.new("true"); $r.Supply; (try { $r.bind-stderr($*ERR); "bound" }) // h($!) }, " ", do { my $s = Proc::Async.new("cat", :w); (try { $s.bind-stdin($*IN); "bound" }) // h($!) }, " ", do { my $s = Proc::Async.new("true"); $s.stderr(:bin); (try { $s.bind-stderr($*ERR); "bound" }) // h($!) }, " ", do { my $s = Proc::Async.new("true"); $s.stdout; (try { $s.bind-stderr($*ERR); "bound" }) // $!.^name }, " ", (try Proc::Async.new("true").bind-stdout("notahandle")) // $!.^name, " ", (try Proc::Async.new("true").bind-stdin(IO::Handle.new)) // $!.^name, " ", (X::Proc::Async::BindOrUse.new(:handle<x>, :use<y>) ~~ X::Proc::Async), " ", do { my $s = Proc::Async.new("cat"); $s.bind-stdin($*IN); (try { $s.bind-stdin($*IN); "rebound" }) // $!.^name }, " ", do { my $s = Proc::Async.new("true"); $s.bind-stdout($*ERR); (try $s.stdout) // $!.^name }
# rakudo 2026.08: X::Proc::Async::BindOrUse:stdout:get the stdout Supply X::Proc::Async::BindOrUse:stderr:get the output Supply X::Proc::Async::BindOrUse:stdin:use :w X::Proc::Async::BindOrUse:stderr:get the stderr Supply bound X::TypeCheck::Binding::Parameter X::AdHoc True rebound X::Proc::Async::BindOrUse
```
rakupp 4.0.1-84: differs — `bind-stdout` and `bind-stderr` are `X::Method::NotFound`; both lines die at once.

### PA-32  bind-stdin                                                   D:yes R:yes V:spec
`.bind-stdin` accepts an opened readable IO::Handle (left open after the
process ends), a `Proc::Async::Pipe` from another Proc::Async's `.stdout`
or `.stderr` (the two are then chained; both must be started), or a
`run(:out).out` IO::Pipe, which is closed once the consumer exits. It
returns Nil; on a `:w` object it is `X::Proc::Async::BindOrUse` with
`.handle` `stdin`; binding twice keeps the last.
```
spurt("in22", "L1\nL2\n"); my $t = Proc::Async.new("cat"); my $f = open("in22"); say $t.bind-stdin($f).raku; my $o = ""; $t.stdout.tap({ $o ~= $_ }); await Promise.anyof($t.start, Promise.in(5)); say $o.raku, " ", $f.opened; my $a = Proc::Async.new("printf", "piped\\n"); my $b = Proc::Async.new("cat", "-n"); say $b.bind-stdin($a.stdout).raku; my $bo = ""; $b.stdout.tap({ $bo ~= $_ }); await Promise.anyof(Promise.allof($a.start, $b.start), Promise.in(6)); say $bo.trim.raku; my $c = run("printf", "viapipe", :out); my $d = Proc::Async.new("cat"); $d.bind-stdin($c.out); my $o2 = ""; $d.stdout.tap({ $o2 ~= $_ }); my $dr = $d.start; await Promise.anyof($dr, Promise.in(5)); say $o2.raku, " ", $c.out.opened, " ", $dr.status; my $g = Proc::Async.new("cat"); my $ga = Proc::Async.new("sh", "-c", "printf z >&2"); $g.bind-stdin($ga.stderr); my $go = ""; $g.stdout.tap({ $go ~= $_ }); await Promise.anyof(Promise.allof($ga.start, $g.start), Promise.in(6)); say $go.raku
# rakudo 2026.08: Nil|"L1\nL2\n" True|Nil|"1\tpiped"|"viapipe" False Kept|"z"
```
rakupp 4.0.1-84: differs — `bind-stdin` returns True and the line dies at `.opened`; measured without it, a file handle bound to stdin delivers nothing and another Proc::Async's stdout bound to stdin delivers nothing either.

### PA-33  write, print, say, put, close-stdin                          D:yes R:yes V:quirk
On an object made with `:w`, after `.start`: `.write(Blob)` sends bytes,
`.print(Str())` encodes with `.enc`, `.say($x)` sends `$x.gist ~ "\n"`,
`.put($x)` sends `$x.join ~ "\n"` (a list joins without separator); each
returns a Promise kept with the number of bytes handed over. Extra
positionals to `say` or `put` die with `X::AdHoc`. `.close-stdin` returns
True and closes the child's stdin — a process reading it then sees end of
file and exits, and the start Promise stays Planned until then; calling
it after the process has ended, or twice, is harmless. Before `.start`
these five throw `X::Proc::Async::MustBeStarted`, without `:w`
`X::Proc::Async::OpenForWriting`, both with `.method` set and `.proc` the
object — and `put` reports `say` as its method (the quirk). `.write` of a
Str is `X::TypeCheck::Binding::Parameter`; a `.write` after
`.close-stdin` gives a broken Promise (`X::AdHoc`).
```
my $p = Proc::Async.new("cat", :w); my $q = Proc::Async.new("cat"); my $pr0 = $q.start; say (try $p.print("x")) // $!.^name, " ", (try $p.say("x")) // $!.^name, " ", (try $p.put("x")) // $!.^name, " ", (try $p.write("x".encode)) // $!.^name, " ", (try $p.close-stdin) // $!.^name, " ", (try $p.kill) // $!.^name, " ", (try $q.print("x")) // $!.^name, " ", (try $q.say("x")) // $!.^name, " ", (try $q.put("x")) // $!.^name, " ", (try $q.write("x".encode)) // $!.^name, " ", (try $q.close-stdin) // $!.^name, " ", $q.w.raku, " ", (try $q.write("x")) // $!.^name; await Promise.anyof($pr0, Promise.in(5)); my $o = ""; $p.stdout.tap({ $o ~= $_ }); my $pr = $p.start; my $w1 = $p.write("ab".encode); my $w2 = $p.print("cd"); my $w3 = $p.say(<e f>); my $w4 = $p.put(<g h>); my $w5 = $p.print(42); await Promise.anyof(Promise.allof($w1, $w2, $w3, $w4, $w5), Promise.in(5)); say $pr0.status, " ", $w1.^name, " ", $w2.^name, " ", $w3.^name, " ", $w4.^name, " ", $w1.result.raku, " ", $w2.result.raku, " ", $w3.result.raku, " ", $w4.result.raku, " ", $w5.result.raku, " ", $pr.status, " ", (try $p.say(1, 2)) // $!.^name, " ", (try $p.put(1, 2)) // $!.^name, " ", $p.close-stdin; await Promise.anyof($pr, Promise.in(5)); say $pr.status, " ", $o.raku, " ", $pr.result.exitcode, " ", (try { await $p.write("z".encode); "wrote" }) // $!.^name, " ", (try $p.close-stdin) // $!.^name
# rakudo 2026.08: X::Proc::Async::MustBeStarted X::Proc::Async::MustBeStarted X::Proc::Async::MustBeStarted X::Proc::Async::MustBeStarted X::Proc::Async::MustBeStarted X::Proc::Async::MustBeStarted X::Proc::Async::OpenForWriting X::Proc::Async::OpenForWriting X::Proc::Async::OpenForWriting X::Proc::Async::OpenForWriting X::Proc::Async::OpenForWriting Any X::TypeCheck::Binding::Parameter|Kept Promise Promise Promise Promise 2 2 6 3 2 Planned X::AdHoc X::AdHoc True|Kept "abcd(e f)\ngh\n42" 0 X::AdHoc+{X::Await::Died} True
my $p = Proc::Async.new("cat", :w); my $q = Proc::Async.new("cat"); my $pr0 = $q.start; say (try $p.print("x")) // $!.method, " ", (try $p.say("x")) // $!.method, " ", (try $p.put("x")) // $!.method, " ", (try $p.write("x".encode)) // $!.method, " ", (try $p.close-stdin) // $!.method, " ", (try $p.kill) // $!.method, " ", (try $q.print("x")) // $!.method, " ", (try $q.say("x")) // $!.method, " ", (try $q.put("x")) // $!.method, " ", (try $q.write("x".encode)) // $!.method, " ", (try $q.close-stdin) // $!.method, " ", (try $p.print("x")) // $!.proc.^name, " ", (try $q.print("x")) // ($!.proc === $q); await Promise.anyof($pr0, Promise.in(5))
# rakudo 2026.08: print say say write close-stdin kill print say say write close-stdin Proc::Async True
```
rakupp 4.0.1-84: differs — the first line dies at `.w` and the second at `.method`; measured without them: the five exception cases are right, `.kill` before start returns True, the write Promises are kept with the byte count except `.put(<g h>)` (4: it joins with a space), `say(1, 2)` and `put(1, 2)` are accepted and written, and a `.write` after `.close-stdin` throws `OpenForWriting`.

### PA-34  kill                                                         D:yes R:yes V:quirk
`.kill` sends SIGHUP by default, or the signal given as a `Signal` enum
value, an Int, or a Str name with or without the `SIG` prefix (`"TERM"`,
`"SIGKILL"`), and returns the number it sent; before `.start` it is
`X::Proc::Async::MustBeStarted` (`.method` `kill`); after the process has
ended it is harmless, as is a second kill. The Proc delivered for a
killed process has `.signal` set and `.exitcode` 0 (PA-35). A Str that is
not a signal name is `X::TypeCheck::Return`, because `$*KERNEL.signal`
answers the Int type object for it — the quirk. `$*KERNEL.signal` maps a
name, an enum value or an Int to the number (SIGUSR1 is 30 here).
Timing-dependent probe (`sleep 5` children).
```
my $p = Proc::Async.new("sleep", "5"); say (try $p.kill) // $!.^name; my $pr = $p.start; await Promise.anyof($p.ready, Promise.in(5)); my $k = $p.kill; say $k.raku, " ", (try $p.kill(SIGTERM).raku) // $!.^name; await Promise.anyof($pr, Promise.in(5)); say $pr.status, " ", $pr.result.exitcode, " ", $pr.result.signal, " ", ?$pr.result, " ", (try { sink $pr.result; "ok" }) // $!.^name ~ ":" ~ ($!.message ~~ /"exit code: 0, signal: 1"/).so; my @r; for (SIGTERM, "KILL", 2) -> $sig { my $q = Proc::Async.new("sleep", "5"); my $qr = $q.start; await Promise.anyof($q.ready, Promise.in(5)); $q.kill($sig); await Promise.anyof($qr, Promise.in(5)); @r.push($qr.result.signal ~ "/" ~ $qr.result.exitcode) }; say @r.raku, " ", (try $*KERNEL.signal("NOPE").raku) // $!.^name, " ", $*KERNEL.signal("TERM"), " ", $*KERNEL.signal("SIGTERM"), " ", $*KERNEL.signal(SIGTERM), " ", $*KERNEL.signal(9), " ", SIGHUP.value, " ", SIGKILL.value, " ", SIGTERM.value, " ", SIGINT.value; my $z = Proc::Async.new("true"); my $zr = $z.start; await Promise.anyof($zr, Promise.in(5)); say (try { $z.kill; "ok" }) // $!.^name, " ", (try { my $q = Proc::Async.new("sleep", "5"); my $qr = $q.start; await Promise.anyof($q.ready, Promise.in(5)); $q.kill("NOPE"); "no-throw" }) // $!.^name, " ", (try { $z.kill(SIGKILL); "ok" }) // $!.^name
# rakudo 2026.08: X::Proc::Async::MustBeStarted|1 15|Kept 0 1 False X::Proc::Unsuccessful:True|["15/0", "9/0", "2/0"] X::TypeCheck::Return 15 15 15 9 1 9 15 2|ok X::TypeCheck::Return ok
```
rakupp 4.0.1-84: differs — `.kill` before start returns True instead of throwing and `.kill` returns True rather than the signal number; the Proc delivered after the default (SIGHUP) kill reported signal 1 in one run and signal 0 with a True Proc in another; a `SIGTERM` or `2` kill is reported as 15/0 and 2/0, but the name `"KILL"` does not kill (the `sleep` runs out, 0/0); and `$*KERNEL.signal` answers the kernel name `darwin` for every argument. Timing varies between runs.

### PA-35  A process that dies by a signal                              D:partial R:yes V:spec
From `run`, `shell` or `Proc::Async` alike, a process killed by a signal
ends with `.exitcode` 0 and `.signal` the number (15 TERM, 9 KILL, 2 INT,
1 HUP); the Proc is False, `.Numeric` 0, and sinking it throws
`X::Proc::Unsuccessful` whose message says `signal: N`. Its `.out` pipe
reads whatever was written (nothing here) and `.out.close` returns the
Proc with that same status.
```
my $p = run("sh", "-c", "kill -TERM \$\$"); say $p.exitcode, " ", $p.signal, " ", ?$p, " ", +$p, " ", (try { sink $p; "ok" }) // $!.^name; my $q = run("sh", "-c", "kill -KILL \$\$"); say $q.exitcode, " ", $q.signal; my $r = run("sh", "-c", "kill -INT \$\$"); say $r.exitcode, " ", $r.signal; my $s = shell("kill -HUP \$\$"); say $s.exitcode, " ", $s.signal, " ", $s.command.raku; say run("sh", "-c", "kill -TERM \$\$", :out).out.slurp(:close).raku, " ", run("sh", "-c", "kill -TERM \$\$", :out).signal, " ", run("sh", "-c", "kill -TERM \$\$", :out).exitcode, " ", run("sh", "-c", "kill -TERM \$\$", :out).out.close.signal, " ", (try { sink $p; 1 }) // $!.proc.signal
# rakudo 2026.08: 0 15 False 0 X::Proc::Unsuccessful|0 9|0 2|0 1 ("kill -HUP \$\$",)|"" 15 0 15 15
```
rakupp 4.0.1-84: differs — exit code and signal are right, but sinking a signal-terminated Proc never throws.

### PA-36  The exception family                                         D:yes R:yes V:spec
`X::Proc::Async` is a role; each member is an Exception that does it and
has a `.proc` attribute (the Proc::Async, or Any when not given).
`TapBeforeSpawn` and `CharsOrBytes` carry `.handle`; `BindOrUse` `.handle`
and `.use`; `MustBeStarted` and `OpenForWriting` `.method`;
`AlreadyStarted`, `SupplyOrStd` and `MissingColsRows` nothing more.
`X::Proc::Unsuccessful` is an Exception outside the role with `.proc`; its
message is `The spawned command '<first word>' exited unsuccessfully
(exit code: N, signal: N)` plus a second line `(OS error = …)` when set.
`X::OS` has `.os-error` (which is its message) and `.error-code`.
```
say X::Proc::Async::TapBeforeSpawn.new(:handle<stdout>).handle, " ", X::Proc::Async::MustBeStarted.new(:method<kill>).method, " ", X::Proc::Async::OpenForWriting.new(:method<print>).method, " ", X::Proc::Async::CharsOrBytes.new(:handle<stderr>).handle, " ", X::Proc::Async::BindOrUse.new(:handle<stdin>, :use("use :w")).use, " ", (X::Proc::Async::AlreadyStarted ~~ X::Proc::Async), " ", (X::Proc::Async::AlreadyStarted ~~ Exception), " ", (X::Proc::Async::SupplyOrStd ~~ X::Proc::Async), " ", (X::Proc::Async::MissingColsRows ~~ X::Proc::Async), " ", X::Proc::Async.^name, " ", (X::Proc::Unsuccessful ~~ X::Proc::Async), " ", X::Proc::Unsuccessful.new(:proc(run("sh","-c","exit 3"))).message, " ", X::Proc::Unsuccessful.new(:proc(run("nonexistent-cmd-zz"))).message.lines.elems, " ", X::OS.new(:os-error("e"), :error-code(2)).message, " ", (X::OS ~~ Exception), " ", X::OS.new(:os-error("e"), :error-code(2)).error-code, " ", X::Proc::Unsuccessful.new(:proc(run("sh","-c","kill -KILL \$\$"))).message, " ", X::Proc::Unsuccessful.new(:proc(shell("exit 2"))).message, " ", X::Proc::Async::TapBeforeSpawn.new.proc.raku, " ", X::Proc::Async::TapBeforeSpawn.new(:handle<stdout>, :proc(Proc::Async.new("true"))).proc.^name
# rakudo 2026.08: stdout kill print stderr use :w True True True True X::Proc::Async False The spawned command 'sh' exited unsuccessfully (exit code: 3, signal: 0) 2 e True 2 The spawned command 'sh' exited unsuccessfully (exit code: 0, signal: 9) The spawned command 'exit 2' exited unsuccessfully (exit code: 2, signal: 0) Proc::Async Proc::Async
```
rakupp 4.0.1-84: differs — the line dies at `.proc` (its last two fields); measured without them, `.handle`, `.method`, `.use` and the role membership match, but `X::Proc::Unsuccessful.new(:proc(…)).message` is `(Any)` and `X::OS.new(:os-error("e")).message` is `(Any)` too.

### PA-37  qx and qqx                                                   D:yes R:partial V:spec
`qx{…}` and `qqx{…}` run the string through `shell` with `:out` and
return the captured stdout as a Str; `qqx` interpolates first (a `{ }`
closure cannot be used when `{}` are the delimiters). Stderr is not
captured and reaches the parent's stderr; a failing or missing command
gives `""` and never throws, because the Proc is not sunk.
```
say qx{printf hi}.raku, " ", qx{printf hi}.^name, " ", qqx[printf {1+1}].raku, " ", qx{nonexistent-cmd-zz 2>/dev/null}.raku, " ", qx{exit 3}.raku, " ", qx{(printf b >&2) 2>/dev/null; printf a}.raku, " ", qx{printf "\$0"}.raku, " ", qqx[printf %s "{"a" x 3}"].raku, " ", qx{printf 'a\nb\n'}.lines.raku, " ", do { my $v = "e11"; qqx{printf $v}.raku }, " ", (try { qx{exit 3}; "ok" }) // $!.^name, " ", qx{printf '\303\251'}.raku, " ", qx{printf '\303\251'}.chars
# rakudo 2026.08: "hi" Str "2" "" "" "a" "\$0" "aaa" ("a", "b").Seq "e11" ok "é" 1
```
rakupp 4.0.1-84: matches.

## Counts

| | items |
|---|---|
| total | 37 |
| not fully stated by docs (D:yes) nor asserted by Roast (R:yes) | 9 — PA-04, PA-05, PA-08, PA-09, PA-15, PA-16, PA-22, PA-23, PA-26 |
| Rakudo bugs (do not imitate) | 2 — PA-08 status read before a spawn poisons the Proc and a re-spawn keeps the old status, PA-15 `read(n)` on a pipe ignores n |
| quirks (recorded, step two decides) | 10 — PA-02 signal 254 on a spawn failure, PA-06 the two-pipe deadlock, PA-14 `X::IO::Closed` for a binary pipe's text methods, PA-16 `:nl("\r")` never splits on a pipe, PA-17 the program is found through the parent's PATH under an empty `:env`, PA-18 `:started` and the documented `:path`/`:args` form, PA-22 an obtained Supply must be tapped before the start Promise can be kept, PA-26 a second tap gets nothing, PA-33 `put` reports method `say`, PA-34 an unknown signal name is `X::TypeCheck::Return` |
| rakupp 4.0.1-84 differs | 36 |
| rakupp 4.0.1-84 matches | 1 — PA-37 |

Recurring rakupp gaps, for step two. The pipe object: `run(:out).out` is
a `FileHandle` whose `.raku` dumps the process record, not an `IO::Pipe`
(no `.proc`, no `.opened`), `.in` a `ProcIn` whose writers answer that
dump; `read`, `readchars` and `getc` return nothing, only `slurp` reads;
`:bin`, `:enc`, `:nl`, `:!chomp` are ignored, latin1 decoding is wrong in
both directions, malformed UTF-8 is accepted, and an unrequested stream
prints as a handle dump instead of the type object. Status: a spawn that
fails is exit 127 with a pid and no `.os-error` instead of -1/254/Nil; a
signal-terminated Proc, `sink $proc`, and the Proc a start Promise
delivers never throw when sunk; `run()`, `shell()`, `shell($c, $x)` are
accepted; `Proc.new(:exitcode)` is ignored; a second `spawn` and `.shell`
on a run Proc are missing. Waiting: `.exitcode` and `.slurp` do not wait
for an open `:in`, so late input is lost; `.ready` is never kept; `:ENV`
and a `temp %*ENV` change do not reach the child. Proc::Async surface:
`.w`, `.enc`, `.translate-nl`, `.arg0`, `.pty`, `.started`, `.path`,
`.args`, `.pid`, `native-descriptor`, `bind-stdout`, `bind-stderr` are
missing; `.stdout` is a plain Supply; none of the TapBeforeSpawn,
CharsOrBytes, SupplyOrStd or BindOrUse rules is enforced; an untapped
stream leaks to the parent and a late tap gets nothing; the merged Supply
carries stdout only; Supplies never quit; `bind-stdin` delivers nothing;
`\r\n` is never translated; the exceptions lack `.proc` and `.method` and
`X::Proc::Unsuccessful`'s message is `(Any)`; `.kill` returns True and the
default SIGHUP kill is not reported. Two places where Raku++ is already
ahead and should stay so: no two-pipe deadlock (PA-06), and `Proc.new`
answers -1 before a spawn and records the spawn's status (PA-08).

## Method (how this sheet was produced)

As for [Supply.md](Supply.md) and [IO.md](IO.md): the two source files
were read in full, the observable rules listed, and each rule turned into
one probe line run through both engines in a fresh sandbox per line. Three
probe rounds (38, 44 and 47 lines) plus a reduced rakupp-only round for
the items whose full line dies at a method rakupp lacks. Traps met, for
the next sheet: the harness's `head -6` silently cuts a probe that prints
more than six lines — keep to `print` and one final `say`; a tap handler
written as `{}` is a Hash, not a Callable, so use `-> $ {}`; a child that
inherits the parent's stdout writes ahead of the parent's buffered output
in no fixed order, so never let such a child run after the probe has
printed; `$promise.result` on a Planned Promise blocks forever, check
`.status` first; `qqx{…}` cannot contain a closure because `{}` is its
delimiter, use `qqx[…]`; `Str.raku` does escape `\r`, and a reading that a
lone CR was dropped came from the display, so check bytes with `.ords`;
each line's sandbox is fresh, so a directory made in one line is gone in
the next; `.raku` of a `Proc::Async::Pipe` prints a block address, print
`.^name`; `sleep`-based kill probes need the `ready` Promise awaited first
and a 5-second bound; under rakupp `yes | head` prints `Broken pipe` on
stderr because SIGPIPE is ignored process-wide, and its kill loop overran
the alarm, so its long lines say less than Rakudo's. D flags from
`doc/Type/Proc.rakudoc`, `doc/Type/Proc/Async.rakudoc`,
`doc/Type/independent-routines.rakudoc` (`run`, `shell`),
`doc/Type/X/Proc/*.rakudoc`, `doc/Language/ipc.rakudoc` and
`doc/Language/traps.rakudoc`; R flags from `S17-procasync/*.t`,
`S29-os/system.t`, `S32-io/pipe.t` and `S32-io/io-handle.t`.
