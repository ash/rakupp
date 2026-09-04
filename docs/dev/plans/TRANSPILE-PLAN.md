# Plan: `--target=js` — a second Codegen, with JavaScript as its output

**Status: P0 and the first P1 batch landed 2026-09-03** — `--target=js`,
`-o`, `--standalone`, `--verify`, `--fallback=wasm`, the manifest line, the
runtime in `src/js-rt/` (embedded through `src/JsRuntimeSrc.cpp` by
`tools/js/gen-rt-src.raku`), the emitter in `src/codegen/Js.cpp`, the corpus
gate `t/js/run.raku` and the ranking tool `tools/js/rank-builtins.raku`;
user docs in [JS.md](../../guide/JS.md). The six P1 kernels verify; the
gate's numbers are in *Where it stands* at the end. The design below is the
draft as reviewed; where the code decided differently it says so there. Probes
run 2026-09-03 against `build-arm64/rakupp` (v3.25.0), Node 20.11.1 and Bun
1.3.14 on the benchmark machine (Darwin 24.6, arm64). Every number below is
from that sitting unless it cites [BENCHMARKS.md](../../status/BENCHMARKS.md)
(2026-08-31, same box) or [rakujs/INTERNALS.md](../../../rakujs/INTERNALS.md).
Prompted by [Raku/problem-solving#527](https://github.com/Raku/problem-solving/issues/527)
(lizmat, 2026-09-03), the proposal to remove Rakudo's JavaScript backend, with
[rakudo/rakudo#6632](https://github.com/rakudo/rakudo/pull/6632) as its first
sweep; *Prior art* below says what that changes here.

Goal: `rakupp --target=js prog.raku -o prog.js` emits a JavaScript program
that runs under Node, Bun, Deno and in a browser, behaves exactly as the
interpreter would run `prog.raku`, and calls into the JavaScript host — the
DOM, npm packages, Promises — as naturally as `--exe` output calls C. It is
the **second transpiler** next to [Codegen.cpp](../../../src/Codegen.cpp),
and the design keeps a **third** in mind (Rust): the parts that are about
Raku, not about the target, get a seam the next backend reuses.

It is not [Raku.js](../../../rakujs/README.md). Raku.js is the interpreter
compiled to WebAssembly: it runs the *whole* language today, at the price of
a 7.5 MB engine, a tree-walk behind `-fexceptions` trampolines, a ~200-level
recursion cap, and no way to touch the page it runs in. This plan produces
JavaScript *code*; Raku.js remains the fallback that makes the mode total.
Raku.js also keeps shipping with every release regardless of this plan —
`rakujs-<tag>.zip` on each tag, the engine the playground runs — and the
fallback tier below *depends* on it: a transpiled program that leaves the
core runs on the release's own WASM build, never on an older one.

## Why a second backend, and why JavaScript first

The C++ backend exists for **speed**: it turns a program into straight-line
C++ that links the runtime, and `--exe` beats the interpreter by 1.0–11.8×
on the kernels. A JavaScript backend exists for **reach** — places where a
native binary cannot go and where the WASM engine is too big or too slow:

| Scenario | What it needs from the backend | Phase |
|---|---|---|
| A Raku script run by `node prog.js` / `bun` / `deno`, no rakupp installed | the core language, stdio, `@*ARGS`, `%*ENV`, files | P1–P2 |
| The raku.online playground running in-core programs at JIT speed instead of tree-walking in WASM (mandel, the showcase interpreters) | the core + regexes/grammars, a Web Worker host adapter | P1–P3, P5 |
| A page whose behaviour is written in Raku: DOM access, event handlers, `fetch` | JS interop (`use JS`), closures as callbacks, Promise ↔ `await` | P4 |
| A Raku grammar shipped as a 30 KB parser module for a JS project, instead of a 7.5 MB engine | the regex/grammar runtime, ESM export of a `grammar` | P3–P4 |
| A library written in Raku, published to npm with type declarations | ESM exports from `is export`, `.d.ts` from signatures, `--standalone` | P4 |
| Edge/serverless functions (Workers, Deno Deploy) with size budgets a WASM engine cannot meet | small output, no WASM, `--standalone` | P1, P4 |
| Teaching: "show me the JavaScript" beside a Raku program; a Raku program debugged in browser devtools with its own source | readable output, source maps | P1, P4–P5 |
| Deep recursion in the browser (the ~200-level Raku.js cap) | plain JS functions: 10k frames on Node's default stack, 50k+ on Bun | P1 |

JavaScript before Rust because Rust would be *another native target*, and we
have one: its runtime question answers itself (link `librakupp` through the C
ABI, as [bindings/rust](../../../bindings/rust) already does). JavaScript is
the target with no runtime to link, and therefore the one that forces every
design question the seam must answer. It is also the target no Raku
implementation will have once Rakudo's is gone: Raku++ and Mutsu both reach
the browser through WebAssembly, and neither emits JavaScript code or can
touch the page it runs in.

## What the probes said (2026-09-03)

Four kernels from `tools/bench/`, hand-shaped into the JavaScript a transpiler
would plausibly emit, timed like everything else here (7 runs, first discarded,
minimum of the rest). "checked" is the realistic shape: every `+`, `-`, `<`
goes through a runtime call that keeps Ints as JS numbers and promotes to
`BigInt` past 2⁵³.

| kernel | interp (today, wall) | `--exe` (08-31) | Raku.js, Node (engine 07-22) | JS "checked", Node | JS "checked", Bun |
|---|---:|---:|---:|---:|---:|
| fib(29)  | 330.1 ms | 51.3 ms | 5,623.6 ms | **4.7 ms** | 4.7 ms |
| loopsum  | 108.1 ms | 12.4 ms | 1,271.0 ms | **2.1 ms** | 1.8 ms |
| strcat   | 27.7 ms  | 3.0 ms  | 70.0 ms    | **0.2 ms** | 0.3 ms |
| hash     | 37.3 ms  | 7.4 ms  | 249.0 ms   | **1.7 ms** | 1.5 ms |

Read it carefully rather than triumphantly: these kernels flatter a JIT, the
hand-shaped JS carries none of the container, dispatch or `say` machinery a
real runtime adds, and `strcat`'s number is a JS engine's rope concatenation,
not ours. What the table *does* establish is the order of magnitude: a program
inside the core would run in the browser roughly **a thousand times** faster
than the WASM interpreter runs it today, and faster than `--exe` runs it
natively. That is the reason the playground is a first-class scenario, not a
footnote.

**The Int representation is decided by measurement.** Four flavours of the
same fib/loopsum:

| flavour | fib(29) Node / Bun | loopsum Node / Bun |
|---|---:|---:|
| raw JS numbers (ceiling, not reachable) | 3.4 / 4.4 ms | 0.7 / 0.3 ms |
| number, promoted to BigInt on overflow, via `R.add` calls | 4.7 / 4.7 ms | 2.1 / 1.8 ms |
| every value boxed `{t, v}` (the naive "Value object") | 8.5 / 9.9 ms | 3.4 / 6.0 ms |
| BigInt everywhere | 12.2 / 38.5 ms | 12.8 / 27.6 ms |

So: **Int is a JS number until it overflows, then a BigInt**; the check costs
1.4× on call-dense code and 3× on a bare loop against the ceiling, and beats
uniform boxing by 2× and uniform BigInt by 3–8×. Primitive values stay
unboxed; only containers, Rats, objects and type objects are JS objects.

**Graphemes cannot lean on `Intl.Segmenter`.** Counting the graphemes of a
50,000-char ASCII string took **423 ms under V8** (2.7 ms under JSC) — 150×
apart between hosts, and 2,000× slower than `.length`. A 60k-codepoint mixed
string: 916 ms vs 2.3 ms. `normalize('NFC')` is fast on both (0.5 / 1.4 ms).
Therefore: an **ASCII fast path** (0.2 ms), and for non-ASCII strings **our
own grapheme-break tables**, generated into JS by the same generator that
produces `unicode_gb_gen.cpp` — so `.chars` prints the same number under
Node, Bun and Safari, and cannot drift from the native engine.

**NFG is a requirement of this target, not a property of its runtime.** The
argument the community now makes for retiring Rakudo's JS backend (#527) is
that the WASM alternatives keep graphemes where that backend never did; a
transpiled Raku++ that lost them would undercut an argument made in Raku++'s
name. So P1's first batch takes the 48 regression programs that touch
graphemes as in-core targets, and `"e\x[301]".chars` being 1 under Node, Bun
and a browser is a named check in the corpus gate.

**Hash → `Map`.** Map vs plain object: 1.7 vs 1.3 ms on Node, 1.5 vs 6.1 ms
on Bun. Map is also the semantically right one (insertion order, any key
including `__proto__`, `.size`).

**Recursion.** A plain JS function recurses 5,000 deep on Node's default
stack and fails at 10,000; Bun manages 50,000. Raku.js caps near 200. Node's
`--stack-size` raises its limit further; the main wrapper can say so when it
catches a `RangeError`.

**How much of the language the C++ subset already covers**, as the starting
boundary the JS backend inherits: `--cpp` over `t/regression/*.raku` accepts
**249 of 413** programs. The 164 that leave the subset do so for (top causes)
roles/packages (29), phasers and `CATCH` (25), prefix operators (15), `<42>`
allomorph literals (12), assignment targets (13), regex interpolation and
embedded code blocks (17), enums with computed values (5), `where`/`:D`
multis (6), NativeCall shapes (7). Several of those exist only because the
target is C++: closures in JS capture by reference, a `{…}` block inside a
regex is just a JS closure once the matcher is JS, and a `where` clause is a
callable predicate. The JS boundary will be *inside* the C++ one on day one
and is expected to pass it on those rows.

**Sizes, for the fallback tier.** `rakujs.wasm` is 7,514,447 B (2,160,551
gzipped) for the browser build and 5,088,269 B (1,465,348 gzipped) for the
Node build, plus a 105 KB loader. A transpiled program plus its runtime must
be measured, not guessed; the budget line in the gates is filled in after P1.

## The architecture decision: which runtime does the JavaScript call?

The C++ backend "calls the runtime for Value semantics" — its output links
`librakupp`, so it gets 13,000 lines of builtins, 11,000 of methods, the
regex engine and the Unicode tables for free. JavaScript output cannot link
C++. Three ways out:

1. **The WASM engine as the runtime.** Transpiled JS drives `librakupp`
   compiled to WebAssembly through ABI 2 handles. Every operator crosses the
   JS↔WASM boundary; values live in WASM memory; the program still ships
   7.5 MB and still cannot touch the DOM without marshalling. Rejected as
   the primary tier — but it is *exactly* the right **fallback tier**, and
   it is ~50 lines: emit a JS file that loads `rakujs.js` and runs the
   embedded source. It makes `--target=js` total from the first day.
2. **A JavaScript runtime** (`rakupp-rt.js`): Value semantics reimplemented
   in JS for a defined **core** of the language. Readable, idiomatic output;
   small; JIT-compiled; native interop. The cost is the runtime itself,
   which this plan sizes honestly: it is the bulk of the work.
3. **Hybrid per expression** — native JS for what the core covers, WASM for
   the rest, inside one program. Ruled out: it is the scope-bridging problem
   the C++ backend already documents for regexes (native locals invisible to
   the engine) made worse by having to marshal every value on every crossing.
   Tiering is **whole-program** or nothing.

Decision: **(2) for the core, (1) as the fallback, decided per program,
never mixed** — the same discipline as `--exe` → `--aot` → `--bundle`:
the backend throws on the first construct outside the core, the caller
reports it, and the program either gets the fallback or a refusal. The core
grows the way everything here grows: by the batch loop, one row of the
refusal histogram at a time, gated on the differential suite.

## Why the WASM build stays

Asked directly, 2026-09-03, once the plan existed: does a JavaScript target
make Raku.js unnecessary? No — and not only because the fallback tier needs
it. Recorded here so the question is not reopened every time the JS tier
grows.

- **It is the only tier that runs the whole language, and it costs nothing
  to keep that true.** Raku.js is the interpreter itself, compiled. Every
  engine fix reaches the browser on the next build, with no second
  implementation to maintain. The JS core will be a subset for a long time:
  the C++ backend, after months, covers 249 of 413 regression programs, and
  the JS one starts inside that boundary. Anything that parses at run time —
  `EVAL`, the showcase interpreters, grammars with LTM until P3 ports it —
  stays WASM-only for good.
- **Other people stand on it.** Raku/problem-solving#527 cites Raku++ in the
  browser as the reason Rakudo can retire its own JS backend. The spec site,
  the tour, the course and third-party pages using the `raku.js` widget all
  run this engine. Dropping it would pull the floor from an argument being
  made in Raku++'s name.
- **This plan assumes it.** "Refuse by default, `--fallback=wasm` to opt in"
  only works if the fallback works; without the WASM build the JS mode is
  partial and the corpus gate has nowhere to send what it refuses.
- **The two are complementary, not competing.** Where JS wins, WASM loses:
  a thousand times faster on the kernels, kilobytes against 7.5 MB, DOM and
  npm access, deep recursion. Where WASM wins, JS may never catch up:
  completeness, exact semantics by construction, run-time parsing. It is the
  relation between `--exe` and `--bundle`, and nobody proposes dropping
  bundling because native compilation exists.
- **The cost is small.** An additive build, one CI job, one zip per tag, a
  smoke test; nothing in `src/` is touched by it.

What shifts is the roles, not the presence. Today WASM is the only browser
path; after P1–P3 the playground tries the transpiler first and falls back to
WASM, as `--exe` falls back to `--aot`. The WASM build may also improve from
its own side if Emscripten fixes the exception-handling personality bug that
forces `-fexceptions` (see [rakujs/INTERNALS.md](../../../rakujs/INTERNALS.md)):
native Wasm-EH would lift the ~200-level recursion cap and cut the trampoline
tax. The only world in which WASM becomes unnecessary is one where the JS
runtime reaches full parity, parser included — a second implementation of
Raku, which is exactly the weight that sank Rakudo.js. Not a goal.

## The shape of the output

`tools/bench/fib.raku`, as P1 should emit it (mangling reused from the C++
backend: sigil dropped, injective byte encoding, so `$a-b` and `$a_b` cannot
collide):

```js
// Generated by `rakupp --target=js` — rakupp <version>, source sha1 …
import * as R from "./rakupp-rt.js";
function u_fib(v_n) {
    return R.lt(v_n, 2) ? v_n : R.add(u_fib(R.sub(v_n, 1)), u_fib(R.sub(v_n, 2)));
}
export default R.main(import.meta, () => {
    R.say(u_fib(29));
});
```

`R.main` is the counterpart of `__rakupp_main_body`: it installs the host
adapter (stdio, `@*ARGS`, exit codes), catches `last`/`next`/`redo` outside
a loop, `ExitEx`, `RakuError`, and a `RangeError` (reported as a recursion
limit with the `--stack-size` hint), and flushes stdout. A sub with named,
slurpy or defaulted parameters takes `(pos, named)` and binds through
`R.bind(sig, …)`; a fixed-arity positional sub takes plain parameters, as the
`-O` C++ path does today. Named arguments travel as a `Map`.

A class, a `gather`, and a `for` with a phaser:

```js
const c_Point = R.class("Point", { attrs: ["$!x", "$!y"], rw: ["$.x"] }, (C) => ({
    dist(pos) { return R.sqrt(R.add(R.mul(this.a_x, this.a_x), R.mul(this.a_y, this.a_y))); },
}));
const v_evens = R.gather(function* () {           // every `take` is lexically inside → a generator
    for (const v_ of R.range(1, 10)) if (R.divisible(v_, 2)) yield v_;
});
LOOP1: for (const v_x of R.list(v_evens)) {         // `next LOOP1` → continue LOOP1, no exception
    try { R.say(v_x); } finally { /* LEAVE */ }
}
```

The reader should be able to see their program in the output. That is a
design constraint, not a nicety: it is what makes the "show me the
JavaScript" scenario and devtools debugging work.

## The runtime: `rakupp-rt.js`

Representations, from the probe and from what Raku's semantics force:

| Raku | JS | note |
|---|---|---|
| Int | `number` (integral) → `BigInt` past 2⁵³ | `R.add` and friends check with `Number.isSafeInteger` |
| Num | `number` (non-integral), boxed `RNum` when integral | the *only* way to keep both Ints and float-heavy code unboxed: a Num that happens to be integral (`2e0`, `1.5+0.5`) is rare in float arithmetic and needs a box to stay distinguishable (`.WHAT`, `.raku`, `/` producing Rat vs Num). **P1 probes this against mandel before committing.** |
| Rat / FatRat / Complex | boxed classes | exact; `0.1 + 0.2 == 0.3` must hold, it is the language's showpiece |
| Str | `string`, NFC-normalized at construction as the C++ literal is | ASCII fast path; grapheme index built lazily from our own GB tables and cached on the string's side table |
| Bool | `boolean` | |
| Nil, Any, Mu, type objects | singletons / `RType` objects | never `null`/`undefined` — those are reserved for JS interop |
| Array / List / Seq | `RArray` over a JS array, flags for `isList`/`itemized`/shape; Seq over an iterator | laziness through generators; `.head`, `.first`, infinite ranges and `…` sequences short-circuit naturally |
| Hash / Set / Bag / Mix | `RHash` over `Map`, `kind` tag | key stringification as the native engine does it |
| Pair, Range | small classes | Range lazy |
| Code | a JS function with a `sig` descriptor property | arity, named binding, multi dispatch and introspection read the descriptor |
| objects | JS classes built by `R.class`, a metaclass side table for `.^methods`, MRO, roles composed at class creation | `does`/`but` compose at runtime |
| Regex, Match, Grammar | runtime classes; the *pattern* travels as a pre-parsed tree, see P3 | |
| Promise, Supply, Channel | JS Promise; queues | no threads, see P4 |

Containers: a scalar is a JS `let`. `is rw` parameters and `:=` binding get a
`Scalar` box only where the analysis says so — the C++ backend's
`rwSubs`/capture analysis, minus the cases JS closures make unnecessary.

Control flow: `last`/`next`/`redo` lexically inside their loop become
labelled `break`/`continue` (and `redo` a labelled inner loop); only a loop
control that crosses a routine boundary (`next` inside a `.map` block) is
thrown and caught at the loop. `return` likewise. This is not only cheaper
than the interpreter's exception path — [NATIVE-MATH-PLAN.md](NATIVE-MATH-PLAN.md)
measured an unlabelled mainline `next` at ~80 µs — it is what a reader
expects to see.

`gather`/`take`: when every `take` is lexically inside the gather block
(loops included, nested routines excluded) the block is a generator and the
result is lazy. Otherwise `take` pushes into a dynamically scoped collector
and the gather is eager — correct for finite gathers, and the one place the
JS backend is knowingly less lazy than the interpreter. It is recorded as a
listed limitation; a program that depends on laziness across a call boundary
is a fallback candidate rather than a wrong answer once P2 can detect the
infinite case (`lazy`, `…`, `xx *` feeding it).

Exceptions and phasers: `die`/`try`/`CATCH`/`CONTROL` map onto JS
`try`/`catch` with a runtime `X::` hierarchy; `LEAVE`/`KEEP`/`UNDO` are
`finally` blocks; `FIRST`/`NEXT`/`LAST` are loop-local flags; `INIT`/`END`
live in `R.main`; `BEGIN` is evaluated at transpile time by the interpreter
(the transpiler has one in the process) and its result emitted as a literal —
only for the values the AST serializer can represent, else refuse.

Builtins and methods: the order in which they enter the runtime is not a
guess — a P1 tool walks the corpus (413 regression programs, 24 examples,
the showcases) and ranks every builtin and method by how many programs call
it. Each batch takes the next slice of that ranking. Everything absent throws
`X::NYI`-style with the name, which the gate turns into a refusal at
transpile time (the emitter knows the runtime's table).

The host adapter: one file per host (node/bun/deno, browser main thread,
Web Worker) chosen at load, behind one interface — stdout/stderr/stdin,
`@*ARGS`, `%*ENV`, `$*CWD`, files, `exit`, time, random. In a browser
`slurp("x")` dies with a message that says where it would have worked.

Regexes and grammars (P3): the C++ engine parses a pattern from its string
at run time. The JS backend instead **pre-parses at transpile time** with the
engine's own regex parser and emits the resulting node tree as a JS literal;
`rakupp-rt.js` carries only a matcher — backtracking, quantifiers with `%`,
captures and named captures, lookarounds, `<[…]>` classes with Unicode
properties from generated tables, packrat memoization, `make`/`made`, action
classes. Embedded `{…}` blocks and `<$var>` interpolations are ordinary JS
closures and values — the C++ backend's refusals on those rows disappear.
Longest-token matching ships second, as a port of
[LtmNfa.cpp](../../../src/LtmNfa.cpp); until then the matcher uses the
first-match ordering the native engine exposes under `RAKUPP_LTM=0`, and says
so in the refusal report for programs whose grammars have alternations with
declarative prefixes. (This is not the grammar-compilation idea discarded on
2026-08-13: that was compiling patterns to code *for speed* in the native
engine. Here the pattern is data and the matcher interprets it — the native
design, ported.)

What the runtime will **not** contain: the parser. `EVAL` of a string,
`require` of a computed name, and `use MONKEY-SEE-NO-EVAL` programs go to the
fallback tier, as `--slim=-eval` cuts them today. NativeCall goes there too;
there is no native code in a browser. Threads do not exist — `start` is a
Promise (concurrency, not parallelism), and `Lock`/`Semaphore`/atomics are
no-ops or refusals per the corpus ranking.

## JS interop — the reason to do this at all for the browser

Two directions, one rule: **values cross by copy, objects cross by identity.**

Raku → JS: `Int` → `number` (a `BigInt` past 2⁵³), `Num` → `number`,
`Str` → `string`, `Bool` → `boolean`, `Nil` → `undefined`, a type object →
`null`, `Array` → a JS array (copy), `Hash` → a plain object (copy), `Pair` →
`{key: value}`, `Code` → a JS function whose arguments are marshalled back,
so a Raku closure is an event handler with nothing extra written.

JS → Raku: `number` → `Int` if integral else `Num`, `string` → `Str`,
`boolean` → `Bool`, `null`/`undefined` → `Nil`, an array → `Array`, a
function → `Code`, a Promise → `Promise` (awaitable), **any other object →
`JS::Object`**, an opaque handle — deliberately *not* a Hash, so identity,
prototype methods and the DOM survive the crossing.

Surface, under `use JS;`:

- the term `JS` is `globalThis`; `JS.document`, `JS.fetch(...)`,
  `JS.console.log(...)`.
- on a `JS::Object`, `.name(args)` calls the property if it is a function
  and reads it otherwise; `$o<name>` reads a property and `$o<name> = …`
  writes one (the subscript form is the assignable one, as it is on a Hash).
- `EVAL $literal, :lang<JavaScript>` — the door the language specifies;
  accepted for literal strings only, emitted verbatim.
- a Raku `class` is exportable as a JS class; `sub … is export` becomes a
  named ESM export; a `grammar` exports `parse(text)`.

Under the interpreter, `use JS` loads a stub whose every call dies naming
`--target=js`. Interop programs therefore cannot be checked by the
differential gate; they get their own goldens run under Node against a small
DOM stub (and jsdom where a case needs more).

Async: Raku's `await` blocks; JavaScript's cannot. The transpiler **colours**
routines: any routine that transitively contains `await` becomes `async`,
every call to a coloured routine is `await`ed, and `R.main` is async. It is
the well-known function-colouring compromise and it is correct for the
programs people write in this scenario (`await JS.fetch(...)`, `await start
{...}`). An `await` inside a block passed to the runtime (`.map({ await … })`)
is refused with the reason, not silently made eager. `sleep` in synchronous
code uses `Atomics.wait` where the host allows it (Node, and Web Workers —
which is where the playground runs).

Source maps: every node carries `line`, so `-o prog.js` also writes
`prog.js.map` at line granularity. That is enough for devtools to show
`prog.raku` while stepping the JS.

## The command line

- **`--target=js`**, no alias. It is the extensible key (`--target=parse|ast`
  already exist as Rakudo muscle memory; Rakudo's JS backend answered to
  `--target=js`, so it is the spelling people know — and with that backend
  being removed (#527) there is no live Rakudo feature left for it to
  conflict with), and `--target=rust` slots in later without a new flag.
- Output: to stdout without `-o` (as `--cpp` does); with `-o prog.js` the
  program, `prog.js.map`, and the runtime sidecar `rakupp-rt.js` next to it,
  written from a copy embedded in the `rakupp` binary the way the grammar
  shim is. `--standalone` (existing flag) inlines the runtime and every
  transpiled module into the one file. A versioned copy is published at
  `raku.online/rt/<version>/` for pages that prefer a URL.
- **`--verify`**: run the program under the interpreter and under the JS
  host, compare stdout, stderr and exit status byte for byte, emit nothing on
  disagreement — the exact protocol of `--slim=verify`, exit 6. Host from
  `RAKUPP_JS` (default: `node`, then `bun`, on `PATH`).
- **`--fallback=wasm`**: opt in to the WASM-wrapper tier for a program
  outside the core. The **default is a refusal** naming the construct and
  line, in the message shape `--cpp` prints today. `--exe` falls back
  silently because its fallback is the same kind of artifact; here the
  artifact class changes (a 7.5 MB dependency, no DOM, the recursion cap),
  and the user asked for JavaScript.
- `--slim` under `--target=js`: the feature scan already computes what a
  program can reach; here it selects which runtime modules to inline. Later,
  when the runtime is big enough for it to matter, and measured then.
- Every output carries a one-line manifest comment (version, mode, source
  hash) so `--exe-info`'s question can be answered for a `.js` too.
- No format flag (ESM everywhere; `<script type="module">` covers the
  script-tag case), no host flag (detected at load), no minifier (the JS
  toolchain owns that). A flag is added when a scenario in the table above
  cannot be served without it, and not before.

## The promise, stated as gates

1. **Differential gate**, per program: `--verify`, as above.
2. **Corpus gate**, `t/js/run.raku`: every program in `t/regression/`,
   `examples/` and the showcases is transpiled; each in-core program runs
   under Node and is compared byte for byte with the same binary
   *interpreting* it (not with a golden — the interpreter is the oracle,
   as in `tools/slim-diff.raku`). Report: in-core and agreeing / refused,
   with the histogram of reasons / **disagreeing — must be 0**. The first
   number is the one a stranger re-measures; the histogram is the work
   queue.
3. **Kernel gate**: the bench kernels under Node against the interpreter and
   Raku.js, in `perf-baseline.raku`'s format; not a release gate for the
   interpreter, a regression guard for the runtime.
4. **Size budget**: hello-world's `.js` plus runtime, gzipped, pinned after
   P1's first measurement and never raised without a sentence saying why.
5. **Doc examples** — the 953 byte-identical programs of the conformance
   campaign gain a third column when P2 lands.
6. **Interop goldens**: the P4 DOM and npm cases under Node, since the
   differential gate cannot see them.

## The multi-target seam

Two families of backend will exist, and the seam must serve both:

- **runtime-sharing** targets link `librakupp`: C++ today, Rust tomorrow
  (through the C ABI that `bindings/rust` already speaks — a Rust backend
  holds `rk_*` handles and emits `extern "C"` calls; it is *not* a Rust
  port of the runtime). Their printers differ; their analyses and their
  runtime calls are the same.
- **runtime-porting** targets carry their own runtime: JavaScript. The
  analyses are the same; the runtime calls are the port's.

What both need, and what therefore moves out of `Codegen.cpp`'s anonymous
namespace into `src/codegen/Common.{h,cpp}` — **on demand, one analysis at a
time, when the JS backend first needs it, each move pinned by the suites
that already cover `--exe`** (no speculative refactor of a 3,100-line file
that works):

1. the analyses: closure capture (`exprAssignsCaptured`, `isCapturedTarget`),
   declaration collection, `redo`/Whatever/slip detection, the user-sub,
   multi and `is rw` tables, placeholder computation, `DeclCheck`'s lax-name
   scan, the module export table from `collectModuleGraph`;
2. the mangling policy;
3. the **tiering policy and its report** — the `CodegenError` catch, the
   "Compiled … embedded …" reporting — behind a small `Backend` interface:
   `name()`, `emit(Program, Options) → files`, `runCommand(output)` for
   `--verify`, `fallback()`;
4. the differential runner, parametrised by backend so `t/js/run.raku` and
   a future `t/rust/run.raku` are one script with one argument;
5. the CLI: `--target=X`, `-o`, `--verify`, `--standalone`.

What is **not** built now: a target-neutral IR. [IR-PLAN.md](../experiments/IR-PLAN.md)
found that half a lowering pass already exists as verdicts cached on nodes,
and two printers over one AST are exactly the evidence that says which
desugarings (string interpolation, chained comparisons, `for` over ranges,
`given`/`when`, phaser placement) are worth a shared `Lower` pass. The third
backend earns the IR; the second only shows where it would go.

Layout when the dust settles: `src/codegen/Common.*`, `src/codegen/Cpp.cpp`
(today's `Codegen.cpp`, moved when Common exists), `src/codegen/Js.cpp`,
`src/js-rt/*.js` (the runtime, embedded into the binary at build time,
tested on its own under Node), `t/js/`.

## Prior art: what Rakudo.js taught

Rakudo had a JavaScript backend (Paweł Murias, 2015–2019 grants; merged into
rakudo and nqp; published to npm as `rakudo`; the 6pad playground). It
answered to `--target=js` and it compiled the **whole CORE setting** through
nqp-js, so every program shipped the setting — many megabytes and seconds of
startup — and the runtime was a translation of MoarVM's object model into JS.
It is now being removed: on 2026-09-03 lizmat opened
[Raku/problem-solving#527](https://github.com/Raku/problem-solving/issues/527)
— the backend has bit-rotted, and Raku++ and Mutsu already run Raku in the
browser through WebAssembly *with* NFG, which the JS backend never had — with
[rakudo/rakudo#6632](https://github.com/rakudo/rakudo/pull/6632) as the first
sweep (59 files, about 1,900 lines). It is prior art, not a dependency, and its
programs are not a migration target: they leaned on nqp-level JS ops that
nothing here reproduces. This plan differs in each of the three places that
made it heavy: the runtime is written for the core rather than compiled from
a setting, the interpreter stays the oracle and the differential suite the
gate, and the fallback for everything else is an engine that already exists
rather than a compile of everything.

## Order of work

**P0 — the frame.** `Mode::Transpile` with `--target=js`; `-o`; the
manifest header; the **WASM-wrapper tier** behind `--fallback=wasm` (Node and
browser flavours); `--verify`; `t/js/run.raku` reporting 0 in-core; the
corpus-ranking tool for builtins and methods; docs stubs in
[CLI.md](../../guide/CLI.md) and a new `docs/guide/JS.md`. Value on day one:
every program produces a runnable `.js` under `--fallback=wasm`, and the
mode's gates exist before its first native line.

**P1 — the core.** Scalars; Int/Num/Str/Bool/Rat with the representations
above (the Num probe first, against mandel); operators, string interpolation,
chained comparisons; arrays, hashes, pairs, ranges; subs with full signatures,
multis by arity and type; control flow with labels; `given`/`when`; `say`,
`print`, `note`; the first slice of the builtin ranking; the host adapter.
Gate: the kernels fib, loopsum, strcat, hash, streq, sortnums under Node; the
corpus count.

**P2 — objects and control.** Classes, roles, enums, subsets; `where` and
`:D` candidates; `gather`/`take` with the generator/eager rule; Seq and
laziness; exceptions, `CATCH`, phasers; `MAIN` and its usage protocol (the
C++ backend's, reused); modules as ESM files and `--standalone`. Gate: the 21
non-concurrent `examples/` in-core and agreeing; the doc-examples column.

**P3 — regexes and grammars.** Pre-parsed pattern trees; the matcher;
captures, actions, `make`/`made`; Unicode classes from generated tables; LTM
second. Gate: the showcase interpreters (lisp, forth, perl, python, js) in-core
— the same programs `rakujs/smoke.cjs` runs through WASM, now as JS.

**P4 — the JavaScript world.** `use JS` and marshalling; async colouring;
`sleep` via `Atomics.wait`; source maps; `.d.ts`; the npm recipe. Gate: a
DOM application and a Node script using an npm package, as goldens.

**P5 — the sites.** raku.online gains "show the JavaScript" and runs in-core
programs transpiled (mandel's time, before and after, is the number); the
spec site's runnable editors get the fast path; the tour's lessons show
their JS. This is where the thousand-fold speed-up reaches a reader.

**Rust:** nothing until P2 has shipped and the seam has carried two backends
for real; then a one-page design note against `Backend`, written the same
way this one was — probes first.

**P4 comes before P3.** With Rakudo's backend gone, nothing in the Raku world
can drive a web page from Raku code — the WASM route runs Raku in a browser
but cannot touch the page — while grammars in the browser already exist
through the WASM engine. Interop fills the hole the removal opens; grammars
make an existing thing smaller and faster. If review prefers the playground
and parser-module scenarios first, the two phases swap cleanly; nothing in
either depends on the other.

## Decisions for review

1. **`--target=js` with no alias** (against `--js`). Recommended as written.
2. **Refuse by default, `--fallback=wasm` to opt in** (against `--exe`'s
   silent fallback). Recommended as written, for the artifact-class reason.
3. **P4 before P3** — recommended, for the reason above: interop is what the
   Raku world loses with #527; grammars in the browser it already has.
4. **Where the runtime is published** — inside the binary (decided) and at
   `raku.online/rt/<version>/` (proposed).
5. **The Num representation** — decided by the P1 probe, not here; the plan
   records the candidate and why.

## Where it stands (2026-09-03, first sitting)

What landed, measured on the benchmark machine (Darwin 24.6, arm64, Node
20.11.1), against `build-arm64/rakupp` at v3.25.0-6:

- **P0, complete.** `--target=js`, `-o` (program + `rakupp-rt.js` sidecar),
  `--standalone`, `--verify` (exit 6 on disagreement), `--fallback=wasm`
  (the Node and browser wrapper; checked against `rakujs/node-build`), the
  manifest line (`--exe-info` reads a `.js`), `t/js/run.raku` with its three
  numbers and the refusal histogram, `tools/js/rank-builtins.raku`, the
  docs. The CLI-only CMake list keeps the 220 KB runtime out of `--exe`
  binaries. `t/run.raku` gained seven checks (the six kernels under
  `--verify`, the refusal shape, the wrapper's manifest).
- **P1, most of it, and the P2 objects.** The runtime covers the value table
  above (Int→BigInt promotion, boxed integral Nums, exact Rats with the
  2⁶⁴ degradation, graphemes from the generated UAX #29 tables, Map-backed
  hashes), subs with full signatures and multi dispatch, classes/roles/
  enums/subsets with accessors and `BUILD`/`TWEAK`, `gather`/`take` as a
  generator or eagerly, `CATCH`/`LEAVE`/`END`, junctions, sets/bags/mixes,
  `MAIN` with the interpreter's usage shape, IO::Path under Node/Bun/Deno.
- **Numbers.** The six kernels agree with the interpreter under `--verify`.
  Examples: 15 of 21 in-core and agreeing; the other 6 are the five
  regex/grammar programs (P3) and `life` (random seed, not judgeable).
  Regression corpus (418 programs, the edge-case suite by design): **60
  agree, 307 refused, 71 disagreeing** (the first commit read 35/306/96;
  the second sitting's batches — lexically scoped sub names, placeholders
  in signature-less subs, user-method lookups that no longer recurse into
  the core tables, modifier-form declarations, Iterable objects, the
  evaluation order of compound assignment through a subscript, sink
  context (a Failure throws, a lazy Seq runs), soft `div`/`%` by zero,
  multi narrowness by type depth with untyped-is-Any, `--runtime` — moved
  twenty-five). What is left is mostly design-level: container
  itemization (`$(1, 2)`), binding through slots, allomorphs, DateTime
  zones, Version wildcards, `.rw` accessors through `handles`. The refusal histogram's head is
  legitimate — names outside the core (Proc::Async, Buf, Supplier…), regex
  matches and literals, grammars, EVAL, NativeCall, nqp, programs that spawn
  `$*EXECUTABLE` — and the disagreements are a long tail of one-feature
  cases (multi narrowness ties, `.^mro` details, Date arithmetic, Str-range
  cross products, sink context), each named in the gate's output.
- **Speed, wall clock with Node's ~40 ms startup:** fib(29) 65 ms vs 297 ms
  interpreted, streq 82 vs 238, loopsum 41 vs 90; strcat/hash/sortnums are
  startup-bound and slower than the interpreter's 10–27 ms.

Where the code decided differently from the draft:

- Default output is an ES module with the sidecar; `--standalone` is a plain
  script. Node before 22.7 will not run the ESM `.js` unflagged, so `--verify`
  and the gate run the standalone form (documented in JS.md).
- No source map yet (`-o` writes no `.map`); P4 as the phase list says.
- The `Num` box was not probed against mandel separately: mandel is in-core
  and agrees, which is the same evidence.
- `--fallback=wasm` prepends `@*ARGS = (…);` to the source on the first line
  (the WASM engine has no argv), so line numbers hold.
- Value-collecting loops (`do for`) are in; `redo` is a labelled inner loop;
  loop variables are declared outside the loop so `LAST` sees the last one.

Next, in the plan's order: the disagreement tail down to 0 for the examples
and the kernels' neighbours (it is the gate's list), then P4 (`use JS`,
async colouring) before P3 (the regex tree + matcher), as decided above.
