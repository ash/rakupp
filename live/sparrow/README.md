# sparrow — somebody else's framework, unchanged

[Sparrow6](https://github.com/melezhik/Sparrow6) is a Raku automation framework
(`zef:sp1983`) — an alternative to Ansible or Chef, in its author's words. This
directory is a four-task scenario that runs under it.

**Sparrow6 itself is not in this directory.** It is melezhik's, published to
the ecosystem, installed from there and left alone — no patch, no copy, no
adjustment. What is here is only the four tasks and the scenario that drive it,
which we wrote.

It is also where **process startup** becomes measurable, because of how Sparrow
is built. A scenario is Raku glue; every `task-run` spawns a *separate process*
running that task in its own language. Four tasks, four spawns — and one of
them is another Raku. That makes it a fair test of what an implementation
charges to start, a cost a long-running program pays only once.

```
tasks/hello/task.bash        echo, in Bash
tasks/greet/task.raku        reads its parameters with Sparrow's config<name>
tasks/report/task.bash       talks back to the scenario through stdout
tasks/perl-check/task.pl     the same scenario driving Perl 5
```

## Running it

Sparrow6 is not shipped here — install it, with either installer. Both write
the same store, so one copy serves both engines, and its six dependencies
(`JSON::Fast`, `Data::Dump`, `Hash::Merge`, `YAMLish`, `Terminal::ANSIColor`,
`File::Directory::Tree`) come with it:

```sh
build/rakupp tools/install.raku Sparrow6      # or: zef install Sparrow6
```

Then just run it — no `RAKULIB`, no `-I`:

```sh
cd live/sparrow && ../../build/rakupp scenario.raku
```

`compare.sh` runs the scenario under both engines and diffs stdout, with
Sparrow's wall-clock line prefixes normalised:

```sh
RAKUPP=../../build/rakupp sh live/sparrow/compare.sh
```

(If you would rather run against an unpacked checkout than an installed dist,
both engines honour a comma-separated `RAKULIB`; Sparrow6 needs its
`resources/` directory on the path as well as its `lib/`.)

## What it costs to start, measured

Best of three warm runs, one machine, the four-task scenario above:

| | wall clock |
|---|---:|
| Rakudo throughout | 1,549 ms |
| Raku++ scenario, Raku tasks still spawning `raku` | 931 ms |
| Raku++ all the way down | **117 ms** |

The middle row is not a curiosity, it is the thing to know before believing the
bottom one. Sparrow6 builds its Raku task command with a **literal `raku`**
(`Sparrow6::Task::Runner::Helpers::Raku`), so running the scenario under Raku++
still starts a Rakudo for every Raku task — and that spawn is where the time
is. `compare.sh` puts a `raku` shim on PATH to get the bottom row; a cleaner
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
that do not have Sparrow6 installed, and the scenario writes under
`~/sparrow6` and spawns processes. `compare.sh` is the check, run by hand.
