# NativeCall on `libffi` — implementation plan

Status: proposal, nothing implemented. Measurements below were taken on
2026-08-02 with `build-arm64/rakupp` on macOS/arm64 and the system `libffi`.

---

## 1. Where NativeCall is today

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
- **Load-time self-test, and this is what makes a hand-declared ABI safe:**
  prep and call `abs(-3) == 3`, `ldexp(3,2) == 12`, `ldexpf(3f,2) == 12`, and
  a small struct return. Any failure, or a non-zero `ffi_prep_cif` status,
  disables the backend and falls back.
- `RAKUPP_FFI=0` kill switch; `RAKUPP_FFI=/path/to/libffi.so` to point at one;
  the live backend must be reportable (`--version` line or a debug flag) —
  bug reports and the CI matrix both need it.

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

### By-value structs

**Verify before designing.** As far as I can determine, Rakudo's NativeCall
passes `CStruct` arguments and returns as *pointers* only — so by-value is not
Rakudo-parity surface, it is new. A local Rakudo probe of a by-value struct
return (`div(7,2) --> DivT`) had not returned after several minutes; finish
that check before committing to a syntax.

Pointer-passing must remain the default for `CStruct`-typed parameters — the
existing tests depend on it — with by-value behind an explicit marker.

**So the headline libffi feature is not the main win here.** The wins, in
order of value: scalar-width correctness, unlimited arity, typed callbacks,
variadics. By-value structs are a bonus.

---

## 7. Risks

- **Hand-declared ABI on an untested target.** Mitigated by the load-time
  self-test plus fallback. CI must gain a leg running the whole suite with
  `RAKUPP_FFI=0`.
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
- **`--exe`.** Compiled binaries call the same `RT.callNative` and inherit
  everything, but `Codegen.cpp:529-543` still refuses `is native(&sub)`,
  expression libraries, `is rw` out-params and buffer parameters. This work
  does not change that; say so in the docs rather than implying otherwise.

---

## 8. Test plan

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
