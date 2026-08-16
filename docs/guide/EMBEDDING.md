# Embedding Raku++ — running Raku from your own program

The mirror of [EXTENSIONS.md](EXTENSIONS.md). That one is native code called
**from** Raku; this one is Raku called **from** native code. Same boundary, same
value vocabulary, one header including the other — because a project that grew
two ways to spell "a Raku value" would spend the rest of its life keeping them
in agreement.

- **The header:** [`include/rakupp/rakupp.h`](../../include/rakupp/rakupp.h), installed to
  `<prefix>/include/rakupp/rakupp.h`. It includes `rakupp_ext.h`, so everything
  in the [extension ABI](EXTENSIONS.md#the-abi) — `rk_int_get`, `rk_at_pos`,
  `rk_call`, `rk_root` — is available to a host too.
- **Worked example:** [`tools/embed/embed-host.c`](../../tools/embed/embed-host.c),
  plain C99, run as a gate by `tools/embed-smoke.raku`.
- **Real embedder:** [`rakujs/rakupp_web.cpp`](../../rakujs/rakupp_web.cpp) —
  the WebAssembly entry point behind raku.online, written against this API.

## Hello

```c
#include <rakupp/rakupp.h>
#include <stdio.h>

int main(void) {
    RkInterp rk = rk_new(0);              /* 0 = default config */
    RkValue v;

    if (rk_eval(rk, "(1..10).grep(*.is-prime).sum", &v) == RK_OK)
        printf("%lld\n", rk_int_get(rk_ctx(rk), v));      /* 17 */
    else
        fprintf(stderr, "raku: %s\n", rk_last_error(rk));

    rk_free(rk);
}
```

Link against `librakupp` (built with `-DRAKUPP_BUILD_SHARED=ON`) or against
the static archive set that `--exe` already uses —
`librakupp_{rt,parse,ucd_names,ucd_coll,ucd_props}.a`, inside
`-Wl,--start-group … -Wl,--end-group` on GNU ld (rt and parse reference each
other; ld64 and link.exe iterate on their own and need no group).

## An interpreter is a session

`rk_eval` runs in the mainline scope and keeps it, exactly as the REPL does:

```c
rk_eval(rk, "my $x = 41", 0);
rk_eval(rk, "$x + 1", &v);            /* 42 */
rk_eval(rk, "sub twice($n) { $n * 2 }", 0);
```

…which is what makes the next part work.

## Calling a Raku routine

There is no separate host-side call API, because the extension ABI already had
one. `rk_ctx` hands you the context and you use `rk_call`:

```c
RkCtx c = rk_ctx(rk);
RkValue arg = rk_int(c, 21);
RkValue out = rk_call(c, "twice", &arg, 1);       /* 42 */
```

If the routine throws, `rk_call` returns NULL and `rk_error(c)` has the message
— a Raku exception never unwinds through your frames, because C++ unwinding into
C is undefined behaviour rather than a diagnosable bug.

## `rk_eval` or `rk_run`?

Two different jobs, and picking the wrong one is the most likely early mistake.

| | `rk_eval` | `rk_run` |
|---|---|---|
| Runs | an expression, in the session | a whole program |
| Gives you | the value | the exit code |
| On failure | `RK_ERROR` + `rk_last_error` | prints `===SORRY!===` to stderr, exit code 1 |
| `MAIN`, `exit`, phasers | no | yes |
| Use it for | Raku as a scripting/rules language | hosting Raku *as* Raku |

The playground uses `rk_run`; a game embedding Raku for its rules wants
`rk_eval`.

## Lifetime

**A value from `rk_eval` is valid until the next `rk_eval` on that
interpreter.** Each evaluation releases what the previous one produced — the
host-shaped spelling of the extension rule that a handle dies with its call.

To keep one longer, root it:

```c
RkValue kept = rk_root(c, v);   /* survives any number of evaluations */
...
rk_unroot(c, kept);             /* you free it */
```

That is the one place this ABI lets you leak, which is exactly why it is opt-in
and why the arena — where you cannot — is the default.

## Output and input

```c
void on_output(void* ud, const char* text, size_t len, int is_err) { ... }
rk_set_output(rk, on_output, 0);        /* capture everything Raku prints */
rk_set_output(rk, 0, 0);                /* hand the streams back */

rk_set_input(rk, "line one\n", 9);      /* `get`/`lines` read this, then EOF */
```

Without `rk_set_input`, a Raku program calling `get` blocks on the host's real
stdin — which a server or a GUI may not have.

## What a library must not do to its host

Three things the CLI does and this does **not**, unless asked:

```c
RkConfig cfg = {0};
cfg.size = sizeof cfg;
cfg.own_stack      = 1;   /* run Raku on a big-stack thread (deep recursion) */
cfg.handle_sigpipe = 1;   /* ignore SIGPIPE process-wide (TCP servers want it) */
cfg.own_stdout     = 1;   /* flush cout/cerr after each evaluation */
RkInterp rk = rk_new(&cfg);
```

All default off. `SIGPIPE` in particular is a **process-wide** disposition: it is
your signal handling, not ours, so you have to ask. Set `size` and the struct can
gain fields without breaking you.

## Threads

One interpreter, one thread, unless you serialise access yourself.

Raku code *inside* the interpreter still uses as many threads as it likes —
`start`, `race`, `hyper` — and since v3.0.0 that is the default rather than
opt-in. So a callback reached from Raku can be entered on a thread you never
created, and by more than one at once. Make those re-entrant.

## Limits worth knowing

- **One interpreter per process.** `rk_new` returns NULL if one is already live:
  the interpreter wires up process-global state at construction, so a second
  would quietly break the first. Sequential create/free is fine. Concurrent
  instances are a separate piece of work
  ([EMBED-PLAN](../dev/plans/EMBED-PLAN.md)'s E5).
- **No sandbox.** An embedded Raku++ has the host's privileges. Pretending
  otherwise would be worse than saying so.
- **No `rakupp_register` yet** — a C function installed as a Raku sub. You can
  get the same effect today by loading an extension
  ([EXTENSIONS.md](EXTENSIONS.md)), which is the same mechanism from the other
  side.

## Bindings

You may not need the C surface directly. [bindings/](../../bindings/README.md)
holds five hosts built for the first real workload embedding was named for —
**using Raku grammars from a host language**
([GRAMMAR-PLAN](../dev/plans/GRAMMAR-PLAN.md)): Python (ctypes; ships as a
platform wheel with librakupp bundled), C++ (`<rakupp/grammar.hpp>`,
header-only, in the install layout), JS/TS (bun:ffi), Go (cgo), and Rust
(zero-dependency crate). All five drive the same Raku shim, which lives
INSIDE the library (`rk_grammar_shim`) so a binding can never skew against
its engine, and all five are byte-compared against plain `rakupp` by
`tools/grammar-smoke.raku` in CI. The other direction — your functions
callable from Raku — is `rk_register`.

## See also

- [EXTENSIONS.md](EXTENSIONS.md) — the other direction, and the value ABI both
  share
- [ABI-PLAN.md](../dev/plans/ABI-PLAN.md) — why the boundary is C, why an
  extension never sees `Value`, and what is still to come
- [GRAMMAR-PLAN.md](../dev/plans/GRAMMAR-PLAN.md) — grammars as a service:
  the design the Python binding implements, and the measured cost of each
  access pattern
