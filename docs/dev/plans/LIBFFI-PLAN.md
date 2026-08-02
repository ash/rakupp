# NativeCall on `libffi` — implementation plan

**Status: implemented on 2026-08-02.** Two things are deliberately left open:
by-value structs (§6) and linking libffi rather than loading it (**§10** — the
one that matters on Windows). What landed, and what each decision cost, is
recorded in §9; the body below is the plan as written before the work, kept
because the reasoning is what makes the result reviewable.

Measurements were taken with `build-arm64/rakupp` on macOS/arm64 and the system
`libffi`.

---

## 1. Where NativeCall is today

> **Outcome:** every row of the table below is fixed except the by-value struct
> one — which turns out to be something Rakudo cannot express either, so it is
> new surface rather than a gap (§6). The `explicitly-manage` half of the
> `CUnion` row is also still absent.
> The user-facing description of what the FFI does *now* is
> [guide/FFI.md](../../guide/FFI.md).

The FFI is `dlsym` plus **one over-wide prototype**
(`src/Interpreter.cpp:7209`), called from `Interpreter::callNative`
(`src/Interpreter.cpp:7381`):

```c
typedef long   (*NcFnI)(long×8, double×8);
typedef double (*NcFnD)(long×8, double×8);
```

It works because the SysV-AMD64 and AAPCS register banks are independent and a
callee ignores argument registers it does not declare. The compiled (`--exe`)
path goes through the same function (`Codegen.cpp:552` emits
`RT.callNative(...)`), so anything landed here lands in both engines.

Working today: integer/pointer/float arguments, mixed int+float, `Str`→`char*`,
`CArray`, `CStruct`/`CPointer` handles, `is rw` out-params with copy-back,
`Buf`/`CArray` copy-back, synchronous callbacks via a 64-slot trampoline pool,
and a per-`Callable` `dlopen`/`dlsym` cache (`Value.h:83`).

### Measured gaps

| probe | result today | verdict |
|---|---|---|
| `snprintf($b, 32, "%d", 42)` | returns 1, buffer holds `"0"` | **silently wrong** — variadic args go on the stack on Apple ARM64 |
| `strtof("2.5", Pointer) --> num32` | `5.31535078498625e-315` | **silently wrong** — a `float` return read as `double` |
| `ldexpf(3e0, 2) --> num32` | `0` | **silently wrong** — a `float` arg passed as `double` |
| 9 integer arguments | `X::NYI: too many register arguments` | clean error |
| struct by value (arg or return) | not expressible | absent |
| `CUnion`, `explicitly-manage` | not implemented | absent |
| callback with >6 args, float args, or a non-integer return | truncated to `long` | wrong |

The callback path is the weakest part: `runCallback`
(`src/Interpreter.cpp:7346`) takes six `long`s and returns a `long`, the slot
table is capped at 64 and never freed, and every return goes through `.toInt()`.

`docs/dev/MODULE-WISHLIST.md` already describes most of this; the `num32` rows
above are new and are worse than that document states.

---

## 2. What libffi costs (measured, not estimated)

Microbenchmark, arm64, system `libffi` loaded with `dlopen`, calling
`abs(int32)`:

| path | ns/call |
|---|---|
| `ffi_call` (cif prepared once) | 17.2 |
| current wide prototype | 1.4 |
| a full Raku++ NativeCall crossing today (300k × `abs`, 146 ms) | ~487 |

libffi adds ~16 ns to a ~487 ns crossing — about 3%. **Conclusion: no dual
fast path.** One marshaller, everything through libffi when it is available.
Re-run the same probe on x86-64 Linux before committing to this.

> **Outcome:** the conclusion held, the *reasoning* was incomplete. The
> measured end cost is ~23 ns/crossing (157 ms vs 150 ms per 300k, best of 3),
> but getting there needed a fix this section did not predict — see §9.

---

## 3. Decision 1 — how the binary gets libffi

The constraint is the one stated in `MODULE-WISHLIST.md`: Raku++'s value is a
single portable binary, and every native dependency spends it.

| option | verdict |
|---|---|
| (a) link the system libffi at build time | adds a build **and** runtime dependency to every release artifact; breaks the WASM build; rejected |
| (b) vendor libffi source | autotools plus per-arch assembly across six build configurations (arm64, x64, universal, mingw, gcc16, emscripten) plus CI; large permanent maintenance surface; rejected |
| (c) **`dlopen` at runtime, declare the ABI ourselves** | zero build dependency, zero link dependency, graceful fallback where absent — the same pattern the project already uses for OpenSSL (`docs/guide/HTTPS.md`) |

**Recommend (c).** Verified today on stock macOS: `dlopen("libffi.dylib")`
succeeds and resolves `ffi_prep_cif`, `ffi_prep_cif_var`, `ffi_call`,
`ffi_closure_alloc`, `ffi_prep_closure_loc` and the `ffi_type_*` globals. On
Linux, `libffi.so.8` is pulled in by glib/gio/python on essentially every
desktop and server image — but it must never be *assumed*.

Mechanics:

- Candidate list: `libffi.so.8`, `libffi.so.7`, `libffi.so`, `libffi.dylib`,
  `/usr/lib/libffi.dylib`, `libffi-8.dll`.
- Redeclare only `ffi_type` — its layout (`size`, `alignment`, `type`,
  `elements`) is stable public ABI. **Never touch `ffi_cif` fields**: its size
  is target-dependent, so allocate an oversized zeroed buffer (512 B) and only
  ever pass the pointer.
- `FFI_DEFAULT_ABI` is a per-target enum value (1 on aarch64 SysV and on
  x86-64 UNIX64; different on Win64) — one small table in one header.
  > **Outcome: no table.** This bullet is wrong, and finding out how wrong is
  > the single most useful thing the self-test did. The value depends on the
  > libffi *release* as well as the target, and on this one machine arm64 wants
  > 1 while the x86-64 slice wants **2** — so the shipped loader probes
  > candidates and keeps the first that passes the self-test.
- **Load-time self-test, and this is what makes a hand-declared ABI safe:**
  prep and call `abs(-3) == 3`, `ldexp(3,2) == 12`, `ldexpf(3f,2) == 12`, and
  a small struct return. Any failure, or a non-zero `ffi_prep_cif` status,
  disables the backend and falls back.
- `RAKUPP_FFI=0` kill switch; `RAKUPP_FFI=/path/to/libffi.so` to point at one;
  the live backend must be reportable (`--version` line or a debug flag) —
  bug reports and the CI matrix both need it.
  > **Outcome:** all three shipped. The report is its own flag, `--ffi-info`,
  > rather than a `--version` line, so nothing that parses `--version` moves.

The fallback is the **current code, unchanged**, and stays permanently for
WASM (no `dlopen`-able libffi under Emscripten), Windows without
`libffi-8.dll`, and stripped containers.

---

## 4. Decision 2 — make the fallback loud, first

Silent wrongness is worse than a clean error. Land this **before** any libffi
work: the existing path throws `X::NYI` for what it cannot do — `num32`
argument or return, by-value struct, variadic — alongside the >8-argument
error it already produces. (`num32` can alternatively be fixed on the old path
with a second prototype family taking `float`s; cheap, and worth measuring.)

This batch is independently valuable, testable today, and defines exactly what
the fallback must do after libffi lands.

> **Outcome: landed, but folded into batch 2 rather than shipped first.** The
> reason is worth recording: the list of things the fallback cannot express is
> not knowable until the marshalling pass has classified the arguments, so the
> check belongs in the same pass. Doing it separately would have meant writing
> that classification twice.

---

## 5. Batches

Each batch is gated on the Roast run, `t/regression`, and `perf-guard --check`
(per `docs/dev` practice — regenerate conformance data only before a release).

| # | batch | size |
|---|---|---|
| 0 | Loud fallback (§4) | S |
| 1 | `src/Ffi.h` / `Ffi.cpp`: loader, `ffi_type` registry, self-test, cif cache. **No behaviour change** — a hidden env var selects it, and every existing `t/regression/nativecall-*.raku` must pass identically under both backends. This is the correctness gate for everything after. | M |
| 2 | Scalar correctness: true widths for arguments and returns, `num32`, `bool`, `int8/16/32`, and unlimited argument count (deletes the 8-register ceiling) | S–M |
| 3 | Cif cached per `Callable`, next to `nativeSymCache` (`Value.h:83`); re-run the 300k-crossing benchmark and `perf-guard --check` | S |
| 4 | Callbacks on `ffi_closure`: typed arguments and returns, any arity, closure lifetime tied to the `Value`. Replaces the 64-slot pool and the `g_cbSlots` leak. Foreign-thread callbacks stay unsupported but become **detected** (thread-local marker → throw or warn) instead of corrupting the interpreter. | M |
| 5 | One struct-layout engine feeding both `ncFieldOffset`/`ncStructSize` and the `ffi_type` aggregate; use `ffi_get_struct_offsets` when the loaded libffi exports it, cross-checked against ours under a debug assert. Adds nested `CStruct` and arrays in structs. | M |
| 6 | By-value struct/union arguments and returns, `CUnion` (see §6) | M |
| 7 | Variadics via `ffi_prep_cif_var` (see §6) | S–M |
| 8 | Docs: `FEATURES.md:139`, `HIGHLIGHTS.md:49,113`, `OVERVIEW.md:81,85`, the `MODULE-WISHLIST.md` "silently wrong" table, `CHANGELOG`, `MILESTONES`, the divergence log, and the spec site | S |

> **Outcome per batch is in §9.** Short version: 0–4, 7 and 8 landed as
> written; 5 landed partly (`CUnion` yes, embedded structs no — rakupp already
> matches Rakudo there); 6 is deliberately open. Batch 8 also gained
> [guide/FFI.md](../../guide/FFI.md), which did not exist when this was
> written — the feature turned out to need a page, not a bullet.

---

## 6. Two things that need a semantic decision, not just plumbing

### Variadics

Rakudo's NativeCall has no variadic support, so there is no signature to copy.
Proposal: **a slurpy in a native signature means "variadic from here"**:

```raku
sub snprintf(Blob, size_t, Str, *@args --> int32) is native {*}
```

`nfixed` = the number of declared parameters; each extra argument is typed from
its runtime `Value` under C's default argument promotions (`Int`→`sint64`,
`Num`→`double`, `Str`/`Buf`/`Pointer`→pointer). Optionally also
`is variadic(N)` for a fully-declared signature.

Both spellings are Raku++ extensions: they go in the divergence log and the
spec site, and neither may change the meaning of a signature that works today.

> **Outcome: the premise of this section was wrong.** "Rakudo's NativeCall has
> no variadic support" was asserted here without being tested, and it is false:
> `NativeCall.rakumod` sets a `variadic` flag from a trailing slurpy Positional
> parameter, and the installed 2026.07 produces byte-identical output for every
> variadic example in [guide/FFI.md](../../guide/FFI.md). So the slurpy spelling
> is not invented — it is Rakudo's, and this work is a **parity fix**, not an
> extension. It was caught by running the guide's examples on both engines,
> which is what should have happened before the claim was written down.

> **Outcome: the slurpy spelling shipped; `is variadic(N)` did not.** One way to
> say a thing is enough, and the slurpy carries the arity itself, so the trait
> would only have been a second spelling of the same fact. The compiled (`--exe`)
> bridge rebuilds the parameter list in generated C++ and initially dropped
> `slurpy`, which made variadic calls right in the interpreter and wrong in the
> binary; `t/regression/exe-nativecall.raku` now covers it.

### By-value structs

**Verify before designing.** As far as I can determine, Rakudo's NativeCall
passes `CStruct` arguments and returns as *pointers* only — so by-value is not
Rakudo-parity surface, it is new. A local Rakudo probe of a by-value struct
return (`div(7,2) --> DivT`) had not returned after several minutes; finish
that check before committing to a syntax.

> **Outcome: verified, and the guess was right.** Not by the probe — that never
> answered — but by reading Rakudo's own `NativeCall.rakumod`, both the 2021
> checkout and the installed **2026.07** sources. A `CStruct`-repr type maps to
> exactly one type code (`"cstruct"`); there is no by-value variant, and the
> string "by value" does not appear in the module at all. So a Rakudo user has
> no way to ask for it, and by-value here would be a Raku++ extension needing an
> invented syntax. Still deferred — but now for a stated reason rather than an
> unanswered question.

Pointer-passing must remain the default for `CStruct`-typed parameters — the
existing tests depend on it — with by-value behind an explicit marker.

> **Outcome: not built, on purpose.** The verification this section asks for
> never completed — the Rakudo probe hung twice without answering — so the
> premise the syntax rests on is still unconfirmed. See §9.

**So the headline libffi feature is not the main win here.** The wins, in
order of value: scalar-width correctness, unlimited arity, typed callbacks,
variadics. By-value structs are a bonus.

---

## 7. Risks

- **Hand-declared ABI on an untested target.** Mitigated by the load-time
  self-test plus fallback. CI must gain a leg running the whole suite with
  `RAKUPP_FFI=0`.
  > **Materialised, and the mitigation worked.** Two of the two architectures
  > tried wanted different ABI numbers, neither of them the documented one. The
  > `RAKUPP_FFI=0` leg is run and green (273/273).
- **Two paths = the semantic-duplication problem** from the duplication audit.
  Mitigated by: the fallback being the *unchanged* old code, every native
  regression test running under both backends, and any case libffi handles but
  the fallback cannot throwing there rather than guessing.
- **Performance on hot native loops** (zef, the OpenSSL/HTTPS crossings).
  `perf-guard --check` is the gate; re-run the 250k-crossing import case from
  cognates finding 14 specifically.
- **W^X policies** (macOS hardened runtime, SELinux, grsec) can make
  `ffi_closure_alloc` return NULL. The closure path must degrade to the
  existing trampoline pool rather than fail.
  > **Did not materialise on either architecture here; the degrade path is
  > implemented regardless, and is the reason the 64-slot pool was kept rather
  > than deleted.**
- **`--exe`.** Compiled binaries call the same `RT.callNative` and inherit
  everything, but `Codegen.cpp:529-543` still refuses `is native(&sub)`,
  expression libraries, `is rw` out-params and buffer parameters. This work
  does not change that; say so in the docs rather than implying otherwise.
  > **"Inherit everything" was too optimistic and this risk bullet is why it
  > got checked.** The bridge does not pass the `Callable` through — it rebuilds
  > the parameter list as generated C++ — so it inherits only the fields that
  > rebuild copies, and `slurpy` was not one of them. Variadic calls were right
  > interpreted and wrong compiled until that was fixed. The refusal list is
  > unchanged and is documented in [guide/FFI.md](../../guide/FFI.md).

---

## 8. Test plan

> **Outcome:** landed as `t/regression/nativecall-libffi.raku`, with one
> addition the plan did not anticipate: rather than skipping when libffi is
> absent, the file asserts the *other* half of the contract there (that the same
> calls throw), so it is a real test in both CI legs instead of a hole in one.

- Extend the five existing `t/regression/nativecall-*.raku` files, and add:
  widths (`int8/16/32/uint*`, `num32` round trip), a 12-argument call, variadic
  `snprintf`, a typed callback (float arguments, `num64` return, arity 8),
  struct-by-value (`div`/`imaxdiv`), `CUnion`.
- Keep the existing "portable — system libc only" contract: `div`, `imaxdiv`,
  `qsort`, `gettimeofday`, `snprintf`, `strtof`, `ldexpf` cover nearly all of
  it, so no C compiler is needed at test time.
- Oracle: run each new test under Rakudo where Rakudo supports the construct.
  Where it does not (variadics, by-value structs), the test is Raku++-only and
  the divergence log records why.
- CI: the full suite twice — `RAKUPP_FFI` unset, and `RAKUPP_FFI=0`.

---

## 9. What landed (2026-08-02)

New: `src/Ffi.h` / `src/Ffi.cpp` (the loader, ABI probe, self-test and type
registry). Everything else is in `Interpreter::callNative` and its helpers.

| # | batch | outcome |
|---|---|---|
| 0 | Loud fallback | done — folded into batch 2 rather than landed first, because the neutral marshalling pass is what knows which cases the fallback cannot express |
| 1 | `Ffi.h`/`Ffi.cpp` | done |
| 2 | Scalar correctness, unlimited arity | done |
| 3 | Cif cached per `Callable` | done |
| 4 | `ffi_closure` callbacks | done |
| 5 | Struct layout | partly — `CUnion` landed, along with two layout bugs it turned up (below). Embedded structs (`HAS`) did not: rakupp already matches Rakudo's default, where a struct-typed field is a pointer |
| 6 | By-value structs | **not done — deliberately.** See below |
| 7 | Variadics | done |
| 8 | Tests + docs | done — `t/regression/nativecall-libffi.raku`, which asserts the *other* half of the contract (that the same calls throw) when the backend is off, so it is meaningful in both CI legs |

### The two predictions that were wrong

**"ffi_prep_cif is the per-call cost."** It is not. Caching the cif changed
nothing measurable; the ~97 ns/crossing regression was two `std::vector` heap
allocations for libffi's argument arrays. Stack-first arrays (heap only past 16
arguments) took it to ~23 ns, which is close to the 16 ns `ffi_call` itself
costs. The cif cache stayed anyway — it is correct, and it is free.

**"FFI_DEFAULT_ABI can be a per-target table."** The plan already refused to
hardcode it, and that was right for a reason better than the one given: this
machine's arm64 libffi wants ABI 1 and its x86-64 slice wants ABI **2**, which
is neither the 8 that current libffi headers imply nor the 1 that older ones do.
A table built from any single libffi release would have been wrong on one of the
two architectures of the same machine.

### Verified on two ABIs

The full suite passes on arm64 (ABI 1) and on the x86-64 build (ABI 2), and the
whole 273-check suite passes with `RAKUPP_FFI=0` as well. Roast is unchanged:
628/1462 files fully passing before and after, with a byte-identical sorted list
of fully-passing files.

### Why by-value structs are still open

The plan said to verify against Rakudo first. The probe route failed — a Rakudo
program calling a by-value struct return did not return, twice — but the
question is now **answered from Rakudo's source** (`lib/NativeCall.rakumod`, in
both a 2021 checkout and the installed 2026.07 core sources): a `CStruct`-repr
type maps to the single type code `"cstruct"`, there is no by-value variant, and
"by value" appears nowhere in the module. A Rakudo user cannot ask for it.

So by-value is confirmed to be **new surface, not parity**, and it needs a
spelling that does not exist yet: `is by-value` on a parameter is easy enough,
but a return type has no trait slot, so the return case needs a second idea. It
stays deferred — now because the design question is open, not because the facts
were. Everything it would have bought is otherwise available: the four wins the
plan ranked above it — scalar widths, arity, typed callbacks, variadics — all
landed, and a struct return of up to 8 bytes can still be taken as `int64` and
unpacked by hand.

### Bugs found on the way

The first two predate this work; the third was introduced by it and caught while
writing [guide/FFI.md](../../guide/FFI.md). All three are fixed here:

- `nativesizeof` answered from a private table that disagreed with the
  marshaller placing the value — `int` was 4 bytes there and 8 everywhere else
  — and returned a flat 8 for any `CStruct` class instead of its real size.
- C's `bool` was 8 bytes wide in the type table, so every `CArray[bool]` stride
  was wrong and a one-byte return was read as a whole register.
- The `--exe` bridge rebuilds a native sub's parameter list as generated C++ and
  did not copy `slurpy`, so a **variadic call was correct interpreted and wrong
  compiled** — prepared as a fixed call, with the `...` arguments in registers
  instead of on the stack. It surfaced only because documenting "does this work
  compiled as well as interpreted?" meant actually running both and diffing:
  `snprintf(Str, 0, "%d-%s", 42, "hi")` answered 5 interpreted and 8 compiled.
  Covered now by `t/regression/exe-nativecall.raku`.

### Where to read about the result

- Users: [guide/FFI.md](../../guide/FFI.md) — how the FFI works, whether you
  need to install anything (no), and whether it works compiled (yes).
- Inventory: [guide/FEATURES.md](../../guide/FEATURES.md) NativeCall entry.
- Release notes: `CHANGELOG.md`, Unreleased.

---

## 10. Deferred: linking libffi instead of loading it

Raised 2026-08-02, postponed the same day. Not built — recorded so the analysis
does not have to be redone.

### The gap it would close

`dlopen` at run time is the right default and costs nothing on macOS or Linux,
where a libffi is essentially always present. **Windows is the exception, and it
is not a small one.** The release matrix ships two Windows binaries (MSVC x64
and MinGW-w64); stock Windows has no `libffi-8.dll` anywhere, so both are
*permanently* on the fallback path — no `num32`, no variadics, no more than 8
register arguments, no more than 64 distinct callbacks — and a user has no way
to change that short of sourcing a DLL from somewhere. Static musl / `scratch`
containers are the same shape, smaller.

Note what this does *not* rescue: a program binding `libsqlite3` needs sqlite3
on the target regardless, so libffi is a marginal addition to a dependency that
already exists. The programs this actually rescues are the ones calling **libc**
— `printf`, `snprintf`, `open` with a mode, `ioctl` — which is precisely the
variadic surface.

### It is a build option, not an `--exe` flag

The question arrived as "should `--exe` get a static-link option". It should
not, for two reasons:

1. `--exe` links the same static runtime (`librakupp_rt.a` / `rakupp_rt.lib`)
   that `rakupp` itself is built from, so a runtime built against libffi is
   inherited by `--exe` output for free — no new flag needed.
2. An `--exe`-only flag would fix half the problem. `rakupp.exe` *interpreting*
   a script has exactly the same gap.

So: a CMake option, `RAKUPP_LINK_FFI=ON`, **off by default** so the ordinary
build keeps needing nothing.

### Sketch

Under a `RAKUPP_STATIC_FFI` define, `ffi::load()` fills the same `ffi::Lib`
table with `&ffi_prep_cif` and friends and `FFI_DEFAULT_ABI` from `<ffi.h>`,
instead of `dlsym`ing them — and then still runs `selfTest()`, which stops
validating a *probe* and starts validating the header's constant, which is
worth as much. Nothing downstream changes: same struct, same marshaller, so
this does not reintroduce the two-implementations problem the design avoided.
Roughly 30 lines plus a CMake block. `--ffi-info` should then say "built in"
rather than a path. `libffi.a` is ~43 KB.

### Where the real cost is

Not in `Ffi.cpp`. It is the **Windows CI plumbing**: vcpkg for the MSVC leg, an
MSYS2 package for MinGW, and libffi's Windows build wanting a particular
assembler path. Treat that as a separate follow-up from the code, which can be
validated locally in an hour against any Homebrew/`-dev` `libffi.a`.

Also real, if smaller: libffi is MIT-licensed, so static linking is fine but
carries an attribution obligation — a line in `LICENSE` and in the release
notes of any binary that ships it.

### The two questions that decide it

1. **Are there Windows NativeCall users?** If FFI on Windows is theoretical, this
   is cost without payoff, and the honest position is the current one:
   `--ffi-info` and [guide/FFI.md](../../guide/FFI.md) tell a Windows user
   exactly what they do not have.
2. Is anyone shipping Raku++ in a `scratch`/musl container *and* calling libc
   variadically?

Until one of those is a yes, the dependency-free default is worth more than the
four features it costs on one platform.
