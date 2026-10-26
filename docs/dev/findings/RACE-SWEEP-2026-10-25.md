# Race sweep, 2026-10-25: Roast S17 and the first dists under `--detect-races`

RACE-DETECTOR-PLAN P4. Each file ran twice with `tools/race-sweep.raku`:
once plain, once under `--detect-races`. A file is **clean** when the
detector ran it and said nothing. It is **races** when the detector reported,
and the reports are kept below. It is **changed** when the two runs passed a
different number of tests, or exited differently (the detector's own 66
aside). The detector must never cause a changed file.

Every report below was read against the memory model
([guide/ASYNC.md](../../guide/ASYNC.md)). A race is the program's bug on
every engine, because the same program is a race under Rakudo. So the
findings are written as fixes to the test or the module, never as "fails on
rakupp".

## Roast S17

Roast da425eb92 (2026-10-07), fudged for rakudo.moar as `tools/run-roast.raku`
fudges it. The six stress files ran on their own, one at a time. The other 93
ran with `-j=1`, under `nice 19`.

| | files |
|---|---|
| clean | 91 |
| races, each read as a race in the test | 8 |
| changed | 0 |
| timed out | 0 |
| false positives (the detector wrong) | 0 |

### The races

**S17-lowlevel/cas.t and cas-int.t: a `cas` loop that reads plainly.** Both
build a lock-free counter or list the textbook way:

```raku
loop {
    my $orig = $head;                       # a plain read
    my $next = Node.new(value => $i, next => $orig);
    last if cas($head, $orig, $next) === $orig;
}
```

The `cas` validates what the read saw, so the loop is correct in practice.
But the read itself is a plain access to a location other threads write
atomically. That is a data race under the model: `⚛` orders atomic accesses
only with each other, and Go's `-race` reports the same mix. The `cas` page
on docs.raku.org shows the code-object form, which "will first do an atomic
fetch", and uses `my $seen = $target` only in its pseudocode. Either of these
fixes the tests, and both run silent under the detector and pass on Rakudo:

```raku
my $orig = ⚛$head;                                    # an atomic fetch
cas $head, -> $orig { Node.new(value => $i, next => $orig) }   # the code-object form
```

The same holds for `@values[0]`, `$!value` and `$!head`, the element and
attribute variants of the same loops (7 reports over the two files).

**S17-lowlevel/thread.t, lines 91–95: reading after a `sleep`.**
`Thread.start({ $t1id = $*THREAD.id })`, then `sleep 2`, then
`is $t1id, $t1.id` before `$t1.finish`. The test's own comment says "a little
fragile, yes". The fix is to read after `.finish` or hand the id back
through the thread's result. The test wants the value before `finish` on
purpose, though, so a `Promise` kept from inside the thread fits what it
tests.

**S17-promise/at.t, lines 11–16, and in.t, lines 9–14: ordering by sleeps.**
The ticket test (old-issue-tracker #3631) pushes `3` to `@order` from a
`start` block after `sleep 3`, and `2` from the main thread after
`await Promise.anyof(…)` and `sleep 1`. Only the sleeps order the two
pushes. The test checks that the scheduler runs things in time order, so it
is timing-based by design. A `Lock` around the pushes would still leave the
order to the sleeps, but it would make the array accesses well defined.

**S17-supply/construction-recursion.t, lines 38–55: polling flags.**
`start { $tap = $s2.tap(…) }`, then `for ^10 { last if $tap; sleep .1 }`. The
same goes for `$closed` and `$done`. The fix is a `Promise` kept by the
`start` block (or `await` on it) instead of polling a variable.

**S17-supply/supplier-preserving.t, lines 13–18: a worker spinning on a
flag.** `start { until $done { $s.emit: ++$ } }`, and `$done = True` in the
supply's `on-close`, on the thread that closes it. Nothing orders the write
with the worker's reads. An `atomicint` with `⚛=`/`⚛`, or a `Promise` the
worker polls, makes it well defined.

**S17-supply/throttle.t, lines 64–85: reading after `sleep 3`.** The test
says so itself: `sleep 3;  # no way to wait yet  :-(`. It then reads `@seen`
and `@statuses`, which the throttle's threads push to.

## The first dists

Each dist's `t/` ran from the dist's root with its own `lib/` (`--root`),
against the dependencies installed in `~/.raku`. The tarballs are the
current versions from the REA index.

| dist | files | clean | races | changed |
|---|---|---|---|---|
| Log::Async 0.0.17 | 17 | 15 | 2 | 0 |
| OO::Monitors 1.1.7 | 5 | 4 | 1 | 0 |
| Cache::Async 0.3.4 | 10 | 10 | 0 | 0 |
| Cro::Core 0.8.10 | 9 | 9 | 0 | 0 |

**Log::Async t/05-concurrent.rakutest: reading after `sleep 2`.** The test
taps the logger with `push @messages, $^message` and
`$string ~= $^message<msg>`, logs 400 messages from `Promise.at(…).then`
chains, sleeps two seconds, then checks `+@messages` and `$string`. The taps
themselves are fine: the logger emits through a `Supplier`, whose emissions
are serialized, and the detector orders them. The main thread's reads are
ordered with none of them. The sweep's first run reported only `$string`.
`push @messages, …` (the sub form) and `+@messages` had no hooks, and adding
them found the engine bug below. Waiting on the logger (`logger.done`, or
the Promises the test makes) instead of sleeping fixes the test.

**Log::Async t/06-sleep.rakutest: the same, on purpose.** A tap sleeps one
second and sets `$message`. The test checks `!$message` at once, then
`sleep 2`, then that `$message` is set. It tests timing, so the race is the
point. A `Promise` the tap keeps would make the second check well defined.

**OO::Monitors t/clone.rakutest: two locks, one flag.** The test shows that a
clone of a monitor has a lock of its own: `wait` on the original spins on a
plain `$hold`, and `release` on the clone sets it. With two different locks
nothing orders that write and those reads, which is what the test sets out
to show, and the spin itself races. An `atomicint` `$hold` (`⚛$hold` in the
loop, `$hold ⚛= 0`) keeps the test's point and makes the flag well defined.

## An engine bug the sweep found

**`push @a, …`, `pop @a` and `shift @a`, the sub forms, crashed rakupp when
two threads did them at once**, with the detector off as well: 3 runs of 6
for two workers pushing 20,000 each. They mutate the array in place, past
the stripe that the methods (`@a.push`) take, so two threads grew or shrank
one buffer at once. That is the no-crash contract broken. They now take the
array's stripe, and for `pop` and `shift` the emptiness test runs under it
too. `unshift`, `append`, `prepend` and `splice` already went through the
methods. `t/stress/ub-array-push.raku` has a sub-form phase now, which the
binary before crashed on every run (6 of 6) and which passes after.

### What the sweep changed in the detector

- **`RAKUPP_OPT=--detect-races` re-ran with a bad argv** (`execv` failed
  with "Bad address"). The `RAKUPP_OPT` merge builds an argv with no null
  at its end. The re-run now takes the argv the OS gave, and the new
  process merges `RAKUPP_OPT` again. There is a new `t/run.raku` check.
- **A thread the engine forks where no statement runs** (a supply's own
  thread) printed as "thread T6" with no spawn site, which reads like an
  unregistered thread. It now says "started by the engine". It is
  registered, and it inherits its parent's clock.
- **Two unhooked forms, now hooked:**
  - A built-in sub given an Array or a Hash first (`push @a, …`,
    `elems %h`) records what the method form records: a length write for
    a resizer, else a length or keys read. The hook sits where `evalCall`
    dispatches a builtin.
  - `+@a` reads the length.

  It is one hook, `raceOn`, which also serves the methods. With the
  detector off it costs at most +0.7% instructions and +0.4% cycles (the
  sub-form `push` loop, stripe included), and the rest are within ±0.2%. A
  new must-report program, `array-push-sub`, keeps the sub form covered.
- `tools/race-sweep.raku` is new. It runs Roast files (`PATTERN`), a
  directory (`--dir`), or a distribution's `t/` from its root with its
  `lib/` (`--root`), and `--skip=WORD` leaves files out.
