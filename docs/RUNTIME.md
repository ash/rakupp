# The Runtime Model — how static C++ runs dynamic Raku

Raku is a dynamically-typed language: a variable can hold an `Int` now and a
`Hash` later, functions dispatch on the runtime types of their arguments, lists
can be infinite, and objects can grow roles at run time. Raku++ is written in
statically-typed C++17 with no garbage collector. This document explains how the
second runs the first — what a value *is* at runtime, how variables and
containers relate, how calls and dispatch work, and how lazy/infinite sequences
are represented.

It is the companion to [ARCHITECTURE.md](ARCHITECTURE.md), which covers the
pipeline (lexer → parser → AST) and the four execution modes. Here we stay inside
the runtime library (`librakupp_rt`) and the tree-walking interpreter. Native
`--exe` code reuses the exact same runtime — see the [last section](#one-runtime-two-front-ends).

The load-bearing mechanisms are shown inline as short code excerpts, each tagged
with the source **file** it comes from (`// src/Value.cpp`). The excerpts are
lightly trimmed for the page — `…`, comments, and elided error text mark where —
but are otherwise verbatim. File names, not line numbers, are the anchors: grep
for the function or the quoted code to find the current source, which moves as the
code changes.

## Contents

- [The one big idea](#the-one-big-idea)
- [`Value` — the fat tagged struct](#value--the-fat-tagged-struct)
- [`ValueList` — the universal currency](#valuelist--the-universal-currency)
- [Variables, `Env`, and scope](#variables-env-and-scope)
- [Containers vs. values: copy semantics](#containers-vs-values-copy-semantics)
- [Binding (`:=`) vs. assignment (`=`)](#binding--vs-assignment-)
- [The scalar-container metaphor](#the-scalar-container-metaphor)
- [Function and method calls](#function-and-method-calls)
- [Cooperative `return` (and `next`/`last`/`redo`)](#cooperative-return-and-nextlastredo)
- [Multiple dispatch](#multiple-dispatch)
- [The object model](#the-object-model)
- [How built-in types get methods](#how-built-in-types-get-methods)
- [Closures](#closures)
- [Junctions](#junctions)
- [Lazy and infinite sequences](#lazy-and-infinite-sequences)
- [`but` / `does` mixins](#but--does-mixins)
- [One runtime, two front ends](#one-runtime-two-front-ends)
- [Honest limitations](#honest-limitations)

## The one big idea

Two decisions carry most of the design:

1. **Every Raku value is the same C++ type.** There is a single struct,
   [`Value`](../src/Value.h), that can represent an `Int`, a
   `Str`, an `Array`, a `Hash`, a `Code`, a type object, an `Object`, a
   `Junction`, a lazy `Seq` — anything. A one-byte enum tag says which. This is
   what lets a statically-typed container (`std::unordered_map<std::string,
   Value>`) hold a dynamically-typed variable.

2. **Execution is a tree walk.** The interpreter recursively evaluates AST
   nodes; `Interpreter::eval(Expr*)` returns a `Value`, `Interpreter::exec(Stmt*)`
   runs a statement. There is no bytecode and no separate value stack — the C++
   call stack *is* the Raku call stack, and a `Value` returned from `eval` *is*
   the Raku expression's result.

Everything else is a consequence of those two, plus a handful of optimizations to
keep the tree-walker from being slow (the `-fexceptions`-free
[cooperative return](#cooperative-return-and-nextlastredo), the native-int
[fast paths](OPTIMIZATION.md), in-place string append).

## `Value` — the fat tagged struct

A dynamic language needs a uniform value representation. The three classic
choices are a tagged union, a `std::variant`, or a class hierarchy with virtual
dispatch. Raku++ uses **none** of them directly. It uses a *fat struct*: one
plain struct that carries a type tag plus a field for every kind of payload,
with the heavy payloads behind `shared_ptr`.

```cpp
enum class VT { Nil, Any, Bool, Int, Num, Str, Array, Hash, Code, Range,
                Pair, Type, Whatever, Object, Rat, Regex, Match, Complex };

struct Value {
    VT t = VT::Any;              // the discriminator — which fields are live
    bool b;  long long i;  double n, im;   // Bool / Int / Num / (Complex imag)
    std::string s;               // Str; also the type name for Type, key for Pair
    // ... flags: isList, itemized, readonly, namedArg, fatRat, ...
    std::shared_ptr<ValueList> arr;              // Array / List / Seq elements
    std::shared_ptr<std::map<std::string, Value>> hash;   // Hash entries
    std::shared_ptr<Callable> code;              // Code
    std::shared_ptr<ObjectData> obj;             // Object (user class instance)
    std::shared_ptr<BigInt> big, ratN, ratD;     // bignum Int / Rat numerator+denominator
    std::shared_ptr<void> ext;                   // opaque: Promise/Channel/lazy-seq state
    // ... Range bounds, enum name, element type, native-int width, ...
};
```

(Full definition: `src/Value.h`.) The `VT t` tag is the discriminator;
code reads the fields that tag makes live. Small scalars (`Bool`, `Int`, `Num`,
`Range` bounds, `Complex`) live inline in the struct; anything with sharable or
unbounded storage (`Array`, `Hash`, `Code`, `Object`, bignums) lives behind a
`shared_ptr`.

**Why a fat struct instead of a `union`, a `variant`, or a class hierarchy?**

The instinct is "a value is one of N types, so use a union." But a union models
*mutual exclusivity* — one active member selected by the tag — and a `Value` is
frequently **several fields at once**, not one active member:

- A **`Match`** sets the subject `s`, the span `rFrom`/`rTo`, the positional
  captures in `arr`, *and* the named captures in `hash` — all live together
  (`src/Value.h`). A **`Pair`** is the key `s` *plus* `pairVal`. An **enum**
  value is the ordinal `i` *plus* `enumName`/`enumType`. A **`Rat`** is `ratN`
  *plus* `ratD` *plus* `fatRat`. And cross-cutting flags — `isList`, `itemized`,
  `readonly`, `namedArg`, `ofType`, `natBits` — are set *regardless* of the tag
  (a `readonly` Int, an `itemized` Array). So `VT t` isn't "which one field is
  valid"; it's "how to read a bag of fields, several of them set." That is a
  struct, not a sum type.
- The heavy members are **non-trivial C++ types** (a `std::string`, nine
  `shared_ptr`s). A raw `union` of those is undefined behavior without
  hand-written placement-`new` and a tag-switched copy ctor / move ctor / both
  assignments / destructor — at which point you've rebuilt `std::variant`, which
  re-imposes the one-active-member model *and* `std::get`/`std::visit` access
  that the ~16k lines of `Interpreter`/`Builtins` (which just poke `v.i`,
  `v.s`, `v.arr` directly) would find painful.
- The memory a union would save is smaller than it looks: the overlap-able
  members are mostly `shared_ptr` — **null, and thus allocation-free, when
  unused** — and, per the first point, often can't be overlapped at all.

So the fat struct is the natural fit, and it brings three concrete wins:

- **No virtual dispatch, no heap allocation for scalars.** An `Int` is just a
  `Value` with `t == VT::Int` and `i` set — it fits in the struct, copies by
  memcpy-ish value semantics, and needs no allocation. Constructing one is
  `Value::integer(42)` (`src/Value.h`), a stack value.
- **Uniform copy/move.** Passing a `Value` by value, returning it from `eval`,
  storing it in a `vector` — all use the compiler-generated copy/move. The
  `shared_ptr` members make copies of `Array`/`Hash`/`Object` cheap (a refcount
  bump) and give them **shared identity**, which is exactly what Raku's container
  semantics need (below).
- **Type coercion is a method, not a cast.** Each coercion is a member function
  that `switch`es on `t` and does the Raku conversion:

  ```cpp
  // src/Value.h — declared on the struct; each switches on `t` in Value.cpp
  bool truthy() const;   long long toInt() const;   double toNum() const;
  std::string toStr() const;   std::string gist() const;   std::string typeName() const;
  ```

  `~$x` calls `toStr`, `+$x` calls `toNum`, boolean context calls `truthy`. There
  is no C++ inheritance making `Int` "be a" `Cool`; these functions encode the
  numeric/string tower directly.

The cost is memory: every `Value` carries all the fields even when only one is
live (a `Str` still has an unused `arr` pointer, `i`, `n`, …). For a tree-walker
whose values are transient this is an accepted trade — it buys branch-free field
access and trivial copyability. See [Honest limitations](#honest-limitations).

A few tag choices are worth noting because they reuse fields cleverly:

- **A `Rat`** stores its normalized numerator and denominator as two `BigInt`s
  (`ratN`, `ratD`). Exact rational arithmetic stays exact until the denominator
  would exceed 64 bits, where a plain `Rat` spills to `Num` (Raku's `Rat`→`Num`
  rule). A **`FatRat`** is the *same* storage with the `fatRat` flag set
  (`src/Value.h`): it carries the `FatRat` type identity — contagious through
  arithmetic, so any `FatRat` operand makes the result a `FatRat` — and is
  **exempt from the spill**, staying an arbitrary-precision rational forever
  (`src/Interpreter.cpp`).
- **An `Int`** is a `long long i` until it overflows, then it grows a
  `shared_ptr<BigInt> big`; `Value::bigint` picks inline vs. heap automatically
  (`src/Value.h`).
- **An enum value** is a `VT::Int` carrying its integer plus `enumName`/`enumType`
  strings (`src/Value.h`), so `Less`/`Same`/`More` compare as `-1/0/1` yet
  stringify as their name.
- **A `Junction`** is a `VT::Array` tagged by `enumName ∈ {any,all,one,none}` —
  no dedicated `VT`. See [Junctions](#junctions).

The constructors show the pattern — a `VT` tag plus whichever fields that kind
needs, several at once:

```cpp
// src/Value.h — factory constructors (each sets a VT tag + the live fields)
static Value bigint(const BigInt& b) {                          // inline i, or heap `big` on overflow
    Value v; v.t = VT::Int;
    if (b.fitsLL()) v.i = b.toLL(); else v.big = std::make_shared<BigInt>(b);
    return v;
}
static Value enumVal(const std::string& name, long long val) {  // Int ordinal + the enum KEY
    Value v; v.t = VT::Int; v.i = val; v.enumName = name; return v;
}
static Value matchVal(std::string text, long from, long to) {   // subject + span + BOTH capture stores
    Value v; v.t = VT::Match; v.s = text; v.rFrom = from; v.rTo = to;
    v.arr = std::make_shared<ValueList>();                       // positional captures ($0, $1, …)
    v.hash = std::make_shared<std::map<std::string, Value>>();   // named captures ($<name>)
    return v;
}
```

And the FatRat spill exemption, in `applyArith`:

```cpp
// src/Interpreter.cpp — a FatRat operand makes the result a FatRat, exempt from the Num spill
bool fat = (l.t == VT::Rat && l.fatRat) || (r.t == VT::Rat && r.fatRat);
Value v = Value::rat(std::move(n), std::move(d)); v.fatRat = fat;
if (!fat && v.ratD && !v.ratD->fitsU64()) return Value::number(v.toNum());  // plain Rat: spill
return v;
```

## `ValueList` — the universal currency

```cpp
using ValueList = std::vector<Value>;              // src/Value.h
using BuiltinFn = std::function<Value(Interpreter&, ValueList&)>;
```

A `ValueList` is just a vector of values, and it is the single currency for
"more than one value" everywhere in the runtime:

- **Array/List/Seq storage** is a `shared_ptr<ValueList>` (`Value::arr`).
- **Call arguments** are a `ValueList` — `callCallable(code, args)` and every
  builtin takes `ValueList& args`.
- **Return of multiple values** (a list literal, `@a`, a `Seq`) is a `Value` of
  kind `Array` wrapping a `ValueList`.

Because arguments and list elements are the same type, spreading (`|@a`),
slurping (`*@rest`), and flattening are all just vector operations. The
distinction between a *List* (parenthesized, flattening) and an *Array* is a flag
(`isList`, `src/Value.h`), not a different container.

## Variables, `Env`, and scope

A lexical scope is an `Env`: a hash map from sigil'd name to `Value`, plus a
pointer to the enclosing scope.

```cpp
struct Env {                                        // src/Interpreter.h
    std::unordered_map<std::string, Value> vars;    // "$x", "@a", "%h", "&sub"
    std::shared_ptr<Env> parent;
    // ... temp-restores, per-scope container defaults (varDefault) ...
    Value* find(const std::string& name) {          // walk the parent chain
        auto it = vars.find(name);
        if (it != vars.end()) return &it->second;
        return parent ? parent->find(name) : nullptr;
    }
};
```

The variable's **full sigil'd name is the key** — `"$x"`, `"@a"`, `"%h"`. Subs
live in the same map under a `&`-prefixed key, `"&foo"`, so they occupy a
separate namespace from `$foo`. Lexical scoping *is* the `parent`-pointer walk in
`find`; there is no separate symbol table.

Reading and writing a variable are two sides of the same slot. A read looks the
name up and **returns a copy of the `Value`**; a write goes through
`lvalue(Expr*)`, which hands back a `Value*` pointing straight into the owning
`Env`'s map, and assigns through it:

```cpp
// read  $x   — eval(VarExpr), the plain-lexical fast path
if (Value* p = tctx_.cur->find(ve->name))
    if (!(p->t == VT::Hash && p->hashKind == "Proxy")) return *p;   // a COPY of the slot

// my $x      — lvalue(): make the slot in the current scope, return a pointer to it
tctx_.cur->define(ve->name, std::move(init));
return &tctx_.cur->vars[ve->name];

// $x = 5     — evalAssign(): write through the lvalue pointer
*lv = rhs;
```

So `my` is a `define` in the current scope, a read is a `find` + copy, and a
write is a `find` + overwrite — lexical scoping is entirely the `parent`-chain
walk in `Env::find`. `state` is the one exception: its slot lives in the
`Callable`'s persistent `stateEnv` rather than the per-call frame, so it survives
across calls:

```cpp
// state $n — lvalue(): the slot lives in the once-created stateEnv
if (!tctx_.curStateEnv->vars.count(ve->name))
    tctx_.curStateEnv->define(ve->name, typedDefault(ve->declType, sigil));
return &tctx_.curStateEnv->vars[ve->name];
```

The three declarators differ only in *which* `Env` holds the slot:

| Declarator | Storage | Notes |
|---|---|---|
| `my` | the current lexical `Env` (`tctx_.cur`) | ordinary lexical |
| `our` | the package env (`curPkgEnv_`, ultimately `global_`) | on package-block exit, `our`-vars are also republished under a package-qualified name (`src/Interpreter.cpp`) |
| `state` | a per-`Callable` `stateEnv`, created **once** | `std::once_flag stateInit` (`src/Value.h`); persists across calls, initialized on first call only (`src/Interpreter.cpp`) |

`our` additionally republishes its variables under a package-qualified global
name when the package block closes (a `my` package var is skipped):

```cpp
// src/Interpreter.cpp — on package-block exit, publish `our` symbols globally
if (sigilVar && !ourVars.count(sym)) continue;            // a `my` package var — not published
qual = std::string(1, sym[0]) + tctx_.pkgPrefix + sym.substr(1);   // e.g. "$Foo::Bar::x"
global_->define(qual, kv.second);
```

The "current scope" pointer, the dynamic-variable (`$*foo`) caller chain, the
gather/supply collectors, and the call depth are the thread's **execution
registers** — they live in a per-thread `ExecContext` held in a `static
thread_local`, so each real worker thread has its own set:

```cpp
// src/Interpreter.h — the per-thread execution registers (excerpt)
struct ExecContext {
    std::shared_ptr<Env> cur;                             // current lexical scope
    std::vector<Env*> dynStack;                           // dynamic ($*foo) caller chain
    int callDepth = 0;
    std::vector<std::shared_ptr<ValueList>> gatherStack;  // active gather collectors
    bool returning = false;  Value returnV;               // cooperative return (below)
    uint64_t frameTop = 0, curRoutineFrame = 0;           // call-frame counters
    int loopCtl = 0;  uint64_t curLoopFrame = 0;          // cooperative next/last/redo
    // …
};
```

That is the foundation for the concurrency model described in
[ASYNC.md](ASYNC.md).

## Containers vs. values: copy semantics

This is the subtle part, and it falls straight out of the fat struct. Copying a
`Value` copies the inline scalar fields but only bumps the refcount on the
`shared_ptr` members. So a raw C++ copy of an `Array` value **shares** its
backing `ValueList`. Raku, however, wants:

```raku
my @a = 1, 2, 3;
my @b = @a;      # @b is a COPY — pushing to @b must not change @a
$b[0] = 99;
say @a;          # (1 2 3)   ← unchanged
```

So the interpreter **deliberately breaks the sharing** at `=`-assignment, and
only for the `@`/`%` sigils. Array assignment routes through `coerceArray`, which
allocates a fresh buffer:

```cpp
// coerceArray, src/Interpreter.cpp
if (v.t == VT::Array) {
    if (v.itemized) { ... }          // an itemized array is ONE element
    if (v.ext) return v;             // a lazy seq stays lazy (see below)
    Value r = Value::array(*v.arr);  // *v.arr copies the vector — fresh buffer
    r.isList = false; return r;
}
```

`*v.arr` dereferences the `shared_ptr` and copies the underlying `std::vector`,
so `@b` gets its own `ValueList`. Hashes copy the same way (`coerceHash`,
`src/Interpreter.cpp`, `*h.hash = *v.hash`). Scalar (`$`) assignment, by
contrast, is a plain struct overwrite (`*lv = rhs`) — so `my $x = @a` stores an
`Array` value that *does* still share `@a`'s buffer, because an item container is
a reference to one thing.

Two consequences to keep straight:

- **The copy is one level deep** (Rakudo's semantics). `my @b = @a` copies the
  top-level buffer, but a nested itemized array inside is copied as a `Value`
  struct — so its `arr` pointer is still shared. Inner containers are "shared by
  value" (`src/Interpreter.cpp`).
- **A lazy sequence is *not* copied** on assignment — `if (v.ext) return v`
  keeps it lazy so `my @a = 1, 2, 4 ... *` doesn't try to drain an infinite list.
  See [Lazy and infinite sequences](#lazy-and-infinite-sequences).

The native `--exe` backend has its own mirror of this, `rtArrayVal`
(`src/Interpreter.cpp`), with the same "fresh buffer" rule, so interpreted
and compiled code agree byte-for-byte.

## Binding (`:=`) vs. assignment (`=`)

`=` copies a value into an existing container. `:=` *rebinds the container
itself* — after `$y := $x`, the two names are the same container, and a write to
either is seen by both. But `Env` stores `Value`s by value, in a map, so two map
slots can't literally be the same storage. Raku++ fakes the alias with a
**`Proxy`**:

```cpp
// $y := $x  —  src/Interpreter.cpp  (scalar case)
Value proxy = Value::makeHash(); proxy.hashKind = "Proxy";
// FETCH reads owner->vars["$x"];  STORE writes owner->vars["$x"]
(*proxy.hash)["FETCH"] = fetch;    // a builtin Code closing over the owning Env
(*proxy.hash)["STORE"] = store;
*lvalue(target) = proxy;           // $y's slot now holds the Proxy
```

`$y`'s slot holds a `Proxy` (a `Hash` tagged `hashKind == "Proxy"`) whose `FETCH`
and `STORE` closures read and write `$x`'s slot in the `Env` that owns it. A read
of `$y` notices the `Proxy` tag and calls its `FETCH` instead of returning the
hash; a write routes through `STORE`:

```cpp
// src/Interpreter.cpp — reading a variable: a Proxy fetches rather than returning itself
Value* p = tctx_.cur->find(ve->name);
if (p) {
    if (p->t == VT::Hash && p->hashKind == "Proxy" && p->hash) {
        auto it = p->hash->find("FETCH");
        if (it != p->hash->end()) return callCallable(it->second, { *p });   // → $x's value
    }
    return *p;
}
```

Binding chains deref one extra `Proxy` level so `$z := $y := $x` works.

**Array binding is cheaper** — no proxy needed, because sharing an `Array`'s
buffer is exactly what a `shared_ptr` copy already does:

```cpp
// @a := @b  —  src/Interpreter.cpp
if (a->op == ":=" && rhs.t == VT::Array) { Value b = rhs; b.isList = false; *lv = b; }
```

`Value b = rhs` copies the struct but *shares* `rhs.arr`, so `@a` and `@b` point
at one `ValueList`. This is the deliberate opposite of `@a = @b` (fresh buffer).
`constant` reuses the bind path — a constant is `:=` in disguise.

## The scalar-container metaphor

Raku's `$`-variable is really a *Scalar container* holding a value, with
introspectable machinery (`.VAR`, `is default`, type constraints). Raku++ models
that without a separate container object, using flags on the `Value` plus
per-`Env` side tables:

- **`.VAR`** builds a `Hash` of kind `"Scalar"` reporting the variable's name,
  value, and default (`src/Interpreter.cpp`).
- **`is default(v)` / typed defaults** live in `Env::varDefault`
  (`src/Interpreter.h`), a per-scope map. `my Int $x` stores `(Int)` as both
  the initial value and the reset default; `$x = Nil` walks `varDefault` and
  restores it (`src/Interpreter.cpp`).
- **Type constraints** on a scalar (`my Int $x = 3`) are enforced at assignment
  for the core nominal types, throwing `X::TypeCheck::Assignment` on a mismatch
  (`src/Interpreter.cpp`).
- **Native integers** (`my int $x`, `my uint8 $b`) carry a bit-width in
  `natBits` (`src/Value.h`) and **wrap on every assignment** — `wrapNative`
  masks the value to the declared width (`src/Interpreter.cpp`).
- **`readonly`** (`src/Value.h`) marks a value bound to a read-only parameter;
  mutating ops like `s///` check it and die.

These four behaviors, concretely:

```cpp
// .VAR  — src/Interpreter.cpp: a Hash tagged "Scalar" describing the container
Value sc = Value::makeHash(); sc.hashKind = "Scalar";
(*sc.hash)["name"]    = Value::str(ivar->name);
(*sc.hash)["default"] = dv;              // varDefault walked up the Env chain
(*sc.hash)["value"]   = inv;

// $x = Nil  — restore the container's default (walk varDefault up the chain)
for (Env* en = tctx_.cur.get(); en; en = en->parent.get()) {
    auto di = en->varDefault.find(nm);
    if (di != en->varDefault.end()) { dv = di->second; break; }
    if (en->vars.count(nm)) break;       // owner scope reached, no declared default
}
*lv = dv;                                // (else Any)

// my Int $i = "x"  — typed scalar: assignment enforces the core nominal types
if (di->second.t == VT::Type && kChecked.count(di->second.s) &&
    !rtTypeMatch(rhs, di->second.s) && !(di->second.s == "Int" && rhs.t == VT::Bool))
    throw RakuError{Value::typeObj("X::TypeCheck::Assignment"), ...};

// my uint8 $b = 300  — wrapNative() masks to the declared width  →  44
unsigned long long u = (unsigned long long)x & ((1ULL << bits) - 1);
```

**Sigils are mostly a parse-time fact.** The runtime dispatches on the `VT` tag,
not the sigil. The sigil (the first character of the variable name) is consulted
only twice: to pick the empty container shape at declaration (`@`→empty Array,
`%`→empty Hash, `$`→`Any`), and to choose the assignment coercion (`@`→
`coerceArray`, `%`→`coerceHash`, else scalar overwrite). Once a value is stored,
its behavior follows its `VT` and the `isList`/`itemized` flags. `itemized`
(`src/Value.h`, set by `$(...)`/`$[...]`) marks an array that should count as
*one* element in list context rather than flattening.

## Function and method calls

A call happens in three phases: build the argument list, activate the callee,
bind the parameters.

### Building the argument list

`evalArgs` evaluates each argument expression into one flat `ValueList`, handling
the spread and naming rules:

```cpp
// src/Interpreter.cpp — evalArgs
} else if (a->kind == NK::Unary && ((Unary*)a.get())->op == "|") {    // a Slip: |@a / |%h
    Value v = eval(...);
    if (v.t == VT::Array || v.t == VT::Range) { for (auto& x : v.flatten()) args.push_back(x); }
    else if (v.t == VT::Hash && v.hash)                              // |%h → named args
        for (auto& kv : *v.hash) { Value p = Value::pair(kv.first, kv.second); p.namedArg = true; args.push_back(p); }
} else {
    Value v = eval(a.get());
    // ONLY a syntactic k=>v / :k(v) with a bare-identifier key is a NAMED arg;
    // a Pair from a variable, or `3 => 4`, stays positional.
    if (v.t == VT::Pair && a->kind == NK::Pair && !((PairExpr*)a.get())->quotedKey && ident)
        v.namedArg = true;
    args.push_back(std::move(v));
}
```

So an argument list is a single `ValueList` in which named args are simply
`Pair` values flagged `namedArg`; the positional/named split is done later, at
bind time.

### Activating the callee

`callCallable` is a thin **wrap layer**: if the routine has been `&r.wrap(...)`'d
it runs the wrapper stack (each able to `callsame` to the next inner layer);
otherwise it passes straight through to `callCallableRaw`, the real activation:

```cpp
// src/Interpreter.cpp — callCallable
if (codeVal.code && !codeVal.code->wrappers.empty()) { /* run the wrapper stack, innermost last */ }
return callCallableRaw(codeVal, std::move(args), rwArgs);   // the common no-wrapper path
```

`callCallableRaw` handles the special callables first — native FFI, `Format`
sprintf, junction autothreading, multi-dispatch, builtins — then activates a
user sub:

```cpp
auto env = std::make_shared<Env>();                 // fresh per-call frame
c.stateEnv->parent = c.closure ? c.closure : global_;  // (once) state env -> closure
env->parent = c.stateEnv;                           // frame -> stateEnv -> closure -> ... -> global
tctx_.dynStack.push_back(caller_scope);             // dynamic ($*var) chain, kept SEPARATE
```

Two things matter here:

1. **The callee's parent is its lexical closure, not its caller.** Free variables
   resolve *lexically* — through `Callable::closure`, the scope where the sub was
   defined (`src/Value.h`). The caller's scope is pushed onto a **separate**
   `dynStack` used only for dynamic variables (`$*foo`). Lexical and dynamic
   scoping are two different chains.
2. **Each activation bumps `frameTop`**, and a routine (not a bare block) records
   its frame as `curRoutineFrame`. Those two counters drive the cooperative
   `return` below.

### Binding parameters

`bindParams` maps the argument `ValueList` onto the signature. A fast path takes
the common case (all mandatory positional `$` scalars, no named args); the
general path first splits named from positional, then binds:

```cpp
// src/Interpreter.cpp — bindParams
if (simple) {                                        // all plain positional $ params, no nameds
    for (size_t i = 0; i < params.size(); i++) {
        Value v = i < args.size() ? args[i] : typedDefault(params[i].type, '$');
        v.readonly = true;                           // a plain scalar param is readonly
        env->define(params[i].name, std::move(v));
    }
    return;
}
// general path: split named vs positional, then bind each
for (auto& a : args)
    if (isNamedArg(a)) named[a.s] = a.pairVal ? *a.pairVal : Value::any();
    else positional.push_back(a);
```

The general path covers:

- **positional**, with optionals and defaults — a default is evaluated in the
  param scope, so `sub f($g, $a = $g/2)` can see earlier params;
- **readonly vs. `is rw` vs. `is copy`** — a plain `$` param is marked `readonly`
  unless it's `rw`/`copy`/the invocant:
  `if (p.sigil == '$' && !p.isRw && !p.isCopy && !p.invocant) v.readonly = true;`
  (`src/Interpreter.cpp`);
- **slurpy** `*@rest` (flattening), `**@rest` (non-flattening), `+@rest`
  (single-arg rule), and `*%named`;
- **named** params, including `:a(:$b)` aliases and sub-signature destructuring
  `sub f([$a, $b])`;
- the implicit **`$_`** topic and placeholder params (`$^a`, `$^b`, bound in
  sorted order).

Note that **type constraints, `where` clauses, and `:D`/`:U` smileys are *not*
checked here** for an ordinary (non-multi) call — single dispatch is largely
duck-typed at the bind boundary. Those checks live in `scoreCandidate`
(next section), used for multi dispatch.

### `is rw` writeback

Because arguments are passed as `Value`s (copies), a mutated `is rw` parameter
has to be copied *back* into the caller's variable after the call. That is
`copyOutRw` (`src/Interpreter.cpp`): the call site also passes the argument
*expressions* (`rwArgs`), and on a normal return each `is rw` param's final value
is written back by re-resolving its argument expression via `lvalue()`:

```cpp
if ((p.isRw || p.sigil == '\\') && pi < rwArgs->size())
    if (Value* lv = lvalue((*rwArgs)[pi].get())) *lv = env->vars[p.name];
```

This works for **direct** calls, where the caller supplied the argument
expressions; multi-dispatched and indirect calls can lose that fidelity.

## Cooperative `return` (and `next`/`last`/`redo`)

The natural way to implement `return` in a tree-walker is to throw a C++
exception (`ReturnEx`) and catch it at the routine boundary. That is correct but
slow — and in the WebAssembly build, where C++ exceptions run through JS
trampolines, it is *very* slow. So Raku++ has a **cooperative** path that avoids
the throw in the common case.

The insight: a `return` only needs to unwind C++ frames if there is a
callable/closure boundary between it and its routine (a `.map` block, a HOF).
When the `return` runs directly inside its routine's own statements or native
loops, `frameTop` hasn't advanced past `curRoutineFrame`, and unwinding is just a
matter of *stopping* the statement loop:

```cpp
// return  —  src/Interpreter.cpp
if (tctx_.curRoutineFrame != 0 && tctx_.frameTop == tctx_.curRoutineFrame) {
    tctx_.returning = true; tctx_.returnV = std::move(v);   // set a flag, don't throw
    return Value::any();
}
throw ReturnEx{v};                                          // boundary crossed: must unwind
```

Native statement loops and `runLoopBody` check `tctx_.returning` after each
statement and bail out; `callCallableRaw` consumes the flag at the routine
boundary, adopting `returnV` as the call's result:

```cpp
// src/Interpreter.cpp — callCallableRaw statement loop, after each statement
if (tctx_.returning) {                       // cooperative return reached this frame
    if (isRoutine) { tctx_.returning = false; last = std::move(tctx_.returnV); }
    break;                                   // a bare block just propagates it to its routine
}
```

`next`/`last`/`redo` use the identical trick with a `loopCtl` register and
`curLoopFrame` — set a flag when the loop is in the same frame, else throw:

```cpp
// src/Interpreter.cpp — LastStmt (Next/Redo mirror it)
if (t.empty() && tctx_.curLoopFrame != 0 && tctx_.frameTop == tctx_.curLoopFrame) {
    tctx_.loopCtl = 2; return Value::any();  // cooperative last (runLoopBody consumes it)
}
throw LastEx{t};                             // labelled or cross-frame: unwind
```

Labelled control, or control that crosses a closure boundary, still throws — so
the semantics are exactly the exception version, just cheaper on the hot path.
(This mechanism was the subject of a subtle frame-boundary bug fixed in the
method path; the counters must be maintained consistently across every
activation kind.)

## Multiple dispatch

A `multi sub`/`multi method` is one `Callable` with `isMultiDispatcher = true`
and a `candidates` vector (`src/Value.h`); each `multi` declaration pushes
its `Code` onto the dispatcher. At call time `scoreCandidate`
(`src/Interpreter.cpp`) scores every candidate against the actual arguments
and returns `-1` for "doesn't apply" or a non-negative **specificity** score:

- arity gates first (too few required, or too many for a non-slurpy → `-1`);
- a **nominal type** mismatch → `-1`; a matching constraint scores, and an
  *exact* type match scores higher than a supertype match;
- `:D`/`:U` smileys and satisfied `where` clauses add specificity;
- **literal** params (`multi fact(0)`) and sub-signature destructures are treated
  as very specific;
- a required **named** that wasn't supplied → `-1`.

The per-positional scoring core:

```cpp
// src/Interpreter.cpp — scoreCandidate, per positional param
if (subsets_.count(p->type)) { if (!subsetMatches(p->type, pos[i])) return -1; score += 2; }
else if (!typeMatchesArg(pos[i], p->type)) return -1;              // nominal type gate
if (p->defConstraint == 1 && !isDefined(pos[i])) return -1;        // :D wants a defined arg
if (p->defConstraint == 2 &&  isDefined(pos[i])) return -1;        // :U wants an undefined one
if (p->defConstraint) score++;                                     // a smiley is more specific
if (!p->type.empty() && p->type != "Any" && p->type != "Mu") {
    score++;                                    // constrained at all beats unconstrained
    if (p->type == pos[i].typeName()) score++;  // exact type beats a supertype (Int beats Numeric)
}
```

The best score wins:

```cpp
const Value* best = nullptr; int bestScore = -1;
for (auto& cand : c.candidates) {
    int s = scoreCandidate(cand, args);
    if (s > bestScore) { bestScore = s; best = &cand; }   // strict >: first of a tie wins
}
if (!best || bestScore < 0) throw /* X::Multi::NoMatch */;
```

Two honest notes: the comparison is a strict `>`, so on **equal** scores the
**first-declared** candidate wins and there is no `X::Multi::Ambiguous`
diagnostic; and a genuine no-match throws `X::Multi::NoMatch`. A `visited` set and
a pushed redispatch frame implement `callsame`/`nextsame`/`callwith`/`nextwith`.

## The object model

A user class is a `ClassInfo`; an instance is an `ObjectData` it points to:

```cpp
// src/Value.h (excerpts)
struct ClassInfo {
    std::string name;
    std::shared_ptr<ClassInfo> parent;
    std::vector<std::shared_ptr<ClassInfo>> extraParents;   // additional `is` parents
    std::vector<ClassAttr> attrs;                           // with defaults
    std::map<std::string, Value> methods;                   // Code values
    bool isRole = false;  std::set<std::string> doneRoles, requiredMethods;
};
struct ObjectData {
    std::shared_ptr<ClassInfo> cls;
    std::map<std::string, Value> attrs;                     // per-instance attribute storage
    Value boxed; bool hasBoxed = false;                     // for `5 but Role` (see mixins)
};
```

A `Value` of kind `VT::Object` wraps a `shared_ptr<ObjectData>`. `ClassInfo` is
registered in `classes_` at declaration and also installed as a lexical type
object.

**Construction.** The default `new`/`bless` walks the class chain **parent-first**,
gives each attribute its default, folds named args into `attrs`, then runs
`BUILD`/`TWEAK`:

```cpp
// src/Builtins.cpp — default construction
for (auto it = chain.rbegin(); it != chain.rend(); ++it)   // parent-first
    for (auto& at : (*it)->attrs) {
        Value dv = at.hasDefVal ? at.defVal : at.def ? eval(at.def)
                 : at.sigil == '@' ? Value::array() : at.sigil == '%' ? Value::makeHash() : Value::any();
        od->attrs[at.name] = dv;
    }
for (auto& arg : args) if (arg.t == VT::Pair) od->attrs[arg.s] = *arg.pairVal;   // :attr(…) named args
if (Value* build = ci->findMethod("BUILD")) invokeMethod(*build, self, args);
if (Value* tweak = ci->findMethod("TWEAK")) invokeMethod(*tweak, self, args);
```

A user-defined `new` is preferred when present; `new` and `bless` share this path.

**Attributes and accessors.** `$!x` is a direct read/write of the `attrs` map.
`$.x` is a public accessor: `methodCall` looks the method up, and on miss checks
`findAttr` for a public attribute and returns it. The native backend reads and
writes attributes with the same map directly:

```cpp
Value rtAttrGet(const Value& self, const std::string& name) {   // $.x / $!x read
    if (self.t == VT::Object && self.obj) {
        auto it = self.obj->attrs.find(name);
        if (it != self.obj->attrs.end()) return it->second;
    }
    return Value::any();                                          // absent attribute → (Any)
}
Value& rtAttrRef(Value& self, const std::string& name) {         // $!x write slot (autovivifies)
    return self.obj->attrs[name];
}
```

**Method resolution.** `ClassInfo::findMethod` *is* the MRO — check the class's
own methods, then the parent, then each extra parent, recursively. It's a
depth-first, parent-before-extra-parents walk (a simple DFS, not full C3
linearization):

```cpp
Value* findMethod(const std::string& m, ClassInfo** owner) {
    auto it = methods.find(m);
    if (it != methods.end()) { if (owner) *owner = this; return &it->second; }   // own methods
    if (parent) { if (Value* r = parent->findMethod(m, owner)) return r; }        // then the parent
    for (auto& p : extraParents) if (p)                                           // then extra parents
        { if (Value* r = p->findMethod(m, owner)) return r; }
    if (owner) *owner = nullptr;
    return nullptr;
}
```

The `owner` out-param reports which class the method was found in, so
`invokeMethodChain` can resume `callsame`/`nextsame` from that class's parent.

**Roles.** A role is a `ClassInfo` with `isRole = true`, a set of
`requiredMethods`, and `doneRoles`. Composition copies the role's methods and
attributes into the class and records membership; **required methods are checked
at class declaration** (using the very `findMethod` above), throwing
`X::Role::Unimplemented` if unmet:

```cpp
for (ClassInfo* role : composed)
    for (const std::string& req : role->requiredMethods)
        if (!ci->findMethod(req))
            throw RakuError{Value::typeObj("X::Role::Unimplemented"), ...};
```

`.does` / `~~` consult `doesRole`, which returns true for the class itself,
directly or transitively composed roles, and roles done by parents. (Role method
composition is a copy-into-table — last writer wins, with no conflict
diagnostic.)

## How built-in types get methods

An `Int`, `Str`, or `Array` is a native `Value` (`VT::Int`/`VT::Str`/`VT::Array`),
**not** an `ObjectData` — so it has no `ClassInfo` and no method table. `5.is-prime`
and `"x".uc` dispatch through one enormous function, `Interpreter::methodCall`,
which is a long cascade of branches keyed on the method name (and the invocant's
tag, consulted inside each branch):

```cpp
if (m == "uc")       return Value::str(mapCase(inv.toStr(), true, 0));    // "abc".uc
if (m == "chars")    return Value::integer(graphemeCount(inv.toStr()));   // .chars (graphemes)
if (m == "is-prime") { /* Miller–Rabin on inv.toInt() */ }               // 7.is-prime
// … hundreds more …
```

The C++ `if`-ladder *is* the built-in method set. This is the pragmatic
counterpart to the fat `Value`: since every native value is the same struct, its
methods are one big dispatch function rather than per-type classes.

Two things run *before* the built-in branches:

1. **Junction autothreading** (below).
2. **`augment` / `builtinExt_`**: methods a program adds to a built-in type via
   `augment class Int {...}` are parked in a `map<typeName, map<methodName, Code>>`
   and consulted *first* — walking the native ancestry so augmenting `Cool`/`Any`
   also reaches `Int`/`Str` — so they can override built-ins:

   ```cpp
   // src/Builtins.cpp — before the native branches
   if (!builtinExt_.empty() && inv.t != VT::Object) {
       std::string tn = inv.t == VT::Type ? inv.s : inv.typeName();
       if (Value* f = lookup(tn)) return invokeMethod(*f, inv, std::move(args), rwArgs);
       for (const std::string& anc : typeAncestry(tn))          // Cool/Any reach Int/Str
           if (anc != tn) if (Value* f = lookup(anc)) return invokeMethod(*f, inv, ...);
   }
   ```

User objects take the other branch: a `VT::Object` invocant dispatches through
`ClassInfo::findMethod` + the MRO. So there are two dispatch worlds — the
type-switched ladder for native values, the `ClassInfo` table for objects — and
`augment` is the bridge that lets user code add to the former.

## Closures

`makeClosure` (`src/Interpreter.cpp`) turns a `{ ... }` block or `sub { }`
into a `Code` value by capturing the **defining** environment:

```cpp
code.code->params  = &be->params;   // borrowed from the AST
code.code->body    = &be->body;     // borrowed from the AST
code.code->closure = tctx_.cur;     // captured: the scope where the block was written
```

Only the environment is *owned* (a `shared_ptr<Env>` copy); the parameter list
and body are borrowed pointers into the AST, which outlives execution. At call
time the fresh per-call `Env`'s parent chain runs
`env → stateEnv → closure → … → global` (`src/Interpreter.cpp`), so a
free variable in the body resolves through the captured `closure` scope. Because
the capture is the live `Env` (not a copy of its values), a closure sees and
mutates the *same* container as its defining scope — real closures, e.g. a
counter that keeps incrementing the same `my $n`.

## Junctions

A junction has no dedicated `VT`. `any(1, 2, 3)` is a `VT::Array` whose elements
are the eigenstates, tagged by `enumName ∈ {any, all, one, none}`; `typeName`
reports `"Junction"` for exactly that shape:

```cpp
// src/Value.cpp — typeName(), the VT::Array case
if (enumName == "any" || enumName == "all" || enumName == "one" || enumName == "none")
    return "Junction";
```

They are built by the `any`/`all`/`one`/`none` builtins, the `.any`/`.all`
methods, and the `|`/`&`/`^` infix operators.

**Autothreading** — distributing an operation over the eigenstates and
recombining — happens at each place a value is consumed:

- **Operators** (`applyArith`, `src/Interpreter.cpp`): a comparison
  *collapses* to a single `Bool` per the junction type (`any` → "any eigenstate
  true", `all` → "all true", etc.); any other operator produces a **new**
  junction of the per-eigenstate results:

  ```cpp
  Value out = Value::array(); out.enumName = j.enumName;
  for (auto& e : *j.arr) out.arr->push_back(applyArith(op, jleft ? e : l, jleft ? r : e));
  return out;                       // any(1,2) + 10  ==>  any(11, 12)
  ```

- **Method calls** (`methodCall`, `src/Builtins.cpp`): a small allow-list of
  methods act on the whole junction (`Bool`, `gist`, `new`, …); everything else
  autothreads, returning a junction of the results.
- **Callable invocation** (`callCallableRaw`) and **smartmatch** (`~~`) autothread
  the same way.

This is why `if 3 == any(1, 2, 3)` works: the `==` sees a junction on the right,
threads the comparison across `1, 2, 3`, and collapses `any` to `True`.

## Lazy and infinite sequences

An infinite list like `1, 2, 4 ... *` or `(1..Inf).map(*²)` obviously can't be a
materialized `ValueList`. Raku++ represents laziness by attaching a generator to
an otherwise-ordinary array value.

### Representation

A lazy list is a `VT::Array` `Value` whose already-computed **prefix** lives in
`arr`, plus a `LazySeqState` stashed in the generic `Value::ext` handle:

```cpp
struct LazySeqState {                               // src/Interpreter.h
    std::function<bool(ValueList&)> appendNext;     // compute ONE more element; false = exhausted
    bool infinite = false;                          // truly unbounded: elems/pop/[*-1] must die
};
```

`appendNext` appends exactly one element to the prefix and returns whether more
exist. `ext` is the same opaque slot used for `Promise`/`Channel` state — a lazy
seq just parks a different kind of state there.

### Building one — the `...` operator

`seqOp` (`src/Interpreter.cpp`) splits the left side into a seed list (a
trailing `Code` seed becomes the *generator*) and classifies the right endpoint.
A **bounded** endpoint (`1 ... 10`) is computed eagerly in a loop (capped at
1,000,000). An **infinite** endpoint (`... *` or `... Inf`) builds a
`LazySeqState` whose `appendNext` closure captures the generator or the detected
progression (arithmetic difference, geometric ratio, or string `succ`/`pred`) and
produces one element per call:

```cpp
if (infinite) {
    auto st = std::make_shared<LazySeqState>();
    st->infinite = true;               // list assignment must keep it lazy, not drain it
    st->appendNext = [...](ValueList& cache) -> bool {
        /* feed the last `arity` cached elements to the generator, or step the
           progression; push the next value; return true (or false on `last`) */
    };
    out.ext = st;
}
```

A bare `1..Inf` assigned to `@a` builds a similar counting `LazySeqState`:

```cpp
// src/Interpreter.cpp — coerceArray, an infinite Range (rTo ≈ 2^63)
auto st = std::make_shared<LazySeqState>(); st->infinite = true;
auto next = std::make_shared<long long>(start);
st->appendNext = [next](ValueList& cache) -> bool { cache.push_back(Value::integer((*next)++)); return true; };
a.ext = st;
```

### Forcing elements

Consumers grow the prefix on demand with `materializeLazy(v, n)`, which calls
`appendNext` until the prefix has `n` elements or a hard cap of 1,000,000 is hit:

```cpp
void Interpreter::materializeLazy(const Value& v, size_t n) {   // src/Interpreter.cpp
    auto st = std::static_pointer_cast<LazySeqState>(v.ext);
    while (v.arr->size() < n && v.arr->size() < CAP)
        if (!st->appendNext(*v.arr)) break;
}
```

So `@lazy[5]` materializes six elements then indexes; `.head(3)` materializes
three; `.first(&pred)` pulls one at a time until the predicate matches.
Operations that need the *end* of an infinite list throw `X::Cannot::Lazy`:

```cpp
// src/Builtins.cpp — whole-list ops on an INFINITE lazy source can't complete
if (m == "elems" || m == "end" || m == "pop" || m == "tail" || m == "reverse" ||
    m == "sort" || m == "sum" || m == "min" || m == "max" || m == "join" ||
    m == "Str" || m == "gist")
    throw RakuError{Value::typeObj("X::Cannot::Lazy"), "Cannot " + m + " a lazy list onto an Array"};
```

(A *finite* lazy value, like a `gather` that outgrew its probe, instead forces
full materialization.)

### Lazy `.map` / `.grep`

`.map` and `.grep` over a lazy source build a **new** `LazySeqState` that pulls
from the source on demand, so `(1..Inf).grep(*.is-prime).head(5)` terminates:

```cpp
// .map  —  src/Builtins.cpp
st->appendNext = [self, src, fn](ValueList& cache) -> bool {
    size_t si = cache.size();
    self->materializeLazy(src, si + 1);            // pull one more from the source
    if (si >= src.arr->size()) return false;
    cache.push_back(self->callCallable(fn, { (*src.arr)[si] }));
    return true;
};
```

`.grep` loops pulling source elements until its predicate matches; `.skip`
builds a shifted view.

### `gather` / `take`

`gather { ... take ... }` is lazy without coroutines, using a
**probe-and-double** strategy. The gather collector and a per-gather element cap
live on the `ExecContext` (`gatherStack`, `gatherLimits`,
`src/Interpreter.h`). The block is first run under a small cap (64); if it
finishes within the cap it was finite and is returned eagerly. If the cap was
*hit*, the result becomes a `LazySeqState` that grows by **re-running the block**
with a larger cap (doubling, so re-run cost stays amortized linear):

```cpp
st->appendNext = [this, runGather](ValueList& out) -> bool {
    ValueList grown;
    bool more = runGather(out.size() + std::max<size_t>(64, out.size()), grown);
    for (size_t i = out.size(); i < grown.size(); i++) out.push_back(grown[i]);
    return more;
};
```

A `take` that pushes past the current cap throws `StopGatherEx`
(`src/Interpreter.h`), an empty marker the gather runner catches to unwind the
(possibly infinite) block:

```cpp
auto& coll = *tctx_.gatherStack.back();
for (auto& x : a) coll.push_back(x);
if (lim && coll.size() >= lim) throw StopGatherEx{};   // src/Builtins.cpp
```

So a `gather` producing an infinite stream runs the block only far enough to
satisfy each demand, then stops via the exception, re-entering later for more.

## `but` / `does` mixins

`5 but Role` and `%h does R` mix a role (or an attribute) into a value at run
time. For a value that is already an object, the role is composed into a fresh
anonymous subclass. For a **non-object base** — `5 but Role` — there is no
`ObjectData` to extend, so `mixinValue` (`src/Interpreter.cpp`) *boxes* it:

```cpp
obj = std::make_shared<ObjectData>();
obj->boxed = base;        // the original 5 is kept here (ObjectData.boxed)
obj->hasBoxed = true;
// ... build an anonymous subclass composing the role, wrap in a VT::Object ...
```

The result is a `VT::Object` whose `obj->boxed` holds the untouched original.
Method dispatch on the mixed object checks the role's methods first; anything not
found (and not an identity method like `.WHAT`) is **delegated back to the box**:

```cpp
// src/Builtins.cpp — methodCall: a mixed non-object base forwards to its box
if (inv.t == VT::Object && inv.obj && inv.obj->hasBoxed && inv.obj->cls &&
    !inv.obj->cls->findMethod(m) && !inv.obj->cls->findAttr(m)) {
    static const std::set<std::string> keepOnObj = {"does","HOW","WHAT","WHICH","defined","DEFINITE"};
    if (!keepOnObj.count(m)) return methodCall(inv.obj->boxed, m, args, rwArgs);   // (5 but R).succ → Int.succ
}
```

So `(5 but Role).succ` runs `Int.succ` on the `5`, while the role's methods and
`.does`/`.WHAT` see the mixed object.

## One runtime, two front ends

Everything above is the *runtime library* (`librakupp_rt`). The tree-walking
interpreter is one client of it. The native `--exe` compiler is the other: it
emits C++ that calls the *same* runtime functions rather than re-implementing
them. A compiled `$a + $b` becomes a call to `rtAdd(a, b)`, which inlines the
small-int fast path and otherwise falls back to `applyArith` — the identical
function the interpreter uses:

```cpp
// src/Interpreter.h — the fast path native codegen emits for `+`
inline Value rtAdd(const Value& l, const Value& r) {
    long long z;
    if (rtBothInt(l, r) && !add_ovf(l.i, r.i, &z)) return Value::integer(z);   // inline int64
    return applyArith("+", l, r);                                             // else the runtime
}
```

`Value` is the shared currency, `callBuiltin`/`callCallable` the shared calling
convention, `rtIndexRef`/`rtAttrRef`/`rtArrayVal` the shared container ops.

That is why the two backends produce byte-identical output and why a feature
implemented once in the runtime works in both. The codegen side is documented in
[ARCHITECTURE.md](ARCHITECTURE.md) (§4) and [OPTIMIZATION.md](OPTIMIZATION.md);
this document is the value model they share.

## Honest limitations

The design buys simplicity and a single source of truth for semantics, at some
real costs:

- **Memory per value.** The fat `Value` struct carries every field even when one
  is live, so a `ValueList` of `Int`s is larger than a `vector<int64_t>`. This is
  a deliberate trade for branch-free access and trivial copyability.
- **Tree-walking throughput.** Re-dispatching every AST node on every execution
  is inherently slower than bytecode or JIT; `--exe` and the `-O` native-int
  lanes exist to close that gap where it matters. See
  [BENCHMARKS.md](BENCHMARKS.md).
- **"Infinite" is capped at 1,000,000.** Every materialization path shares that
  ceiling as a runaway safety net, so a truly unbounded consumer is bounded in
  practice.
- **MRO is DFS, not C3.** `findMethod` walks parent-then-extra-parents depth
  first; for the diamond hierarchies where C3 and DFS differ, this can pick a
  different method than Rakudo.
- **Multi dispatch has no ambiguity error.** Equal-specificity candidates
  silently resolve to the first declared, rather than throwing
  `X::Multi::Ambiguous`.
- **Role composition is last-writer-wins.** Composing two roles with the same
  method name copies both into the table without a conflict diagnostic.

These are the known seams between "runs the language" and "is the reference
implementation"; the [ROADMAP.md](ROADMAP.md) tracks which ones are on the list to
close.
