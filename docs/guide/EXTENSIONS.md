# Native extension modules — the C ABI, and the `Rakupp::` convention

Raku++ walks an AST. It does not JIT, so a tight loop written in Raku costs it
roughly an order of magnitude more than it costs Rakudo — and some jobs, like
tokenizing a 300 KB JSON document, are nothing *but* a tight loop.

An extension module is the escape hatch: a distribution ships C source alongside
its Raku, the build step compiles it against this ABI at install time, and the
compiled routines become ordinary Raku subs. Perl calls this XS; Python calls it
a C extension. The point is the same, and so is the most important property —
**the module versions independently of the compiler.** A faster JSON parser
should not require a new release of Raku++.

- **Reference implementation:** `JSON::Native` in
  [github.com/ash/raku-modules](https://github.com/ash/raku-modules) — 5.7 ms on
  a 278 KB document where the same module's Raku fallback takes ~440 ms.
- **The header:** [`src/rakupp_ext.h`](../../src/rakupp_ext.h), installed to
  `<prefix>/include/rakupp/rakupp_ext.h`.

## Extension or NativeCall?

They solve different problems, and the wrong choice is painful.

| | NativeCall ([FFI.md](FFI.md)) | extension module |
|---|---|---|
| calls | an **existing** C library you did not write | C **you** write for this module |
| data | C types — ints, nums, pointers, structs | Raku values — Hash, Array, Int, Rat, Str |
| build | none; the `.so` already exists | compiled at install time |
| use it when | wrapping libcurl, sqlite3, libgit2 | a hot loop in your own module is the bottleneck |

If you need to *return a Hash of Rats*, NativeCall cannot express it and an
extension can. If you need to call `curl_easy_perform`, use NativeCall.

## The one rule

**An extension never sees `Value`.** Every Raku value crosses the boundary as an
opaque `RkValue` handle.

This is not fastidiousness. `sizeof(Value)` moved 392 → 376 → 344 bytes in a
single afternoon of ordinary optimisation work, and it is expected to move
again. An ABI that exposed the struct would have to freeze the interpreter's
internals forever, or silently miscompile every extension built against an older
header — the failure mode where your module reads a `Str` out of a field that is
now an `Int` and nothing crashes until much later.

Handles cost an indirection. They buy the ability to install your module today
and keep it working across compiler releases, which is the entire point.

The corollary: this is plain C. No C++ types cross the boundary, no exceptions
cross it, and nothing you receive needs freeing.

**Why C, when the interpreter is C++?** Because C++ has no stable binary
interface: `std::string` alone has a different layout under libstdc++ and
libc++, and the Linux release links `libstdc++` statically — so a C++ type
crossing this boundary would be crossing between two different copies of the
standard library. In C your extension builds with whatever compiler you have
and loads into whatever rakupp the user installed. You do not give up C++ to
write one, either: compile your extension as C++ if you like, and keep the
`extern "C"` entry points. The longer version, including what the choice costs,
is in [ABI-PLAN.md](../dev/plans/ABI-PLAN.md#why-c-and-not-c).

## Hello, world

`hello.c`:

```c
#include <rakupp/rakupp_ext.h>

static RkValue answer(RkCtx c) { return rk_int(c, 42); }

static const RkSubDef subs[] = { {"answer", answer}, {0, 0} };
static const RkModule mod = { RAKUPP_EXT_ABI, "Hello::Ext", subs };

RAKUPP_EXT_EXPORT const RkModule* rakupp_ext_init(unsigned host_abi) {
    return host_abi == RAKUPP_EXT_ABI ? &mod : 0;
}
```

Build it (see [Building](#building) for why `dynamic_lookup` is needed):

```bash
# Headers sit beside the binary: <prefix>/bin/rakupp -> <prefix>/include/rakupp/
INC=$(dirname "$(dirname "$(command -v rakupp)")")/include

cc -shared -fPIC -I"$INC" -Wl,-undefined,dynamic_lookup \
   hello.c -o libhello.dylib
```

From a git checkout there is no `<prefix>`; point at the source tree, which has
the header at `src/rakupp_ext.h`:

```bash
mkdir -p /tmp/inc/rakupp && cp /path/to/raku++/src/rakupp_ext.h /tmp/inc/rakupp/
cc -shared -fPIC -I/tmp/inc -Wl,-undefined,dynamic_lookup hello.c -o libhello.dylib
```

Use it:

```raku
use Rakupp::Ext;
rakupp-ext-load('./libhello.dylib');
say answer();      # 42
```

`rakupp-ext-load` installs each of the extension's subs into the **calling
scope**, so inside a module they land exactly where an `our sub` would and that
module's `is export` carries them onward.

## The ABI

Everything below is declared in `rakupp_ext.h`. `RkCtx` is the per-call context;
you receive it and pass it back to every call.

### Constructing values

```c
RkValue rk_any  (RkCtx c);                       /* Any — JSON's null */
RkValue rk_bool (RkCtx c, int truthy);
RkValue rk_int  (RkCtx c, long long v);
RkValue rk_int_s(RkCtx c, const char* decimal);  /* arbitrary precision */
RkValue rk_num  (RkCtx c, double v);
RkValue rk_rat_s(RkCtx c, const char* n, const char* d);   /* a Rat, normalised */
RkValue rk_str  (RkCtx c, const char* utf8, size_t len);   /* copied */

RkValue rk_array(RkCtx c);
void    rk_push (RkCtx c, RkValue array, RkValue v);
void    rk_list (RkCtx c, RkValue array);        /* mark it a List, not an Array */

RkValue rk_hash (RkCtx c);
void    rk_set  (RkCtx c, RkValue hash, const char* key, size_t keylen, RkValue v);
void    rk_map  (RkCtx c, RkValue hash);         /* mark it a Map, not a Hash */
```

`rk_int_s` and `rk_rat_s` take **decimal strings** rather than integers on
purpose: Raku's `Int` has no width, and a document may carry a 40-digit number
that no C integer type can hold. `rk_rat_s("1", "7")` gives you exactly `1/7`,
not `0.14285714285714285`.

### Inspecting values

```c
RkType      rk_type   (RkCtx c, RkValue v);   /* RK_INT, RK_STR, RK_HASH, … */
int         rk_truthy (RkCtx c, RkValue v);
long long   rk_int_get(RkCtx c, RkValue v);
double      rk_num_get(RkCtx c, RkValue v);
const char* rk_str_get(RkCtx c, RkValue v, size_t* len);   /* borrowed */

size_t      rk_elems  (RkCtx c, RkValue v);                /* array or hash */
RkValue     rk_at_pos (RkCtx c, RkValue array, size_t i);
const char* rk_key_at (RkCtx c, RkValue hash, size_t i, size_t* keylen);
RkValue     rk_val_at (RkCtx c, RkValue hash, size_t i);
```

`rk_str_get` on a non-`Str` gives you its `Str` coercion, which is usually what a
serializer wants.

`RkType` is deliberately coarse — it tells you what you can *do* with a value,
not where it sits in Raku's type hierarchy. Anything the ABI has no vocabulary
for arrives as `RK_OTHER`; stringify it.

### Arguments

```c
size_t  rk_argc (RkCtx c);                    /* positionals only */
RkValue rk_arg  (RkCtx c, size_t i);          /* NULL if absent */
RkValue rk_named(RkCtx c, const char* name);  /* NULL if not passed */
```

`rk_named` returning `NULL` means *not passed*, which is distinct from being
passed an undefined value — so `:immutable` and `:!immutable` are
distinguishable.

### Calling back into Raku (ABI 2)

```c
RkValue rk_call      (RkCtx c, const char* name, const RkValue* argv, size_t argc);
RkValue rk_call_value(RkCtx c, RkValue code,     const RkValue* argv, size_t argc);
int     rk_can       (RkCtx c, const char* name);
```

`rk_call` resolves a name **the way your sub's own call site would**: the
lexical scope that invoked you, then outward to GLOBAL. So an extension loaded
by a module reaches that module's routines, which is what lets a native fast
path hand back the cases it does not want to implement:

```c
static RkValue render(RkCtx c) {
    RkValue v = rk_arg(c, 0);
    if (rk_type(c, v) == RK_OTHER) {         /* not ours — ask Raku */
        RkValue r = rk_call(c, "render-fallback", &v, 1);
        if (!r) return 0;                     /* let the failure through */
        return r;
    }
    …
}
```

`rk_call_value` is the same for a Code value you were *handed* — a callback
argument, `&comparator` and its kind. Positional arguments only; named ones are
not expressible yet.

### Failing

```c
void        rk_die        (RkCtx c, const char* message);
const char* rk_error      (RkCtx c);          /* ABI 2 */
void        rk_clear_error(RkCtx c);          /* ABI 2 */
```

Call `rk_die` and **return `NULL`**. The host raises a Raku exception at the call
site, catchable with `try`/`CATCH` like any other. A second `rk_die` keeps the
first message, so an error deep in a recursive parse survives the unwind.

Never throw a C++ exception across the boundary. The host did not compile your
code and cannot catch it — **and that is exactly why a Raku exception thrown
inside `rk_call` does not unwind through your frames either.** It comes back as
`NULL` with a pending error, and you choose:

- **return `NULL`** and the original exception is re-raised at your call site
  *with its own type* — `CATCH { when X::Whatever }` still works, as though C
  had never been in the way;
- or call `rk_clear_error` and carry on, having read `rk_error` for the message.

### Values that outlive the call (ABI 2)

```c
RkValue rk_root  (RkCtx c, RkValue v);
void    rk_unroot(RkCtx c, RkValue rooted);
```

**For hosts embedding rakupp, not for extensions.** This is the one place the
ABI lets you leak, which is precisely why the arena — where you cannot — is what
an extension sees by default. If you think you want a rooted handle, you almost
certainly want ordinary C state instead: a compiled pattern, a parsed schema, a
cache. Keep those in C.

### Registration

```c
typedef RkValue (*RkSubFn)(RkCtx c);
typedef struct { const char* name; RkSubFn fn; } RkSubDef;
typedef struct {
    unsigned        abi_version;   /* must be RAKUPP_EXT_ABI */
    const char*     module_name;   /* for diagnostics */
    const RkSubDef* subs;          /* terminated by {NULL, NULL} */
} RkModule;
```

The host looks up one symbol, `rakupp_ext_init`, and passes its own ABI version.
**Return `NULL` if you do not recognise it** rather than guessing — the host then
reports a clean mismatch instead of calling through a wrong-shaped table.

## Lifetime

Handles belong to **the call**, not to your extension.

Everything you create is allocated in the `RkCtx`'s arena and released when your
sub returns; the single value you return is copied out first. So:

- you never free anything;
- you cannot leak;
- **a handle stored in a global and used on the next call is dangling.** There is
  no reference counting to save you. If you need state across calls, keep it in
  C — not in handles.

Borrowed pointers from `rk_str_get` and `rk_key_at` are valid until the call
returns, which is long enough to copy out of them and no longer.

## Building

An extension resolves the `rk_*` symbols from the **host executable** at load
time, exactly as a Python C extension resolves `Py_*`. Undefined symbols at
*link* time are therefore expected and must be permitted:

| platform | flag |
|---|---|
| macOS | `-Wl,-undefined,dynamic_lookup` |
| Linux / BSD | none — ELF resolves lazily by default |
| Windows | link against the import library (`lib/rakupp.lib`) |

The host holds up its half of that bargain by carrying the `rk_*` surface in
its dynamic symbol table — Mach-O executables export their globals by default,
ELF ones need `-Wl,--dynamic-list=src/rakupp_ext.dynlist` (part of the build
since 2026-08-10), and on Windows the `RK_API` dllexports plus `ENABLE_EXPORTS`
produce the import library above. A rakupp built before that date loads
extensions only on macOS: on Linux the first `rk_*` call dies with an
undefined-symbol error, because a plain ELF executable keeps its symbols out
of `.dynsym`.

The same surface is exported by `librakupp`, the shared library built with
`-DRAKUPP_BUILD_SHARED=ON` — so a process that *embeds* rakupp can host
extensions too. (An ELF host must load it with `RTLD_GLOBAL` for the
extension's lookup to see it; see
[ABI-PLAN.md](../dev/plans/ABI-PLAN.md).)

Headers come from `cmake --install`, which lays out
`<prefix>/{bin,lib,include/rakupp}`. From a git checkout, point at `src/`
instead. `JSON::Native`'s `Build.rakumod` looks in both, and honours
`RAKUPP_SRC` for the checkout case.

## Writing a portable module

A module that *only* works on Raku++ is a poor citizen. The pattern that works
everywhere: a compiled fast path with a pure-Raku fallback, chosen at load time.

The load-bearing detail is how you reach the loader:

```raku
my &ext-load = try &::('rakupp-ext-load');
```

**Why not just call `rakupp-ext-load(...)` directly?** Because Raku resolves sub
names at *compile* time. On Rakudo the name does not exist, so the file fails to
compile and your fallback never gets the chance to run:

```
$ raku -e 'sub f() { rakupp-ext-load("x") }'
===SORRY!=== Error while compiling -e
Undeclared routine:
    rakupp-ext-load used at line 1
```

`&::('...')` is a **symbolic lookup** — the name is a string, resolved at run
time. The line is valid Raku either way, so the *same source file* compiles on
both engines and simply finds nothing on Rakudo:

```
$ raku   -e 'my &l = try &::("rakupp-ext-load"); say &l.defined'   # False
$ rakupp -e 'my &l = try &::("rakupp-ext-load"); say &l.defined'   # True
```

That is the whole trick. `use Rakupp::Ext` is accepted too, and is the
discoverable spelling for code that is Raku++-only by design — but it is a `use`
statement, so writing it makes the file uncompilable on Rakudo. Use the
symbolic form in anything portable.

Put together:

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

Note `&fast` and `&ext-load` keep their `&` sigil at every mention. Writing
`fast.defined` would *call* `fast` and ask the result — a mistake that produces
a baffling `No such method 'CALL-ME'`.

## Packaging

```
My-Thing/
├── META6.json
├── Build.rakumod          # compiles src/ at install time
├── src/thing.c
├── lib/My/Thing.rakumod
├── resources/libraries/   # build output lands here (gitignore it)
└── t/01-basic.t
```

`META6.json` declares the library as a resource:

```json
"resources": [ "libraries/thing" ]
```

Raku maps that logical name to the platform spelling — `libthing.so`,
`libthing.dylib`, `thing.dll` — so **your build must emit `libthing.dylib`, not
`thing.dylib`**, or `%?RESOURCES` will not find it.

`Build.rakumod` should **never abort an install**. If the compiler is missing, or
the headers are not there, or the engine is Rakudo, warn and return `True`: the
module then runs on its fallback. A build hook that fails turns an optimisation
into a hard dependency.

Find the library with `%?RESOURCES` (correct on both engines), falling back to a
`$*CWD`-relative path so `-Ilib` works from a checkout:

```raku
sub libraries() {
    my $ext  = $*DISTRO.is-win ?? 'dll' !! ($*KERNEL.name eq 'darwin' ?? 'dylib' !! 'so');
    my $stem = $*DISTRO.is-win ?? "thing.$ext" !! "libthing.$ext";
    my @c;
    with (try %?RESOURCES<libraries/thing>) { @c.push($_) if .defined }
    @c.push($*CWD.add("resources/libraries/$stem"));
    @c
}
```

**Do not derive the path from `$?FILE`.** Under Raku++ a module's `$?FILE` is the
*main program's* path, not the module's file, so anything computed from it lands
somewhere unrelated (recorded in
[dev/findings/BUGS.md](../dev/findings/BUGS.md)).

## Naming: the `Rakupp::` convention

A module whose behaviour depends on Raku++ should say so in its name.
`JSON::Native` tells a reader at the point of `use` that this is not a portable
choice — better than a footnote in a README, and better than discovering it when
the deploy target turns out to be Rakudo.

Two rules that follow:

- **A `Rakupp::` module is still a normal distribution.** It is installed by zef,
  it carries a `META6.json`, and it should degrade gracefully rather than refuse
  to load elsewhere.
- **The compiler must not special-case a module name.** An early draft of
  `JSON::Native` was built into the interpreter, and `use JSON::Native`
  intercepted the name before module loading — so the real distribution's tests
  silently exercised the built-in copy instead of the extension they were written
  for. If a name can be a module, it must *only* be a module.

## Versioning

`RAKUPP_EXT_ABI` is a single integer, bumped when the header gains capability
you cannot detect any other way, or when the meaning or order of anything in it
changes. It is **2** today; 1 was the original surface, and 2 added calling back
into Raku.

The host passes its own value to your `rakupp_ext_init` **and retries downward
if you return NULL** — so the `host_abi == RAKUPP_EXT_ABI` test extensions were
originally written with keeps working forever, while `>=` is what you should
write now:

```c
RAKUPP_EXT_EXPORT const RkModule* rakupp_ext_init(unsigned host_abi) {
    return host_abi >= RAKUPP_EXT_ABI ? &mod : 0;   /* "a host at least this new" */
}
```

`>=` says what you actually need. `==` also refuses every *future* host, which
means a rakupp upgrade silently drops you onto your fallback path until someone
rebuilds. Going the other way, an extension that reports a version newer than
the host is refused outright — it was built against entry points that host does
not have.

All three failure modes report themselves and none corrupt:

```
'…/thing.dylib' was built for a different extension ABI (host is 1)
cannot load extension '/nope.dylib'
'/bin/ls' is not a rakupp extension (no rakupp_ext_init)
```

Because loading returns a falsy value (or throws, catchably), a module that
follows the pattern above simply takes its fallback path when its extension is
stale. Rebuilding is `zef install` again.

## Current limits

Worth knowing before you design around them.

- **There is a per-value cost.** Each value crosses as an arena-allocated handle
  and is then copied into its container — roughly one extra `Value` copy per
  node. Measured on `JSON::Native`, that is 5.7 ms against 2.7 ms for the same
  parser compiled into the interpreter. Still 6.6× faster than Rakudo on that
  workload, but it means an extension is worth it for *bulk* work, not for a
  routine called once with two integers.
- **Hash iteration is still index-based**, but no longer quadratic: the host
  remembers where it was between `rk_key_at`/`rk_val_at` calls, so a sequential
  walk costs O(1) per key. Jumping about between two hashes, or writing to a
  hash while walking it, falls back to the O(i) positioning — correct either
  way, just slower.
- **No `--exe` bundling.** A program using a native extension cannot be compiled
  into a standalone binary — the shared library is loaded at run time and is not
  part of the module graph the bundler walks.
- **One `RkCtx` per call, single-threaded within that call.** Handles are not
  safe to pass between threads.

## See also

- [FFI.md](FFI.md) — NativeCall, for calling C libraries you did not write
- [MODULES.md](MODULES.md) — module loading, zef, and the search path
- [../../src/rakupp_ext.h](../../src/rakupp_ext.h) — the header, which is the
  normative reference
