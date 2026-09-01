# Rakudo::Internals in the wild — public modules on private surface

Measured over the top-200 battery (raku-module-battery, pins of 2026-08-12):
**nine of the most-depended-on distributions call `Rakudo::Internals`**, an
implementation-internal namespace with no documentation and no stability
promise — including the ecosystem's own installer. This page is the census,
the explanation, and rakupp's policy about it.

## The census

| dist | what it calls | why |
|---|---|---|
| **zef** (the installer) | `::("Rakudo::Internals::JSON")` from-json/to-json; `::("Rakudo::Internals").?LL-EXCEPTION` | dependency-free JSON for every META it reads; verbose exception mode. Reached *dynamically*, so it compiles anywhere and explodes (or no-ops) at run time |
| **OpenSSL** | `Rakudo::Internals::JSON.from-json` | reads its `resources/libraries.json` at load time |
| **Pakku** (installer) | `::JSON.from-json/to-json`, `.IS-WIN`, `.DIR-RECURSE` | JSON, platform probe, directory walk |
| **Pakku::Spec** | `::JSON.from-json` | spec parsing |
| **DBIish** | `.IS-WIN`, `.REGISTER-DYNAMIC` | platform probe; registering a `$*…` dynamic |
| **NativeHelpers::Blob** | `.IS-WIN` | picks its libc |
| **Base64::Native** | `.IS-WIN` | same |
| **Digest::SHA1::Native** | `.IS-WIN` | same |
| **IO::Socket::Async::SSL** | `.NORMALIZE` | Unicode normalization of hostnames |

## Why they do it (the honest part)

Nobody reaches into another project's internals for fun. They do it because
the language offers **no dependency-free standard library** for the things a
toolchain needs before modules exist: JSON (every META6.json), a platform
test, a directory walk. A module that must work on a bare Rakudo — zef
first among them, since it *is* the module installer and cannot install its
own dependencies — has exactly one JSON parser it can assume: the one the
compiler itself carries. So `Rakudo::Internals::JSON` became load-bearing
public surface in everything but name, and `IS-WIN` the de-facto platform
test. The blame, such as it is, lands on the missing stdlib, not on the
module authors.

The consequence lands on every *other* implementation: to run real code,
rakupp must answer names that appear in no specification — cloned from
observed behaviour, tested against the modules that call them.

## What rakupp answers today

- `Rakudo::Internals::JSON` — from-json/to-json, backed by **rakupp's own
  C++ codec** (original code; only the class name is Rakudo's). Typing
  matches JSON::Fast (Int/Rat/Num per `Str.Numeric`), trailing content is
  refused (9072beb — caught by JSON::Native's value-by-value suite).
- `Rakudo::Internals.IS-WIN` and `.REGISTER-DYNAMIC` — the platform probe
  and dynamic-variable registration NativeCall dists need at BEGIN time.

Known gaps, each a fix candidate when its dist comes up in the campaign:
`.DIR-RECURSE` (Pakku), `.NORMALIZE` (IO::Socket::Async::SSL);
`.LL-EXCEPTION` is absent but zef guards it with `try … .?`, so it degrades
to the terse mode harmlessly.

## The two-name policy

The codec's **first-party name is `Rakupp::Internals::JSON`** — what
rakupp's own tooling (`tools/install.raku`, JSON::Native's engine backend)
calls. `Rakudo::Internals::JSON` is the **compatibility alias**: answered
so real code runs, and nothing more. Only `Rakupp::Internals::*` is
whitelisted for the first-party spelling — a typo'd `Rakupp::` name is
still an error, because `JSON::Native` is a real ecosystem module and the
namespace must not quietly swallow mistakes.

Plan of record, deliberately **not implemented yet**: once the battery
campaign no longer needs those dists measured as-is, the Rakudo spelling
should start saying out loud — a note, not a refusal — that the program
leans on another implementation's internals. Removal is not realistic while
zef itself is a caller; honesty is. First-party code should never wait for
that: it has its own name today.

## Aren't we doing the same thing?

JSON::Native's engine backend calls `Rakupp::Internals::JSON` — the same
shape as OpenSSL calling Rakudo's. Two differences are the point, one
hazard is real:

- **Opposite failure mode.** The census entries hard-depend: no internals,
  no module. JSON::Native probes functionally and falls back to JSON::Fast
  — on any engine, the module works; internals only buy speed. Relying on
  internals for *function* is the anti-pattern; for *speed behind a
  fallback* it is a fast path.
- **Vertical, not cross-project.** Rakudo's failure was an accidental
  surface with no promise behind it. This codec and its callers are one
  project: the small surface (from-json/to-json, JSON::Fast-typed values,
  trailing-content strict) is a deliberate commitment, pinned by
  JSON::Native's value-by-value suite and the install gate.
- **The hazard:** third-party modules copying the spelling would mint a
  second de-facto internal API — the history above, replayed. The blessed
  public doorway is **the JSON::Native module**; `Rakupp::Internals::JSON`
  is for toolchain code that cannot have dependencies (install.raku), and
  nothing else.

## The narrow case: private surface reached through the MOP

`Rakudo::Internals` is the *named* version of this problem. There is an
unnamed one: a module that needs a language feature Raku does not document,
and reaches it through the meta-object protocol instead of a namespace.
[BINARYHEAP.md](BINARYHEAP.md) is the worked example — `Parameterizable`
declares `method ^parameterize` on a class (undocumented Rakudo, by its own
POD's admission) because roles cannot autovivify and classes cannot be
parameterized, and `BinaryHeap` builds on it with `BIND-POS`, `Mu.CREATE`
and an `is rw` invocant.

Same diagnosis, different scale. The census above is nine of the top 200
including the installer, so removal is not realistic and honesty is the
policy. That chain is *one* dist deep in each link and its only consumer
replaced it in Graph 0.1.3 — so the policy there is narrower: answer the
MOP hook generically (which costs 0.2% and adds no documented-language
surface), fix the engine faults it exposed on their own merits, and tune
for nothing.

## The compiler as an object: `nqp::getcomp` + `REPL`

There is a third shape, and it is the most load-bearing of the three: a module
that wants **a persistent evaluation scope** — `my $x = 42` typed in one cell
still there in the next — and has no public way to ask for one. `EVAL` forgets,
so the ecosystem reaches for the compiler and its REPL object directly:

```raku
use nqp;
$!compiler := nqp::getcomp("Raku") || nqp::getcomp('perl6');
$!repl = REPL.new($!compiler, {});
...
$!repl.repl-eval($code, $exception, :outer_ctx($!save_ctx), :interactive(1));
```

Those five lines are Jupyter::Kernel's sandbox, copied verbatim into
Text::CodeProcessing and from there into the notebook and document-weaving
dists that build on it. `nqp::getcomp` alone accounts for two of the ecosystem
sweep's self-failures (Text::CodeProcessing, CodeUnit), and everything that
depends on those inherits it.

rakupp answers both names:

- **`nqp::getcomp('Raku')`** (and `'perl6'`, which is why the idiom's `||`
  exists) — the compiler object `$*RAKU.compiler` already was. An unknown HLL
  name is null, as in NQP.
- **`REPL`** — `.new($compiler, $adverbs)`, `.repl-eval($code, $exception,
  *%adverbs)`, `.input-incomplete`, `.ctxsave`. Rakudo persists the scope by
  handing back the eval'd code's *context* and taking it again as
  `:outer_ctx`; rakupp keeps the scope on the REPL object, which is the same
  promise with none of the context plumbing — two REPLs are two independent
  sessions, `:outer_ctx` is accepted and ignored, and `$*MAIN_CTX` stays
  undefined, which the sandboxes already handle (`if $*MAIN_CTX` guards every
  use). A line runs in the session scope with the caller's frame still on the
  dynamic stack, so `my` lands in the session while `$*OUT` resolves where the
  caller put it — which is what lets a weaver capture a chunk's output by
  wrapping the call in `my $*OUT = $*OUT but role {…}`.

Same policy as the census above: answered so real code runs, and nothing more.
