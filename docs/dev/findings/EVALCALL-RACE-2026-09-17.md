# A data race in `evalCall`'s name lookup segfaults the process

Found by the v4.0.0 release gate, 2026-09-17. **Not introduced by that release's
work** — measured below. Recorded here rather than fixed, because v4.0.0 is the
modules-and-embedding release and a data-race fix is a project rather than a
release task.

## What happens

Call the same subroutine from several promise threads at once and the process
segfaults, about half the time. Nine lines:

```raku
sub worker(Any $a, Int $b) {}
my $deaths = 0;
for ^200 {
    my $value = Any;
    my @workers = (^4).map: { start { worker($value) } };
    try {
        await @workers;
        CATCH { default { $deaths++ } }
    }
}
say "deaths: $deaths";
```

Correct output is `deaths: 200` — every call fails to bind (one argument for two
parameters), so every promise breaks, and the `CATCH` counts them. Instead the
process dies with `Segmentation fault: 11` (exit 139) roughly one run in two.

This is reduced from **`S17-promise/start.t` lines 73–89**, whose own description
is *"Getting signature bind failure in Promise reliably breaks the Promise"* — a
Roast test that exists to provoke exactly this. The bind failure is not itself the
bug; it is a cheap way to get many threads through the call path at once.

## The crash

From the macOS report (`~/Library/Logs/DiagnosticReports/rakupp-*.ips`):

```
EXC_BAD_ACCESS (SIGSEGV)
KERN_INVALID_ADDRESS at 0x1658b395c0717664 -> 0x00003395c0717664
    (possible pointer authentication failure)

std::unordered_map<std::string, rakupp::Value>::find(...)
rakupp::Interpreter::evalCall(rakupp::Call*)
rakupp::Interpreter::exec(rakupp::Stmt*, bool)
rakupp::Interpreter::callCallableRaw(...)
rakupp::Interpreter::callCallable(...)
rakupp::Interpreter::spawnPromise(rakupp::Value, rakupp::Value)::$_0
rakupp::Interpreter::BigStackThread::BigStackThread<...>::__invoke(void*)
_pthread_start
```

A hash-table lookup walking a garbage pointer, on a promise worker thread. The
address is not merely unmapped — it fails pointer authentication, which is what a
torn or recycled node pointer looks like on arm64. That is the signature of a
read concurrent with a rehash or an erase, not of a null.

`evalCall` is resolving the call target, so the map is a symbol table reached on
every call. Nothing in the repro writes to it from Raku, so the writer is inside
the engine — that is the thing to find first.

## It is not new

Two independent measurements say so.

**Rates.** The same repro, same machine, same sitting:

| binary | crashes |
|---|---|
| `v3.28.0-102-g887c9366` (HEAD, unmodified) | **7 of 16** |
| HEAD + the two v4.0.0 gate fixes | 4 of 8 |
| `v3.25.0` (`build/rakupp`) | **0 of 8** |

HEAD and HEAD-plus-fixes are indistinguishable. v3.25.0 is clean across 8 runs,
so the race arrived after it — somewhere in v3.26.0, v3.27.0 or v3.28.0, or in
the 102 commits since. That is the range worth bisecting.

**Crash reports.** The `evalCall` SIGSEGV signature appears at 2026-09-16
23:29:48, 2026-09-17 00:20:01, 08:43:56, 11:12:41 and 11:17:08 — all of them
*before* the first edit of the v4.0.0 gate sitting (11:23:21), and the last two
during that sitting's own baseline Roast runs on the unmodified binary.

A separate `SIGBUS` / `___chkstk_darwin` / `applyArith` signature also appears in
those reports. That is a **stack overflow in arithmetic**, a different bug, and
the two should not be conflated.

## Why it is easy to misread

The crash is a coin flip, so *which* Roast file it kills varies from run to run.
A file that dies mid-run reports as `[part]` with a truncated count — the same
shape as a file that genuinely lost assertions. During the v4.0.0 gates this read
as `S17-promise/start.t` regressing from 41/44 to 0/1 and was briefly attributed
to an engine change; the pre-change binary had in fact crashed identically two
runs earlier, on a different file.

Two rules follow, both of which cost time here:

- **A single run cannot attribute a probabilistic failure.** The first control
  run passed and was treated as decisive; at a ~44% rate that is a coin flip.
- **A collapsed denominator in the Roast join may be a crash, not a regression.**
  Check the exit status before reading it as lost assertions.

## Where to start

The map is `unordered_map<std::string, Value>` and it is read in `evalCall` on
the call-resolution path. Find the writer: anything that inserts into a symbol
table after threads exist — a lazily-populated cache, a `state` or `INIT` slot, a
memoized lookup. A read-mostly map that is *written* on first use is the usual
shape, and the usual fix is to populate it before any thread can run, or to give
it the same protection the rest of the shared state already has.

`tools/` has no TSan build wired up, but `build-tsan/` exists; running this repro
under it should name the writer directly.
