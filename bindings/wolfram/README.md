# RakuLang — Raku from the Wolfram Language

A pure Wolfram Language package over `librakupp`'s C ABI. No compiled glue:
the loader is `ForeignFunctionLoad` (the Wolfram FFI, 13.3+), values cross
through [`rakupp.h`](../../include/rakupp/rakupp.h), and the grammar logic
lives in the Raku shim that ships inside the library (`rk_grammar_shim`).

Where the other bindings are a package named `rakulang`, the Wolfram Language
names contexts: this one is ``RakuLang` ``, one `Get`-able file,
[RakuLang.wl](RakuLang.wl). Everything it exports starts with `Raku`.

## 1. What you need

- **Wolfram Language 13.3 or newer** — the release that added the foreign
  function interface. Any product carrying the language will do: Mathematica,
  or the free-for-developers
  [Wolfram Engine](https://www.wolfram.com/engine/) (`wolframscript`, no
  notebook front end), which needs a one-time sign-in with a free Wolfram ID
  to activate its license. Nothing else in this binding needs an account or
  the network.
- **`librakupp`.** From the repo root:

  ```bash
  cmake -B build -DCMAKE_BUILD_TYPE=Release -DRAKUPP_BUILD_SHARED=ON
  cmake --build build -j
  ```

  A build directory configured without `-DRAKUPP_BUILD_SHARED=ON` is
  static-only and this package cannot use it.

## Hello, before the package

[`rakupp.h`](../../include/rakupp/rakupp.h) opens with a HELLO in C; this is
the same program in fifteen lines of raw FFI, no package involved. It is the
clearest possible view of what `RakuLang.wl` is made of, and each name below
is a declaration from that header, one to one:

```wl
lib = "/path/to/raku++/build/librakupp.dylib";   (* .so on Linux *)

rkNew    = ForeignFunctionLoad[lib, "rk_new", {"OpaqueRawPointer"} -> "OpaqueRawPointer"];
rkCtx    = ForeignFunctionLoad[lib, "rk_ctx", {"OpaqueRawPointer"} -> "OpaqueRawPointer"];
rkEval   = ForeignFunctionLoad[lib, "rk_eval",
  {"OpaqueRawPointer", "RawPointer"::["UnsignedInteger8"], "RawPointer"::["OpaqueRawPointer"]} -> "CInt"];
rkStrGet = ForeignFunctionLoad[lib, "rk_str_get",
  {"OpaqueRawPointer", "OpaqueRawPointer", "RawPointer"::["UnsignedInteger64"]} -> "RawPointer"::["UnsignedInteger8"]];

rk  = rkNew[OpaqueRawPointer[0]];
ctx = rkCtx[rk];

out = RawMemoryAllocate["OpaqueRawPointer", 1];
rkEval[rk, RawMemoryExport["(1..10).grep(*.is-prime).gist"], out]   (* -> 0 *)

len = RawMemoryAllocate["UnsignedInteger64", 1];
p = rkStrGet[ctx, RawMemoryRead[out], len];
Print[RawMemoryImport[p, {"String", RawMemoryRead[len]}]]           (* (2 3 5 7) *)
```

The `0` is the one thing here that reads wrong on first sight: `rk_eval`
hands back a *status* (0 is `RK_OK`), and the value itself lands in the
`out` slot — `RawMemoryRead[out]` fetches the handle, and `rk_str_get`
Str-coerces any value to text, which makes it the only accessor you need
while poking around. A second `rkEval` on the same `rk` sees everything the
first one declared, exactly as in the REPL.

Everything past this point is what those fifteen lines grow into the moment
you want `2 + 2` back as the Integer `4` rather than the string `"4"`: the
full type switch in both directions, a library search instead of a
hard-coded path, `Failure` objects instead of NULLs to test by hand, and the
grammar walk. That file is [RakuLang.wl](RakuLang.wl); the rest of this
guide uses it.

## 2. Install

There is nothing to install: `Get["/path/to/bindings/wolfram/RakuLang.wl"]`
loads the package into a kernel or a notebook. The examples below locate it
relative to themselves, so they run against a fresh checkout.

Finding the library usually needs no configuration: if `rakupp` is on PATH,
the loader takes `librakupp` from beside it — an installed layout's sibling
`lib/`, a Homebrew keg's, or the build directory the binary sits in.

To override, name one: an explicit path to `RakuLoad["/path/to/librakupp.dylib"]`,
or `RAKUPP_LIB` (the file), or `RAKUPP_HOME` (an install prefix with `lib/`).
**A library you name is used as given.** If it cannot be loaded you get that
error, not a quiet fall-back to whichever other library happens to be
findable — the usual cause is an architecture mismatch, and falling back
makes the symptom (some other build's behaviour) point nowhere near the
cause. Unset the variable to search instead.

## 3. Two minutes

Run both examples from the repo root (`.so` for `.dylib` on Linux):

```bash
RAKUPP_LIB=$PWD/build/librakupp.dylib wolframscript -file bindings/wolfram/examples/calc.wls
```
```bash
RAKUPP_LIB=$PWD/build/librakupp.dylib wolframscript -file bindings/wolfram/examples/shopping.wls
```

`calc` ([examples/calc.wls](examples/calc.wls)) prints:

```
2 + 2 = 4
area(3, 4) = 12
primes below 30: 2 3 5 7 11 13 17 19 23 29
stats: count=8 sum=31 mean=3.88 max=9
greet: Hello, Ada! You are 36.
30! = 265252859812191058636308480000000
died: division by zero
```

`shopping` ([examples/shopping.wls](examples/shopping.wls)) prints:

```
3 items
milk x 2
bread x 1
eggs x 12
total, computed in Raku: 15
as plain Wolfram data: <|"item" -> {<|"name" -> "milk", "qty" -> "2"|>, <|"name" -> "bread", "qty" -> "1"|>, <|"name" -> "eggs", "qty" -> "12"|>}|>
line 2 column 7 while trying <qty>
```

If you see both, the binding works.

## 4. Running Raku

```wl
Get["bindings/wolfram/RakuLang.wl"];

RakuEval["my $x = 41"]
RakuEval["$x + 1"]                       (* 42 — eval keeps state, like the REPL *)

RakuEval[ReadString["calc.raku"]]        (* so loading a file of subs is an eval *)
RakuCall["area", 3, 4]                   (* 12 *)
RakuCall["stats", {3, 1, 4}]             (* <|"count" -> 3, "sum" -> 8, ...|> *)
RakuCall["greet", <|"name" -> "Ada"|>]   (* an Association becomes a Raku hash *)

RakuCan["area"]                          (* True *)
RakuVersion[]                            (* "3.14.0" *)
```

`RakuEval` returns the last statement's value; `RakuCall` looks the routine
up in the mainline scope, so anything an earlier eval declared is callable.
Arguments convert automatically — `Null`, `True`/`False`, `Integer` (any
width), `Real`, `Rational`, `String`, `List`, `Association`. Anything else is
a `Failure`.

The interpreter starts on first use. `RakuLoad[path]` names the library file
instead of searching; `RakuShutdown[]` frees the interpreter so a fresh one
may be started.

## 5. Parsing with grammars

```wl
g = RakuGrammarFromFile["log.raku", "Name" -> "Log", "Actions" -> "LogActions"];

m = RakuParse[g, text];                  (* a RakuMatch, or None if no match *)
Do[                                      (* lazy: one engine call per leaf *)
  Print[RakuStr[m["line"][i]["ip"]], " ", RakuInt[m["line"][i]["status"]]],
  {i, RakuElems[m["line"]]}];
RakuMade[m["line"][1]["size"]]           (* computed by LogActions, in the parse *)

everything = RakuTree[m];                (* eager, opt-in (~1.4x the parse) *)
RakuClose[m];
```

`RakuGrammarFromFile[path, "Name" -> ..., "Actions" -> ...]` compiles and
caches: identical source compiles once, and each *named* compile is isolated
in its own wrapper package, so recompiling an edited grammar under the same
name works and earlier handles keep the body they were compiled from. Without
`"Name"` there is no wrapper, and a same-name recompile fails with the
engine's `X::Redeclaration`. `"Name"` may be omitted only when the grammar
declaration is the file's last statement.

`RakuParse[g, text]` anchors to the whole input and returns a `RakuMatch` or
`None`; pass `"Rule" -> name` to parse a fragment with one rule. Indexing
builds a lazy path — `m["item"]` by capture name, `m["item"][2]` by position,
**counting from 1** as everything in the Wolfram Language does — and nothing
crosses the boundary until a terminal: `RakuStr`, `RakuInt`, `RakuNum`,
`RakuMade`, `RakuElems`, `RakuMatchedQ`, or `RakuTree`.

## 6. Values

Raku `Int` → `Integer`, `Num`/`Rat` → `Real`, `Str` → `String`, `List` →
`List`, `Hash` → `Association`, `True`/`False` → `True`/`False`, `Any` →
`Null`. The same rules run in reverse for arguments — plus one Wolfram bonus:
a `Rational` argument crosses *exactly*, as a Raku `Rat` (`1/3` stays a
third, not `0.333…`), though a `Rat` result still arrives as a `Real`.

An integer wider than 64 bits arrives as a string of digits. In a
`RakuTree`, a match node with no sub-captures becomes its matched *text* —
`qty` is the string `"2"` — so use `RakuInt` on the node, or an actions
class, for numbers.

## 7. Errors

Failures come back as `Failure` objects, the scriptable Wolfram convention:
`FailureQ[r]` to test, `r["Message"]` to read. A Raku `die` is
`Failure["RakuError", ...]`; a diagnosed non-match is
`Failure["RakuParseError", ...]`, carrying `"Line"`, `"Column"`, `"Rule"`
and `"Position"`:

```wl
r = RakuParse[g, text, "Strict" -> True];
If[FailureQ[r],
  Print["line ", r["Line"], " column ", r["Column"],
    " while trying <", r["Rule"], ">"]];
```

A terminal on a missing capture is a `Failure` too; `RakuMatchedQ` and
`RakuElems` answer `False`/`0` instead, which is how you probe for one.

## 8. Lifetime and threading

One interpreter per kernel process, created on first use; one kernel thread
talks to it at a time (Raku code inside it threads freely). A `RakuMatch`
holds a rooted value in the interpreter — `RakuClose` it; there is no
reliable garbage-collection hook to lean on. Values from `RakuEval` and
`RakuCall` are already plain Wolfram data and need nothing.

## 9. Testing

```bash
build/rakupp tools/bindings-smoke.raku
```

Runs both examples in all six languages (skipping toolchains that are not
installed) and checks the output against
[../examples/expected/](../examples/expected). The Wolfram leg needs an
*activated* `wolframscript` — a fresh Engine stops at its sign-in prompt, so
run `wolframscript` once interactively first.

## When things go wrong

- **`librakupp not found`** — the loader lists every path it tried. Set
  `RAKUPP_LIB` to the library file. If a `rakupp` binary was found but no
  library beside it, that build directory is static-only: rebuild with
  `-DRAKUPP_BUILD_SHARED=ON`.
- **`RakuLang needs the Wolfram foreign function interface`** — the kernel
  is older than 13.3. There is no pure-WL fallback; an older kernel would
  need a compiled LibraryLink shim, which this binding deliberately is not.
- **`wolframscript` sits silently at startup** — an unactivated Engine is
  waiting for its one-time sign-in. Run `wolframscript` interactively, log
  in with a (free) Wolfram ID, try again.
- **`rk_new refused`** — something already created an interpreter in this
  kernel. There is one per process; `RakuShutdown[]` frees it if you need a
  fresh one.

## Numbers (2026-08-24, M-series macOS, Wolfram Engine 15.0)

10,000 warm iterations each, Release `librakupp`:

| operation | Wolfram | Python binding, same machine |
|---|---:|---:|
| `RakuCall["area", 3, 4]` | ~85 µs | 3.6 µs |
| `RakuEval["2 + 2"]` | ~42 µs | 3.3 µs |

The gap is the Wolfram FFI itself: each `ForeignFunction` invocation costs
roughly 8–9 µs, and one `RakuCall` makes about ten of them (argument
construction, the call, the type probe, the result read). That is the cost of
having no compiled glue; a LibraryLink shim would cut it, at the price of a
per-platform build. For the workload this binding was asked for — evaluating
Raku from a session, not a hot inner loop — tens of microseconds in-process
replaces a serialize–socket–deserialize round trip to an external process,
plus the process management. If a profile ever shows the per-call cost
dominating a real workload, the shim is the known next step, not a reason to
grow this layer.
