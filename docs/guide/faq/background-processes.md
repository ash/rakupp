# FAQ — background processes and Proc::Async

Every snippet on this page was run on both Raku++ and Rakudo and produces
identical output. Where the two engines genuinely differ, it says so.

`run` and `shell` wait for the command to finish ([shell.md](shell.md) covers
those). `Proc::Async` is the other model: the process runs *alongside* your
program, and you decide separately whether to ever wait for it.

## Start a process and leave it running

Call `.start` and don't await the result:

```raku
Proc::Async.new('sh', '-c', 'sleep 2; date >> /tmp/late.log').start;
say 'main program exits here';
```

The `say` runs immediately, the program ends, and two seconds later the date
still lands in the log — written by a process whose parent no longer exists.
This is the standard way to launch a worker, a server, or anything else that
should not hold your program open. (Raku++ releases up to v3.7.0 never spawned
the process unless something awaited the promise —
[issue #29](https://github.com/ash/rakupp/issues/29). Fixed; both engines now
behave as shown.)

## Does it survive my program exiting?

Yes. The child is an ordinary operating-system process, and nothing ties its
lifetime to yours: when your program exits without killing it, the OS
reparents it and it keeps running.

```
$ cat daemon.raku
Proc::Async.new('sleep', '1000').start;
$ rakupp daemon.raku          # returns immediately
$ ps aux | grep 'sleep 1000'
ash   60191   0.1  0.0  … sleep 1000
```

There is no `:detach` flag because none is needed — detachment is simply the
consequence of not waiting. The only lifecycle control the API gives you is
explicit: `.kill` it, or `await` its promise.

## Is that in Raku's documentation?

Not in so many words — we checked. What
[docs.raku.org/type/Proc/Async](https://docs.raku.org/type/Proc/Async) pins
down about `.start` is that it "Initiates spawning of the external program"
and returns a `Promise` kept with a `Proc` when the program exits — that is,
the spawn happens **at `.start`**, not at `await`. The page also documents the
`X::Proc::Async::AlreadyStarted` exception on a second `.start` and the
sink-throw trap around `await` (see the differences section below). What it
says nowhere is what happens to the child when *your* program exits: nothing
about surviving, detaching, or being killed. Roast, the language's test suite
and de-facto specification, has no test for it either (nothing in
`S17-procasync/` exercises a parent exiting first).

So the survival is unspecified but well-defined emergent behavior: Rakudo
registers no at-exit cleanup of spawned processes, ordinary POSIX orphan rules
take over, and an ecosystem grew on top — Sparrow6 and Sparky detach their
workers exactly this way. Raku++ follows Rakudo here because programs depend
on it, not because a sentence requires it.

If you would like that sentence to exist: the source of the page is
[`doc/Type/Proc/Async.rakudoc`](https://github.com/Raku/doc/blob/main/doc/Type/Proc/Async.rakudoc)
in the [Raku/doc](https://github.com/Raku/doc) repository — the `=head2 method
start` section (currently line 282), whose opening paragraph is where a note
on process lifetime belongs.

## When does the process actually start?

At `.start` — the promise is about the *exit*, not the launch:

```raku
my $flag = $*TMPDIR.add('proc-async-was-here');
my $started = Proc::Async.new('touch', $flag).start;
sleep 1;
say $flag.e;      # True — the process already ran; nothing has awaited it
await $started;
$flag.unlink;
```

## How do I see its output?

Tap `.stdout` (or `.stderr`) **before** calling `.start`; `.lines` gives it to
you line by line:

```raku
my $proc = Proc::Async.new('printf', 'one\ntwo\n');
$proc.stdout.lines.tap({ say "line: $_" });
await $proc.start;
# line: one
# line: two
```

A stream you don't tap is not swallowed — it goes where your own stdout goes,
so an untapped child prints straight to your terminal:

```raku
my $proc = Proc::Async.new('echo', 'hello');
$proc.stdout.tap({ .print });
await $proc.start;    # hello — via the tap; without it, via your terminal
```

## Pipe one process into another

`bind-stdin` connects one process's output stream to another's stdin. Because
both children run from their `.start` on, the data flows concurrently — a
stream larger than the OS pipe buffer cannot deadlock:

```raku
my $proc-echo = Proc::Async.new: 'echo', 'Hello, world';
my $proc-cat = Proc::Async.new: 'cat', '-n';
$proc-cat.bind-stdin: $proc-echo.stdout;
await $proc-echo.start, $proc-cat.start;
#      1	Hello, world
```

Bind before either `.start`; after a start it is too late.

## How do I stop it, and how did it end?

`.kill` sends a signal (SIGHUP unless you name another). The `.start` promise
is kept with the finished process, whose `.exitcode` and `.so` are the exit
status:

```raku
my $proc = Proc::Async.new('sleep', '600');
my $promise = $proc.start;
$proc.kill;
my $result = await $promise;   # returns as soon as the process dies
say $result.so;                # False — it did not exit cleanly
```

```raku
my $proc = Proc::Async.new('sh', '-c', 'exit 7');
my $result = await $proc.start;
say $result.exitcode;   # 7
say $result.so;         # False
```

## Where Raku++ and Rakudo differ

**A failed process throws in Rakudo when you discard it.** The `Proc` a
`.start` promise is kept with throws when *sunk* — so a bare
`await $proc.start;` statement dies under Rakudo if the process exited
non-zero ("The spawned command … exited unsuccessfully"), which is why the
snippets above assign the result. Raku++ does not sink-throw here: the await
returns and you check `.so` or `.exitcode` yourself. (Rakudo's own docs flag
this trap; their suggested spelling is `try sink await $p.start;`.)

**Signal reporting.** A killed process under Rakudo reports `exitcode 0` with
`.signal` set (`1` for SIGHUP); Raku++ has no `.signal` and reports
`exitcode -1` for any signal death. `.so` is `False` on both, so test that.

**Tapping after `.start`.** Rakudo dies with "To avoid data races, you must
tap stdout before running the process". Raku++ accepts the tap silently and it
sees nothing. Tap first on both.

**Output timing.** Both engines stream a tapped process's output as it is
produced: each chunk read from the pipe goes straight to the taps, and a
`.lines` tap fires per line, holding back only a partial one until its newline
arrives. The difference is *when the reading starts*. Rakudo reads from the
moment of `.start`; Raku++ reads while the promise is being realized — the
`await`, or the `whenever $proc.start`. Inside a `react` (the shape every
snippet above uses) that is the same thing. But a process you `.start` and then
leave unawaited is not being read at all under Raku++: its taps stay quiet, and
once it has printed more than the OS pipe buffer (64 KB is common) it blocks
until you await.

**Writing to stdin is not implemented.** `Proc::Async.new(…, :w)` with
`.print`/`.say`/`.write`/`.close-stdin` is a working feature in Rakudo; in
Raku++ these methods are accepted and do nothing. `bind-stdin` (above) covers
the process-to-process case; for feeding your own data to a command, use
`run(…, :in)` from [shell.md](shell.md). Not implemented either: `.started`,
`:ENV` on `.start` (`:cwd` works). `bind-stdin` is POSIX-only — on Windows it
is accepted and does nothing.

**Ctrl-C at the terminal.** Raku++ puts each spawned process in its own
process group, so an interactive Ctrl-C that stops your program does not
propagate to the children; under Rakudo they share the terminal's group and
die with it. For a deliberately detached worker the Raku++ behavior is
usually what you want; for a foreground helper it means a `.kill` you would
not have needed under Rakudo.

## Gotchas

**`await` means "wait for the exit".** Awaiting the promise of a process that
never exits — a server, a watcher — blocks forever, in both engines. Start it
and keep the promise if you'll need it; await only what finishes.

**A second `.start` throws.** `X::Proc::Async::AlreadyStarted`, both engines.
One `Proc::Async` object is one process; make a new object to run it again.

**The command is an argv, not a shell line.** Like `run`:
`Proc::Async.new('sh', '-c', 'sleep 2 && date')` when you want shell syntax.

---

Back to the [FAQ index](README.md).
