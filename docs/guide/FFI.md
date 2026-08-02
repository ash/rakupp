# NativeCall — calling C from Raku++

`is native` calls a C function directly. This page is about how that works in
Raku++, what you need installed (nothing), and what behaves differently from
Rakudo.

```raku
use NativeCall;
sub strlen(Str --> size_t) is native {*}
sub sqrt(num64 --> num64) is native {*}

say strlen("hello");   # 5
say sqrt(2e0);         # 1.4142135623730951
```

Every example on this page was run with the `rakupp` in this repository.

---

## Do I need to install libffi first?

**No.** Nothing to download, nothing to configure, and nothing new is linked
into the binary.

Raku++ ships as one portable executable with no third-party dependencies, and
that property is worth keeping, so libffi is neither linked at build time nor
vendored into the tree. It is looked up at *run* time with `dlopen`, the same
way the TLS layer finds OpenSSL. If it is there, the FFI uses it. If it is not,
NativeCall still works for most signatures and says so plainly for the rest.

Ask the binary what it found:

```sh
rakupp --ffi-info
```

```
libffi: libffi.dylib (abi 1)
```

In practice it is already on the machine:

| Platform | Where it comes from |
|---|---|
| macOS | `/usr/lib/libffi.dylib`, part of the OS |
| Linux | `libffi.so.8`, pulled in by glib/GTK, Python, and much else — present on essentially every desktop and server image |
| Windows | usually **absent**; NativeCall runs on the fallback path below |
| WebAssembly ([rakujs](../../rakujs)) | absent by construction — there is no shared library to open |

If you are on a stripped container and want the full FFI, install the runtime
package your distribution already builds (`libffi8` on Debian/Ubuntu, `libffi`
on Fedora/Arch, `brew install libffi` on macOS if you want a newer one), or
point Raku++ at a specific copy:

```sh
RAKUPP_FFI=/opt/lib/libffi.so.8 rakupp myprogram.raku
```

### What happens when there is no libffi

**Not everything keeps working — be clear-eyed about this.** The fallback is a
narrower FFI, not a transparent substitute.

Calls go through a fixed prototype that hands eight integer and eight
floating-point arguments to the platform ABI and lets it place them. That is
correct for a large majority of real C signatures: pointers, `Str`, integers of
every width, `num64`, `CArray`, `CStruct` and `CUnion` handles, `is rw`
out-parameters, `nativecast`, `cglobal` and synchronous callbacks all behave
exactly as they do with libffi. Most programs genuinely will not notice.

Four things it cannot do, and each **throws at the point of the call** rather
than computing the wrong answer:

| | |
|---|---|
| a `num32` argument or return | a real C `float` cannot be placed by the fixed prototype |
| a variadic signature (`*@args`) | needs `ffi_prep_cif_var` to tell the ABI where `...` begins |
| more than 8 integer or 8 float arguments | the prototype is that wide and no wider |
| more than 64 **distinct** callbacks in one program | the trampoline pool holds 64 |

```
$ RAKUPP_FFI=0 rakupp ldexpf.raku
NativeCall: a num32 argument needs libffi, which is not available (disabled by RAKUPP_FFI)

$ RAKUPP_FFI=0 rakupp printf.raku
NativeCall: a variadic native call needs libffi, which is not available (disabled by RAKUPP_FFI)
```

The failure is a normal Raku exception, so it is catchable — a module that wants
to adapt can `try` the fast path and pick another route. But it is a *runtime*
failure at the first such call, not something the compiler warns about up front,
so a program that only hits the path on an unusual branch will only fail there.
If your program needs any of the four, treat libffi as a requirement and check
`rakupp --ffi-info` in your install steps.

`RAKUPP_FFI=0` forces this path on purpose, which is how the test suite
exercises it: the whole suite is run twice, once each way.

---

## Does it work compiled, or only interpreted?

**Both, from the same code.** `rakupp --exe` transpiles your program to C++ and
links it against the same runtime, and the generated call site goes through the
same marshaller the interpreter uses. There is one implementation, so there is
one behaviour.

```sh
rakupp prog.raku                 # interpreted
rakupp --exe -o prog prog.raku   # compiled to a standalone binary
./prog                           # same answers
```

`--bundle` and `--aot` produce standalone binaries that tree-walk the program,
so they behave exactly like the interpreter here.

Two things to know about the compiled binary:

- **It looks for libffi at *its* runtime, not yours.** A binary built on a
  machine with libffi and run on one without it takes the fallback path, with
  the same error messages. Nothing is baked in at compile time.
- **A few shapes make `--exe` fall back to bundling** — `is native(&lib-sub)`,
  a library name computed by an expression, `is rw` out-parameters, and
  buffer/`CArray` parameters that need copy-back. The compiler says so and
  produces a bundled binary instead of a transpiled one; the program still runs
  and still gives the right answers, just at interpreter speed.

---

## Variadics

This is where Raku++ goes furthest past other Raku implementations, so it is
worth its own section.

C's `...` is not a formality. On the SysV-AMD64 ABI a variadic argument goes in
a register but the callee is told how many; on the Apple ARM64 ABI it goes on
the **stack** while a fixed argument in the same position would go in a
register. A caller that does not know which arguments are variadic cannot place
them, and the failure is silent — you get a number, it is just the wrong one.
`snprintf($buf, 16, "%d", 42)` used to yield `"0"` here, with no error.

Rakudo's NativeCall has no variadic support at all, so there was no signature to
copy. Raku++ spells it with a **slurpy, which marks the position where C's `...`
begins**:

```raku
use NativeCall;
sub snprintf(Buf, size_t, Str, *@args --> int32) is native {*}

my $buf = buf8.allocate(64);
my $n = snprintf($buf, 64, "%s scored %d at %.1f%%", "Ada", 97, 99.5e0);
say $buf.decode('latin-1').substr(0, $n);   # Ada scored 97 at 99.5%
```

```raku
sub printf(Str, *@args --> int32) is native {*}
printf("%d bottles of %s\n", 99, "beer");   # 99 bottles of beer
```

The parameters you declare are the fixed ones; everything you pass beyond them
is variadic and is typed from the value itself, following C's default argument
promotions:

| You pass | C receives |
|---|---|
| `Int` | a 64-bit integer |
| `Num` / `Rat` | `double` — never a bare `float`, exactly as C promotes |
| `Str` | `char*` |
| `Buf` / `Blob` / `Pointer` / `CArray` | the pointer |

That promotion table is the reason `%.1f` works above without you saying
anything: a float argument to a variadic function *is* a `double` in C, and
Raku++ passes one.

Two practical notes:

- **This spelling is a Raku++ extension.** A program that uses it will not run
  on Rakudo, which cannot call variadic C functions at all.
- **It needs libffi** (specifically `ffi_prep_cif_var`). On the fallback path a
  variadic signature throws, as shown above.

This is what makes `curl_easy_setopt`, `printf`-family functions, `open(2)` with
a mode, and `ioctl` reachable at all.

---

## How a call is made

1. **Resolve.** The library is `dlopen`ed — the name as written, then the
   platform-decorated forms (`libfoo.so`, `libfoo.dylib`) — and the symbol
   `dlsym`ed. Both are cached on the sub, because the `dlopen` candidate scan
   is by far the most expensive part of a first call (~67 µs) and would
   otherwise be paid on every one.
2. **Marshal.** Each argument is converted once, at its *declared* width, into
   a slot libffi reads directly.
3. **Describe.** The signature is turned into a libffi call interface, prepared
   once per sub and reused — with `ffi_prep_cif_var` when the signature is
   variadic, which is what teaches the ABI where `...` begins.
4. **Call**, then box the return: a pointer becomes a live `Pointer`/`CArray`, a
   `Str` return is read as a C string, a narrow integer is truncated and
   sign-extended to its declared width, `is rw` out-parameters and mutated
   buffers are copied back to the caller's variables.

The cost of going through libffi rather than calling blind is about **23 ns per
crossing** (157 ms vs 150 ms for 300 000 calls of `abs`, best of three, arm64).
On a crossing that costs ~490 ns end to end, that is under 5%, which is why
there is one code path rather than a fast one and a general one.

### Type map

| Raku | C | Bytes |
|---|---|---|
| `int8` `uint8` `byte` | `int8_t` `uint8_t` | 1 |
| `bool` | `bool` | 1 |
| `int16` `uint16` | `int16_t` `uint16_t` | 2 |
| `int32` `uint32` | `int32_t` `uint32_t` | 4 |
| `int` `int64` `long` `size_t` `ssize_t` | 64-bit integer | 8 |
| `num32` | `float` | 4 |
| `num` `num64` | `double` | 8 |
| `Str` | `char*` | pointer |
| `Buf` / `Blob` | the bytes, copied back after the call | pointer |
| `Pointer` `Pointer[T]` `CArray[T]` | the pointer | pointer |
| a `repr('CStruct')` / `CPointer` / `CUnion` class | a pointer to it | pointer |
| `&callback (…)` | a C function pointer | pointer |

`nativesizeof` answers from this same table, including for your own classes:

```raku
say nativesizeof(int);         # 8
say nativesizeof(bool);        # 1
say nativesizeof(TimeVal);     # 16  — the struct's real laid-out size
```

---

## Structs, unions and pointers

`is repr('CStruct')` lays fields out with natural alignment; `.new` allocates
zeroed native memory, and field reads and writes go to that memory, so a C
function can fill it in:

```raku
use NativeCall;
class TimeVal is repr('CStruct') { has int64 $.sec is rw; has int64 $.usec is rw; }
sub gettimeofday(TimeVal, Pointer --> int32) is native {*}

my $tv = TimeVal.new;
gettimeofday($tv, Pointer);
say $tv.sec > 1_700_000_000;   # True
```

`is repr('CUnion')` overlays every field at offset 0 and is as wide as its
widest member:

```raku
class Word is repr('CUnion') { has uint16 $.half is rw; has uint8 $.lo is rw; }
my $w = Word.new(half => 0x1234);
say $w.lo.fmt('%#x');       # 0x34 on a little-endian machine
say nativesizeof(Word);     # 2
```

Also available: `CArray[T]` (element read/write, with in-place mutation copied
back), `Pointer[T]` with `.deref`, `nativecast`, `cglobal`, and `is rw`
out-parameters marshalled as `T*` — including the `sqlite3**` shape where C
wants somewhere to *write* a pointer.

A struct passed to C is passed **as a pointer**, which is what Rakudo does too.

---

## Callbacks

Hand a `Callable` to C and it becomes a real C function pointer, built with
`ffi_closure`. The parameters are typed from your block's own signature, so a
declared `Pointer` arrives as a live `Pointer` and a declared `num64` as a
number:

```raku
use NativeCall;
sub qsort(CArray[int32], size_t, size_t, &cmp (Pointer, Pointer --> int32)) is native {*}

my $a = CArray[int32].new(5, 2, 8, 1, 9);
qsort($a, 5, 4, -> Pointer $x, Pointer $y {
    nativecast(CArray[int32], $x)[0] <=> nativecast(CArray[int32], $y)[0]
});
say (^5).map({ $a[$_] }).join(',');   # 1,2,5,8,9
```

There is no limit on arity or on how many distinct callbacks a program creates,
and the return value is typed rather than forced through an integer.

The callback must fire **during** the native call — `qsort`, `bsearch`, an
iteration callback. A callback that C stores and fires later from a thread of
its own has no Raku scope to run in; that is detected and the callback is
ignored with a warning on stderr, rather than run anyway.

---

## Not supported

- **C structs passed or returned by value.** Rakudo cannot express this either
  — its NativeCall maps a `CStruct` to one type code with no by-value variant —
  so adding it here would mean inventing syntax rather than matching an existing
  spelling, and that is not settled yet (see
  [dev/plans/LIBFFI-PLAN.md](../dev/plans/LIBFFI-PLAN.md) §6). A return of up to
  8 bytes can be declared `int64` and unpacked by hand.
- **Callbacks fired from a thread the C library owns** (see above).
- **Embedded structs.** A struct-typed field is a pointer, not an inlined
  struct; Rakudo's `HAS` is not implemented.
- `explicitly-manage`.

---

## Diagnostics

| | |
|---|---|
| `rakupp --ffi-info` | which backend is live, or why none is |
| `RAKUPP_FFI=0` | force the no-libffi fallback (used by the second CI leg) |
| `RAKUPP_FFI=/path/to/lib` | use a specific libffi — and *only* that one. If it cannot be loaded, Raku++ reports it and runs on the fallback rather than silently substituting whatever the system ships, because naming a library is a request, not a hint |

When reporting a NativeCall bug, `--ffi-info` is the first line to include: the
answer differs per architecture even on one machine.

---

## Implementation

`src/Ffi.h` and `src/Ffi.cpp` hold the loader, the type registry and the
self-test; `Interpreter::callNative` in `src/Interpreter.cpp` does the
marshalling for both the interpreter and `--exe`.

One detail worth knowing if you touch it: the calling-convention constant
libffi wants is **probed, not tabulated**. Its numeric value depends on the
target *and* on the libffi release, and on the machine this was developed on the
arm64 library wants one value while the x86-64 build of the same program wants
another — neither matching what current libffi headers imply. The loader tries
candidates until one passes a self-test of real calls (`strlen`, `abs`, `ldexp`,
`ldexpf`) and disables itself if none does, which is what makes it safe to use a
library whose ABI is declared by hand.

The design decisions, the measurements behind them, and the two predictions that
turned out wrong are in
[dev/plans/LIBFFI-PLAN.md](../dev/plans/LIBFFI-PLAN.md).
