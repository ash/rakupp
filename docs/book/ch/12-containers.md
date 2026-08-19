# Containers, Binding, and Copy Semantics

A `Value` copy shares its payload. Raku sometimes wants that and sometimes
wants the opposite, and the rules for which are the subtlest part of the value
model. This chapter is about where the sharing is broken on purpose, and how
the things Raku calls *containers* are modelled without a container object.

## Scopes are hash maps with a parent pointer

```cpp
// src/Interpreter.h
struct Env {
    std::unordered_map<std::string, Value> vars;   // "$x", "@a", "%h", "&sub"
    std::shared_ptr<Env> parent;
    bool routineFrame = false;   // $/ scopes here
    bool loopFrame = false;      // a loop's `state` frame
    std::unique_ptr<EnvExtras> ex;

    Value* find(const std::string& name) {
        auto it = vars.find(name);
        if (it != vars.end()) return &it->second;
        return parent ? parent->find(name) : nullptr;
    }
    Value& define(const std::string& name, Value v) {
        return vars[name] = std::move(v);
    }
};
```

The **full sigilled name is the key**. Subs live in the same map under a
`&`-prefixed key, so `&foo` and `$foo` occupy different namespaces without a
second table. Lexical scoping *is* the parent walk in `find`; there is no
separate symbol table anywhere in the interpreter.

Reading a variable returns a **copy** of the slot. Writing goes through
`lvalue(Expr*)`, which hands back a `Value*` pointing straight into the owning
`Env`'s map:

```cpp
// read  $x
if (Value* p = tctx_.cur->find(ve->name)) return *p;      // a COPY

// my $x — make the slot here, hand back its address
tctx_.cur->define(ve->name, std::move(init));
return &tctx_.cur->vars[ve->name];

// $x = 5
*lv = rhs;
```

The three declarators differ only in *which* `Env` holds the slot:

| Declarator | Storage |
|---|---|
| `my` | the current lexical `Env` |
| `our` | the package env, ultimately the global one; also republished under a qualified name when the package block closes |
| `state` | a per-`Callable` `stateEnv`, created exactly once and shared across calls |

`state` is the only one that needs care under concurrency, and it gets a
`std::once_flag` on the `Callable` so the persistent environment is created
exactly once even if two threads call the routine simultaneously.

### `EnvExtras`: the eight rarely-used maps

An `Env` is constructed for every routine call **and every block**. It also
needs somewhere to keep `is rw` write-through links, `temp`/`let` restorations,
`is default` values, and the set of names declared `is dynamic` — eight
containers which are empty in the overwhelming majority of scopes.

Constructing and destroying eight empty maps per block is pure overhead for the
ordinary case, so they live behind one lazily-allocated pointer:

```cpp
// src/Interpreter.h
EnvExtras& x() { if (!ex) ex = std::make_unique<EnvExtras>(); return *ex; }
const EnvExtras& xr() const {
    static const EnvExtras kEmpty;
    return ex ? *ex : kEmpty;
}
```

Writers call `x()` and materialise it; readers call `xr()` and get a shared
empty instance, so a lookup never allocates. This is a small pattern but a
recurring one: **make the common case cost nothing, and pay only where the
feature is actually used.**

## Where the sharing is broken

Copying a `Value` bumps refcounts on the `shared_ptr` members, so a raw copy of
an `Array` shares its backing `ValueList`. Raku wants:

```raku
my @a = 1, 2, 3;
my @b = @a;      # a COPY
@b[0] = 99;
say @a;          # (1 2 3) — unchanged
```

So the interpreter deliberately breaks the sharing at `=` assignment, and **only
for the `@` and `%` sigils**:

```cpp
// src/Interpreter.cpp — coerceArray
if (v.t == VT::Array) {
    if (v.itemized) { … }             // an itemized array is ONE element
    if (v.ext) return v;              // a lazy seq stays lazy
    Value r = Value::array(*v.arr);   // *v.arr copies the vector
    r.isList = false; return r;
}
```

`*v.arr` dereferences the pointer and copies the underlying vector, so `@b` gets
its own storage. Hashes copy the same way. Scalar assignment is a plain struct
overwrite, so `my $x = @a` stores an `Array` value that *does* still share
`@a`'s buffer — because an item container is a reference to one thing.

Two consequences to keep straight:

- **The copy is one level deep**, matching Rakudo. `my @b = @a` copies the
  top-level buffer, but a nested itemized array inside is copied as a `Value`
  struct, so its `arr` pointer is still shared.
- **A lazy sequence is not copied.** The `if (v.ext) return v` line is what keeps
  `my @a = 1, 2, 4 ... *` from trying to drain an infinite list.

The compiled backend has its own mirror of this rule, `rtArrayVal`, with the
same fresh-buffer semantics, so interpreted and compiled code agree.

## Binding: `:=` and the `Proxy`

`=` copies a value into an existing container. `:=` rebinds the container
itself: after `$y := $x`, the two names *are* the same container and a write to
either is seen by both.

But `Env` stores `Value`s by value in a map, so two map slots cannot literally
be the same storage. The alias is faked with a `Proxy`:

```cpp
// src/Interpreter.cpp — $y := $x, scalar case
Value proxy = Value::makeHash(); proxy.hashKind = "Proxy";
(*proxy.hash)["FETCH"] = fetch;    // a builtin Code closing over the owning Env
(*proxy.hash)["STORE"] = store;
*lvalue(target) = proxy;
```

`$y`'s slot holds a `Hash` tagged `Proxy` whose two closures read and write
`$x`'s slot in the environment that owns it. Reading a variable notices the tag
and calls `FETCH` instead of returning the hash:

```cpp
// src/Interpreter.cpp — reading a variable
Value* p = tctx_.cur->find(ve->name);
if (p) {
    if (p->t == VT::Hash && p->hashKind == "Proxy" && p->hash) {
        auto it = p->hash->find("FETCH");
        if (it != p->hash->end())
            return callCallable(it->second, { *p });
    }
    return *p;
}
```

Binding chains dereference one extra level so `$z := $y := $x` works.

This costs a call on every read of a bound variable, which is why the check is
placed inside the *slow* half of the variable-read path and why the fast paths
in Chapter 19 decline any value with a non-empty `hashKind`.

**Array binding is much cheaper**, because sharing a buffer is exactly what a
`shared_ptr` copy already does:

```cpp
// src/Interpreter.cpp — @a := @b
if (a->op == ":=" && rhs.t == VT::Array) {
    Value b = rhs; b.isList = false; *lv = b;
}
```

`Value b = rhs` copies the struct and shares `rhs.arr`. This is the deliberate
opposite of `@a = @b` above. `constant` reuses the binding path — a constant is
`:=` in disguise.

The user-visible `Proxy` type — `Proxy.new(:FETCH{…}, :STORE{…})` — is the same
mechanism exposed, so a program can build one explicitly.

## The scalar-container metaphor

Raku's `$` variable is really a *Scalar container* holding a value, with
introspectable machinery: `.VAR`, `is default`, type constraints. Raku++ models
that without a separate container object, using flags on the `Value` plus
per-`Env` side tables.

**`.VAR`** builds a `Hash` tagged `"Scalar"` reporting the variable's name,
value and default.

**`is default(v)` and typed defaults** live in `EnvExtras::varDefault`, a
per-scope map. `my Int $x` stores `(Int)` as both the initial value and the
reset default. Assigning `Nil` walks the chain to find it:

```cpp
// src/Interpreter.cpp — $x = Nil restores the container's default
for (Env* en = tctx_.cur.get(); en; en = en->parent.get()) {
    auto di = en->varDefault.find(nm);
    if (di != en->varDefault.end()) { dv = di->second; break; }
    if (en->vars.count(nm)) break;   // owner scope, no declared default
}
*lv = dv;                             // else Any
```

**Type constraints** on a scalar are enforced at assignment for the core nominal
types, throwing `X::TypeCheck::Assignment` on a mismatch.

**`readonly`** is a flag on the `Value` itself, set when binding a plain `$`
parameter. Mutating operations such as `s///` check it and die.

**`temp` and `let`** push restoration closures onto the scope's extras.
`tempRestores` run whenever the scope leaves; `letRestores` run only on an
*unsuccessful* exit, which is the distinction the language draws.

## Sigils are mostly a parse-time fact

The runtime dispatches on the `VT` tag, not the sigil. The sigil — the first
character of the name — is consulted exactly twice: to pick the empty container
shape at declaration (`@` gives an empty `Array`, `%` an empty `Hash`, `$` an
`Any`), and to choose the assignment coercion (`coerceArray`, `coerceHash`, or a
scalar overwrite).

After that, behaviour follows the tag and two flags:

- **`isList`** marks a `VT::Array` that is a `List` or `Seq` rather than an
  `Array`. It renders with parentheses instead of brackets and flattens in list
  context. Same storage, different behaviour.
- **`itemized`**, set by `$(...)` or `$[...]`, marks an array that counts as
  *one* element in list context rather than flattening.

That pair carries a surprising amount of Raku's list semantics, and most of the
one-argument-rule subtleties in the built-ins reduce to testing them.

## Shaped arrays and typed containers

```cpp
// src/Value.h
std::shared_ptr<std::vector<long long>> shape;  // my @a[2;3]
std::string ofType;                              // Array[Int], my Int @a
```

A shaped array is a fixed row-major structure with its dimensions recorded; an
unshaped one leaves the pointer null. `makeShapedContainer` builds one,
optionally filling it from a flat list.

`ofType` carries the element type for a typed container, comma-joined when
there is more than one parameter (`Hash[Int,Str]` becomes `"Int,Str"`). It also
drives native-width masking for `my uint8 @a`, through the same
`natWidthOfType` table as scalars.

## `is rw`, and writing back to the caller

Because arguments are passed as `Value` copies, a mutated `is rw` parameter has
to be written back into the caller's variable. The call site passes the argument
*expressions* alongside the values, and the binder records the link:

```cpp
// src/Interpreter.h — EnvExtras
std::map<std::string, std::pair<Expr*, std::shared_ptr<Env>>> rwLinks;
std::map<std::string, Value> rwSynced;
std::map<std::string, Value*> rwDirect;
std::set<std::string> rwDead;
```

There are two mechanisms because there are two situations. `rwLinks` holds the
caller's argument expression and environment, so an assignment to the parameter
can re-resolve the lvalue and push the value through *immediately* — the caller
sees the change mid-call, which is what Raku specifies. `rwSynced` records what
was last pushed, so the copy-out backstop at return can skip parameters that are
already up to date; without it a late copy-out would re-apply stale values over
the callee's own later edits.

`rwDirect` is the simpler case: a hyper-operator element call already has the
caller's slot as a pointer, with no expression to re-evaluate. `rwDead` marks a
raw or `rw` parameter bound to a literal, so assigning it dies with
`X::Assignment::RO` rather than silently writing into a temporary.

This works for **direct** calls, where the caller supplied the argument
expressions. Multi-dispatched and indirect calls can lose that fidelity — a
known limitation, documented rather than hidden.

## Honest limitations

- **A reference cycle leaks.** Lifetime is refcounting. The interpreter breaks
  the one cycle it creates systematically — a nested sub whose closure points
  back at the frame that holds it — and otherwise relies on processes being
  short-lived.
- **`:=` on a scalar costs a call per read.** The `Proxy` is correct and
  general, and it is not free.
- **`is rw` write-back is expression-based.** Where the argument expression is
  not available or not re-resolvable, the write-back does not happen.
- **The one-level copy rule** matches Rakudo, but it surprises people
  regularly, and no amount of documentation seems to fix that.
