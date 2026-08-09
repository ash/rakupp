# The Extension ABI

Raku++ walks an AST. It does not JIT, so a tight loop written in Raku costs it
roughly an order of magnitude more than it costs Rakudo — and some jobs, like
tokenizing a 300 KB JSON document, are nothing *but* a tight loop.

An extension module is the escape hatch. A distribution ships C source alongside
its Raku, the build step compiles it against a published ABI at install time,
and the compiled routines become ordinary Raku subs. Perl calls this XS; Python
calls it a C extension. The point is the same, and so is the most important
property: **the module versions independently of the compiler.** A faster JSON
parser should not require a new release of the language implementation.

## Extension or NativeCall?

They solve different problems and the wrong choice is painful.

| | NativeCall | extension module |
|---|---|---|
| calls | an **existing** C library you did not write | C **you** write for this module |
| data | C types: ints, nums, pointers, structs | Raku values: Hash, Array, Int, Rat, Str |
| build | none; the shared object already exists | compiled at install time |
| use when | wrapping libcurl, sqlite3, libgit2 | a hot loop in your own module is the bottleneck |

If you need to return a *Hash of Rats*, NativeCall cannot express it and an
extension can. If you need to call `curl_easy_perform`, use NativeCall.

## The one rule

**An extension never sees `Value`.** Every Raku value crosses the boundary as an
opaque handle.

```c
/* src/rakupp_ext.h */
typedef struct RkValueOpaque* RkValue;
typedef struct RkCtxOpaque*   RkCtx;
```

This is not fastidiousness. `sizeof(Value)` moved from 392 to 376 to 344 bytes in a
single afternoon of ordinary optimisation work, and the representation plan
intends roughly 204 next. An ABI that exposed the struct would have to freeze
the interpreter's internals forever, or silently miscompile every extension
built against an older header — the failure mode where a module reads a `Str`
out of a field that is now an `Int` and nothing crashes until much later.

Handles cost an indirection. They buy the ability to install a module today and
keep it working across compiler releases, which is the entire point.

The corollary: **this is plain C.** No C++ types cross the boundary, no
exceptions cross it, and nothing received needs freeing. A binding from Rust or
Zig would need nothing beyond the header.

## Hello, world

```c
#include <rakupp/rakupp_ext.h>

static RkValue answer(RkCtx c) { return rk_int(c, 42); }

static const RkSubDef subs[] = { {"answer", answer}, {0, 0} };
static const RkModule mod = { RAKUPP_EXT_ABI, "Hello::Ext", subs };

RAKUPP_EXT_EXPORT const RkModule* rakupp_ext_init(unsigned host_abi) {
    return host_abi == RAKUPP_EXT_ABI ? &mod : 0;
}
```

```raku
use Rakupp::Ext;
rakupp-ext-load('./libhello.dylib');
say answer();      # 42
```

`rakupp-ext-load` installs each of the extension's subs into the **calling
scope**, so inside a module they land exactly where an `our sub` would, and that
module's `is export` carries them onward.

## The value vocabulary

```c
/* constructing */
RkValue rk_any  (RkCtx c);
RkValue rk_bool (RkCtx c, int truthy);
RkValue rk_int  (RkCtx c, long long v);
RkValue rk_int_s(RkCtx c, const char* decimal);       /* arbitrary precision */
RkValue rk_num  (RkCtx c, double v);
RkValue rk_rat_s(RkCtx c, const char* n, const char* d); /* a Rat, normalised */
RkValue rk_str  (RkCtx c, const char* utf8, size_t len); /* copied */
RkValue rk_array(RkCtx c);   void rk_push(RkCtx c, RkValue a, RkValue v);
RkValue rk_hash (RkCtx c);   void rk_set (RkCtx c, RkValue h,
                                          const char* k, size_t kl, RkValue v);
void    rk_list (RkCtx c, RkValue array);   /* mark it a List, not an Array */
void    rk_map  (RkCtx c, RkValue hash);    /* mark it a Map,  not a Hash  */
```

`rk_int_s` and `rk_rat_s` take **decimal strings** rather than integers on
purpose: Raku's `Int` has no width, and a document may carry a 40-digit number
that no C integer type can hold. `rk_rat_s("1", "7")` gives exactly one seventh,
not `0.14285714285714285`.

```c
/* inspecting */
RkType      rk_type   (RkCtx c, RkValue v);
long long   rk_int_get(RkCtx c, RkValue v);
const char* rk_str_get(RkCtx c, RkValue v, size_t* len);   /* borrowed */
size_t      rk_elems  (RkCtx c, RkValue v);
RkValue     rk_at_pos (RkCtx c, RkValue array, size_t i);
const char* rk_key_at (RkCtx c, RkValue hash, size_t i, size_t* keylen);
RkValue     rk_val_at (RkCtx c, RkValue hash, size_t i);
```

`RkType` is deliberately coarse:

```c
typedef enum {
    RK_ANY = 0, RK_BOOL, RK_INT, RK_NUM, RK_RAT, RK_STR,
    RK_ARRAY, RK_HASH, RK_OTHER
} RkType;
```

It describes **what an extension can do with a value**, not where the value sits
in Raku's type hierarchy. Anything the ABI has no vocabulary for arrives as
`RK_OTHER`, and the advice is to stringify it. A coarse enum is a deliberate
choice: a fine one would have to track the language's type system, which is
exactly the coupling this design exists to avoid.

```c
/* arguments */
size_t  rk_argc (RkCtx c);                    /* positionals only */
RkValue rk_arg  (RkCtx c, size_t i);          /* NULL if absent */
RkValue rk_named(RkCtx c, const char* name);  /* NULL if not passed */
```

`rk_named` returning `NULL` means *not passed*, which is distinct from being
passed an undefined value — so `:immutable` and `:!immutable` remain
distinguishable.

## Lifetime: handles belong to the call

Everything an extension creates is allocated in the context's **arena** and
released when the sub returns; the single value it returns is copied out first.

So an extension never frees anything, cannot leak, and — the rule that has to be
stated because it is invisible until it bites — **a handle stored in a global and
used on the next call is dangling.** There is no reference counting to save you.
State that must persist across calls belongs in C, not in handles.

Borrowed pointers from `rk_str_get` and `rk_key_at` are valid until the call
returns, which is long enough to copy out of them and no longer.

The arena is what makes the boundary safe in both directions: the host cannot be
made to leak by a careless extension, and an extension cannot hold a reference
into a value model that is free to change underneath it.

## Errors

```c
void rk_die(RkCtx c, const char* message);
```

Call it and **return `NULL`**. The host raises a Raku exception at the call site,
catchable with `try`/`CATCH` like any other. A second `rk_die` keeps the first
message, so an error deep in a recursive parse survives the unwind.

Never throw a C++ exception across the boundary. The host did not compile the
extension's code and cannot catch it.

## Versioning

```c
#define RAKUPP_EXT_ABI 1u
typedef const RkModule* (*RkInitFn)(unsigned host_abi);
```

One integer, bumped whenever the meaning or order of anything in the header
changes. The host looks up one symbol, `rakupp_ext_init`, and passes its own
version. **Return `NULL` if you do not recognise it** rather than guessing; the
host then reports a clean mismatch instead of calling through a wrong-shaped
table.

All three failure modes report themselves and none corrupt:

```
'…/thing.dylib' was built for a different extension ABI (host is 1)
cannot load extension '/nope.dylib'
'/bin/ls' is not a rakupp extension (no rakupp_ext_init)
```

The host side is one function:

```cpp
// src/Interpreter.h
Value extLoadModule(const std::string& path, std::string& errOut,
                    std::vector<std::pair<std::string, Value>>& subsOut);
```

which `dlopen`s the path, checks the ABI, and hands back the subs it declares.

## Writing a module that also runs on Rakudo

A module that *only* works on Raku++ is a poor citizen. The pattern that works
everywhere is a compiled fast path with a pure-Raku fallback, chosen at load
time — and the load-bearing detail is how the loader is reached:

```raku
my &ext-load = try &::('rakupp-ext-load');
```

**Why not call `rakupp-ext-load(...)` directly?** Because Raku resolves sub
names at *compile* time. On Rakudo the name does not exist, so the file fails to
compile and the fallback never gets the chance to run.

`&::('...')` is a **symbolic lookup**: the name is a string, resolved at run
time. The line is valid Raku either way, so the *same source file* compiles on
both engines and simply finds nothing on Rakudo.

```raku
unit module My::Thing;

my &ext-load = try &::('rakupp-ext-load');
my &fast;

sub load-native(--> Bool) {
    return False unless &ext-load;
    for libraries() -> $lib {
        next unless $lib.IO.e;
        next unless try ext-load($lib.Str);
        &fast = try &::('my-thing-native');
        return True if &fast;
    }
    False
}
my Bool $native = load-native();

our sub do-the-thing($x) is export {
    $native ?? fast($x) !! pure-raku-version($x)
}
```

`use Rakupp::Ext` is accepted too and is the discoverable spelling for code that
is Raku++-only by design — but it is a `use` statement, so writing it makes the
file uncompilable on Rakudo.

Note that `&fast` and `&ext-load` keep their sigil at every mention. Writing
`fast.defined` would *call* `fast` and ask the result, producing a baffling
`No such method 'CALL-ME'`.

## Packaging

```
My-Thing/
├── META6.json          "resources": [ "libraries/thing" ]
├── Build.rakumod       compiles src/ at install time
├── src/thing.c
├── lib/My/Thing.rakumod
└── resources/libraries/
```

Raku maps the logical resource name to the platform spelling, so **the build
must emit `libthing.dylib`, not `thing.dylib`**, or `%?RESOURCES` will not find
it.

`Build.rakumod` should **never abort an install**. If the compiler is missing,
or the headers are not there, or the engine is Rakudo, warn and return true: the
module then runs on its fallback. A build hook that fails turns an optimisation
into a hard dependency.

Find the library through `%?RESOURCES`, falling back to a working-directory
path. **Do not derive it from `$?FILE`** — under Raku++ a module's `$?FILE` is
the *main program's* path, not the module's, so anything computed from it lands
somewhere unrelated. That is a recorded bug, not a design.

## The naming convention

A module whose behaviour depends on Raku++ should say so in its name.
`Rakupp::JSON` tells a reader at the point of `use` that this is not a portable
choice — better than a footnote in a README, and better than discovering it when
the deploy target turns out to be Rakudo.

Two rules follow. A `Rakupp::` module is still a normal distribution: installed
by zef, carrying a `META6.json`, degrading gracefully elsewhere. And **the
compiler must not special-case a module name.** An early draft of `Rakupp::JSON`
was built into the interpreter, and `use Rakupp::JSON` intercepted the name
before module loading — so the real distribution's tests silently exercised the
built-in copy instead of the extension they were written for. If a name can be a
module, it must *only* be a module.

## Current limits

- **There is a per-value cost.** Each value crosses as an arena-allocated handle
  and is then copied into its container — roughly one extra `Value` copy per
  node. Measured on `Rakupp::JSON`: 5.7 ms against 2.7 ms for the same parser
  compiled into the interpreter. Still 6.6 times faster than Rakudo on that
  workload, but it means an extension is worth it for **bulk** work, not for a
  routine called once with two integers.
- **Hash iteration is index-based**, so `rk_key_at` is O(i) and walking a whole
  hash is quadratic. A cursor is the obvious version-2 addition, and it is why
  `Rakupp::JSON` implements only the parser natively and leaves serialisation in
  Raku.
- **No `--exe` bundling.** A program using a native extension cannot be compiled
  into a standalone binary; the shared library is loaded at run time and is not
  part of the module graph the bundler walks.
- **One context per call, single-threaded within it.** Handles are not safe to
  pass between threads.

## The other direction

Extensions are **native code called from Raku**. Embedding is **Raku called from
native code**. Same boundary, opposite direction — and the observation that
shapes the current plan is that the harder half is already built:

> The extension ABI has the value vocabulary, the opaque-handle discipline,
> version negotiation, an error model, and the rule that makes all of it
> survivable.

So an embedding API does not get a second value vocabulary. `RkValue` is the
value type, `rk_int`/`rk_str`/`rk_hash`/`rk_at_pos` are the accessors, and an
earlier proposal for a separate opaque type with its own free function was
retracted on exactly that ground.

That is worth stating as a design principle rather than a project note: **when
you publish a boundary, publish one.** Two vocabularies for the same values is
twice the surface to keep stable, and the whole reason for the handle discipline
was to have as little of that surface as possible.
