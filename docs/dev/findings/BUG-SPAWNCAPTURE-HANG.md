# `spawnCapture` never returns after the child has exited

Found 2026-08-18 while running the rakugrid generators, which drive thousands
of short-lived child processes through `run`/`shell`. **Two independent
occurrences the same evening, with an identical stack.** Not reproduced from a
small script yet — see "How to chase it" below.

## What it looks like

The parent stops dead. It is not spinning: it has burned almost no CPU, and it
never comes back.

| | regex generator | operators generator |
|---|---|---|
| pid | 43365 | 31617 |
| elapsed when stuck | 10 min | 2 h 25 min |
| CPU used in that time | 0.94 s | 7.32 s |
| child state | `<defunct>` | `<defunct>`, 16 min old |
| stuck for | killed after ~10 min | ~18 min, then recovered |

The first was killed. The second **came back**, and how it came back is the most
useful fact here:

| time | |
|---|---|
| 23:22:19 | last probe output, then eighteen minutes of nothing |
| ~23:40:12 | `kill -CONT` and `kill -CHLD` sent by hand |
| 23:40:13 | probe output resumes, the zombie is reaped, CPU advances |

Recovery inside a second of the signals. That is correlation from a single
observation rather than proof, but it is what a **lost wakeup** looks like: the
wait never woke on its own, and any signal — delivering `EINTR` and forcing the
syscall to be re-entered — was enough to free it. Nothing about the child
changed in those eighteen minutes; it had already exited before the wait began.

## The evidence

`sample` puts both in the same place — the deepest Raku++ frame is
`spawnCapture`, at the same offset, and below it only system library frames:

```
rakupp::Interpreter::evalCall(rakupp::Call*) + 8603
  std::__function::__func<rakupp::Interpreter::registerBuiltins()::$_57, …>
    rakupp::spawnCapture(std::vector<std::string> const&, double, …) + 1922
      ???  (in <unknown binary>)  [0x7ff89e480dc8]
        ???  (in <unknown binary>)  [0x7ff89df86ae0]
```

Three facts that together rule out the obvious explanations:

1. **The child has already exited.** It shows as `<defunct>` — a zombie — so
   the parent has not reaped it. If `spawnCapture` were blocked in `waitpid()`
   on that child, the call would have returned the moment the child died.
2. **Nobody holds the pipe.** `lsof` on the parent shows one pipe fd, and a
   search across every process for that pipe id returns only the parent
   itself. With no writer left, a `read()` must see EOF. So it is not simply
   waiting for output that will never come.
3. **A signal frees it.** `SIGCONT` + `SIGCHLD` were followed a second later by
   resumed output and a reaped child. Whatever the wait is waiting for had
   already happened; it just never noticed.

So the child is gone, the pipe has no writers, and the parent is still parked
in a syscall inside `spawnCapture`.

## What was around it

Both times the machine was heavily loaded — another session was compiling and
running its own jobs, load average 7–16 on 8 cores (4 performance). Both
generators were spawning a child every few seconds through the same helper,
which wraps each probe as:

```sh
( exec >/dev/null 2>&1; set -m 2>/dev/null || true; 'raku' probe.raku list ) & p=$!
( sleep $secs; kill -9 -$p 2>/dev/null || kill -9 $p 2>/dev/null ) & w=$!
wait $p; rc=$?; kill $w 2>/dev/null; wait $w 2>/dev/null; exit $rc
```

The watchdog is relevant context but is probably not the cause: it is a
separate process, and by the time the parent hangs it has long since exited.

## Why it matters beyond the generators

Any long-running Raku++ program that shells out in a loop can stop forever with
no error, no exit, and no CPU use — the failure mode is indistinguishable from
"still working" unless something watches for the absence of output. In the run
that found this, 36,000 probed cells were lost because they lived only in the
parent's memory.

## How to chase it

- A loop of `run`/`shell` calls on a deliberately loaded machine is the
  scenario; a quiet machine did not reproduce it in ordinary use.
- The suspicion worth testing first is a **race between reaping the child and
  draining its output** — a lost wakeup when the child exits at the moment the
  parent is deciding whether to keep reading, which would explain the unreaped
  zombie and the absent pipe writer at once.
- `sample <pid>` is the diagnostic: it names the frame immediately and costs
  nothing, and the two samples here were the whole diagnosis.
- Worth checking whether the timeout argument (`double`) is honoured at all on
  this path — neither hang ended, though both had a finite timeout.
