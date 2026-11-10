# Calls and Parameter Binding

Calling something is the most frequent non-trivial act in a Raku program, and
in a tree-walker it is also one of the most expensive: the floor for a trivial
call was around forty-six nanoseconds when this chapter was first measured, and
only about a quarter of that was dispatch. Where the other three quarters went
is what shaped the optimizer of Chapter 27 — and, later, what got most of the
same win back inside the interpreter. Both are easier to see once the mechanism
is laid out.

A call happens in three phases: build the argument list, activate the callee,
bind the parameters. Each has a fast path, and each fast path exists because
something measurable was in the way.

## Building the argument list

`evalArgs` evaluates the argument expressions into one flat `ValueList`:

```cpp
// src/InterpreterCore.cpp — evalArgs, abridged
} else if (a->kind == NK::Unary &&
           static_cast<Unary*>(a.get())->op == "|") {
    Value v = eval(static_cast<Unary*>(a.get())->operand.get());
    forceLazy(v);
    …
    spreadSlipArg(*this, args, v);   // |@a positionally, |%h as nameds
} else {
    Value v = eval(a.get());
    …
    if (v.t == VT::Pair && syntacticNamedArg(a.get(), v))
        v.namedArg = true;
    args.push_back(std::move(v));
}
```

Two things to notice.

**An argument list is just a `ValueList`.** Named arguments are ordinary `Pair`
values carrying a `namedArg` flag; the positional/named split happens later, at
bind time. That is why spreading (`|@a`), slurping (`*@rest`) and flattening are
all list operations rather than a separate protocol.

**And that list is the most frequent one in the tree**, which is why it stopped
being a `std::vector`. One is built, filled, passed and destroyed on every
interpreted call, and for the one- or two-element shape almost the whole of its
cost was the heap block: 32 nanoseconds against a call that takes about 265.
`ValueList` now takes small blocks off a thread-local free list — allocation is
a pop, release is a push — and that shape costs 9.5 ns. See *The list
container* in Chapter 12 for the container itself, including the reason the
free list is kept per exact capacity rather than rounding every small request
up to one size class: an argument list and a million-element array want
opposite things from that policy, and they are the same type.

**Only a *syntactic* pair is a named argument.** `f(k => 1)` and `f(:k(1))` pass
a named argument; `f($pair)` and `f(3 => 4)` pass a positional one, even though
all four produce a `Pair`. The test is on the *expression* kind, and it also
excludes a quoted key — `f('a' => 1)` is positional, per Raku's rule. Getting
this wrong makes every module that passes pairs around behave subtly
differently, so the check is deliberately narrow.

## Activating the callee

```cpp
// src/Interpreter.h
Value callCallable(const Value& codeVal, ValueList args,
                   const std::vector<ExprPtr>* rwArgs = nullptr,
                   bool ownFrame = false, bool arityCheck = false,
                   bool whereVerified = false);
Value callCallableRaw(const Value& codeVal, ValueList args,
                      const std::vector<ExprPtr>* rwArgs,
                      bool ownFrame = false, bool arityCheck = false,
                      bool whereVerified = false);
```

`callCallable` is a thin **wrapper layer**: if the routine has been
`&r.wrap({…})`'d, it runs the wrapper stack, each level able to `callsame` into
the next; otherwise it passes straight through:

```cpp
// src/InterpreterCore.cpp — callCallable, abridged
if (codeVal.t == VT::Code && codeVal.code() &&
    !codeVal.code()->cold().wrappers.empty()) { /* run the stack */ }
return dropNativeTag(callCallableRaw(codeVal, std::move(args), …));
```

`callCallableRaw` handles the special callables first — a native FFI sub, a
`Format`, junction autothreading, a multi-dispatcher, a builtin — and then
activates a user routine:

```cpp
// src/InterpreterCore.cpp — callCallableRaw, abridged
PooledFrame frame_{tcx};                    // a per-call frame, reused
auto& env = frame_.env;
env->parent = stateFrameOf(c);  // frame → state env → closure → … → global
…
tcx.dynStack.push_back(saved ? saved.get() : global_.get());
                                            // …the caller: the OTHER chain
```

`stateFrameOf` answers the routine's `state` scope, made on its first call. A
body that a scan shows can reach no `state` variable has none, and the frame
hangs from the closure directly.

The frame is *pooled*, not freshly allocated, and the reason is one of the
sharper measurements in this book. A call frame is a `shared_ptr<Env>`: a
control block, an `Env`, and the hash buckets inside it, allocated and destroyed
per call. A thread-local pool of up to thirty-two hands one back instead,
clearing the map but keeping its bucket array — the same trick the pad vector
plays with its capacity. A frame that anything captured (a closure, an `rwLink`,
a state chain) fails a `use_count` test on release and is simply dropped rather
than reused, which is what makes the reuse safe.

For a while only `callCallableRaw` had it, and `invokeMethod` did not. That
asymmetry alone was the difference between a sub call at about twice Rakudo's
cost and a method call at 5.8 times it.

Two facts are established here and everything else depends on them.

**The callee's parent is its lexical closure, not its caller.** Free variables
resolve through the scope the routine was *written* in. The caller's scope goes
on `dynStack`, used only for `$*foo`.

**Each activation bumps `frameTop`**, and a routine — as opposed to a bare block
— records its own frame number as `curRoutineFrame`. Those two counters drive
the cooperative control flow in the next chapter.

### Two shorter roads

Most of `callCallableRaw` exists for what a simple sub does not have, and two
tests at its entry take such a sub elsewhere.

A **plain sub** is a routine, not a block, method or multi, whose signature
is plain `$x` positionals with no default, trait or `where`, typed at most
`Int`, `Num`, `Str` or a native `int` or `num`, and whose body has no phaser,
`CATCH`, `CONTROL`, nested named sub or hoisted `my`. That is decided once,
after the first call, and published on the routine as `plainShape`. Called
with exactly its arity and no named, Junction, `Mu` or `Buf` argument, it runs
in `callPlainSub`: the pooled frame, the parameters moved straight into pad
slots, the call bookkeeping, the statement loop and the exits, and nothing
else. When it landed, `fib` ran 22.6% faster
(`docs/dev/plans/INTERP-SPEED-PLAN.md`).

A sub whose body is closed integer arithmetic — its own `$` parameters,
integer literals, the integer operators and comparisons, `if`, `return`, and
calls to subs that qualify themselves — is tried before that as an **integer
kernel** (`tryIntKernel`, `src/IntKernel.cpp`). The kernel computes on
`int64_t` values, not on `Value`s. Its arguments must be
plain Ints, and a result that would need a BigInt, a zero divisor or the
recursion limit makes it bail, after which the call runs the ordinary way from
the start. It is the sub-shaped twin of the loop kernel in Chapter 13, and
`RAKUPP_NO_KERNELS=1` turns both off.

### The call registers

A handful of one-shot parameters are passed to the next activation without
widening every signature:

```cpp
// src/Interpreter.h — ExecContext
Value* topicWriteback = nullptr;
const std::vector<Value*>* pendingRwSlots = nullptr;
const CallArgWriter* pendingArgWriter = nullptr;
bool noAutothread = false;
bool forceRoutineFrame = false;
int loopPhaserCtl = 0;
```

Each is set immediately before a call and consumed at the top of
`callCallableRaw`. `topicWriteback` is how `@a.grep({ $_++; True })` writes
into `@a`'s element: the driver points it at the element, and the mutated
implicit `$_` is copied back after the call.

They are per thread, and the comment in the header explains why in one
sentence: as plain members of the interpreter they were written by *every*
call on *every* thread, and they were ThreadSanitizer's top report — 2,761
lines on a program with no sharing in it at all. The set-then-consume window
is contiguous within one thread, so per-thread is not a workaround, it is
exactly their semantics. They were `static thread_local` variables first, which
cost every call a thread-local lookup apiece; as members of the context the
call path already holds they cost none. One relative, `builtinTopicWB_`, is
still a `static thread_local`: it re-exposes the consumed `topicWriteback` to a
builtin callable, which has no scope for the `$_` copy-back.

## Binding parameters

`bindParams` maps the argument list onto the signature. There is a fast path
and a general path.

```cpp
// src/InterpreterCore.cpp — bindParams, abridged
if (simple) {                       // all plain positional $ params, no nameds
    for (size_t i = 0; i < params.size(); i++) {
        if (i < args.size()) {
            Value v = args[i];
            if (!params[i].type.empty() || isMuTypeObject(v) || …)
                typeCheckBind(params[i], v, blockParams, whereVerified,
                              env.get());
            if (!params[i].isCopy)
                v.readonly = !params[i].isRw && !params[i].isRaw;
            …
            /* into the parameter's pad slot, or: */
            e->define(params[i].name, std::move(v));
        } else { /* the type's default for an unpassed parameter */ }
    }
    return;
}
for (auto& a : args) {              // general: split named from positional
    if (isNamedArg(a))
        named[a.s.copyText()] = a.pairVal() ? *a.pairVal() : Value::any();
    else positional.push_back(a);
}
```

Whether a signature qualifies for the fast path is a static property of the
signature, so it is decided once and stored on the *first* parameter:

```cpp
// src/Ast.h — Param
mutable DecidedOnce<signed char> sigSimple{-1};   // only element [0] is read
```

The general path covers everything else:

- **positional parameters with defaults**, evaluated in the parameter scope so
  `sub f($g, $a = $g/2)` can see earlier parameters;
- **readonly, `is rw`, `is copy`, `is raw`** — a plain `$` parameter is marked
  readonly unless it is one of those or the invocant:

  ```cpp
  if (p.sigil == '$' && !p.isRw && !p.isCopy && !p.isRaw && !p.invocant)
      v.readonly = true;
  ```

- **slurpies**: `*@rest` flattens, `**@rest` does not, `+@rest` applies the
  single-argument rule, `*%named` collects the rest;
- **named parameters**, including the alias forms `:a(:$b)` and nested
  `:x(:y(:z($a)))`, where every layer's key answers;
- **sub-signature destructuring**, `sub f([$a, $b])`, recursing through
  `Param::subSig`;
- the implicit **`$_`** topic, and placeholder parameters `$^a`, `$^b`, bound in
  sorted order.

### What is checked here

When this chapter was first written, a single-dispatch call was largely
duck-typed at the bind boundary: type constraints, `where` clauses and the
`:D`/`:U` smileys were enforced only by `scoreCandidate`, which is multi
dispatch's business (Chapter 16). That is no longer so. Both paths above call
`typeCheckBind` for every parameter that has something to check, and a
mismatch raises what Rakudo raises:

```
$ rakupp -e 'sub f($x where * > 3) { say $x }; f(1)'
Constraint type check failed in binding to parameter '$x'; expected
anonymous constraint to be met but got Int (1)
  (X::TypeCheck::Binding::Parameter)
$ rakupp -e 'sub f(Int:D $x) { say $x }; f(Int)'
Parameter '$x' must be an object instance of type 'Int', not a type object
  (X::Parameter::InvalidConcreteness)
```

A call that can be proved wrong without running it does not get that far.
`findDoomedCalls` (`src/CallCheck.cpp`) reads the whole unit before it runs
and refuses a call to a sub the unit declares whose arguments, by their
literal or declared types, can never bind. It is Rakudo's compile-time
check, and it reports nothing wherever it cannot see enough:

```
$ rakupp -e 'sub f(Int $x) { say $x }; f("a")'
===SORRY!=== Error while compiling -e
Calling f(Str) will never work with declared signature (Int $x)
```

`typeCheckBind` caches on the parameter whether its type names something
enforceable — but **only once true**:

```cpp
// src/Ast.h — Param
mutable DecidedOnce<signed char> typeKnown{0};
```

The asymmetry is important and the header says why: a type name that is
unresolvable now can become resolvable later, when a class further down the file
is declared or a module is loaded. Caching the *positive* answer is safe;
caching the negative one is a bug that would only show up in a program whose
declarations are ordered a particular way.

## The `Callable`, and what it caches

```cpp
// src/Value.h — abridged
struct CodeInfo : RefCounted {         // the code: shared by its closures
    std::string pkg, name;
    const std::vector<Param>* params;      // borrowed from the AST
    const std::vector<StmtPtr>* body;      // borrowed from the AST
    BuiltinFn builtin;                     // set ⇒ this is a builtin
    mutable DecidedOnce<signed char> hoistNeed{-1};
    mutable PublishedOnce<signed char> plainShape{-1};
    mutable KernelSlot intKernel;
    mutable PublishedOnce<signed char> arityShape{-1};
    mutable DecidedOnce<int> arityMaxPos{0}, arityReqPos{0};
    mutable DecidedOnce<bool> arityUnbounded{false};
    mutable std::shared_ptr<const PadLayout> padLayout;
    mutable PublishedOnce<signed char> catchScan{-1};
    mutable DecidedOnce<Stmt*> catchBlkCache{nullptr};
    …
};
struct CallableCold {                  // what most routines never set
    ValueList candidates;                  // multi-dispatch candidates
    std::string retType;
    ValueList wrappers;                    // .wrap stack, outermost last
    …
};
struct Callable {                      // the routine as a value
    CodeInfoPtr info;                      // ci() reads it, ciM() writes
    std::shared_ptr<Env> closure;
    StateSlot state;                       // { shared_ptr<Env> env;
                                           //   once_flag init; … }
    …
};
```

A routine is three structs since the chapter was first written. `CodeInfo` is
the code apart from the closure it was made in: the signature and body borrowed
from the AST, what was declared about the routine, and the caches decided at
its first call. Every closure made from the same code shares one, copy on
write: `ci()` reads it, and `ciM()` gives a `Callable` its own before a write.
`CallableCold` holds the fields most routines never set — the multi
candidates, the wrappers, the native-call state, a declared return type — behind
one pointer. What is left in `Callable` belongs to the one object: the scope it
closed over, its `state` storage, a dispatcher's cache.

The `state` env and its `once_flag` are wrapped in a `StateSlot` for a reason
worth knowing: `std::once_flag` is not copyable, so a bare pair of members would
make `Callable` uncopyable. The holder's copy constructor is empty, which also
gets the semantics right — a cloned routine starts with fresh `state` slots
rather than sharing the origin's, as it does in Rakudo.

The decided-once fields of `CodeInfo` are all static properties of the
routine's AST that `callCallableRaw` used to recompute **on every call**:
whether anything needs hoisting, whether an arity pre-check applies and what
its bounds are, whether the body contains an inline `CATCH` block, and — added
since — the pad layout and whether the lean path or a kernel takes the routine.
Each is cheap once and worthless repeated — a parse that calls a routine 73,603
times paid for them 73,603 times. They fill in place on the shared copy, which
is sound because they depend only on what the sharers hold identically.

`state.env` is created exactly once, under that `once_flag`, by the first call
of a routine that can reach a `state` variable, and chained between the
per-call frame and the closure. That is the whole implementation of
`state`: the variable lives in a scope that is created once per routine rather
than once per call.

## `is rw` write-back

Because arguments are `Value` copies, a mutated `is rw` parameter must be
written back. The call site passes the argument *expressions* alongside the
values, and `setupRwLinks` records, per such parameter, the expression and the
caller's scope. An assignment *inside* the callee then pushes through
immediately, by re-resolving the expression, so the caller sees the change
mid-call. `copyOutRw` is the backstop on a normal return, for a mutation that
bypassed the assignment hook — `s///`, `.=`:

```cpp
// src/InterpreterBinding.cpp — copyOutRw, abridged
if ((p.isRw || p.isRaw || p.sigil == '\\' || hasSynced(p.name)) && ae &&
    !env->xr().rwCelled.count(p.name)) {
    Value* pv = env->local(p.name);
    if (pv) {
        Value cur = paramValue(pv);
        if (!syncedUnchanged(p.name, cur))
            …
            if (Value* lv = lvalue(peelIncDec(ae))) storeInto(lv, cur);
    }
}
```

A parameter whose value still matches the last one pushed is skipped: copying
it back late would put a stale value over the callee's own edits. A parameter
in `rwCelled` was bound to the caller's container itself and needs neither
mechanism. The bookkeeping that keeps the two from fighting is described in
Chapter 12.

The whole thing is guarded by a sticky flag, `anyRwLinks_`, so a program that
never uses `is rw` never runs the per-assignment hook at all.

## Lvalue-mode method calls

`$obj[$i] = $v` on a class whose `AT-POS` is `return-rw @!arr[$i]` must write
the *real* element, not a returned copy. That needs a channel from the subscript
site into the routine's `return-rw`:

```cpp
// src/Interpreter.h — ExecContext
int wantLvalue = 0;      // 0 off, else the callFrames depth being served
Value* lvalueOut = nullptr;
```

The subscript-lvalue path sets `wantLvalue` to the current frame depth plus one
before invoking `AT-POS`; a `return-rw` executing at exactly that depth fills
`lvalueOut` with the address of its operand. The pointer survives the frame
because its target lives in the object's shared containers.

Matching on depth rather than on a plain flag is what keeps an inner call from
accidentally answering an outer call's request.

## What a call costs

Measured against the runtime library, 2 million iterations, minimum of six:

| Shape | ns/call |
|---|---:|
| direct C++ call, user-sub shape | 46.3 |
| cached `BuiltinFn*` call | 47.4 |
| by-name lookup: hash, `unordered_map::find`, `std::function` | 55.0 |

The lookup tax is real but modest at 8 to 9 nanoseconds. The important number is
the **floor**: about 46 nanoseconds for a *trivial* call, nearly all of it the
`ValueList` — at the time, a heap-allocating `std::vector` built per call.

Dispatch was a quarter of the overhead. The argument list was the rest. That
finding shaped two different pieces of work, years apart in the book's ordering
and months apart in fact.

The first is the optimiser of Chapter 27: its opening pass gives fixed-arity
subs direct `Value` parameters and removes the list entirely, which buys more
than any lookup cache can. That only ever helped compiled code.

The second is the free list above, which attacked the same allocation from
inside the interpreter and took most of it: the one-argument shape from 32.35
to 9.48 ns, `fib` 8.8% fewer instructions, a two-argument call loop 13%. The
lesson is not that the optimiser was unnecessary — it removes the list rather
than making it cheap, and it is still the larger win where it applies. It is
that a cost identified once can be worth attacking twice, from different
sides, and that "the allocation is the cost" survived being true for long
enough to be acted on in two entirely different places.
