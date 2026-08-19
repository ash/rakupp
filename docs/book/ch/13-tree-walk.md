\part{The Interpreter}

# The Tree Walk

There is no bytecode. `Interpreter::eval(Expr*)` returns a `Value`;
`Interpreter::exec(Stmt*)` runs a statement and returns its value. Both are
recursive, both switch on the node's `NK` tag, and the C++ call stack *is* the
Raku call stack.

That decision buys a small implementation and costs throughput, and every
optimisation in Chapters 19, 27 and 28 exists because of it.

## The two entry points

```cpp
// src/Interpreter.h
Value eval(Expr* e);
Value exec(Stmt* s, bool sink = false);
Value execBlock(Block* b, std::shared_ptr<Env> scope, bool sink = false);
```

`exec` takes a **sink** flag. When a statement's value is discarded — a loop
body, a statement in the middle of a block — the flag says so, and an assignment
can skip materialising its (possibly very large) result. It is a small thing
that turns the naive `$s ~= …` in a loop from quadratic into linear, in
combination with the in-place append described in Chapter 27.

`execBlock` runs a statement list in a given scope, handling the phaser
schedule around it.

## Execution registers

The state that belongs to one thread of Raku execution is gathered into a
struct rather than scattered across interpreter members:

```cpp
// src/Interpreter.h — ExecContext, abridged
struct ExecContext {
    std::shared_ptr<Env> cur;            // current lexical scope
    std::vector<Env*> dynStack;          // the dynamic ($*foo) caller chain
    int callDepth = 0;
    Env* curStateEnv = nullptr;
    std::vector<std::shared_ptr<ValueList>> gatherStack;
    std::vector<ValueList*> supplyStack;
    std::vector<Value*> makeTargets;
    std::string pkgPrefix;

    bool returning = false;  Value returnV;      // cooperative return
    uint64_t frameTop = 0, curRoutineFrame = 0;
    int loopCtl = 0;         uint64_t curLoopFrame = 0;
    int givenCtl = 0;        Value givenV;  uint64_t curGivenFrame = 0;

    struct CallSite { int line; const Value* code; };
    std::vector<CallSite> callFrames;            // for callframe(N), backtraces
    int wantLvalue = 0;  Value* lvalueOut = nullptr;
};

static thread_local ExecContext tctx_;
```

It is `static thread_local`, so each real worker thread owns its own set. That
is the foundation of the concurrency model in Chapter 37 — with per-thread
registers, running Raku on a second thread does not need a register swap at
every handover.

Two chains, not one. **`cur`** is the lexical chain: a callee's parent is the
scope it was *written* in, reached through its `Callable::closure`.
**`dynStack`** is the dynamic chain: the caller's scope, pushed on entry, walked
only when resolving a `$*foo`. Keeping them separate is what makes lexical and
dynamic scoping both correct and both cheap.

## Evaluating an expression

`eval` is a switch. The interesting arms are the ones that are *not* a
straightforward recursion.

**`VarExpr`** is the hottest node in most programs. Its fast path is a `find`
and a copy:

```cpp
// src/Interpreter.cpp — eval(VarExpr), the plain-lexical fast path
if (Value* p = tctx_.cur->find(ve->name))
    if (!(p->t == VT::Hash && p->hashKind == "Proxy"))
        return *p;
```

with the `Proxy` check placed so that the common case is one comparison. Names
with a twigil — `$*dyn`, `$?FILE`, `$^a`, `$/` — never reach this path; each has
its own lookup rule, and the fast paths in Chapter 19 exclude them by testing
the second character of the name.

**`Binary`** dispatches through `applyArith`, but first consults two
decided-once fields on the node: `simpleOp`, which records whether this operator
needs special handling at all, and `fastShape`, which records the syntactic
shape of the operands. Chapter 19 is entirely about those.

**`Index`** reads a container element, and has a matching `fastShape`.

**`Call`** and **`MethodCall`** are Chapters 14 and 16.

**`InterpStr`** evaluates each part and concatenates. The parts were already
parsed into sub-expressions by the front end, so there is no string scanning
here at all.

**`NqpOp`** exists only under `use nqp` (Chapter 34).

## Evaluating a statement

`exec` is the same shape, with the control-flow statements doing the real work.
Two mechanisms recur through all of them.

### Hoisting

Before a statement list runs, two things are pre-registered.

**`hoistSubs`** walks the list and defines every named `SubDecl` in the scope,
so a sub is callable across its whole enclosing scope regardless of textual
order. This is why subs need no forward declaration while *operators* do
(Chapter 6): sub visibility is a runtime fact, operator visibility a parse-time
one.

**`hoistExprDecls`** pre-declares `my` variables that are buried inside
expressions — in a ternary branch, in an `nqp::` argument — because Raku scopes
them to the block, not to the expression.

Both are guarded by a decided-once flag so the walk happens once per block
rather than once per entry:

```cpp
// src/Interpreter.h
void hoistExprDecls(const std::vector<StmtPtr>& stmts, Env* env,
                    DecidedOnce<signed char>* cache = nullptr);
```

```cpp
// src/Ast.h — Block
DecidedOnce<signed char> hoistNeed{-1};  // -1 undecided, 0 no, 1 yes
```

For a block with nothing to hoist — the overwhelming majority — the cost after
the first entry is reading one byte.

### Phasers

`execBlock` runs the phaser schedule around the statement list:

| Phaser | When |
|---|---|
| `ENTER`, `FIRST` | at block entry, in source order |
| `LEAVE` | at block exit, reverse order, always |
| `KEEP` | at block exit, only on a successful exit |
| `UNDO` | at block exit, only on an unsuccessful one |
| `NEXT` | at the end of each loop iteration |
| `LAST` | once after a loop's final iteration |
| `CATCH`, `CONTROL` | as handlers around the block |

```cpp
// src/Interpreter.h
void runEnterPhasers(const std::vector<StmtPtr>& stmts);
void runLeavePhasers(const std::vector<StmtPtr>& stmts, bool ok = true);
void runNextPhasers(const std::vector<StmtPtr>& stmts,
                    std::shared_ptr<Env>& scope);
```

The program-level phasers — `BEGIN`, `CHECK`, `INIT`, `END` — are scheduled by
`run()` rather than by a block: `BEGIN` in source order after the parse, `CHECK`
reversed, `INIT` immediately before the mainline, `END` at process exit. This is
where the "the parser runs nothing" decision from Chapter 5 becomes visible: a
`BEGIN` block runs *after* the whole file has been parsed, so it cannot
influence how later source is read.

## Loop bodies

Every loop form funnels through one function:

```cpp
// src/Interpreter.h
bool runLoopBody(Block* b, std::shared_ptr<Env> scope,
                 const std::string& label = "",
                 bool isFirst = true, bool isLast = true,
                 ValueList* collect = nullptr,
                 const std::function<void()>& rebind = nullptr);
```

It runs one iteration and handles `redo`, `next` and `last` — returning `false`
when the loop should stop — plus the `FIRST`/`NEXT`/`LAST` phasers. The
`collect` parameter is how a loop used in **value context** works: `my @x = do
for @y { … }` appends each iteration's value rather than discarding it. The
`rebind` callback re-binds the loop variable for a `redo`.

Concentrating this in one function is what makes `for`, `while`, `until`,
`loop` and `repeat` agree about control flow, which they historically did not.

## Aliasing loops

`for @a { $_ = … }` must write **into** `@a`, so the topic has to alias the
element rather than copy it. That requires knowing what the loop source
*expression* is, not just its value, so the interpreter has a small family of
functions that peer at the source expression:

```cpp
// src/Interpreter.h
std::shared_ptr<std::map<std::string, Value>> valuesAliasSource(Expr*);
std::shared_ptr<ValueList> derefArrayAlias(Expr*);
bool scalarListAlias(Expr*, std::vector<Value*>& slots);
Value* topicAliasSlot(Expr* topic, bool skip);
Expr* peelGrepFilter(Expr* listExpr, Expr*& pred);
```

The last one is the sort of detail that only shows up against a real test suite:
`for %h.values.grep(…) { $_ = … }` must *still* alias, because Rakudo's `grep`
is `is raw`. So the loop peels a trailing `.grep(PRED)` off the source
expression, aliases the underlying container, and applies the predicate as a
filter during iteration.

## Sink context and `it does not matter what this returns`

Statement values matter in Raku — the last statement of a block is its value —
so `exec` returns one. But most statements' values are discarded, and building
them can be expensive.

The `sink` flag threads that knowledge down. Its most valuable use is
assignment: in sink context, `evalAssign` does not need to produce the assigned
value as a result, so `@big = @other` does not build a second copy to throw
away. Combined with `rtCatAssign`'s in-place append (Chapter 27), this is what
makes string building in a loop linear.

## Recursion, and the stack it needs

Because the C++ stack is the Raku stack, recursion depth is bounded by the
thread's stack size. The default for a non-main thread on macOS is 512 KB, which
a recursive Raku sub overflows within about a hundred frames.

So every entry point runs the program on a thread with a very large stack:

```cpp
// src/Runtime.h
int rakuppRunBigStack(const std::string& src, std::vector<std::string> args,
                      const std::string& fileName, const std::string& exePath,
                      const std::vector<std::string>& libPaths = {});
int rakuppMainOnBigStack(int (*body)(void*), void* ctx);
```

and worker threads get the same treatment through `BigStackThread`, which
reserves 256 MiB of *virtual* address space — committed only as used
(Chapter 37). A compiled `--exe` binary calls `rakuppMainOnBigStack` for its own
main body, so the recursion budget is the same in every mode.

The interpreter also keeps a `callDepth` register and raises a clean Raku error
when it gets too deep, so a runaway recursion produces a diagnostic rather than
a segmentation fault.

## What this costs, honestly

Re-dispatching every AST node on every execution is inherently slower than
bytecode or a JIT. The profile of a method-heavy loop, after the fixes in
Chapters 10 and 11, looks like this:

| | share |
|---|---:|
| heap allocate and free | 31% |
| `Value` copy and destroy | 11% |
| method-name comparison | 8.5% |
| the dispatch function's own body | 6.1% |

Note what is *not* there: no interpreter loop overhead line, no instruction
decode. The tree walk itself is not the problem; **allocation and value churn
are**, and they would still be there in a bytecode VM built on the same value
model.

That is why the optimisation work in this book goes after allocation — the
per-call `ValueList`, the per-operation `Value` box, the per-copy string — and
not after the dispatch mechanism. The one place the dispatch mechanism itself
was worth attacking is Chapter 19, and even there the win came from removing
copies rather than from removing branches.
