# Calls and Parameter Binding

A call happens in three phases: build the argument list, activate the callee,
bind the parameters. Each has a fast path, and each fast path exists because
something measurable was in the way.

## Building the argument list

`evalArgs` evaluates the argument expressions into one flat `ValueList`:

```cpp
// src/Interpreter.cpp — evalArgs, abridged
} else if (a->kind == NK::Unary && ((Unary*)a.get())->op == "|") {
    Value v = eval(…);                                  // a Slip: |@a / |%h
    if (v.t == VT::Array || v.t == VT::Range)
        for (auto& x : v.flatten()) args.push_back(x);
    else if (v.t == VT::Hash && v.hash)
        for (auto& kv : *v.hash) {                      // |%h → named args
            Value p = Value::pair(kv.first, kv.second);
            p.namedArg = true; args.push_back(p);
        }
} else {
    Value v = eval(a.get());
    if (v.t == VT::Pair && a->kind == NK::Pair &&
        !((PairExpr*)a.get())->quotedKey && ident)
        v.namedArg = true;
    args.push_back(std::move(v));
}
```

Two things to notice.

**An argument list is just a `ValueList`.** Named arguments are ordinary `Pair`
values carrying a `namedArg` flag; the positional/named split happens later, at
bind time. That is why spreading (`|@a`), slurping (`*@rest`) and flattening are
all vector operations rather than a separate protocol.

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
                   bool ownFrame = false, bool arityCheck = false);
Value callCallableRaw(const Value& codeVal, ValueList args,
                      const std::vector<ExprPtr>* rwArgs,
                      bool ownFrame = false, bool arityCheck = false);
```

`callCallable` is a thin **wrapper layer**: if the routine has been
`&r.wrap({…})`'d, it runs the wrapper stack, each level able to `callsame` into
the next; otherwise it passes straight through:

```cpp
// src/Interpreter.cpp — callCallable
if (codeVal.code && !codeVal.code->wrappers.empty()) { /* run the stack */ }
return callCallableRaw(codeVal, std::move(args), rwArgs);
```

`callCallableRaw` handles the special callables first — a native FFI sub, a
`Format`, junction autothreading, a multi-dispatcher, a builtin — and then
activates a user routine:

```cpp
auto env = std::make_shared<Env>();                    // fresh per-call frame
c.stateEnv->parent = c.closure ? c.closure : global_;  // once
env->parent = c.stateEnv;         // frame → stateEnv → closure → … → global
tctx_.dynStack.push_back(caller_scope);                // the OTHER chain
```

Two facts are established here and everything else depends on them.

**The callee's parent is its lexical closure, not its caller.** Free variables
resolve through the scope the routine was *written* in. The caller's scope goes
on `dynStack`, used only for `$*foo`.

**Each activation bumps `frameTop`**, and a routine — as opposed to a bare block
— records its own frame number as `curRoutineFrame`. Those two counters drive
the cooperative control flow in the next chapter.

### The call registers

A handful of one-shot parameters are passed to the next activation without
widening every signature:

```cpp
// src/Interpreter.h
static thread_local Value* topicWriteback_;
static thread_local Value* builtinTopicWB_;
static thread_local bool noAutothread_;
static thread_local int loopPhaserCtl_;
static thread_local const std::vector<Value*>* pendingRwSlots_;
```

Each is set immediately before a call and consumed at the top of
`callCallableRaw`. `topicWriteback_` is how `@a.grep({ $_++; True })` writes
into `@a`'s element: the driver points it at the element, and the mutated
implicit `$_` is copied back after the call.

They are `static thread_local` and the comment in the header explains why in
one sentence: as plain members they were written by *every* call on *every*
thread, and they were ThreadSanitizer's top report — 2,761 lines on a program
with no sharing in it at all. The set-then-consume window is contiguous within
one thread, so thread-local is not a workaround, it is exactly their semantics.

## Binding parameters

`bindParams` maps the argument list onto the signature. There is a fast path
and a general path.

```cpp
// src/Interpreter.cpp — bindParams
if (simple) {                       // all plain positional $ params, no nameds
    for (size_t i = 0; i < params.size(); i++) {
        Value v = i < args.size() ? args[i]
                                  : typedDefault(params[i].type, '$');
        v.readonly = true;
        env->define(params[i].name, std::move(v));
    }
    return;
}
for (auto& a : args)                // general: split named from positional
    if (isNamedArg(a)) named[a.s] = a.pairVal ? *a.pairVal : Value::any();
    else positional.push_back(a);
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
  if (p.sigil == '$' && !p.isRw && !p.isCopy && !p.invocant)
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

### What is *not* checked here

**Type constraints, `where` clauses and `:D`/`:U` smileys are not enforced for
an ordinary, non-multi call.** Single dispatch is largely duck-typed at the bind
boundary. Those checks live in `scoreCandidate`, which is multi dispatch's
business (Chapter 16).

There is one exception, `typeCheckBind`, used when a lone candidate is being
bound and a mismatch should raise `X::TypeCheck::Binding`. It caches its
verdict on the parameter — but **only once true**:

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
// src/Value.h — Callable, abridged
std::string pkg, name;
const std::vector<Param>* params;      // borrowed from the AST
const std::vector<StmtPtr>* body;      // borrowed from the AST
std::shared_ptr<Env> closure;
std::shared_ptr<Env> stateEnv;         // persistent `state` storage
std::once_flag stateInit;
BuiltinFn builtin;                     // set ⇒ this is a builtin
std::vector<Value> candidates;         // multi-dispatch candidates
std::vector<Value> wrappers;           // .wrap stack, outermost last
DecidedOnce<signed char> hoistNeed{-1};
DecidedOnce<signed char> arityShape{-1};
int arityMaxPos = 0, arityReqPos = 0; bool arityUnbounded = false;
DecidedOnce<signed char> catchScan{-1};
Stmt* catchBlkCache = nullptr;
```

The four decided-once fields are all static properties of the routine's AST
that `callCallableRaw` used to recompute **on every call**: whether anything
needs hoisting, whether an arity pre-check applies and what its bounds are, and
whether the body contains an inline `CATCH` block. Each is cheap once and
worthless repeated — a parse that calls a routine 73,603 times paid for them
73,603 times.

`stateEnv` is created exactly once, under a `std::once_flag`, and chained
between the per-call frame and the closure. That is the whole implementation of
`state`: the variable lives in a scope that is created once per routine rather
than once per call.

## `is rw` write-back

Because arguments are `Value` copies, a mutated `is rw` parameter must be
written back. The call site passes the argument *expressions* alongside the
values, and on a normal return each such parameter's final value is written back
by re-resolving its expression:

```cpp
// src/Interpreter.cpp — copyOutRw
if ((p.isRw || p.sigil == '\\') && pi < rwArgs->size())
    if (Value* lv = lvalue((*rwArgs)[pi].get()))
        *lv = env->vars[p.name];
```

`setupRwLinks` additionally arranges for an assignment *inside* the callee to
push through immediately, so the caller sees the change mid-call. The
bookkeeping that keeps those two mechanisms from fighting is described in
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
`ValueList` — a heap-allocating `std::vector` built per call.

Dispatch was a quarter of the overhead. The argument vector was the rest. That
finding is what shaped the optimiser in Chapter 27: its first pass gives
fixed-arity subs direct `Value` parameters and removes the vector entirely,
which buys more than any lookup cache can.
