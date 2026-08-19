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
/* include/rakupp/rakupp_ext.h */
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
    return host_abi >= RAKUPP_EXT_ABI ? &mod : 0;   // see Versioning, below
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

## Both halves of the linking bargain

An extension resolves the `rk_*` symbols from the **host executable** at load
time, exactly as a Python C extension resolves `Py_*`. Undefined symbols at
*link* time are therefore expected, and must be permitted:

| platform | flag |
|---|---|
| macOS | `-Wl,-undefined,dynamic_lookup` |
| Linux / BSD | none — ELF resolves lazily by default |
| Windows | link against the import library, `lib/rakupp.lib` |

That is the extension's half. The host's half is to actually **publish** those
symbols, and for a long time it did not. This was checked rather than assumed,
and the answer was worse than expected:

> **Extensions had only ever worked on macOS.** A Mach-O executable exports its
> globals by default. A plain ELF executable keeps its symbols out of `.dynsym`,
> so on Linux an extension's first `rk_*` call had always died with an
> undefined-symbol error. On Windows, the documentation told authors to link
> against an import library the build never produced.

Nobody had reported it, and the reason is the very pattern this chapter
recommends. A well-written module falls back quietly — `JSON::Native`'s whole
design is to try the extension and drop back to Raku if it does not load. So on
Linux it loaded nothing, ran its fallback, and looked like it was working. Users
would have seen the pure-Raku timings and assumed that was the number.

The fix is deliberately narrow: `-Wl,--dynamic-list=…` puts the `rk_*` glob in
the executable's dynamic symbol table **and nothing else**, where `-rdynamic`
would have exported every C++ symbol in the interpreter as an accidental ABI
promise. On Windows, `RK_API`'s `dllexport` plus `ENABLE_EXPORTS` produce the
import library the docs already described.

The lesson generalises past this bug: **a graceful fallback hides the failure it
was written to survive.** A module that degrades quietly needs some way to say
out loud which path it took, and a build that publishes an ABI needs a test that
the symbols are really there rather than a belief that they are.

### `librakupp` and the export discipline

The same surface is exported by a shared library, built with
`-DRAKUPP_BUILD_SHARED=ON`: position-independent, hidden visibility, versioned,
installed beside the static archive. It is off by default, because it recompiles
every runtime source and the plain CLI neither links nor loads it.

The export list is **the header itself** rather than a second file to keep in
sync. Each entry point carries an `RK_API` marker, the library compiles with
`-fvisibility=hidden`, and what is marked is exactly what is exported — verified
with `nm` rather than assumed: every `rk_*` symbol present, and no
`namespace rakupp` symbol leaked.

That is what keeps the boundary honest in the direction that matters most. A C++
symbol accidentally exported from a shared library is an ABI promise nobody
decided to make, and the only defence is to keep the visible set small and
declare it in one place.

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

## Calling back into Raku

ABI 2 added the other direction: an extension can invoke Raku.

```c
RkValue rk_call      (RkCtx c, const char* name,
                      const RkValue* argv, size_t argc);
RkValue rk_call_value(RkCtx c, RkValue code,
                      const RkValue* argv, size_t argc);
int     rk_can       (RkCtx c, const char* name);
```

`rk_call` resolves a name **the way the extension's own sub would at its call
site**: the lexical scope that invoked it, then outward to global. So an
extension loaded by a module reaches that module's subs — which is what lets a
native fast path delegate the cases it does not handle back to the Raku it
replaced. `rk_call_value` is the same for a `Code` value that was handed in, a
callback rather than a name. `rk_can` asks first, so an extension can use an
optional hook only when the caller supplied one.

The failure model is the interesting part, and it follows from the one rule at
the top of this chapter. **A Raku exception thrown inside `rk_call` does not
unwind through the extension's frames.** It cannot — the host did not compile
them. Instead the call returns `NULL` and leaves a *pending error*:

```c
void        rk_die        (RkCtx c, const char* message);
const char* rk_error      (RkCtx c);
void        rk_clear_error(RkCtx c);
```

Return `NULL` from the sub afterwards and the original Raku exception is raised
at the call site with its own type intact. Or call `rk_clear_error` and carry
on, which is how an extension treats a failed callback as one case among
several rather than as a fatality.

That shape — a return value plus a pending error, never a longjmp through
foreign frames — is the same discipline `errno` and OpenGL use, and it is
forced here by the same thing that forces the handles: neither side compiled
the other.

## Rooted handles, and the one way to leak

```c
RkValue rk_root  (RkCtx c, RkValue v);
void    rk_unroot(RkCtx c, RkValue rooted);
```

`rk_root` lifts a value out of the arena so it can outlive the call. It is
**for hosts embedding rakupp, not for extensions**, and it is the only thing in
the header you can leak — which is precisely why the arena, where you cannot, is
what an extension sees by default.

The guidance in the header is blunt about it: if you think you want a rooted
handle, you almost certainly want ordinary C state instead — a compiled
pattern, a parsed schema, a cache. Keep those in C.

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
#define RAKUPP_EXT_ABI 2u
typedef const RkModule* (*RkInitFn)(unsigned host_abi);
```

One integer, bumped when the header gains capability an extension cannot detect
any other way, or when the meaning or order of anything in it changes. Version 1
was the original surface — construct, inspect, arguments, `rk_die`. Version 2
added re-entering Raku, the pending-error calls, and rooted handles.

The host looks up one symbol, `rakupp_ext_init`, and passes its own version.
**Return `NULL` if you cannot serve it** rather than guessing; the host then
reports a clean mismatch instead of calling through a wrong-shaped table.

### The handshake, and why it is a downgrade

Bumping the number would have broken every extension already in the wild,
because ABI-1 extensions were all written with the equality test the original
documentation showed:

```c
return host_abi == RAKUPP_EXT_ABI ? &mod : 0;
```

A host at 2 calling that gets `NULL`. So **the host retries downward**: `init(2)`
returning null is followed by `init(1)`, and the old extension answers. Nothing
that worked stops working, and it was proven rather than argued — an extension
built against the previous day's header was loaded, unchanged, into the new
host.

An extension built against the *new* header should use `>=`, which states what
it actually needs: a host at least this new. And a module whose `abi_version`
is **newer** than the host's is refused with a sentence saying so, because the
host cannot provide what it has not got.

The failure this avoids is undefined symbols. An extension calling `rk_call` on
a host that predates it would resolve nothing and abort at the first call under
lazy binding; a version check turns a crash into a diagnostic.

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
`JSON::Native` tells a reader at the point of `use` that this is not a portable
choice — better than a footnote in a README, and better than discovering it when
the deploy target turns out to be Rakudo.

Two rules follow. A `Rakupp::` module is still a normal distribution: installed
by zef, carrying a `META6.json`, degrading gracefully elsewhere. And **the
compiler must not special-case a module name.** An early draft of `JSON::Native`
was built into the interpreter, and `use JSON::Native` intercepted the name
before module loading — so the real distribution's tests silently exercised the
built-in copy instead of the extension they were written for. If a name can be a
module, it must *only* be a module.

## Current limits

- **There is a per-value cost.** Each value crosses as an arena-allocated handle
  and is then copied into its container — roughly one extra `Value` copy per
  node. Measured on `JSON::Native`: 5.7 ms against 2.7 ms for the same parser
  compiled into the interpreter. Still 6.6 times faster than Rakudo on that
  workload, but it means an extension is worth it for **bulk** work, not for a
  routine called once with two integers.
- **Hash iteration is still index-based, but no longer quadratic.** The host
  remembers where it was between `rk_key_at` and `rk_val_at` calls, so a
  sequential walk costs O(1) per key. Jumping between two hashes, or writing to
  one while walking it, falls back to O(i) positioning — correct either way,
  just slower.
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

Two pieces of it are already in place. `librakupp` is the shared library an
embedding host links against, and `rk_root` is how such a host keeps a value
past the call that produced it — the two things an extension never needs and an
embedder cannot do without.

## A footnote on guessing

The version-2 work was planned as one feature and turned out to be two, and the
way that came out is worth recording.

The plan asserted that `JSON::Native`'s `to-json` was still written in Raku
because the ABI had no way to call back into Raku, and that `rk_call` would fix
it. **The module's own documentation said otherwise**: the real obstacle was
that hash access was index-based, `std::advance` from the beginning on every
call, so serialising a hash was quadratic in its size.

Both were worth fixing and neither was the other. But the plan had been
repeating a guess for as long as it existed, while the answer was written down
in the thing it was guessing about.

It is the same failure as the benchmark in Chapter 28 that measured a rename
instead of the change a rename enables, and the same remedy: **before designing
around a cause, check whether the code already says what the cause is.**
