# Containers, Binding, and Copy Semantics

A `Value` copy shares its payload. Raku sometimes wants that and sometimes
wants the opposite, and the rules for which are the subtlest part of the value
model. This chapter is about where the sharing is broken on purpose, how the
things Raku calls *containers* are modelled without a container object, and,
at the end, how shared payloads that refer to each other are freed.

## Scopes are hash maps with a parent pointer

```cpp
// src/Interpreter.h — abridged
struct Env {
    EnvVars vars;                // a map: "$x", "@a", "%h", "&sub"
    EnvParent parent;
    bool routineFrame = false;   // $/ scopes here
    bool loopFrame = false;      // a loop's `state` frame
    std::unique_ptr<EnvExtras> ex;
    const PadLayout* layout = nullptr;
    ValueList pad;               // slots for the names the layout resolved
    std::atomic<uint64_t> padLive{0};   // which slots' `my` has run

    Value* find(const std::string& name) {
        for (Env* e = this; e; e = e->parent.get()) {
            auto it = e->vars.find(name);
            if (it != e->vars.end()) return it->second.deref();
            if (e->layout)
                if (Value* p = e->padFind(name)) return p->deref();
        }
        return nullptr;
    }
};
```

The **full sigilled name is the key**. Subs live in the same map under a
`&`-prefixed key, so `&foo` and `$foo` occupy different namespaces without a
second table. Lexical scoping *is* the parent walk in `find`; there is no
separate symbol table anywhere in the interpreter.

That was the whole of it until the pads of Chapter 41. A routine's call frame
and the mainline now also own a **pad**: a `ValueList` sized once at frame
setup, with one slot for each name its `PadLayout` resolved ahead of time. A
name with a slot never lives in the map. `define` puts its value in the slot
and then sets the slot's bit in `padLive`, so a slot answers a lookup only
after its declaration has run, and an outer variable of the same name stays
visible until then. A pad never grows, because `lvalue` hands out pointers
into it. `find` is the slow path for both stores; a variable node whose slot
was resolved reaches it by index.

Reading a variable returns a **copy** of the slot. Writing goes through
`lvalue(Expr*)`, which hands back a `Value*` pointing straight into the owning
`Env`'s map or pad:

```cpp
// read  $x
if (Value* p = tctx().cur->find(ve->name)) return *p;      // a COPY

// my $x — make the slot here, hand back its address
return &tcx.cur->define(ve->name, defaultFor(sigil));

// $x = 5
*lv = rhs;
```

The `deref()` in `find` is the one step between a storage slot and the
container it stands for. A slot that a second name was bound to holds a
**cell**, described under *Binding* below, and `find` answers the `Value`
inside it.

The three declarators differ only in *which* `Env` holds the slot:

| Declarator | Storage |
|---|---|
| `my` | the current lexical `Env` |
| `our` | the package env, ultimately the global one; also republished under a qualified name when the package block closes |
| `state` | a per-`Callable` `stateEnv`, created exactly once and shared across calls |

`state` is the only one that needs care under concurrency, and it gets a
`std::once_flag` on the `Callable` so the persistent environment is created
exactly once even if two threads call the routine simultaneously.

### `EnvExtras`: the rarely-used maps

An `Env` is constructed for every routine call **and every block**. It also
needs somewhere to keep `is rw` write-through links, `temp`/`let` restorations,
`is default` values, and the set of names declared `is dynamic` — eight
containers when this was written and about twenty now, all empty in the
overwhelming majority of scopes.

Constructing and destroying that many empty maps per block is pure overhead for
the ordinary case, so they live behind one lazily-allocated pointer:

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

Copying a `Value` adds a count to its payload body, so a raw copy of an
`Array` shares its backing `ValueList`. Raku wants:

```raku
my @a = 1, 2, 3;
my @b = @a;      # a COPY
@b[0] = 99;
say @a;          # (1 2 3) — unchanged
```

So the interpreter deliberately breaks the sharing at `=` assignment, and **only
for the `@` and `%` sigils**:

```cpp
// src/Interpreter.cpp — coerceArray, abridged
if (v.t == VT::Array) {
    if (v.itemized) { … }             // an itemized array is ONE element
    if (v.ext()) { … }                // a lazy seq stays lazy
    Value r = Value::array(*v.arr()); // *v.arr() copies the element list
    r.isList = false;
    …
    return r;
}
```

`*v.arr()` dereferences the pointer and copies the underlying `ValueList`, so
`@b` gets its own storage. That copy is the most visible O(n) event in the
language, and it is the reason `ValueList` is a container this project owns
rather than a `std::vector` — see *The list container* below. Hashes copy the
same way. Scalar assignment is a plain struct overwrite, so `my $x = @a` stores
an `Array` value that *does* still share `@a`'s buffer — because an item
container is a reference to one thing.

Two consequences to keep straight:

- **The copy is one level deep**, matching Rakudo. `my @b = @a` copies the
  top-level buffer, but a nested itemized array inside is copied as a `Value`
  struct, so its `arr` pointer is still shared.
- **A lazy sequence is not copied.** The `if (v.ext())` line is what keeps
  `my @a = 1, 2, 4 ... *` from trying to drain an infinite list. A finite
  `gather` is reified there; a still-lazy one is returned as it is.

The compiled backend has its own mirror of this rule, `rtArrayVal`, with the
same fresh-buffer semantics, so interpreted and compiled code agree.

One case skips the copy. When the right side's buffer has no other owner — an
operator's or a call's result, never a variable's — `coerceArrayTake` moves the
buffer into the new `Array` instead of copying each element and destroying the
original.

## Binding: `:=` and the `Proxy`

`=` copies a value into an existing container. `:=` rebinds the container
itself: after `$y := $x`, the two names *are* the same container and a write to
either is seen by both.

But `Env` stores `Value`s by value, so two slots cannot literally be the same
storage. What they can do is hold the same counted body. A variable that a
second name is bound to has its `Value` moved into a **cell**, a payload body
of kind `PK::Cell`, and every name bound to it holds that cell:

```cpp
// src/Value.h
PRef<Value> promoteToCell() {
    if (pk_ == PK::Cell) return PRef<Value>::fromSlot(p_);
    auto c = makePayload<Value>(std::move(*this));
    *this = cellHolder(c);
    return c;
}
Value* deref() {
    return pk_ == PK::Cell ? (p_ ? pv<Value>() : nullptr) : this;
}
```

The holder is inert. Only an `Env`'s storage holds one, and `find`, `local`
and the pad readers all answer the `Value` inside, so a cell is never read as
a value of its own. After `$y := $x` a read of either name is the ordinary
read, and a write through either lands in the one container. The binders use
`findRaw` and `localRaw`, which answer the storage slot itself, because they
replace or share the container and do not write into it.

Promotion rewrites the slot, and a `Value` is several words, so it is not
done while another thread could be reading that slot. While worker threads
are live, a slot that is not private to its thread stays as it is.

Until 2026-09-27 every scalar alias was a `Proxy` instead: a `Hash` tagged
`Proxy` holding a `FETCH` and a `STORE` closure over the owning scope, which
cost a call on every read of a bound variable. The `Proxy` is still the form
where a reader takes the slot's `Value` as it stands and cannot be handed a
cell holder: an attribute target such as `$!x := $y`, the topic `$_`, an array
or hash element bound to a container, and the alias of a typed variable,
which has to carry the type so that a store through the alias is checked.
Those are a `Proxy` *over the same cell*, and reading a variable recognizes
one before it considers a call:

```cpp
// src/InterpreterCore.cpp — reading a variable
Value* p = tctx().cur->find(ve->name);
if (p) {
    if (p->t == VT::Hash && p->hashKind == proxyKind() && p->hash()) {
        // an element alias or a shared cell reads directly; FETCH is a call
        if (Value r; engineProxyRead(*this, *p, r)) return r;
        auto it = p->hash()->find("FETCH");
        if (it != p->hash()->end())
            return callCallable(it->second, { *p });
    }
    return *p;
}
```

A `Proxy` a program built itself reaches the `FETCH` call, and so do the few
aliases the engine still makes the old way, such as one whose source is `$_`.
The check still sits inside the *slow* half of the variable-read path, and the fast
paths in Chapter 19 decline any value with a non-empty `hashKind`.

**Array binding needs neither**, because sharing a buffer is exactly what a
copy of the `Value` already does:

```cpp
// src/InterpreterCore.cpp — @a := @b, abridged
if (opEq(a->op, ":=") && rhs.t == VT::Array) {
    …                         // a Seq is refused: it is not Positional
    raceW(lv) = rhs;
    lv->itemized = false;
}
```

The cleared flag is `itemized`, not `isList`, and the difference is two bugs
rather than a detail. Clearing `isList` would make a bound `List` become an
`Array`, which is wrong — `my @a := @b` where `@b` is a `List` must stay a
`List`, and `URI::Path` is where that surfaced. What has to be cleared is the
*itemization*: `$(1,2,3)` is one scalar item that does not flatten, and binding
it to an `@` variable that then flattens it is how `DBDish::Pg` overflowed its
stack.

The assignment copies the struct and shares `rhs`'s buffer. This is the
deliberate opposite of `@a = @b` above. `constant` reuses the binding path — a constant is
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
// src/InterpreterCore.cpp — $x = Nil restores the container's default
for (Env* en = tctx().cur.get(); en; en = en->parent.get()) {
    auto di = en->xr().varDefault.find(nm);
    if (di != en->xr().varDefault.end()) { dv = di->second; break; }
    if (en->local(nm)) break;   // owner scope reached, no declared default
}
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

## The list container

`ValueList` is the type behind every Array — `Value::arr()` answers the
`ValueList` in the payload body — and it is also the argument list of every
call. Until
2026-09 it was a `typedef` for `std::vector<Value>`. It is now `RVec<Value>`,
a container this project owns ([src/ValueVec.h](../../../src/ValueVec.h)),
implementing the `std::vector` subset the tree uses with `std::vector`'s
semantics: raw-pointer iterators, growth invalidating everything,
`push_back(v[0])` still legal.

It differs in two places, both of them measured.

**It grows in one pass.** `std::vector` reallocating means filling a second
buffer element by element and then walking the old buffer *again* to destroy
each source. `RVec` constructs the new element and destroys the old one in a
single walk. Over a hundred megabytes of `Value`, one pass instead of two is
the larger half of this change: `arrayops` retires 30% fewer instructions,
`listbuild` 26%, `sortnums` 19%.

**Where the standard library permits it, that pass is a `memcpy`.** A `Value`
is *trivially relocatable* — moving its bytes to a new address and not
destroying the source is equivalent to move-constructing and then destroying,
which is exactly what a reallocation does. Nothing in it points at itself:
`i`/`n` are scalars, `IStr` is an index, the payload and cold-block slots are
handles to counted blocks, and `CowStr` holds a short text or a body's handle.

Until 2026-11-07 there was one exception. `CowStr` held a `std::string`, which
is relocatable on libc++ and MSVC, which compute a short string's data address
from `this`, and is **not** on libstdc++, which stores `_M_p` aimed at its own
inline buffer — a bitwise copy there reads the original object's storage and
dangles the moment it is reused. `bitwiseRelocOk()` asks the library at run
time rather than trusting a macro, relocating a short string and checking where
the copy's characters come from, and a library that fails the probe keeps
correct behaviour and loses only the speedup. A `Value` holds no `std::string`
now, so on libstdc++ the probe is more careful than it needs to be. It still
decides the path there; dropping it is a change to measure on Linux, where it
would apply.

That probe is worth a warning to anyone editing `Value`. Its first version
asked whether a short string's `data()` lay *inside* the string object — which
is true everywhere, because that is what the small-buffer optimisation is — so
it answered "not relocatable" on every platform, the `memcpy` path never ran
for a whole day of measurement, and nothing failed, because the fallback is
correct. [tools/reloc-probe.cpp](../../../tools/reloc-probe.cpp) prints which
path is live, and exists precisely because a correctness-preserving fallback
can hide a dead fast path indefinitely.

### Small blocks come off a free list

A `ValueList` is not only a list. It is the argument list of every interpreted
call, and for a one- or two-element list the heap block *is* the cost: 32 ns
against a call that takes about 265. `RVec` keeps a thread-local free list of
blocks for capacities 1 through 4, so allocation is a pop and release is a
push. The one-argument shape drops to 9.5 ns; `fib` retires 8.8% fewer
instructions, a two-argument call loop 13%.

The lists are kept **per exact capacity**, not per rounded-up size class, and
that is the whole design decision. One four-element block for every small
request is simpler and marginally faster on call-shaped work — but a
`ValueList` is also every Array's payload, and a program holding a million
one-element arrays then holds a million four-element blocks: 560–663 MB
against 292–296, paying 22% of its cycles in the extra memory traffic. The two
users of the type want opposite things from a block-sizing policy, and
per-capacity lists are what give both of them what they want.

This is Perl 5's arena-and-free-list discipline (`sv.c`) applied to the one
allocation two separate investigations had already named as the thing to
remove — see Chapter 41.

## Shaped arrays and typed containers

```cpp
// src/Value.h — ValueContExt, the cold block's container part
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

## Packed arrays

A `my int @a` or `my num @a` does not keep a `Value` per element. Its payload
is a `PackedArr`, of kind `PK::Packed`:

```cpp
// src/Value.h — abridged
struct PackedArr {
    bool isNum = false;
    bool selfPacked = false;       // an untyped Array that packed itself
    std::vector<int64_t> w;        // the words; a Num is bit-cast into one
    PRef<ValueList> boxed;         // built once, when something asks
    std::atomic<bool> unpacked{false};
};
```

The seam is `Value::arr()`. It had 3,129 call sites when packing was planned
and none of them changed: on a packed payload `arr()` **unpacks** — it builds the `ValueList`
once, inside the shared body, and from then on the payload answers as a list
for every holder. So a path that knows nothing about packing is correct by
construction and costs one unpack. Only full-width `int` and `num` arrays
pack; sized and unsigned natives stay boxed.

What keeps an array packed is a short list of lanes that read and write the
words directly: construction by list assignment, an element read or store
with an in-range `Int` subscript, `.elems` and `.end`, `push` of plain machine
words, `for @a`, the `+=` family and `++` on an element, `.sum`, `.min` and
`.max`, and the loop kernels. A store of anything a word cannot hold unpacks
and stores as before. A value may be a word only if it differs from
`Value::integer(x)` or `Value::number(x)` in its number alone:
`plainWordValue` checks every flag, tag and name for that.

Measured as each batch landed, when a `Value` was 80 bytes: a million-element
`my int @a = ^N` read back by index went from 83 to 9 bytes an element, and a
million-element `@a[$i] += 1` loop from 0.16 to 0.04 s. A declaration makes an
empty packed body, so an array grown by `push` packs too.

**An untyped Array of Nums packs itself.** Since 2026-11-07 a `my @a = LIST` of
8 or more plain `Num`s, on an Array with no type, default, shape or bound
element, stores their bits in the same body with `selfPacked` set, and
`my @v = @u` copies the words. JavaScriptCore keeps an array of doubles the
same way. The interpreter's loop kernels and the unboxed loop lanes of
`--exe -O` then run on the words, checking each array once on entry. On the
three untyped kernels the change was measured against, the interpreter went
from 100 to 4.9 ns per operation on a dot product, 216 to 9.3 on a matrix
multiply and 272 to 7.1 on a stencil.

The costs are stated with it. Peak memory is not lower, because the words are
built beside the `Value`s they came from. Where no kernel takes the loop, the
tree-walker alone ran the matrix multiply 16% slower. And an array that
unpacks keeps both forms. `RAKUPP_NO_SELFPACK=1` turns self-packing off, and
`RAKUPP_SELFPACK_MIN` sets the fewest elements packed.

**So does one of machine Ints, in the interpreter.** Since 2026-11-10 the same
holds for 8 or more plain Ints that fit 64 bits, and `my @a = LO .. HI` fills
the words from the Range without building a `Value`. The words are a
`my int @a`'s, but its rules are not: a Bool stored stays a Bool, a result
past 64 bits is a big Int, and a gap is holes; each of those unpacks the
array. The loop kernels read, store and step the words, and one compiled
kernel takes the array in either form. A sieve to two million went from
2,396M to 1,395M instructions and from 198 to 107 MB, and a
two-million-element `1..N` summed by index from 72 to 24 MB. An array that is
packed and then asked for its elements, by `.sort` or `.map`, costs 2.5% more
instructions and 5–8% more memory than it did. An Array keeps its `.WHICH`
when its storage changes form. Compiled code packs Nums
only: it has no lane for an Int word yet. `RAKUPP_NO_SELFPACK_INT=1` turns the
Int kind off alone.

An untyped Array of plain `Int`s does not pack itself. That is the next
question the measurements point at, and it is not started.

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

## Freeing cycles

Every payload this chapter has described is reference-counted: an array, a
hash, an object, a Pair's value, a container cell. The count lives in the body
(`src/Ref.h`), and the release that takes it to zero frees the body on the
spot. That is what gives Raku++ its small floor and its pause-free run time,
and it has the classic hole: two bodies that refer to each other keep each
other's count at one after the program has let go of both. Issue #151 was
the plainest form of it, a loop that made a pair of objects pointing at each
other and dropped them. Rakudo held that loop at 143 MB; Raku++ grew by 235 MB
every 20,000 pairs and freed none of them.

The fix is a **backup cycle collector** (`src/Cycle.h`, `src/Cycle.cpp`).
Reference counting stays the primary mechanism and keeps everything it was
already good at. The collector only looks for groups that counting cannot
free, and frees them by clearing their contents, after which ordinary counting
does the rest.

### Why the end of a scope cannot decide

The obvious first idea is to free an object when the variable that named it
goes out of scope. It is wrong whenever the object escaped: pushed onto an
outer array, captured by a closure, stored in another object, returned.
Proving that nothing escaped is escape analysis through `new`, `BUILD`, `is
rw` accessors and every method call, and in Raku the honest answer is "might
escape" almost everywhere.

What the end of a scope *can* supply is a trigger. A group becomes garbage at
the moment its last outside reference goes away, so every release that leaves
a container alive is a moment it may have done so. The container is then a
**candidate**.

### Trial deletion

The algorithm is the synchronous form of Bacon and Rajan's cycle collection
(ECOOP 2001), done without touching the live counts:

1. From the candidates, walk the references that bodies hold.
2. For each body reached, count the references to it from other bodies the
   walk reached.
3. A body whose real count is higher is held from outside the walk. It is
   live, and so is everything it reaches.
4. Whatever is left is referenced only from inside the group: garbage.
5. Run the garbage objects' `DESTROY`s while every link is still intact, then
   count again, because a `DESTROY` may have stored `self` somewhere.
6. Clear the garbage bodies' contents. Their counts fall to zero and the
   normal release path frees them.

The safety argument is one sentence. A body is cleared only when every
reference its count records comes from a body in the garbage set. A reference
the walk cannot see (a C++ temporary, a native callback's capture, another
thread's copy) leaves the count higher than the walk explains, so the body
looks held and nothing is freed. A walk cut short by a budget leaves edges
uncounted, with the same effect. The single error that could free something
live is counting an edge that holds no reference, so `forEachEdge`, the
function that lists a body's references, reports only handles that each own
exactly one count. When a count comes out *below* what the walk explains,
that is proof of such an error, and the collection is abandoned.

Two holders need care. A hash entry handed out as a live Pair keeps the
entries alive through a shared generation, not through a count, so a hash
with outstanding aliases is treated as held from outside. And the `DESTROY`
registry holds one count on every object whose class declares a `DESTROY`;
a flag bit on the body tells the walk to account for it.

### Candidates that hold nothing

The first design held a counted reference to every candidate, and that is the
design to avoid: it delays every free until the next collection, and a
handle closed by its destructor would close later than it does today. The
candidate list holds **raw pointers**. A flag bit in the body's `pflags_`
word marks it as buffered, and the bits above carry its index in the list.
When a buffered body's count reaches zero, the release path removes it
(`cycleForget`) before freeing it. Being a candidate never keeps anything
alive, so a collection can wait for 1,024 candidates without changing what a
program observes.

### Which releases make a candidate

The plan was to note candidates at scope exits only. Measured against eight
ways a program drops a cycle (reassigning an outer variable, replacing an
array element, `:delete`, `@a = ()`, a sunk `N.new.link`, `.shift`,
overwriting an attribute, the result list of a `.map`), scope exits caught
one. Rakudo frees all eight. The drops that matter happen everywhere a
`Value` is released, and the only place that sees all of them is the release
itself.

Hooking every release costs on the hottest path in the engine. A comment in
`Ref.h` records that inlining the destroy call there once cost 2.5% from code
size alone. The answer folds the decision into the count:

```cpp
// src/Ref.h — PayRef
void release() noexcept {
    if (s_) {
        Payload* p = s_.raw();
        std::uint32_t old = p->refs_.fetch_sub(1, std::memory_order_acq_rel);
        if ((std::int32_t)old <= 1) payloadReleaseSlow(p, old);
    }
}
```

An object's body is created with the top bit of its count set
(`kRefTracked`). Its count therefore reads negative, so every release of it
leaves the inline path, exactly as the last release of any body does, and the
inline test is still one compare. The out-of-line half frees on a last
release and otherwise notes a tracked body as a candidate. Every reader of a
count masks the bit. A buffered object has the bit cleared while it waits, so
its later releases stay inline, and gets it back if a collection finds it
alive.

Arrays, hashes, Pairs and cells are not tracked: they are released far more
often than objects, and the cycles they form on their own are rarer. They
are noted where a scope exit drops them. When the drop frees one instead, its
own contents lose a reference too, so the note looks into the dying body, a
bounded number of levels down.

### Scopes are nodes too

A routine that declares `my sub helper`, or keeps a block in a variable,
makes a cycle every time it runs: the frame holds the block, and the block
holds the frame as its closure. Before the collector, every call of such a
routine leaked its whole frame; Rakudo freed 20,000 of 20,000 and Raku++
none. Frames are `Env`s reached through `std::shared_ptr`, not payload
bodies, so the walk treats an `Env` as a node of its own. Its count is the
pointer's `use_count`, and an edge to it is a handle that shares its control
block (an aliasing or non-owning handle is not counted). The walk does not
enter a program's, a module's, a package's or an `EVAL`'s scope: the process
holds those for good, and their slots are most of what a program has.

Breaking such a cycle clears the routine, not the scope. Resetting a garbage
`Callable`'s `closure` and `state` scope releases the frame, and the frame's
own count takes it from there. A frame is not cleared in place: interpreter
code holds raw pointers into frames' slots, and a frame that dies by its
count dies the way it always has.

The trigger here is a frame that a closure keeps alive when its routine
returns or its loop iteration ends: its variables become candidates. A kept
frame can hold any amount of memory, so these candidates weigh 64 toward the
1,024 that start a collection. Counted as one each, 300 calls of a routine
keeping a block beside a 1 MB string peaked at 294 MB; weighted, at 18 MB.

### When it runs

A collection reads counts while it walks, and parallel mode has threads
changing them. It therefore runs only at a **quiet point**: on the main
thread, with no worker live, at the end of a scope (a pooled frame returned,
a loop iteration's scope cleared, a flat loop body's iteration) or at the top
of a compiled loop iteration. While workers run, candidates wait, and the
threshold doubles each time a collection is put off. Candidates may be noted
on any thread; the list sits behind a spinlock that costs an exchange and a
store when uncontended.

`DESTROY` runs from the collection, at those same points. That is no new
licence: the registry sweep already runs `DESTROY` when constructing an
object pushes the registry past its threshold. A collection never starts
while an exception unwinds.

### Bounding the cost

The danger is a hot routine whose local variable names the root of a large
structure that is cyclic but live, such as a tree with parent links. Every
return would walk the tree to find it held. Four bounds keep that in check:

- **A node budget per candidate.** A walk that would add more than 1,024
  nodes stops adding, and pins the nodes it has not looked inside. A pinned
  node counts as held from outside.
- **A read budget per walk.** The node cap alone was not enough: a walk read
  every element of a node before checking the cap. Running `t/scaling` with a
  collection at every scope exit made two of its shapes quadratic, because a
  growing attribute array was read whole each time. A walk now stops after
  16,384 values, and the node it stopped inside is pinned.
- **Shared work within a collection.** Candidates are walked one at a time,
  but a node an earlier walk reached is not walked again. A collection that
  finds nothing doubles the threshold for the next, up to 2^20; one that finds
  garbage resets it.
- **No node for nothing.** An empty array or hash is not given a node at all,
  since it holds nothing and so is on no cycle. Together with an
  open-addressing index in place of `std::unordered_map` and one reused
  queue, that took the strategy's worst measured case (200,000 children each
  pointing back at a live parent) from +8.4% to +2.9% instructions.

One more saving is static. A class whose attributes can only hold values (a
native, `Int`, `Str`, `Num`, `Rat`, `Bool`, …), counting its parents' and
roles' attributes, cannot be on a cycle, and its objects are not tracked. The
verdict is cached on the `ClassInfo` the first time one of its objects is
released, a point every way of making an object passes through. It is a
heuristic, and it errs only toward a leak: a value type can be subclassed
(`class A is Int { has Int $.next }`), and a cycle made only of such objects
has no tracked member.

### How it was checked

`t/leak/` holds one file per shape of cyclic garbage and per shape of live
cycle that must survive, each counting `DESTROY` calls. It passes under
Rakudo as well. Every file also runs with `RAKUPP_CYCLE_POISON=1`: instead of
clearing a collected object, the collector gives it a class whose every
method dies with `CYCLE-COLLECTED[…]` in the message. `DESTROY` still runs,
so every count still holds, and any later use of a collected object fails
loudly. `RAKUPP_CYCLE_TRIGGER=1` makes a collection run at nearly every scope
exit, which matters because at the normal threshold most Roast files are too
short to collect at all. Roast under that setting, with and without poison,
passed 220,845 of 220,845 tests, and the threaded paths ran clean under
ThreadSanitizer.

On the benchmark kernels the collector costs under 0.6% on twenty of
twenty-two. The object-construction kernel pays 1.3–1.5%: each of its
200,000 objects goes on the candidate list once and comes off when it is
freed.

## Honest limitations

- **Some cycles still leak.** A cycle larger than one walk's budget, and one
  that runs through a promise, a channel, a lazy generator or a native
  callback, are not freed. A program that always has a worker thread running
  collects only at the moments it has none.
- **A bound scalar has two forms.** A lexical bound to a lexical shares a
  cell and reads at full speed. An attribute, the topic and a typed variable's
  alias still read through a `Proxy` over the cell, and a `Proxy` with a
  `FETCH` of its own costs a call per read.
- **`is rw` write-back is expression-based.** Where the argument expression is
  not available or not re-resolvable, the write-back does not happen.
- **The one-level copy rule** matches Rakudo, but it surprises people
  regularly, and no amount of documentation seems to fix that.
