# Cooperative Control Flow

The natural way to implement `return` in a tree-walker is to throw a C++
exception and catch it at the routine boundary. It is correct, it is short, and
it is what Raku++ did first.

It is also slow. On macOS a C++ throw walks the dynamic loader's unwind
information under a lock, costing tens of microseconds. In the WebAssembly
build, where C++ exceptions run through JavaScript trampolines, it is very much
worse. And `return` is not rare — it is on the exit path of a large fraction of
all routines.

## The insight

A `return` only needs to unwind C++ frames if there is a **callable boundary**
between it and its routine. If the `return` executes directly inside its
routine's own statements, or inside a native `for` loop in the interpreter, then
no C++ frame between them belongs to another Raku routine — and "unwinding" is
just a matter of *stopping the statement loop*.

Distinguishing those two cases needs a way to ask "has a callable been entered
since my routine started?", and the answer is two counters.

```cpp
// src/Interpreter.h — ExecContext
bool returning = false;  Value returnV;
uint64_t frameTop = 0;         // incremented per callCallableRaw activation
uint64_t curRoutineFrame = 0;  // frameTop at the enclosing ROUTINE entry
```

Every activation bumps `frameTop`. A routine records the value at its own
entry. So the test is an equality:

```cpp
// src/Interpreter.cpp — return
if (tctx_.curRoutineFrame != 0 && tctx_.frameTop == tctx_.curRoutineFrame) {
    tctx_.returning = true; tctx_.returnV = std::move(v);
    return Value::any();                       // set a flag, do not throw
}
throw ReturnEx{v};                             // a boundary was crossed
```

The statement loops check the flag after each statement and bail out, and
`callCallableRaw` consumes it at the routine boundary:

```cpp
// src/Interpreter.cpp — after each statement in the activation loop
if (tctx_.returning) {
    if (isRoutine) { tctx_.returning = false; last = std::move(tctx_.returnV); }
    break;               // a bare block just propagates it to its routine
}
```

A bare block does *not* consume the flag — it breaks out and lets it propagate
— because a `return` inside `if { … }` returns from the enclosing routine, not
from the block.

## The same trick, three more times

`next`, `last` and `redo` use an identical mechanism with their own register
and frame counter:

```cpp
// src/Interpreter.h — ExecContext
int loopCtl = 0;              // 0 none, 1 next, 2 last, 3 redo
uint64_t curLoopFrame = 0;    // frameTop of the innermost native loop
```

```cpp
// src/Interpreter.cpp — LastStmt
if (t.empty() && tctx_.curLoopFrame != 0 &&
    tctx_.frameTop == tctx_.curLoopFrame) {
    tctx_.loopCtl = 2; return Value::any();
}
throw LastEx{t};              // labelled, or across a frame: unwind
```

`when`, `default` and `succeed` got the same treatment, and for a specific
measured reason. `given $v { when Int {…} when Str {…} … }` executed per row of
a data set means a throw per row:

```cpp
// src/Interpreter.h — ExecContext
int givenCtl = 0;             // 1 = a when matched; break the given
Value givenV;
uint64_t curGivenFrame = 0;
```

In each case the rule is the same: **same frame, set a flag; different frame or
a label, throw.** Labelled control always throws, because a labelled `last`
targets a specific enclosing loop that may be several frames up, and walking to
it is exactly what unwinding is for.

## The rest of the control exceptions

Everything that genuinely has to cross frames is still an exception, and they
are all tiny structs:

```cpp
// src/Interpreter.h
struct ReturnEx { Value v; };
struct ExitEx { int code = 0; };
struct LastEx { std::string label; };
struct NextEx { std::string label; };
struct RedoEx { std::string label; };
struct DoneEx {};                    // exits a whenever/supply/react body
struct BreakGivenEx { Value v; bool hasVal = false; };
struct LeaveEx { Value v; bool hasVal = false; };
struct ResumeEx {};                  // .resume inside a CATCH
struct StopGatherEx {};              // a lazy gather has enough
struct ProceedEx {};                 // leave a when, keep matching later ones
struct RakuError { Value payload; std::string message; };
struct WorkerAbortEx {};             // unwind a background worker at shutdown
```

`RakuError` is the user-visible one: every Raku exception is a `RakuError`
carrying a payload `Value` — normally an exception object — and a message.
`WorkerAbortEx` is deliberately *not* a `RakuError`, so a user's `CATCH` cannot
accidentally swallow a shutdown (Chapter 34).

## `CATCH`, and where it lives

A `CATCH` block is a `Block` statement with `isCatch` set, sitting inside the
block it guards. Finding it means scanning the body — so the answer is cached on
the `Callable`:

```cpp
// src/Value.h — Callable
DecidedOnce<signed char> catchScan{-1};   // 1 = the body holds an inline CATCH
Stmt* catchBlkCache = nullptr;            // …and which one
```

When a `RakuError` reaches a block that has one, the handler runs with `$_` and
`$!` bound to an exception instance. `exceptionFor` guarantees that instance is
always *defined*, whatever was actually thrown, because a `when X::Foo` inside
the handler needs something to smartmatch against.

`.resume` throws `ResumeEx`, caught at the throw point. Because a bare
`ResumeEx` with nothing to absorb it would reach `std::terminate`, the
interpreter tracks how many `CATCH` handlers are live and turns a `.resume`
outside any of them into a catchable Raku error:

```cpp
// src/Interpreter.h
int catchDepth_ = 0;
```

Typed exceptions are built rather than hard-coded:

```cpp
// src/Interpreter.h
[[noreturn]] void throwTyped(const std::string& type,
        std::vector<std::pair<std::string, std::string>> attrs,
        const std::string& message);
Value makeTypedEx(const std::string& type,
        std::vector<std::pair<std::string, Value>> attrs,
        const std::string& message);
```

so `X::TypeCheck::Binding` arrives with its `got` and `expected` attributes
populated and can be matched on class rather than on message text. That
distinction matters: the project's rule is that error *message* wording is only
copied from Rakudo where Roast or the documentation actually asserts it — the
behaviour is what must match, not the prose.

## Backtraces

```cpp
// src/Interpreter.h — ExecContext
struct CallSite { int line; const Value* code; };
std::vector<CallSite> callFrames;
```

One entry per live routine activation: the line the *call* was written on, and
the routine itself. It is pushed next to `dynStack`, which every call already
pays for, so it costs a vector append.

That is what `callframe(N)` walks — `Log::Async` stamps every message with
`callframe(1)` — and what `Exception.throw` turns into a `BacktraceFrame` list.
The routine's declaring file comes from `Callable::declFile`, recorded when the
routine was declared, because by the time a backtrace is taken the module that
declared it is long gone from the current scope.

## What was gained, and one bug it cost

The cooperative path removes a C++ throw from the exit of most routines and the
body of most loops and `given` blocks. On macOS, where a throw is expensive, and
in WebAssembly, where it is much worse, that is the difference between a
tolerable and an intolerable tree-walker.

The mechanism has one hazard, and it bit: **the counters must be maintained
consistently across every activation kind.** Methods go through `invokeMethod`
rather than `callCallableRaw`, and for a period that path did not establish the
routine frame boundary the same way. The symptom was specific and baffling — a
method lost an early `return` when the `return` was inside a loop, but only in
some call contexts. The cooperative flag was set, and the frame that should have
consumed it did not recognise itself as a routine boundary, so the return value
evaporated.

The fix was to establish the boundary in `invokeMethod` exactly as
`callCallableRaw` does, and the regression test is a method with a `return`
inside a loop, called every way the language allows.

The general lesson is worth stating: **an optimisation that replaces a
language-level mechanism with a hand-maintained invariant is only as correct as
the least careful place that maintains it.** C++ unwinding could not be got
wrong this way, because the compiler maintained it. Frame counters can.
