# Plan: one ABI, two directions

*Written 2026-08-09, before any code. A **v4 pillar** — the substrate under
[EMBED-PLAN.md](EMBED-PLAN.md), which it corrects in one important place. See
[VERSIONS.md](VERSIONS.md).*

## The observation this plan exists for

An extension ABI shipped on 2026-08-09 — [`src/rakupp_ext.h`](../../../src/rakupp_ext.h)
(157 lines), [`src/ExtApi.cpp`](../../../src/ExtApi.cpp) (216), a
`rakupp-ext-load` builtin, and `Rakupp::JSON` as its first user (2.7 ms on a
278 KB document against ~440 ms for the same module's Raku fallback).

**That ABI is the harder half of an embedding API, already designed, already
shipped, already proven by a real distribution.** It has the value vocabulary,
the opaque-handle discipline, ABI-version negotiation, an error model, and the
rule that makes all of it survivable:

> An extension never sees `Value`.

That rule is not fastidiousness — `sizeof(Value)` went 392 → 376 → 344 in a
single afternoon and [REPRESENTATION-PLAN.md](REPRESENTATION-PLAN.md) intends
~204 next. Handles are what let the interpreter keep changing under a published
boundary.

Extensions are **native code called from Raku**. Embedding is **Raku called
from native code**. Same boundary, opposite direction, one value vocabulary —
or two, if nobody says otherwise.

### The correction

[EMBED-PLAN.md](EMBED-PLAN.md), written a day before the extension ABI landed,
proposed an opaque `rakupp_value*` with `rakupp_value_free`. **That is
retracted.** `RkValue` is the value type, `rk_int`/`rk_str`/`rk_hash`/`rk_at_pos`
are the accessors, and there will not be a second vocabulary for the same
values. Everything else in that plan — the phases, the per-host examples, the
threading contract — stands; this plan is the layer beneath it.

---

## What already exists — verified

| capability | today | where |
|---|---|---|
| opaque handles (`RkValue`, `RkCtx`) | **yes** | `rakupp_ext.h` |
| construct Int / big Int / Num / Rat / Str / Bool / Any | **yes** | `rk_int`, `rk_int_s`, `rk_num`, `rk_rat_s`, `rk_str`, … |
| construct Array / Hash, and their immutable forms | **yes** | `rk_array`/`rk_push`/`rk_list`, `rk_hash`/`rk_set`/`rk_map` |
| inspect: type, elems, positional, hash-by-index | **yes** | `rk_type`, `rk_elems`, `rk_at_pos`, `rk_key_at`/`rk_val_at` |
| arguments, positional and named | **yes** | `rk_argc`, `rk_arg`, `rk_named` |
| errors without exceptions crossing | **yes** | `rk_die` → `X::AdHoc` at the call site |
| ABI version negotiation | **yes** | `RAKUPP_EXT_ABI`, `rakupp_ext_init(host_abi)` |
| lifetime | **call-scoped arena** — a `std::deque<Value>`; handles die with the call, the return value is copied out first | `ExtApi.cpp` |
| build story for a distribution | **yes** | `Rakupp::JSON`'s `Build.rakumod`, with a quiet fallback |

The lifetime model deserves the emphasis. A `deque` rather than a `vector` so
thousands of handles never reallocate and invalidate; nothing to free; a handle
saved across calls is dangling *by construction rather than by accident*. It is
exactly right for an extension, and exactly wrong for a host that wants to keep
a result. That asymmetry is the single largest item below.

## What embedding adds

1. **Interpreter lifecycle.** `rk_new` / `rk_free`, plus a config for the three
   host-hostile behaviours EMBED-PLAN found (the 1 GiB stack thread, the
   process-wide `SIGPIPE`, the owned stdout).
2. **Evaluate and load.** `rk_eval(rk, src, &out)`, `rk_load_file`.
3. **`rk_call` — native code calling a Raku routine.** Missing today in *both*
   directions: an extension cannot call back into Raku either, which is why
   `Rakupp::JSON` implements `from-json` natively but leaves `to-json` to
   `JSON::Fast` (its README says so in as many words).
4. **A lifetime that outlives a call.** Rooted handles: `rk_root` / `rk_unroot`,
   or a host-owned scope. The arena stays the default for extension calls
   because it cannot leak; roots are the opt-in for hosts that must hold a
   value. This is where the two directions genuinely differ, so it is where the
   design has to be most explicit.
5. **Output capture** — the `rdbuf` swap Raku.js performs by hand today.
6. **A written thread contract**, which v3.0.0 made urgent: parallel is now the
   default (`RAKUPP_GIL=1` is the escape hatch), so host callbacks can arrive on
   several rakupp threads at once. See EMBED-PLAN's *Threads at the boundary*.

Items 3 and 4 are wanted by extension authors as well as hosts, which is the
strongest evidence that this is one ABI and not two.

---

## Why C, and not C++

The interpreter is C++17 throughout, so the boundary being plain C looks like an
odd choice from the inside. It is the only workable one, and the reasoning is
worth recording because it is the kind of decision that gets re-litigated.

**There is no C++ ABI to target.** The Itanium ABI standardises name mangling
and roughly stops there. `std::string` has a different size and layout under
libstdc++ and libc++; GCC 5's `_GLIBCXX_USE_CXX11_ABI` split `std::string`
against *itself*; exception object layout and the debug-versus-release shape of
`std::vector` are not guaranteed either. An extension built with Clang/libc++
handing a `std::string` to a rakupp built with GCC/libstdc++ does not fail to
link — it reads garbage, later, somewhere else.

That is not hypothetical here:

- the **Linux release links `libstdc++` statically**
  ([COMPILERS.md](../../guide/COMPILERS.md)), so a C++ type crossing into an
  extension built against the system libstdc++ would be crossing between two
  distinct copies of the standard library in one process;
- releases are built with **Clang**, gated on **GCC**, and use **MSVC** on
  Windows.

A C++ boundary would therefore mean *your extension must be built with the same
compiler, the same standard library and compatible flags as the binary you
downloaded* — which nobody installing a distribution from the ecosystem can
promise.

**Every host FFI speaks C, and none speaks C++.** `ctypes`, `bun:ffi`,
`Deno.dlopen`, `purego`, `DllImport`, Rust's `extern "C"`. Even if C++ were
stable, the binding story below would need a C shim in front of it — two
boundaries instead of one.

**Exceptions cannot cross regardless.** A C++ exception unwinding into Python or
Node is undefined behaviour, so errors have to be status-shaped: `rk_die` plus a
NULL return. That part of the C design is not a compromise, it is the only
correct answer.

**And what C++ would actually buy is what the one rule forbids** — passing
`Value` across directly. Sugar over opaque handles buys nothing the C API does
not already have, while making the layout freedom impossible to keep.

**C++ ergonomics survive anyway.** A header-only RAII wrapper (`rakupp.hpp`,
EMBED-PLAN's C++ example) compiles into the *caller*, so no C++ type ever
crosses the boundary. C ABI plus C++ header is strictly better than a C++ ABI:
the same ergonomics, none of the coupling — and Rust, Python and the rest get
the same treatment in their own idioms.

**What it costs, honestly.** No RAII on the lifetime story, so A1's rooted
handles have to be released by hand — which is exactly why roots are flagged
below as reintroducing a leak the arena made impossible. No overloads, hence the
slightly clumsy decimal-string route for bignums (`rk_int_s`, `rk_rat_s`). And
weaker compile-time type safety, since everything is an opaque pointer: a
mistake is a runtime error rather than a build failure.

**The one place C++ *is* already the interface** is `--exe`, which compiles
generated C++ against the installed `librakupp_rt.a` using whatever `$CXX` the
user has. That path carries exactly the risk described above. It is an argument
for keeping the extension and embedding boundary out of that category — not for
joining it.

## Why this makes bindings nearly free

Because the boundary is **plain C with opaque handles**, every host language
that can call C can bind rakupp **with no compiled glue at all**:

| host | mechanism | compiled glue |
|---|---|---|
| Python | `ctypes` / `cffi` | none |
| Bun | `bun:ffi` | none |
| Deno | `Deno.dlopen` | none |
| Node | `koffi` / `ffi-napi` (a napi addon only if measured to be worth it) | none |
| Rust | `libloading`, or a `-sys` crate | none |
| Go | `purego` (no cgo), or cgo | none |
| C# | `DllImport` | none |
| C / C++ | the header | n/a |

EMBED-PLAN.md priced each binding as a compiled extension per language. It does
not have to be: a stable C ABI means the first version of every binding is a
**pure-source package** wrapping `dlopen` + a function table. That is a
different order of cost, and it follows directly from a design decision that
has already been made and shipped.

**The one prerequisite:** rakupp must exist as a **shared library**. Today
`rakupp_rt` is `STATIC` with no `POSITION_INDEPENDENT_CODE` (`CMakeLists.txt:52`),
so there is nothing for a host to `dlopen`. That is phase A0 and it blocks
everything else here.

---

## Phases

### A0 — `librakupp` exists

A shared library exporting the C ABI, beside the static one `--exe` uses.
`POSITION_INDEPENDENT_CODE`, an explicit export list (the `rk_*` surface and
nothing else), and a version-scripted soname.

**Also fix what A0 exposes.** Extensions resolve `rk_*` from the *host
executable* today, the way a Python C extension resolves `Py_*`. On Windows
that needs an import library, and `CMakeLists.txt` sets neither `ENABLE_EXPORTS`
nor `WINDOWS_EXPORT_ALL_SYMBOLS` — so what [EXTENSIONS.md](../../guide/EXTENSIONS.md)
describes for Windows may not currently be producible. **Verify before
designing**; if it is broken, a shared `librakupp` fixes it as a side effect,
because then extensions link against the library rather than the executable.

**Gate:** `Rakupp::JSON` builds and passes its suite unchanged, against both the
executable and the shared library.

> **Outcome (2026-08-10).** Landed as planned, and the verification A0 demanded
> turned up more than the plan feared. What exists now:
>
> - **`-DRAKUPP_BUILD_SHARED=ON`** builds `librakupp` (`.dylib`/`.so`/
>   `librakupp.dll`) from the runtime sources — PIC, hidden visibility,
>   `VERSION`/`SOVERSION` from the project version, installed to `lib/`. OFF by
>   default: it doubles a clean build, and the plain CLI neither links nor
>   loads it. Release/CI builds turn it on.
> - **The export list is the header.** Each of the 26 entry points in
>   `rakupp_ext.h` is marked `RK_API` (`visibility("default")`, or `dllexport`
>   under `RAKUPP_BUILDING`); the library compiles `-fvisibility=hidden`, so
>   the marked surface is the only thing exported. Verified by `nm`: all 26
>   `rk_*`, zero `namespace rakupp` symbols — libc++'s own weak typeinfo rides
>   along, as it does in any hidden-visibility C++ dylib, and is the
>   toolchain's ABI rather than ours. The extension ABI stays at 1: the macro
>   changes no declaration's meaning.
> - **The Windows suspicion was true, and Linux was worse.** A plain ELF
>   executable keeps its symbols out of `.dynsym`, so on Linux and the BSDs an
>   extension's first `rk_*` call has always died with an undefined-symbol
>   error — extensions only ever actually worked on macOS. Fixed surgically:
>   the executable links `-Wl,--dynamic-list=src/rakupp_ext.dynlist` (the
>   `rk_*` glob, so a new entry point needs no edit) rather than `-rdynamic`,
>   which would have exported every C++ symbol as accidental ABI. On Windows,
>   `RK_API` plus `ENABLE_EXPORTS` now produce and install the import library
>   EXTENSIONS.md always promised. *Verified on macOS; the ELF and Windows
>   halves are build-system reasoning awaiting a CI run.*
> - **`-DRAKUPP_LINK_SHARED=ON`** is the test configuration the gate ran on:
>   the CLI linked against the shared runtime. It is deliberately named
>   `librakupp_testrt` and deliberately default-visibility (the CLI needs the
>   whole C++ surface), so it can never be mistaken for the shipping artifact.
> - **`tools/embed-smoke.raku`** is the standing gate: it compiles and runs
>   EMBED-PLAN E0's C++ host (`tools/embed/host.cpp`) against `librakupp_rt.a`,
>   and rejects a `librakupp` whose export table has lost an `rk_*` or leaked
>   an internal — it caught the 928-symbol test artifact the moment that
>   artifact briefly wore the shipping name.
>
> **Gate results:** `Rakupp::JSON` builds and passes 35/35 against both the
> static CLI and the shared-linked one, reporting the `native` backend on both;
> `perf-guard --check` OK; plain `rakupp` byte-identical in size with an
> unchanged linked-library list; Roast 197,105 assertions / 595 files fully
> passing / 11 timeouts (v3.0.1's four-run band: 197,056–197,098 / 593–595 /
> 12–16); `t/run.raku` 398/398; `--exe` compiles and runs.

### A1 — `rk_call` and rooted handles

The two additions both directions want. `rk_call(ctx, "name", args…)` invoking a
Raku routine from C, and a rooted lifetime for values that must outlive a call.

**Gate:** an extension that calls back into Raku, and a `to-json` in
`Rakupp::JSON` that beats `JSON::Fast` — the module is the ABI's regression
test, and this is the feature its README names as blocked.

> **Outcome (2026-08-10).** Landed, with **one correction to this plan and
> three interpreter bugs found by building the gate.**
>
> **The correction.** This plan said `to-json` was blocked on `rk_call`. It was
> not: the module's own pod names the real blocker, which is that ABI 1 offered
> only index-based hash access, `std::advance` from `begin()` on every call —
> so walking a hash was quadratic. Both were fixed, but they are different
> problems, and the plan was repeating a guess where the module had written
> down the answer.
>
> **ABI 2**, additive, with a **downgrade handshake** so nothing breaks: the
> host calls `rakupp_ext_init` with its own version and retries downward, so
> the `host_abi == RAKUPP_EXT_ABI` test every ABI-1 extension was written with
> still succeeds. Proven by building an extension against the *previously
> committed* header and loading it into the new host unchanged; an extension
> claiming a version newer than the host is refused with a sentence that says
> what to do. New surface:
>
> - `rk_call` / `rk_call_value` / `rk_can` — native code calling Raku, by name
>   or on a Code value it was handed. Names resolve the way the extension's own
>   call site would resolve them: the invoking lexical scope, then GLOBAL.
> - `rk_error` / `rk_clear_error` — a Raku exception inside `rk_call` never
>   unwinds through C frames. It comes back as NULL plus a pending error, and
>   if the extension returns NULL the **original exception is re-raised with
>   its type intact**, so `CATCH { when X::Whatever }` still works across a C
>   frame. Gated on exactly that.
> - `rk_root` / `rk_unroot` — the host-side lifetime, over a mutex-guarded
>   store because parallel is the default since v3.0.0. `rk_unroot` refuses a
>   handle it never issued, which is the only safety C allows here.
> - **Hash walks are amortised O(1)**, via a remembered position rather than a
>   cursor type — no new ABI surface, and it invalidates itself if the hash is
>   written during a walk. 40,000 keys serialise in 9.6 ms, a flat 0.24 µs per
>   key from 5,000 up; the old walk would have needed ~800 million iterator
>   steps for the same document.
>
> **Gate results.** `tools/embed/callback-ext.c` + `ext-callback.raku` exercise
> every entry point above and run in `embed-smoke.raku`. Native `to-json`
> ships in `Rakupp::JSON`, byte-identical to `JSON::Fast` across 117 checks on
> both engines — including the two escapes JSON::Fast does *not* write as `\b`
> and `\f`, and identical **refusal** for a Range, because a fast path that
> invented an encoding would be a worse bug than a slow one. On the 278 KB
> corpus:
>
> | | parse | serialise |
> |---|---:|---:|
> | Rakudo + JSON::Fast | 36 ms | 40.8 ms |
> | rakupp + JSON::Fast | ~440 ms | 329.5 ms |
> | rakupp + Rakupp::JSON | 2.7 ms | **3.6 ms** |
>
> **The three bugs the gate found**, each general and each now filed rather
> than folded in here:
>
> 1. **`|c` flattened a single Array argument** — `sub f(|c)` got 3 positionals
>    from `f([1,2,3])` where Rakudo gets 1. This had *already* broken the
>    shipped module: `to-json([1,2,3])` returned `1`, because the sub was
>    declared `(|c)`. The idiom `sub wrapper(|c) { inner(|c) }` is everywhere.
>    **Fixed**: a capture was sharing `+@a`'s single-argument rule, and now takes
>    the no-flatten path — it is the argument list as passed, not a slurpy that
>    gets to reinterpret it. Roast exactly neutral.
> 2. **`my $x; $x === Any` was False** — an untyped undefined slot holds a
>    different representation from the `Any` type object, while a typed one
>    (`my Int $y; $y === Int`) was correct. **Fixed**: identity now agrees with
>    `.WHICH`, which already rendered both as `Any|`. Worth +5 Roast assertions
>    in `S02-types/{nil,autovivification}.t`, and it was the ABI's own bug too —
>    every JSON null an extension returned failed the idiom used to test for one.
> 3. **An untyped parameter accepted `Mu`**, where Rakudo applies the implicit
>    `Any` constraint and refuses. **Fixed**, and the fix turned up the rule that
>    made it subtle: an untyped parameter is Any-constrained only in a ROUTINE.
>    A Block's is Mu-constrained, so `(-> $x {…})(Mu)` is legal while
>    `sub f($x) {…}; f(Mu)` is not — and `for (Mu) -> $x` and `.map(-> $x {…})`
>    depend on that, which is how a too-eager version announces itself. Still
>    open, and deliberately: the same laxness in *assignment*
>    (`my Any $x = Mu`), which is a separate allow-list where admitting `Any`
>    needs `undefOk` to understand subtyping first, or `my Any $x = Int` starts
>    failing wrongly.
>
> That is the argument for gates that use a real distribution rather than a
> test double: none of the three is about the ABI, and all three were sitting
> in paths a synthetic test would not have walked.

### A2 — `rakupp.h`: lifecycle, eval, output

`rakupp.h` includes `rakupp_ext.h`; the extension header stays standalone so a
module author never sees the embedding surface. Then **port Raku.js onto it**
and delete `rakupp_web.cpp`'s private shim — the proof the API is usable by a
real embedder, available before any binding exists.

> **Outcome (2026-08-10).** Landed, and the port did the job it was put in the
> plan to do: it changed the header.
>
> [`src/rakupp.h`](../../../src/rakupp.h) + `EmbedApi.cpp`. `rk_new`/`rk_free`
> with an `RkConfig` carrying the three host-hostile behaviours — the 1 GiB
> stack thread, the process-wide `SIGPIPE`, the owned stdout — **all default
> off**, and the struct carries its own `size` so it can grow without breaking a
> host compiled against an older copy. `rk_eval` over `Interpreter::evalString`,
> so an interpreter is a *session*: `my $x = 41` then `$x + 1` gives 42, which
> is not a new mechanism but the one the REPL has always used. `rk_last_error`,
> `rk_set_output`, `rk_set_input`.
>
> **`rk_ctx` is where the two directions meet.** It hands back the same `ExtCtx`
> an extension is given, so a host gets `rk_int_get`, `rk_at_pos`, `rk_root` —
> and `rk_call`, meaning **a host calls a Raku routine through the entry point
> A1 built for extensions**. One vocabulary, and the C host proves it.
>
> **What the port changed.** Raku.js does not want `rk_eval`. A playground needs
> whole-*program* semantics: MAIN dispatch, `exit`, and a parse error printed as
> `===SORRY!===` rather than handed back as a string. So **`rk_run` was added**,
> and `Runtime.cpp` grew `rakuppRunOn(Interpreter&, …)` — `rakuppRun` is now
> that plus "make me an interpreter first", which is what lets a host run a
> program in the interpreter it already has instead of silently acquiring a
> second one whose construction would take the process globals off the first.
> That is the difference between an API designed against its callers and one
> designed against the C++ it wraps, which is the failure mode EMBED-PLAN names.
>
> `rakujs/rakupp_web.cpp` now includes `rakupp.h` and nothing from the
> interpreter: the hand-rolled `std::cin` rdbuf swap is `rk_set_input`, and
> `rakuppRun` + `Interpreter` is `rk_run`. The `fsync` dance stays, correctly —
> that is Emscripten's TTY device, not something an embedding ABI should own.
> A fresh interpreter per run is what keeps one program's `my $x` out of the
> next, and it is now one `rk_new`/`rk_free` pair rather than an implicit
> consequence of how `rakuppRun` happened to work.
>
> **Verified**, with the caveat that Emscripten is not installed on this
> machine: the ported file compiles and links against the real headers and
> runtime natively (only the `EMSCRIPTEN_KEEPALIVE` macro stubbed), and running
> it reproduces the old behaviour — stdin fed, exit code 0, `===SORRY!===` and
> exit 1 on a parse error, highlight and version intact, and no sub, variable or
> stdin leaking between runs. **A real `rakujs/build.sh` run is still owed.**
>
> **Gates:** the C host (`tools/embed/embed-host.c`, plain C99 — every FFI
> binding reaches this ABI through a C declaration, so if it needed C++ the
> header would be wrong) passes 20 checks in `embed-smoke.raku`; `librakupp`
> exports all 42 `rk_*` with no internals leaked; plain `rakupp` grew 48 bytes
> with an unchanged library list and unmoved startup.
>
> **Still open from EMBED-PLAN's list:** host functions callable from Raku
> (`rakupp_register`) — an extension can already be loaded to do this, so it is
> a convenience rather than a gap, and it wants the threading contract settled
> first; and multiple interpreters per process, still E5. `rk_new` returns NULL
> for a second live interpreter rather than corrupting the first.

### A3 — the FFI bindings

Python, Bun, Deno, Node — pure-source packages over the shared library, no
compiled glue. Rust and Go alongside if they cost what the table above says
they cost. Each one is a test of A2's design: if a binding needs something
awkward, the header changes rather than the binding.

> **Note from A0 (2026-08-10):** on ELF hosts a binding must load `librakupp`
> into the global namespace — `ctypes.CDLL(..., mode=ctypes.RTLD_GLOBAL)` and
> equivalents — or a Raku extension dlopen'ed *afterwards* cannot resolve
> `rk_*` from it: an `RTLD_LOCAL` library is invisible to later lookups.
> macOS's `dynamic_lookup` searches all loaded images and has no such
> requirement. This belongs in every binding's loader, not its README.

### A4 — WebAssembly, which is the exception

WASM has **no `dlopen`**, so neither of the two mechanisms above applies:

- extensions cannot be loaded dynamically → a **static registry**
  (`rakupp_ext_register(&mod)` at link time) so a WASM build can carry
  extensions compiled in;
- the host does not FFI into a shared library → the same C ABI must appear in
  Emscripten's `EXPORTED_FUNCTIONS`, reached by `ccall`/`cwrap`.

The static registry is worth having on native too: it is what a `--exe` binary
needs in order to carry an extension the way it already carries modules, and it
connects to [MODULES-PLAN.md](MODULES-PLAN.md)'s standalone-binary work — a
program using `Rakupp::JSON` cannot currently be a self-contained binary,
because the `.so` is a resource loaded from disk.

Also: the WASM build is single-threaded, so the parallel default must be off
there, and the thread contract has to say so.

### A5 — compiled bindings, only where measured

A napi addon or a PyO3-style extension *only* if the FFI version is measured to
be too slow for a real workload. Not before.

---

## ABI stability policy

The moment a wheel or an npm package exists, this header is a contract with
strangers.

- **Additive only.** New functions append; existing signatures never change
  meaning. `RAKUPP_EXT_ABI` bumps only when something existing changes, and a
  bump must keep the old entry point working for at least one release.
- **Embedding-only additions must not bump the extension ABI.** An extension
  built today keeps loading; that is the promise the version negotiation was
  written for.
- **Nothing about `Value` ever leaks** — not its size, not its layout, not a
  field, not through a macro. REPRESENTATION-PLAN's freedom to reach ~204 bytes
  depends on it, and one leak makes that permanent.
- **Every entry point catches everything.** A C++ exception unwinding into
  Python or Node is undefined behaviour, not a bug report.

---

## Gates

1. **`Rakupp::JSON` is the ABI's regression test** — it builds, loads and passes
   its suite unchanged after every batch here. It is a real distribution outside
   this repo, which is exactly what makes it a good gate.
2. **Roast** zero regressions; **module battery** unchanged; **`perf-guard
   --check`**.
3. **Plain `rakupp` pays nothing** — size, linked-library list and startup time
   unmoved by the existence of a shared library. Checked, not assumed.
4. **Raku.js on the public API**, examples byte-identical (A2 onward).
5. **The threaded matrix** from EMBED-PLAN, in whichever modes a binding claims.
6. **`--exe` still works**, and the WASM build still builds.

---

## Risks, named

- **Two ABIs drifting apart.** The whole reason this plan exists. One header
  including the other, one value vocabulary, one lifetime story with a
  documented exception.
- **`--slim` versus an exported ABI.** [SLIM-PLAN.md](SLIM-PLAN.md) makes the
  **parser** strippable — but `rk_eval` *is* the parser. A slim build and an
  embeddable build want opposite things, so `librakupp` cannot simply inherit
  the slim defaults, and the two plans have to agree on which features the
  exported ABI pins. This is a real conflict between two v4-era campaigns and
  it is better found here than in a release.
  *Resolved 2026-08-11, before SLIM's code started:* **`librakupp` pins
  `eval`** — always the real parser archive, never the stub, and `--slim` is a
  property of `--exe` output only. Recorded in SLIM-PLAN (the constraint
  section and its risk list); the parser is ~6 points of SLIM's 51, so the
  campaign keeps essentially its whole win. The Unicode tables stay cuttable
  for a slim `librakupp` variant if one is ever wanted.
- **Windows symbol resolution** — see A0; possibly already broken for
  extensions, and it is the platform where "resolve from the host executable"
  is least natural.
- **The arena is load-bearing.** Rooted handles reintroduce the possibility of
  a leak, which the extension ABI currently makes impossible by construction.
  Roots must be opt-in, host-only, and never the default an extension sees.
- **`Value` is moving under us** — 392 → 344 → ~204. The handle discipline is
  what makes that safe; every new entry point has to be checked against it
  rather than assumed compliant.
- **Host callbacks on rakupp threads**, now that parallel is the default.
- **Scope creep into "expose all of Raku".** The vocabulary is deliberately
  coarse — `RK_OTHER` means *stringify it*. Growing it to the full type
  hierarchy would trade the freedom the plan is built on for convenience.

---

## Non-goals

- Exposing `Value`, or any C++ type, in any form.
- Calling Python/JS *from* Raku as a language feature — that is NativeCall's
  and `Inline::*`'s territory, and a different plan.
- A stable **C++** API. C is the boundary.
- Growing `RkType` into Raku's type hierarchy.
- Multiple interpreters per process — still EMBED-PLAN's E5, still blocked on
  the process-global singletons.
