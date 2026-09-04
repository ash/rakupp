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
accidentally swallow a shutdown (Chapter 37).

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
`callframe(1)` — and what a program reads through `Backtrace.new`, which answers
the chain at the point of call; an `Int` argument drops that many innermost
frames, so a routine can report its caller's position rather than its own.

The routine's declaring file comes from `Callable::declFile`, recorded when the
routine was declared, because by the time a backtrace is taken the module that
declared it is long gone from the current scope. Frames with no declaring
routine — the mainline, a bare block — take the file whose *top level* is
running, `curDeclFile_`. That is the program ordinarily, but a module load
switches it while the module's body runs, and so does `EVALFILE`, which is how
a backtrace taken inside an `EVALFILE`d file names that file instead of the
program that read it.

### Where the chain has to be captured

For a long time all of that machinery existed and almost nothing used it. An
uncaught error printed its message and stopped: no file, no line, no chain.
`$!.backtrace` after a `try` answered a single frame on the line of the
question rather than the line of the throw.

The reason is the interesting part, and it is the same C++ unwinding this
chapter has been about. `callFrames` is maintained by a scope guard:

```cpp
struct CFGuard { ExecContext& t; Interpreter* self; int line;
    ~CFGuard() { if (!t.callFrames.empty()) t.callFrames.pop_back(); self->curLine_ = line; }
};
```

When a `RakuError` is thrown, the C++ runtime unwinds through every frame
between the throw and the handler, running exactly those destructors on the way.
By the time any `catch` block runs, the stack it would want to describe has
already been dismantled, frame by frame. **A backtrace cannot be taken where the
exception is caught. It has to be taken where it is thrown.**

There are two places that could do it. One is the unwind itself: have `CFGuard`
notice `std::uncaught_exceptions() > 0` and append its own frame to a
thread-local record on the way out. That is cheaper per throw, since it records
only the frames actually unwound — but it puts a thread-local read on *every
routine return* in the interpreter, and it needs a discipline at every `catch`
site to stop frames from a swallowed exception leaking into the next trace.

The other is the throw. `RakuError` is the one type every Raku-level error is
raised as, so giving it a constructor makes every one of the roughly 320 throw
sites in the tree capture without any of them being edited:

```cpp
RakuError::RakuError(Value p, std::string m)
    : payload(std::move(p)), message(std::move(m)) {
    bt = btCaptureNow();
}
```

This is the version that shipped. The cost model decided it: a constructor costs
one reference-count bump per live frame *on the path that throws*, and exactly
nothing on the path that does not. The unwind-based version is faster in the
rare case and slower in the common one, which is the wrong way round.

The record is one shared allocation:

```cpp
struct BtFrame  { std::shared_ptr<Callable> code; int line = 0; };
struct BtRecord { std::vector<BtFrame> frames; std::string originFile; };
```

`code` is an *owning* pointer, not the borrowed `const Value*` the live stack
holds, precisely because the record has to outlive the unwind that destroys
what the live stack points at.

### Handing it to the program

`exceptionFor` is the single funnel through which a caught `RakuError` becomes
a Raku exception object — `try`, `CATCH`, the mainline handler and `.resume`
all go through it — so that is where the record is attached, as an opaque
handle rather than a materialised list. A `try` that never asks for the
backtrace pays one pointer store; the `BacktraceFrame` objects are built the
first time a program actually asks, and cached back onto the exception.

An existing record is never overwritten. A `rethrow`, a `.resume`, or a
`die $caught` keeps the position the exception was *first* raised from, which
is the only position that answers "where is the bug".

### Errors that happen in one place and are noticed in another

Two constructs break the assumption that an error has a single position, and
both are common enough that reporting the wrong one is worse than reporting
none.

A **Failure** is created by `fail`, or by any failed coercion, and then lies
dormant until something uses the value. The throw happens at the *use*. Reported
naively, every such error blames a line that merely read a variable. So a
Failure records its own creation:

```cpp
Value rakuppNewFailure() {
    Value f = Value::makeHash(); f.hashKind = "Failure";
    f.extM() = btCaptureNow();
    return f;
}
```

and `failureDetonate` puts the creation chain in front and labels the throw
`Actually thrown at:`, which is Rakudo's spelling. That helper replaced 26
hand-written `Value::makeHash(); hashKind = "Failure"` pairs scattered across
five files — the kind of duplication that guarantees a later change reaches
only some of them.

Failures are the one part of this feature that is not free, because ordinary
code makes them in bulk. The measured cost is about 9% of making a Failure, of
which roughly half is the walk and half the allocations. The first version cost
12.6%; the difference is where the record lives. Storing it as a `__bt` entry in
the Failure's hash cost a map node and a second `Value`; a Failure's own `ext`
handle was sitting unused, so the record went there instead.

A **`start` block** dies on a worker thread, and some later `await` on some
other thread is where a program hears about it. `PromiseState` carries the
worker's record across the boundary alongside the cause, and `await` reports
the worker's frames first, then labels its own line `Awaited at:`.

### One renderer

Everything that prints a chain goes through one function, so the uncaught
printer, `.gist`, `Backtrace.Str`, `warn`, the JSON handler and the REPL cannot
drift apart on what an error looks like. The style struct is what separates
them: the terminal gets a source-line excerpt under the origin frame, the
exception type, folded runs of identical frames and colour; `.gist` and
`Backtrace.Str` get none of it, because those are strings that programs print
and compare rather than terminal output.

There is one deliberate departure from Rakudo's frame text. Rakudo prints
`in method new`; Raku++ prints `in method Foo::new`. In a program where six
classes each declare a `new`, the bare form does not say which one ran. The
package goes inside the name part, ahead of the ` at `, so anything parsing
these lines by splitting on ` at ` is unaffected.

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
