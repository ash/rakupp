# sparrow — somebody else's framework, unchanged

[Sparrow6](https://github.com/melezhik/Sparrow6) is a Raku automation framework
(`zef:sp1983`) — an alternative to Ansible or Chef, in its author's words. This
directory is a four-task scenario that runs under it.

**Sparrow6 itself is not in this directory.** It is melezhik's, published to
the ecosystem, installed from there and left alone — no patch, no copy, no
adjustment. What is here is only the four tasks and the scenario that drive it,
which we wrote.

It is also where **process startup** becomes measurable, because of how Sparrow
is built. A scenario is Raku glue, and every `task-run` spawns *separate
processes* to do the actual work. That makes it a fair test of what an
implementation charges to start — a cost a long-running program pays only once,
and this one pays per task.

What gets spawned, watched by putting logging shims for `raku`, `perl` and
`bash` on `PATH` — in order, for the four tasks above:

```
1.  bash  cmd.bash                            tasks/hello
2.  raku  -I… -Mglue -Msparrow6lib task.raku  tasks/greet — started directly
3.  bash  cmd.bash                            tasks/report
4.  bash  cmd.bash                            tasks/perl-check — the wrapper…
5.  perl  -I… -Msparrow6lib task.pl           …which starts perl
6.  bash  cmd.bash                            tasks/perl-check, a second time
7.  perl  -I… -Msparrow6lib task.pl
```

Each task runs in **its own** language: a Bash task does not start a Raku, and
neither does a Perl one. Bash and Perl tasks are launched through a small
generated `cmd.bash` wrapper — so those cost a shell process on top of whatever
the task itself needs — while the Raku task is started directly, with no
wrapper. Only that one task starts a `raku`, and that single spawn is most of
the difference between the second and third rows of the table below.

(Sparrow launches the Perl task twice for one line of output. That is Sparrow's
own behaviour — verified identical under both engines — so it cancels out of
the comparison rather than distorting it.)

```
tasks/hello/task.bash        echo, in Bash
tasks/greet/task.raku        reads its parameters with Sparrow's config<name>
tasks/report/task.bash       talks back to the scenario through stdout
tasks/perl-check/task.pl     the same scenario driving Perl 5
```

## Running it

Sparrow6 is not shipped here — install it, with either installer. Both write
the same store, so one copy serves both engines. It declares six direct
dependencies (`JSON::Fast`, `Data::Dump`, `Hash::Merge`, `YAMLish`,
`Terminal::ANSIColor`, `File::Directory::Tree`) and resolves to a plan of 17
distributions once theirs are counted; the installer handles all of it:

```sh
rakupp install Sparrow6      # or: zef install Sparrow6
```

Then just run it — no `RAKULIB`, no `-I`:

```sh
cd live/sparrow && rakupp scenario.raku
```

`compare.sh` runs the scenario under both engines and diffs stdout, with
Sparrow's wall-clock line prefixes normalised:

```sh
sh live/sparrow/compare.sh
```

Both take `rakupp` and `raku` from `PATH`. Running out of a build tree instead?
Name it: `RAKUPP=./build/rakupp sh live/sparrow/compare.sh`.

(If you would rather run against an unpacked checkout than an installed dist,
both engines honour a comma-separated `RAKULIB`; Sparrow6 needs its
`resources/` directory on the path as well as its `lib/`.)

## What it costs to start, measured

```sh
sh live/sparrow/bench.sh          # or: RUNS=5 sh live/sparrow/bench.sh
```

`bench.sh` times the three configurations below and prints this table. It warms
each one first, because Rakudo's *very first* run compiles Sparrow6's module
graph and can take tens of seconds — a real cost, but a one-off, and not what
this measures. Best of five, warm, on one machine:

| configuration | wall clock |
|---|---:|
| Rakudo throughout | 905 ms |
| Raku++ scenario, Raku tasks still spawning `raku` | 689 ms |
| Raku++ all the way down | **121 ms** |

Absolute numbers are machine-specific and the Rakudo row is the load-sensitive
one — run it yourself rather than trusting these. The ratio between the last
two rows is what stays put.

Each row is one command, if you would rather see them separately than trust a
script:

```sh
cd live/sparrow

# 1. Rakudo throughout
time raku scenario.raku

# 2. Raku++ runs the scenario — but Sparrow6 still spawns `raku` for Raku tasks
time rakupp scenario.raku

# 3. Raku++ all the way down: a `raku` on PATH that is really rakupp
mkdir -p /tmp/shim && printf '#!/bin/sh\nexec %s "$@"\n' "$(command -v rakupp)" > /tmp/shim/raku
chmod +x /tmp/shim/raku
time PATH=/tmp/shim:$PATH rakupp scenario.raku
```

The middle row is not a curiosity, it is the thing to know before believing the
bottom one. Sparrow6 builds its Raku task command with a **literal `raku`**
(`Sparrow6::Task::Runner::Helpers::Raku`), so running the scenario under Raku++
still starts a Rakudo for every Raku task — and that spawn is where the time
is. The corollary is worth stating: for a scenario with **no** Raku tasks the gain
is confined to the scenario process itself — its startup, plus however fast the
engine runs Sparrow's own orchestration — because the Bash and Perl tasks cost
the same either way. `compare.sh` puts a `raku` shim on PATH to get the bottom row; a cleaner
fix would be for Sparrow6 to honour something like `SP6_RAKU_BIN`.

The gap is that wide for a reason specific to Sparrow's design. Its cache
directory is per-run (`$.sparrow-root ~ "/tmp/" ~ $*PID ~ …`), so the generated
`glue.rakumod` and `sparrow6lib.rakumod` land somewhere fresh every time.
Rakudo therefore has no precompilation to reuse and **recompiles both on every
Raku task, on every run**. Raku++ has no precompilation cliff to fall off.

## What running it exposed

Sparrow6 did not work here at all until four faults were fixed, none of them
about Sparrow (`t/regression/sparrow6-blockers.raku` pins all four):

- `IO::Path.absolute` answered an `IO::Path` where Rakudo answers a `Str`
- `Proc::Async.ready` did not exist
- `whenever $proc.stdout` never fired inside a `react`, so every task ran and
  printed nothing
- `config<name>` parsed as `config("name")` — Rakudo reads a tight `name<key>`
  as `name()<key>` — which is the API every Sparrow task and plugin uses

That is the argument for [live/](..) existing at all: four general divergences
in an afternoon, found by code we did not write and would not have thought to
write.

## Not in `t/run.raku`

Deliberately, as the [live/](..) entries all are: the suite runs on machines
that do not have Sparrow6's seventeen distributions installed, and the scenario
writes under `~/sparrow6` and spawns processes. `compare.sh` is the check, run
by hand.
