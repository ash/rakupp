# NativeCall and the libffi Backend

Sooner or later a program needs something the language does not have: a system
call, a compression library, a database client. Raku's answer is to let a
program declare the C function's signature in Raku and then call it as if it
were an ordinary sub.

```raku
use NativeCall;
sub strlen(Str --> size_t) is native {*}
sub hypot(num64, num64 --> num64) is native {*}

say strlen("hello");     # 5
say hypot(3e0, 4e0);     # 5
```

`is native` calls a C function directly. This chapter is about how that works
inside, which is a more interesting story than the syntax: Raku++ ships as one
portable binary with no third-party dependencies, and it would like to keep that
property — so it does not link libffi and does not vendor it.

## libffi is loaded at run time, and its ABI is restated by hand

```cpp
// src/Ffi.h — the header comment, condensed
// Raku++ ships as ONE portable binary with no third-party dependencies, so
// it does not link libffi and does not vendor it. Instead the library is
// loaded at RUNTIME with dlopen ... and the handful of ABI declarations we
// need are restated here.
```

Two things make hand-declaring another project's ABI safe, and they are worth
stating because the technique generalises.

**We never touch `ffi_cif`'s fields.** Its size and layout are
target-dependent, so the code hands libffi an over-sized zeroed buffer and only
ever passes the pointer back:

```cpp
// src/Ffi.h
struct Cif { alignas(16) unsigned char buf[512] = {}; };
```

512 bytes is far past any target's real size. `ffi_type`'s layout, by contrast,
*is* stable public ABI — callers have always had to build struct types by hand —
so it is declared normally:

```cpp
// src/Ffi.h
struct Type {
    size_t         size;
    unsigned short alignment;
    unsigned short type;
    Type**         elements;   // null-terminated, for T_STRUCT
};
```

**The calling-convention constant is probed, not tabulated.**
`FFI_DEFAULT_ABI` is an enum whose value differs per target *and* per libffi
release. On the machine this was developed on, the arm64 library wanted one
value while the x86-64 build of the same program wanted another — neither
matching what current libffi headers imply.

So the loader tries candidates until one passes a **self-test of real calls** —
`strlen`, `abs`, `ldexp`, `ldexpf` — and disables the backend entirely if none
does:

```cpp
// src/Ffi.h
struct Lib {
    bool ok = false;
    std::string path, why;
    int abi = 0;                 // FFI_DEFAULT_ABI for this target, probed
    int (*prep)(void*, int, unsigned, Type*, Type**);
    int (*prep_var)(void*, int, unsigned, unsigned, Type*, Type**);
    void (*call)(void*, void (*)(void), void*, void**);
    void* (*closure_alloc)(size_t, void**);
    int (*prep_closure_loc)(void*, void*,
                            void (*)(void*, void*, void**, void*),
                            void*, void*);
    Type *t_void, *t_uint8, /* … */ *t_pointer;
};
const Lib& lib();          // loads and self-tests on first call; never retries
```

The rule that makes this defensible: **a failed probe disables the backend
rather than guessing.** Miscalling through a wrong-shaped interface would
produce plausible wrong answers, which is the worst possible failure mode for an
FFI.

```sh
rakupp --ffi-info      # libffi: libffi.dylib (abi 1)
```

## What happens with no libffi

Not everything keeps working, and being clear-eyed about that is part of the
design. The fallback is a **narrower** FFI, not a transparent substitute.

Calls go through a fixed prototype that hands eight integer and eight
floating-point arguments to the platform ABI and lets it place them. That is
correct for a large majority of real C signatures: pointers, `Str`, integers of
every width, `num64`, `CArray`, struct and union handles, `is rw`
out-parameters, `nativecast`, `cglobal` and synchronous callbacks all behave
exactly as they do with libffi.

Four things it cannot do, and each **throws at the point of the call** rather
than computing a wrong answer:

| | |
|---|---|
| a `num32` argument or return | a real C `float` cannot be placed by the fixed prototype |
| a variadic signature | needs `ffi_prep_cif_var` to say where `...` begins |
| more than 8 integer or 8 float arguments | the prototype is that wide and no wider |
| more than 64 distinct callbacks | the trampoline pool holds 64 |

`RAKUPP_FFI=0` forces this path, which is how the test suite exercises it: the
whole suite runs twice, once each way. WebAssembly takes it by construction —
there is no shared library to open.

## How a call is made

1. **Resolve.** The library is `dlopen`ed — the name as written, then the
   platform-decorated forms — and the symbol `dlsym`ed.
2. **Marshal.** Each argument is converted once, at its *declared* width, into a
   slot libffi reads directly.
3. **Describe.** The signature becomes a call interface, prepared once per sub —
   with the variadic form when the signature is variadic.
4. **Call**, then box the return: a pointer becomes a live `Pointer` or
   `CArray`, a `Str` return is read as a C string, a narrow integer is truncated
   and sign-extended, and `is rw` out-parameters and mutated buffers are copied
   back.

Steps 1 and 3 are cached **on the `Callable`**, and the reason is measured:

```cpp
// src/Value.h — Callable, the native fields
bool isNative = false;
std::string nativeLib, nativeSym, nativeLibSub;
const Expr* nativeLibExpr = nullptr;
void* nativeSymCache = nullptr;   // dlopen/dlsym once, not per call:
                                  // 5 dlopen candidates cost a flat ~67 µs
void* nativeCifCache = nullptr;   // ffi_prep_cif is ~80 ns — 20% of a whole
                                  // crossing — so it must not run per call
```

The cost of going through libffi rather than calling blind is about **23
nanoseconds per crossing** — 157 milliseconds against 150 for 300,000 calls of
`abs`. On a crossing that costs about 490 nanoseconds end to end, that is under
5%, which is why there is one code path rather than a fast one and a general one.

`nativeLibExpr` handles a genuinely awkward case: `is native(EXPR)` where the
expression could not be evaluated at declaration time. It is retried once at the
first call, and the raw `Expr*` is safe to keep because the AST outlives the
interpreter.

## Variadics

Variadic C functions are the easiest thing in an FFI to get quietly wrong.

C's `...` is not a formality. On the SysV AMD64 ABI a variadic argument goes in
a register but the callee is told how many; on the Apple ARM64 ABI it goes on
the **stack**, while a fixed argument in the same position would go in a
register. A caller that does not know which arguments are variadic cannot place
them, and the failure is silent — you get a number, it is just the wrong one.

Raku++ spells it with a **slurpy marking the position where C's `...` begins**,
which is the same spelling Rakudo uses:

```raku
use NativeCall;
sub snprintf(Buf, size_t, Str, *@args --> int32) is native {*}

my $buf = buf8.allocate(64);
my $n = snprintf($buf, 64, "%s scored %d at %.1f%%", "Ada", 97, 99.5e0);
say $buf.decode('latin-1').substr(0, $n);   # Ada scored 97 at 99.5%
```

Everything beyond the declared parameters is variadic and is typed from the
value itself, following C's **default argument promotions**:

| You pass | C receives |
|---|---|
| `Int` | a 64-bit integer |
| `Num` / `Rat` | `double` — never a bare `float` |
| `Str` | `char*` |
| `Buf` / `Pointer` / `CArray` | the pointer |

That promotion table is why `%.1f` works without saying anything: a float
argument to a variadic function *is* a `double` in C.

Until this was fixed, Raku++ placed `...` arguments in registers and answered
`"0"` for `snprintf($buf, 16, "%d", 42)`. It is a parity fix, not an extension —
and it is what makes `curl_easy_setopt`, the `printf` family, `open(2)` with a
mode, and `ioctl` reachable at all.

## The type map

| Raku | C | Bytes |
|---|---|---|
| `int8` `uint8` `byte` | `int8_t` `uint8_t` | 1 |
| `bool` | `bool` | 1 |
| `int16` `uint16` | `int16_t` `uint16_t` | 2 |
| `int32` `uint32` | `int32_t` `uint32_t` | 4 |
| `int` `int64` `long` `size_t` | 64-bit integer | 8 |
| `num32` | `float` | 4 |
| `num` `num64` | `double` | 8 |
| `Str` | `char*` | pointer |
| `Buf` / `Blob` | the bytes, copied back after the call | pointer |
| `Pointer[T]` `CArray[T]` | the pointer | pointer |
| a `repr('CStruct')` / `CUnion` class | a pointer to it | pointer |
| `&callback (…)` | a C function pointer | pointer |

The table lives in `Interpreter.cpp` and `ffi::scalar(width, sign, isFloat)`
maps into it, deliberately keeping the Raku type names in one place.

Structs are laid out with natural alignment; `.new` allocates zeroed native
memory, and field reads and writes go to that memory, so a C function can fill
one in. `nativesizeof` answers from the same layout code.

```cpp
// src/Interpreter.h — the pointer helpers
Value ncMakePointer(const std::string& type, void* p);
Value ncMakeLiveCArray(const std::string& type, void* p);
static Value ncReadElem(long long addr, const std::string& ofType, long long i);
static void ncWriteElem(long long addr, const std::string& ofType,
                        long long i, const Value& val);
static long long ncFieldOffset(ClassInfo* ci, const std::string& field,
                               std::string& type);
static long long ncStructSize(ClassInfo* ci);
```

## Callbacks

A `Callable` handed to C becomes a real C function pointer built with an
`ffi_closure`. Parameters are typed from the block's own signature:

```cpp
// src/Interpreter.h
long runCallback(int slot, long a0, long a1, long a2,
                 long a3, long a4, long a5);
void runFfiClosure(void* closure, void* ret, void** args);
bool onRakuThread() const { return (bool)tctx_.cur; }
```

The last one is the guard that matters. A callback must fire **during** the
native call — `qsort`, `bsearch`, an iteration callback. A callback that C
stores and fires later from a thread of its own has no Raku scope to run in, so
`onRakuThread()` detects it and the callback is ignored with a warning rather
than run against a thread the interpreter never entered.

Note what that predicate actually tests: whether this thread has a current
lexical scope. A thread the C library made for itself has none.

## Your declaration is a promise nothing can check

A C library exports addresses, not types. Nothing — not Raku++, not Rakudo, not
any FFI in any language — can compare a declaration against the real prototype,
because that information does not exist at run time. A wrong declaration does
not fail; it produces a plausible answer.

```raku
sub abs(num64 --> num64) is native {*}   # WRONG — C's abs is int abs(int)
say abs(-3.34e0);   # -3.34
say abs(-99.5e0);   # -99.5
```

Every answer is the argument, unchanged, and that is the tell. libffi put the
`num64` in the first *floating-point* register, but `int abs(int)` reads the
first *integer* register and returns an integer in the integer return register.
The floating-point return register is never written, so it still holds what was
passed. The call really happened; the wrong question was asked.

Rakudo prints exactly the same three lines. This is not an implementation quirk
to work around — it is what an FFI is.

## Tracing

```
RAKUPP_FFI_TRACE=1
```

```
[ffi] backend: libffi: libffi.dylib (abi 1)
[ffi] snprintf(0x156f1c9a0, 64, "%s scored %d at %.1f%%", "Ada", 97, 99.5) -> 22
[ffi] sqlite3:sqlite3_libversion() -> "3.43.2"
```

It shows the resolved C symbol rather than the Raku sub name, and — importantly
— **what actually crossed**: the marshalled argument values and the raw return,
not the Raku values re-printed. So a marshalling bug shows up in the trace
instead of being hidden by it.

An external tracer cannot do this job. `lldb`, `ltrace` and `dtrace` see C
symbols, and the interpreter's own C++ calls `strlen` and `malloc` constantly,
so a breakpoint on `strlen` cannot distinguish a crossing the program asked for
from one the runtime made for itself. Only this layer knows which call came from
Raku.

## In compiled code

`--exe` emits the same `callNative`, so `is native` behaves identically
interpreted and compiled — and a compiled binary looks for libffi at *its* run
time, not at build time.

A few shapes make `--exe` fall back to bundling: `is native(&lib-sub)`, a
library name computed by an expression, `is rw` out-parameters, and buffer or
`CArray` parameters needing copy-back. The compiler says so and produces a
bundled binary instead.

## Not supported

- **C structs passed or returned by value.** Rakudo cannot express this either —
  its NativeCall maps a struct to one type code with no by-value variant — so
  adding it here would mean inventing syntax rather than matching an existing
  spelling. A return of up to 8 bytes can be declared `int64` and unpacked by
  hand.
- **Callbacks fired from a thread the C library owns**, as above.
- **Embedded structs.** A struct-typed field is a pointer, not an inlined
  struct.
- `explicitly-manage`.
