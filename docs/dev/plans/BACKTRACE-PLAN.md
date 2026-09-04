# Plan: backtraces — the default tracer (issue #67)

**Status: DRAFT 2026-09-04 — code not started.** Probes run against
`build/rakupp` at e5db047; Rakudo 2025.x from `/usr/local/bin/raku` as the
oracle. Prompted by [issue #67](https://github.com/ash/rakupp/issues/67)
("Stack trace wanted"); the verbosity knob was already on the wish list in
[CLI-BORROW-PLAN.md](CLI-BORROW-PLAN.md) ("Backtrace verbosity control").

Goal: an uncaught error tells the reader **what** went wrong, **where**
(file, routine, line, module), and **how the program got there** — in a
form a human can scan in one look, with the full detail one knob away.
`$!.backtrace`, `.gist`, `warn`, Failures and awaited exceptions all read
from the same record and print through the same renderer.

## Where we stand

The data is already collected: every routine activation pushes a
`CallSite {line, code}` onto `tctx_.callFrames`
([Interpreter.h:714](../../../src/Interpreter.h)), `curLine_` tracks the
executing statement, and `Interpreter::captureBacktrace()`
([Interpreter.cpp:12816](../../../src/Interpreter.cpp)) turns that into an
innermost-first list of `BacktraceFrame`s with `.file/.line/.code`.
`Backtrace.new` works (t/regression/backtrace-new.raku).

What is missing is the link between throwing and that capture:

- Only `$exception.throw` on a Raku-level object captures (`__bt` attr,
  [MethodCallPart2.cpp:3235](../../../src/MethodCallPart2.cpp)).
- `die`, `fail`, and the ~320 `throw RakuError{payload, message}` sites in
  the C++ builtins (Builtins.cpp 85, Interpreter.cpp 143, MethodCall* 85,
  others 6) throw a bare payload + message. C++ unwinding then runs the
  `CFGuard` destructors, which pop `callFrames` — by the time any `catch`
  runs, the chain is gone.
- `.backtrace` on an exception without `__bt` captures **at the call**:
  after `try { h() }; $!.backtrace` answers one frame, the line of the
  `.backtrace` call itself.
- The top-level handler ([Interpreter.cpp:4544](../../../src/Interpreter.cpp))
  prints `e.message` and exits 1. Nothing else.

Probe results (scratch programs, both engines, 2026-09-04):

| scenario | Rakudo | rakupp today |
|---|---|---|
| `die` three calls deep | message + 4 frames (`in method baz … line 3` … `in block <unit> … line 10`) | message only |
| builtin error (`No such method`) | message + frames | message only |
| error inside a `use`d module | frames name `lib/Mod.rakumod (Mod) line 2` | message only |
| `warn` inside a sub | message + `in sub f at w.raku line 1` | message only |
| `fail` used later | frames at the `fail`, then `Actually thrown at:` + frames at the use | message only |
| `die` inside `start`, then `await` | `An operation first awaited:` frames, then `Died with the exception:` + worker frames | message only |
| recursion 200 deep | 200 identical frame lines | message only |
| `$!.gist` after `try` | message + frames | message only |
| `$!.backtrace.Str` after `try` | frame lines | a hash dump (`file\t…\nline\t5`) with the WRONG line |
| `$!.backtrace.list` after `try` | 5 frames (incl. Rakudo's own `throw`/`die`) | 1 frame, at the `.backtrace` call |

## The output we want

Principles, in priority order:

1. **Line 1 is the message, unchanged.** Every test, golden and grep that
   reads the first line keeps working.
2. **Frame lines are Rakudo's, verbatim**: `  in sub f at FILE line N`,
   `  in method m at …`, `  in block <unit> at …`, `  in block  at …`
   (anonymous, two spaces — Rakudo's spelling), `  in regex r at …`,
   `(Module::Name)` after a module's file. Editors, test suites and
   Log::Async-style consumers already parse this shape; there is no gain
   in inventing another.
3. **Add information only where it is cheap to read**: one source-line
   excerpt under the origin frame, the exception type for typed
   exceptions, paths relative to the working directory, repeated frames
   collapsed, an explicit "N frames omitted" instead of a wall.
4. **One renderer**, so the uncaught printer, `.gist`, `Backtrace.Str`,
   `warn`, the JSON handler and the REPL/Jupyter/MCP error paths never
   drift apart.
5. **Never lose the detail**: `--ll-exception` / `RAKUPP_BACKTRACE=full`
   prints everything, uncollapsed, uncapped.

Target for the three-call `die` (file run as `rakupp t1.raku`):

```
boom in baz with 2
  in method baz at t1.raku line 3
      3 │     method baz($x) { die "boom in baz with $x" }
  in method bar at t1.raku line 2
  in sub helper at t1.raku line 7
  in block <unit> at t1.raku line 10
```

Typed exception — the type on its own line after the message, so a reader
knows what to `CATCH` without guessing (the first line still equals
`.message`):

```
No such method 'nonexistent-method' for invocant of type 'Int'
  (X::Method::NotFound)
  in sub g at t3.raku line 1
      1 │ sub g($x) { $x.nonexistent-method }
  in block <unit> at t3.raku line 2
```

Module frames — as-invoked path for the program, cwd-relative path for a
module under the cwd, absolute otherwise; the package in parentheses as
Rakudo prints it:

```
deep 7
  in sub deep at lib/Mod.rakumod (Mod) line 2
      2 │ sub deep($x) is export { die "deep $x" }
  in sub top at lib/Mod.rakumod (Mod) line 3
  in block <unit> at mod.raku line 3
```

Recursion — runs of identical `(routine, file, line)` longer than 3
collapse; more than 40 frames in total are cut with a pointer to the knob:

```
bottom
  in sub fact at rec.raku line 1
      1 │ sub fact($n) { return $n if $n == 0 ?? die "bottom" !! 0; 1 + fact($n - 1) }
  in sub fact at rec.raku line 1
  in sub fact at rec.raku line 1
  … 198 more frames of sub fact at rec.raku line 1
  in block <unit> at rec.raku line 2
```

Colour, only when stderr is a terminal and `NO_COLOR` is unset: the
message bold, routine names bold, paths dim, the excerpt plain. The
non-TTY text is exactly the same bytes minus escapes.

What stays Rakudo-shaped without additions: `warn` prints the message
plus the innermost user frame only (that is what Rakudo prints and it is
the right amount for a warning); `.gist` is message + frame lines with
no excerpt, no type line and no colour, because `.gist` is a *string*
programs print and compare — the extras belong to the uncaught printer.

## Architecture

### 1. Capture where the throw happens — in `RakuError`'s constructor

`struct RakuError { Value payload; std::string message; }` gains a
constructor that snapshots the live chain:

```cpp
struct BtFrame { std::shared_ptr<Callable> code; int line; };   // code null = mainline
struct RakuError {
    Value payload; std::string message;
    std::shared_ptr<std::vector<BtFrame>> frames;  // innermost first
    std::string originFile;                        // curDeclFile() at the throw
    RakuError(Value p, std::string m);             // captures; brace-init sites compile unchanged
};
```

Innermost line = `curLine_` at the throw; every outer frame's line is the
`CallSite.line` already recorded (the call-site line in that activation).
`code` is copied as an owning `shared_ptr<Callable>` (`Value::codeS()`),
not the borrowed `const Value*` the live stack holds, so the record
survives the unwind. The mainline frame's file is `curDeclFile()` (the
module or EVALFILE whose top level is running), as `captureBacktrace`
already does.

Cost model: **zero on the non-throwing path** (nothing changes in
`callCallable`), and depth × one refcount increment per throw. A `try` in a
hot loop at depth 20 pays ~20 atomic increments per iteration — under
100 ns. Deep recursion that dies at 100 000 frames pays ~1 ms once. Rakudo
captures every frame on every throw too.

Alternative considered and rejected: record frames as `CFGuard` pops them
during unwinding (`std::uncaught_exceptions() > 0`). It is cheaper per
throw (only the frames actually unwound), but it adds a TLS read to
**every** routine return, and the perf gate ([rakupp-perf-release-gate])
has caught 1–2% regressions from less. It also needs a marker discipline
at every catch site to keep frames of C++-swallowed exceptions from
leaking into the next trace; the constructor needs none of that.

Sites that build a `RakuError` only to convert it
(`failureException` → `exceptionFor(RakuError{…})`) use a tagged
constructor that captures nothing.

### 2. Carry the record into the Raku object, lazily

`exceptionFor(e)` (called at every Raku-level catch: `try`, `CATCH`,
top-level, `.resume`) attaches `e.frames` to the exception object as
`__bt` — but as a native handle (an `ext`-style shared payload), **not** as
materialised `BacktraceFrame` hashes. A `try` that never asks for the
backtrace pays one pointer store. `.backtrace` materialises into the
existing `Backtrace` list of `BacktraceFrame` hashes on first use and
caches the result. An existing `__bt` is never overwritten: `rethrow`,
`.resume`, and `die $caught` keep the origin, as Rakudo's do.

`die $obj` with a user exception object: the constructor captured; the
object gets `__bt` the same way (`e.payload` is the object).

### 3. One renderer

```cpp
struct BtStyle { bool excerpt, typeLine, colour, collapse; int cap; bool ll; };
std::string Interpreter::renderBacktrace(const std::vector<BtFrame>&, const BtStyle&);
```

Frame naming from `Callable`: `isMethod` → `method`, `isSubmethod` →
`submethod`, `isRegexRoutine` → `regex`, `isBlock` → `block` (anonymous
spelled Rakudo's way), name empty at depth 0 → `block <unit>`; runtime-made
callables (`langRev == -1`: WhateverCode, `.assuming` wrappers, `wrap`
shims) are hidden in short mode and shown in full mode, which is what
Rakudo's `is-hidden` does for its setting frames. `pkg` supplies the
`(Module)` annotation when it differs from GLOBAL and the frame's file is
not the program.

Paths: program frames print `srcFile_` (as invoked). Module frames print
`declFile` made relative to the cwd when it is under it, absolute
otherwise. `BacktraceFrame.file` (the API) keeps the absolute path —
backtrace-new.raku and Log::Async match on `ends-with`/prefix, both fine.

Excerpt: a per-file source-line cache filled on demand (`-e` text, EVAL
strings keyed by their `EVAL_n` name, files read once). No source → no
excerpt line, silently (`--exe` binaries, slim builds, precompiled
modules whose source moved).

Consumers: the top-level uncaught printer; `Exception.gist` (message +
frames, no extras); `Backtrace.Str/.gist/.full/.nice/.summary/.list/
.next-interesting-index/.is-hidden/.is-routine/.is-setting/.subname`
(the Rakudo surface, several already stubbed at
[MethodCallPart2.cpp:2173](../../../src/MethodCallPart2.cpp));
`BacktraceFrame.Str` (currently hard-codes `in block at`); `warn`;
`RAKU_EXCEPTIONS_HANDLER=JSON` (`backtrace: [{file,line,code}]`); the REPL,
Jupyter and MCP error paths (they print `e.message` today — same renderer,
short style, no colour).

### 4. Knobs

- `--ll-exception` — Rakudo's flag: full frames, hidden ones included, no
  collapse, no cap.
- `RAKUPP_BACKTRACE=0|short|full` — `0` is today's message-only output
  (for goldens that must not change), `short` the default, `full` as
  above. This is the CLI-BORROW item; it is closed here.
- `NO_COLOR` honoured; `RAKUPP_COLOR=0|1` forces.

## Phases

Each phase lands only after `t/run.raku` (regressions + examples), the
Roast slices below, and `tools/perf-guard.raku --check` are green, per
[rakupp-batch-loop] and [rakupp-perf-release-gate].

**P1 — the chain exists and prints (closes the issue's core).**
`RakuError` capture; `exceptionFor` transfer; renderer with Rakudo-exact
frame lines; the uncaught printer; `.gist`; `Backtrace.Str`/`.list` and
`BacktraceFrame.Str` fixed; `$!.backtrace` after `try` reports the throw
site. Regression cases: `t/regression/backtrace-uncaught.raku` (runs
`$*EXECUTABLE` on fixtures, asserts `in sub`/`in method`/`in block
<unit>`/module `(Mod)`/line numbers on stderr, exit 1),
`backtrace-caught.raku` (`$!.backtrace` frames and lines after `try`,
`rethrow` keeps origin, `.gist` shape). Roast: S32-exceptions/misc.t,
S29-context/evalfile.t, S04-exceptions/*.t — record the pass counts before
and after in this file.

**P2 — the readable layer.** Excerpt, type line, relative paths and
`(Module)`, collapse + cap, colour + `NO_COLOR`, `--ll-exception` and
`RAKUPP_BACKTRACE`. Docs: a "When something dies" section in the guide,
FEATURES/REFERENCE per [raku-pp-doc-sync]; the CLI-BORROW entry marked
done.

**P3 — the other throw paths.** `warn` (innermost user frame); `fail`
(capture at the `fail` into the Failure hash → `Actually thrown at:`
section on detonation; measure a fail-heavy loop, e.g. `"abc".Int` × 1e6,
before/after — builtin Failures are created in C++ and should capture in
one shared constructor); `await` (`An operation first awaited:` — keep the
worker's frames in the Promise's stored exception, add the await site on
rethrow); compile-time errors gain Rakudo's `------> …⏏…` source line (the
lexer already knows the column); JSON handler frames; REPL/Jupyter/MCP.

**P4 — optional.** Column tracking (`Node.col`, a per-expression position
store in the evaluator) for a caret under the failing expression: costs a
store per expression evaluation, so it is measured before it is kept; a
cheaper heuristic (underline the method or sub name the message names) may
be enough. `--target=js` parity: the JS runtime's throw path can adopt the
same renderer text; that code is owned by the transpile session and is
not touched here.

## Traps and things to check before landing

- **`CFGuard` restores `curLine_` on pop.** Capture must happen before
  unwinding starts — the constructor guarantees it; nothing in a `catch`
  can recover the innermost line.
- **C++ `catch (...)` swallows.** `die`'s `.message` probe, `gistOf` and a
  few dozen others catch and drop; each throw inside them pays the capture
  for nothing. Negligible, but a place to look if a profile shows
  `RakuError::RakuError`.
- **Builtins push no frame.** `die`, `throw`, `sort` are C++, so
  `.backtrace.list` will not contain the `throw@65 die@253` entries
  Rakudo's does; Roast tests that count frames need a look, and
  `.next-interesting-index` must skip nothing where Rakudo skips two.
- **Goldens.** `t/expected/*.out` that capture stderr change on purpose;
  refresh them one by one, not with a loop. Check whether Rakugrid or the
  conformance campaign compares stderr before P1 lands — if they do, the
  new output should *reduce* the diff against Rakudo, and any golden that
  gets worse is a bug in the renderer.
- **Serialised ASTs** (`--exe`, precomp): `Callable.declFile` is not
  serialised — it is stamped from `curDeclFile()` when the declaration
  RUNS (Interpreter.cpp:7579, 7864, 8516, 9958), so a compiled binary's
  frames name the right file as long as `curDeclFile_` is switched while a
  deserialised module's top level executes. Verify with one `--exe` probe.
  Frames never need the source text, only the excerpt does.
- **Threads.** `tctx_` is thread-local; a worker's throw captures the
  worker's chain; `shared_ptr<Callable>` crosses threads safely. The
  `curLine_` threaded/global split is unchanged.
- **The transpile session** edits `src/codegen/Js.*`, `src/js-rt/`,
  `src/JsRuntimeSrc.cpp`, and also `src/main.cpp` and `t/run.raku`. This
  plan's code lives in `Interpreter.h/.cpp`, `MethodCallPart2.cpp`,
  `Builtins.cpp` (die/warn/fail) and `Value.h` (BtFrame) — disjoint —
  plus one flag in `main.cpp` and nothing in `t/run.raku` (regression cases
  are auto-discovered). Keep the `main.cpp` edit to the argument loop so
  a merge stays trivial, and never `git add` their files
  ([shared-tree-concurrent-sessions]).

## Decisions to confirm

1. The type line `  (X::Method::NotFound)` after the message for typed
   exceptions — on by default, or only under `RAKUPP_BACKTRACE=full`?
   Recommendation: on by default; it is the single most useful line for
   someone about to write a `CATCH`.
2. Excerpt for the origin frame only (recommended) vs every frame
   (Python 3.11 style; four times the height for no more orientation).
3. Cap at 40 frames with collapse threshold 3 — numbers to tune on real
   traces from raku-corpus programs.
