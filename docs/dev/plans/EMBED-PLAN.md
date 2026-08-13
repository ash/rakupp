# Plan: rakupp as a library — one C API, many hosts

*Written 2026-08-08, before any code. A **v4 pillar** alongside
[MODULES-PLAN.md](MODULES-PLAN.md) — see the forming v4 section in
[VERSIONS.md](VERSIONS.md#v400--raku-that-travels-forming-2026-08-08).*

> **Read [ABI-PLAN.md](ABI-PLAN.md) first.** The native extension ABI shipped
> on 2026-08-09, one day after this was written, and it is the harder half of
> an embedding API — already designed, already proven by `JSON::Native`. This
> plan's *value* API is superseded by it (`RkValue`, not `rakupp_value*`), and
> the per-language cost estimates below are pessimistic: a stable C ABI means
> most bindings need no compiled glue at all. The phases, the per-host examples
> and the threading contract here still stand.

Two asks that arrived together:

1. **rakupp as a library** — link the interpreter into another program.
2. **Raku embedded in Python et al.** — call Raku from a host language.

## Yes, they are connected — and the shape matters

They are one substrate and N thin consumers. **(1) is the project; (2) is a
shim per language.** A Python module, a Node addon and the existing WebAssembly
build are all the same handful of C entry points wearing different clothes.

The connection is only that clean if the C API is designed **for bindings from
the start**. The failure mode is designing a C++-shaped API for C++ consumers —
`Interpreter&`, `Value`, exceptions, `std::string` — and then discovering that
no binding can express it, so each one grows its own private shim. Which is
exactly the state today (see below).

---

## Where we are — verified, not assumed

Checked against this tree on 2026-08-08.

**rakupp is already a library, for C++ callers.** `rakupp_rt` is a static
library carrying everything except the CLI entry point and the REPL; `cmake
--install` puts it in `lib/` with every header in `include/rakupp/`, because
`--exe` already links generated C++ against it. So the linking half exists and
is exercised on every `--exe` compile.

**An embedding already ships, in production.**
[`rakujs/rakupp_web.cpp`](../../../rakujs/rakupp_web.cpp) is 95 lines of
`extern "C"` over `rakuppRun()` — `rakupp_run(src, stdin_text)`,
`rakupp_highlight(src)`, `rakupp_version()` — living entirely outside `src/`
and powering raku.online. This is the plan's best evidence: the idea is proven,
and what it lacks is exactly the specification of what to build.

What Raku.js **cannot** do marks the boundary of today's surface: run a whole
program and get an `int` back. No calling a sub, no getting a value out, no
passing data in except as a blob of pretend stdin, no host function callable
from Raku. It also has to route stdout through a manual `std::cin`/`cout`
`rdbuf` swap, and it deliberately avoids `rakuppRunBigStack` because it cannot
spawn the thread — a workaround every future embedder would otherwise rediscover.

**The single most important precondition already holds:** there are **zero
`exit()` / `abort()` calls in the runtime**. Every process-exit path lives in
`main.cpp`. A library that kills its host is unembeddable, and rakupp is not
one.

### What blocks it, specifically

1. **`onBigStack` does three things a library must not do to its host.** It
   spawns a pthread with a 1 GiB stack and joins it; it sets
   `signal(SIGPIPE, SIG_IGN)` **process-wide**; and it flushes `std::cout` /
   `std::cerr`. Uninvited, a library may do none of these. All three are right
   for the CLI and must become opt-in.
2. **One interpreter per process.** Process-global singletons wire up *the*
   interpreter: `g_cbInterp` (the NativeCall callback trampoline target),
   `g_matchClasses`, `g_deproxy`, `g_objListItems`, `g_subsetCheck`. Two
   instances would fight over them.
3. **Execution state is per-thread** — 63 `thread_local` declarations. Whatever
   the threading contract turns out to be, it has to be written down rather
   than discovered.
4. **`rakupp_rt` is `STATIC` with no `POSITION_INDEPENDENT_CODE`.** Every
   language binding is a shared object. Today it cannot be linked into one.

---

## The API

C, not C++. Opaque handles, no exceptions crossing the boundary, no `std::`
types in the header, UTF-8 everywhere. Roughly fifteen functions:

- **lifecycle** — `rakupp_new()`, `rakupp_free()`, `rakupp_version()`, and a
  `rakupp_config` for the three host-hostile behaviours in blocker 1 (own
  stack / touch signals / own stdout), all defaulting to *off*.
- **evaluate** — `rakupp_eval(rk, src, &out)` and `rakupp_run_file(...)`,
  returning a status code and a value handle.
- **call** — `rakupp_call(rk, "name", argv, argc, &out)` to invoke a Raku sub
  with marshalled arguments.
- **values** — ~~an opaque `rakupp_value*` … plus `rakupp_value_free`~~
  **superseded.** The native extension ABI shipped the day after this was
  written and already defines the vocabulary: `RkValue`, `RkType`, `rk_int` /
  `rk_str` / `rk_hash` / `rk_at_pos` and the rest, in
  [`src/rakupp_ext.h`](../../../src/rakupp_ext.h). There will not be a second
  value type for the same values — see [ABI-PLAN.md](ABI-PLAN.md), which is the
  layer beneath this plan. What embedding adds there is a **rooted** lifetime,
  since the extension arena is deliberately call-scoped.
- **errors** — every entry point returns a status; `rakupp_last_error(rk)`
  yields message, line and file. A Raku exception becomes an error, never a
  C++ exception unwinding through the host's frames.
- **host functions** — `rakupp_register(rk, "name", fn, userdata)`: a C
  function callable from Raku code. The NativeCall callback trampoline
  (`g_cbInterp`) is the existing precedent for the mechanism.
- **output** — `rakupp_set_output(rk, cb, userdata)`: the `rdbuf` swap Raku.js
  performs by hand, made API, so a host captures what Raku prints instead of
  sharing its own stdout.

---

## What it looks like from each host

**None of this exists yet.** It is the API designed against its callers rather
than against the C++ it wraps — which the section above names as the failure
mode, so the examples are part of the design, not decoration. If a host cannot
express something below cleanly, the header is wrong and gets changed before
the binding does.

Every binding is the same five verbs: **evaluate**, **call a Raku routine**,
**move a value across**, **register a host function**, **capture output**.

### C — the API itself

```c
#include <rakupp.h>

rakupp* rk = rakupp_new(NULL);            /* NULL = default config */
rakupp_value* v;

if (rakupp_eval(rk, "(1..10).grep(*.is-prime).sum", &v) != RAKUPP_OK)
    fprintf(stderr, "raku: %s\n", rakupp_last_error(rk));
else
    printf("%lld\n", rakupp_value_int(v));  /* 17 */

rakupp_value_free(v);
rakupp_free(rk);
```

### C++ — the one that already half-works

Today a C++ program can link `rakupp_rt` and call `rakuppRun()` — that is how
`--exe` output works, and it is all-or-nothing: a whole program, an exit code.
What E1 adds is everything between:

```cpp
#include <rakupp.hpp>            // header-only RAII over the C API

rakupp::Interp rk;
rk.eval(R"(sub score($s) { $s.comb.grep(/<[aeiou]>/).elems })");
int n = rk.call<int>("score", "embedding raku");     // 5

// a host function Raku can call back into
rk.on("log", [](std::string m) { std::cerr << "[app] " << m << "\n"; });
rk.eval(R"(log("grammar loaded"))");

// and the reason you would: rules that ship separately from the binary
rk.eval_file("rules/pricing.raku");
auto ok = rk.call<bool>("applies", order);
```

*Why:* a game, an editor or a server gets a scripting and rules language whose
grammars and multi-dispatch are genuinely good at that, without embedding a
200 MB runtime.

### Python

```python
import rakulang

rk = rakupp.Interpreter()
rk.eval('say "hello from Raku"')

total = rk.eval('(1..10).grep(*.is-prime).sum')        # -> 17 (int)

rk.eval('sub greet($who) { "hi, $who" }')
print(rk.call('greet', 'world'))                        # -> hi, world

# a host function, callable from Raku
rk.register('py_slug', lambda s: s.lower().replace(' ', '-'))
rk.eval('say py_slug("Some Title")')                    # -> some-title

# the real use: Raku grammars as a parsing service inside a Python pipeline
rk.eval_file('grammars/logline.raku')
for line in log:
    rec = rk.call('parse-line', line)                   # -> dict
```

*Why:* Raku grammars are better than a pile of regexes, and this is how a
Python data pipeline borrows them without a subprocess per line.

### JavaScript / TypeScript

Two hosts, one API. The **browser** case exists today in crude form — this is
the real code behind raku.online, and E1 replaces it:

```js
// today: one call, a whole program, an exit code
const rc = Module.ccall('rakupp_run', 'number', ['string', 'string'], [src, stdin]);
```

```ts
// proposed, both Node (napi) and browser (WASM) behind one package
import { Interpreter } from '@rakupp/embed';

const rk = await Interpreter.create();
const primes = rk.eval<number[]>('(1..30).grep(*.is-prime).List');

rk.eval('sub titlecase($s) { $s.split(" ").map(*.tc).join(" ") }');
rk.call<string>('titlecase', 'hello there world');      // "Hello There World"

rk.register('now', () => Date.now());
rk.eval('say now()');
```

*Why:* the playground and editor tooling already need it, and a Node host gets
Raku's text handling without a subprocess.

**Open question, named:** host functions that are `async`. Raku's call into
`now()` above is synchronous, so a JS callback returning a `Promise` has
nothing to await it. Options are to forbid async callbacks, to pump the host's
event loop from inside the call, or to expose them as Raku `Promise`s — this is
a design decision E4 has to make, and the browser build constrains it hardest
(no threads to block on).

### Rust

```rust
let rk = rakupp::Interp::new()?;
rk.eval("sub fib($n) { $n < 2 ?? $n !! fib($n-1) + fib($n-2) }")?;
let n: i64 = rk.call("fib", (25,))?;                 // 75025
rk.register("emit", |s: &str| tracing::info!("{s}"))?;
```

Go (cgo), Ruby and C# reach the same header the same way; none of them needs
anything from the interpreter that the four above do not.

---

## Phases

- **E0 — embeddability hygiene. No API yet.** Add
  `POSITION_INDEPENDENT_CODE` and an optional `SHARED` target; make the
  big-stack thread, the SIGPIPE disposition and the stdout flush opt-in rather
  than automatic. Land a smoke test that links `rakupp_rt` into a tiny C++ host
  and runs Raku from it — **that test is the standing regression gate for
  everything below**, and it is worth having even if the campaign stops here.
- **E1 — `rakupp.h` and the core**: eval, values, errors. Then **port Raku.js
  onto it** and delete `rakupp_web.cpp`'s bespoke shim. That port is the proof
  the API is usable by a real embedder, and it is a proof available before any
  binding exists.
- **E2 — the two-way boundary**: calling Raku subs from the host, and host
  functions callable from Raku. This is where an embedded language stops being
  a calculator.

  > **E2 complete 2026-08-12.** The host→Raku half was A1's `rk_call`; the
  > other direction is `rk_register(rk, name, fn, userdata)` (RAKUPP_ABI 2):
  > the function receives the same RkCtx an extension sub gets plus its
  > userdata, and the installation reuses extLoadModule's wrapping verbatim —
  > one mechanism, so a registered host function is indistinguishable from an
  > extension sub. Gated in embed-host.c (a C `host-add` called from Raku,
  > composing inside `[+] (1..5).map(...)`). The thread warning in the
  > header is load-bearing: Raku may call the function from threads it
  > started.
- **E3 — Python.** A wheel wrapping the C API. The specific risks are below.

  > **Done 2026-08-12**: the ctypes binding (bindings/python, G0) plus
  > `tools/build-wheel.sh` — a platform wheel with librakupp bundled in the
  > package's `_lib/`, built on the macOS (universal) and Linux release legs
  > and attached as a release asset. Verified end to end: installed in a
  > clean venv on a machine path with NO rakupp, parses. PyPI publication
  > stays a manual decision.
- **E4 — Node (napi), and whatever else the same header supports.** Each of
  these should be small. If one is not, E1's design was wrong and that is the
  signal to fix the header rather than the binding.

  > **Served FFI-first 2026-08-12, as A5 prescribes**: JS/TS on `bun:ffi`
  > (bindings/js), Go on cgo, Rust zero-dep, C++ header-only — each a few
  > hundred lines, each byte-identical in the grammar gate. Every one WAS
  > small, which is E1's design validated. A napi addon stays A5-gated:
  > only if the FFI version measures too slow on a real workload.
- **E5 — more than one interpreter per process.** Move the globals in blocker 2
  into `Interpreter`. A separate campaign; v1 documents the limit instead of
  hiding it.

---

## Threads at the boundary

This campaign lands **after v3's parallel flip**, which removes rakupp's GIL:
`RAKUPP_PARALLEL` becomes the default and `RAKUPP_GIL=1` survives only as an
escape hatch ([PARALLEL-PLAN.md](PARALLEL-PLAN.md) P5). That deletes half of
the interaction this section used to describe — and makes the remaining half
harder, not easier, so it is worth being precise about what changes.

**What goes away.** There is no longer a rakupp-side global lock to reason
about, so nothing has to be coordinated between two interpreter locks.

**What survives, one-sided.** The *host's* lock does not go anywhere. CPython
still has a GIL in its default builds (the free-threaded builds are not what a
wheel can assume), so the binding must still release it around a `rakupp_eval`
and acquire it inside every host callback. That requirement was never symmetric
and is unaffected by anything v3 does.

**What gets worse.** With our GIL gone, Raku code inside an embedded
interpreter can run on **several threads at once** — so several rakupp threads
can call registered host functions **simultaneously**. Under the GIL those
callbacks were serialised for free. After the flip they are not, and the host
sees genuine concurrent entry:

- **Python** — each calling thread needs its own thread state, not merely the
  GIL, and the callback must be re-entrant.
- **Node** — a callback from a non-JS thread has to go through a thread-safe
  function; calling a napi function directly from a rakupp worker is undefined
  behaviour, not a race to be fixed later.
- **Rust** — registered closures become `Send + Sync`, which is a signature
  change, not an implementation detail.
- **C++** — the host's own data structures are now touched from threads it
  never created.

So the general statement, which outlives any particular host: **host callbacks
arrive on threads the host did not create, possibly several at once.** That
belongs in `rakupp.h`'s documented contract from the first commit, because
every binding's API shape depends on it.

**Which suggests the sequencing.** Binding v1 can pin `RAKUPP_GIL=1` — the
escape hatch v3 keeps, and which CI keeps exercising — so the first Python
wheel faces the easy, serialised case, and concurrent callbacks become an
explicit later phase with its own threaded test matrix rather than a property
the first release accidentally promises. Whether to do that or take the
concurrency head-on in E3 is the one real decision this section leaves open.

**Which thread Raku runs on** is the other half, and it is unchanged by v3: if
the interpreter uses its own 1 GiB stack thread, a host function registered
from Python is invoked on *that* thread and not on the one that called in.
This is a direct consequence of blocker 1, and why E0 makes the stack behaviour
configurable rather than assuming either answer.

## Python, specifically

Beyond the threading contract above, the binding is small. What is left is
mundane but real: the runtime is ~10 MB linked and that rides in the wheel, and
wheels are per-platform — macOS (universal), Linux (manylinux) and Windows.

---

## Gates

1. **Roast** — zero regressions, and the **module battery** unchanged.
2. **`perf-guard --check`** — the CLI must not pay for the library. Explicitly:
   plain `rakupp` keeps its size, its linked-library list and its startup time.
   Checked, not assumed.
3. **The C-host smoke test** (E0) passes on all three platforms.
4. **Raku.js runs on the public API** and its examples stay byte-identical
   (E1 onward). The playground is the canary for the embedding surface.
5. **The threaded test matrix** (E3 on) — single-threaded host, multi-threaded
   host, and — the case v3's flip creates — **several rakupp threads calling
   host functions at once**, in whichever modes the binding claims to support
   (`RAKUPP_GIL=1` only, or the parallel default too), under TSan where the
   platform allows.
6. **`--exe` still works**, since it is the oldest consumer of `rakupp_rt`.

---

## Risks, named

- **The host-hostile trio** (thread, signals, stdout). E0 exists solely to
  neutralise them; anything built before that inherits them.
- **ABI stability.** The moment a wheel exists, the C header is a contract.
  Version it from the first commit and keep it additive.
- **One instance per process** is a limit users will hit — a web server wanting
  an interpreter per request, say. Document it loudly rather than let it be
  discovered.
- **Exceptions.** A C++ exception unwinding into Python or Node is undefined
  behaviour. Every entry point needs a catch-all at the boundary; this is
  mechanical but must be systematic, not per-function discipline.
- **Scope creep into an FFI project.** This is about hosts calling *into* Raku.
- **Two APIs drifting.** If Raku.js keeps its private shim, the public API will
  quietly rot. Porting it in E1 is the forcing function, not a nice-to-have.

---

## Non-goals

- **Calling Python from Raku.** That is the opposite direction (an
  `Inline::Python`), a different project, and not this one.
- Replacing NativeCall.
- An ABI-stable **C++** API. C is the boundary; C++ callers keep including the
  headers as `--exe` does today.
- Multiple interpreters per process in v1 (E5, and separately gated).
- Sandboxing. An embedded rakupp has the host's privileges, and pretending
  otherwise would be worse than saying so.
