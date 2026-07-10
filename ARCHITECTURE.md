# Raku++ Architecture

This document explains how Raku++ is built and, in particular, **what happens to
a source program** as it flows through the compiler in each of its run modes
(interpret, bundle, AOT, native compile).

For *what* the language supports see [FEATURES.md](FEATURES.md); for *how fast*
it runs see [BENCHMARKS.md](BENCHMARKS.md) and, for the `--exe -O` code
generator's optimizer, [OPTIMIZATION.md](OPTIMIZATION.md). Language-mutation
(custom operators, precedence traits, phasers, the MOP) is in
[METAPROGRAMMING.md](METAPROGRAMMING.md), and the concurrency/async model in
[ASYNC.md](ASYNC.md). This file is about the *how*.

---

## 1. The big picture

Raku++ is a hand-written **C++17** implementation with no third-party
dependencies. Source text goes through a classic front end and then splits by
mode:

```
  source ─► Lexer ─► Parser ─► AST ─┬─ interpret ─► tree-walk (Interpreter)
                                     ├─ --aot ─────► AstEmit (rebuild AST) ─► cc ─► binary (walks embedded tree)
                                     └─ --exe ─────► Codegen (AST → C++)    ─► cc ─► binary (native, no interpreter)

  source ─────────────────────────── --bundle ───► embed source bytes      ─► cc ─► binary (parses at run time)
```

Everything except the CLI entry point is compiled into a **static library,
`librakupp_rt.a`** ("the runtime"). Both the `rakupp` executable and the
programs produced by `--bundle`/`--aot`/`--exe` link against it — so the interpreter and
compiled programs share one implementation of `Value` semantics, built-ins,
method dispatch, regexes and Unicode.

---

## 2. The front end (shared by every mode)

### Lexer — `src/Lexer.{h,cpp}`, `src/Token.h`

A hand-written tokenizer (Raku's grammar is too irregular for a generated one).
It produces a flat `std::vector<Token>`. It carries Raku-specific state that a
naive lexer can't: `Token::spaceBefore` (whitespace significance — `f()` vs
`f ()`, postcircumfix vs block), quote forms (`q//`, `qq//`, `qw<>`, heredocs),
regex-vs-division context, `«…»`/`｢…｣` brackets, and Unicode identifiers.

### Parser — `src/Parser.{h,cpp}`, `src/Ast.h`

Recursive-descent for statements, a **Pratt (precedence-climbing) core** for
expressions. It builds the AST: a tree of `Node`s tagged by a single `NK` enum
(`IntLit`, `Binary`, `Call`, `IfStmt`, `SubDecl`, `ClassDecl`, …). The top-level
result is a `Program` (a `vector<StmtPtr>`). String interpolation is itself
parsed here (`parseInterpString`) into an `InterpStr` node whose parts are
sub-expressions.

The AST is deliberately simple and uniform — one `NK` switch drives both the
interpreter's `eval`/`exec` and the code generator.

You can see both stages directly: `RAKUPP_DUMPTOKENS=1 rakupp prog.raku` prints
the token stream, and `rakupp --ast prog.raku` prints the parsed AST as an
indented text tree (`src/AstDump.cpp`).

---

## 3. The runtime library — `librakupp_rt.a`

This is the heart. Key pieces:

| File | Role |
|---|---|
| `Value.{h,cpp}` | The universal runtime value (see below), coercions, gist/Str. |
| `BigInt.{h,cpp}` | Arbitrary-precision integers (base-1e9) for the exact number tower. |
| `Interpreter.{h,cpp}` | Tree-walking evaluator: `eval`/`exec`, scopes, calls, dispatch, `applyArith`, codegen helpers — plus the concurrency runtime (a CPython-style GIL, thread-local execution registers, and opt-in true parallelism via `RAKUPP_PARALLEL`; see [ASYNC.md](ASYNC.md)). |
| `Builtins.cpp` | Named built-ins, the `Test` module (TAP), and the ~big method dispatcher `methodCall`. |
| `Regex.{h,cpp}` | Recursive-descent regex/grammar engine with a backtracking matcher. |
| `Unicode.*`, `unicode_gen.cpp`, `unicode_names.cpp` | Normalization, grapheme segmentation, properties, names — tables generated from UCD 16.0. |
| `IOSpec.cpp` | `IO::Spec::*` path semantics (Unix/Win32). |
| `Highlight.{h,cpp}` | The parse-aware syntax highlighter behind `--highlight` (HTML + ANSI). |
| `Runtime.{h,cpp}` | Shared entry points: `rakuppRun` (lex+parse+interpret) and `rakuppRunProgram` (interpret a prebuilt AST, for `--aot`). |
| `Codegen.*` | `--exe` back end: transpiles the AST to native C++ (with an optional `-O` optimizer — see [OPTIMIZATION.md](OPTIMIZATION.md)). |
| `AstEmit.cpp` | `--aot` back end: emits C++ that rebuilds the AST. |
| `AstDump.cpp` | `--ast` AST printer. |
| `main.cpp` | CLI: interpret, `-e`, `--ast`, `--cpp`, `--highlight`, `--bundle`, `--aot`, `--exe`. |

### The `Value` type

Every Raku value at runtime is one `Value` struct with a `VT` tag:

```
Nil Any Bool Int Num Str Array Hash Code Range Pair Type Whatever Object Rat Regex Match Complex
```

It holds scalars inline (`i`, `n`, `b`) and shares heavy payloads via
`shared_ptr` (`arr` for arrays, `hash` for hashes/objects-attrs, `code` for
callables, `obj` for objects, `big`/`ratN`/`ratD` for the number tower). Copying
a `Value` is cheap and shares the payload — which is why, e.g., mutating an
object attribute through a copied `self` handle works.

### Environments, classes, callables

- `Env` — a lexical scope: `unordered_map<string,Value>` plus a parent pointer.
- `ClassInfo` — a class: name, parent, `attrs` (with defaults), and `methods`
  (a `map<string,Value>` of `Code` closures). Objects are `Value`+`ObjectData`
  (a `ClassInfo` + an attribute map).
- `Callable` — a sub/block/method. Either an AST body + closure `Env`, or a
  C++ `builtin` (`std::function`). This dual nature is what lets the native
  compiler represent compiled closures/methods as ordinary `Value`s.

---

## 4. What happens to a program — by mode

Consider this program (`demo.raku`):

```raku
sub square($n) { $n * $n }
my $total = 0;
for 1..5 { $total += square($_) }
say $total;
```

The CLI ([`src/main.cpp`](src/main.cpp)) picks a mode from the flags, then:

### Mode 1 — interpret (default): `rakupp demo.raku`

```
demo.raku ─► Lexer ─► Parser ─► Program (AST)
          ─► Interpreter.run(Program)  ─► walks the tree, node by node
```

`main` reads the source and calls `rakuppRunBigStack` (in `Runtime.cpp`, on a
large-stack thread so deep recursion is safe). That lexes, parses, and hands the
`Program` to `Interpreter::run`, which `exec`s each statement. Evaluation is a
recursive `eval(Expr*)` / `exec(Stmt*)` over the AST: a `for` loop iterates and
re-`exec`s its body block; `square($_)` looks the sub up in the `Env` chain and
`callCallable`s it; `$total += …` re-dispatches through `applyArith("+", …)`.

Nothing is cached or compiled — the same AST nodes are re-interpreted on every
iteration. Startup is instant (~12 ms); throughput pays the tree-walking tax.

### Mode 2 — bundle: `rakupp --bundle demo.raku -o demo`

```
demo.raku ─► (embedded verbatim as bytes) ─► generated stub.cpp
          ─► cc stub.cpp librakupp_rt.a ─► ./demo   (self-contained)
```

`compileToExe` does **not** parse the program at build time. It emits a tiny C++
stub that embeds the source as a byte array and calls the runtime:

```cpp
static const unsigned char SRC[] = { 115,117,98,32,... };
int main(int argc, char** argv) {
    std::string src(reinterpret_cast<const char*>(SRC), SRC_LEN);
    /* … collect argv … */
    return rakupp::rakuppRunBigStack(src, args, "demo.raku", exe);
}
```

then compiles it against `librakupp_rt.a`. The result is a standalone native
binary (no `rakupp` needed on the target), but at run time it still **lexes,
parses, and tree-walks** the embedded source — it *is* the interpreter in a box.
So its run time equals interpreting; the win is distribution and a ~10 ms start.

### Mode 3 — AOT (embed the parsed tree): `rakupp --aot demo.raku -o demo`

```
demo.raku ─► Lexer ─► Parser ─► Program (AST)   ← parsed at BUILD time
          ─► AstEmit.emitAstProgram(Program) ─► C++ that rebuilds the AST
          ─► cc gen.cpp librakupp_rt.a ─► ./demo
```

This is genuine ahead-of-time work: `compileAotAst` **parses the program at build
time** (so parse errors are reported *then*, not at run time), and
[`AstEmit`](src/AstEmit.cpp) emits one small builder function per AST node. The
generated `main` reconstructs the identical `Program` and hands it to
`rakuppRunProgram` — which interprets it with **no lexing or parsing at run time**:

```cpp
static ExprPtr e0() { auto n = std::make_unique<IntLit>(1LL); return n; }
static StmtPtr s3() { auto n = std::make_unique<ExprStmt>(); n->e = e2(); return n; }
// … one builder per node …
int main(int argc, char** argv) {
    Program prog;
    prog.stmts.push_back(s3());  /* … */
    return rakupp::rakuppRunProgramBigStack(prog, args, "demo.raku", exe, /*$=finish*/ "");
}
```

It still tree-walks the reconstructed tree, so it runs at the same speed as
`--bundle`. Because the interpreter runs the embedded tree, `--aot` handles the
**whole language, grammars included** — if `AstEmit` ever meets a node it can't
rebuild, it falls back to Mode 2 bundling.

Trade-off: it emits one builder function per AST node, so the generated C++ (and
the compiler's work) grows with program size — a few hundred lines of Raku
becomes tens of thousands of lines of C++, and builds ~10× slower than
`--bundle`. Since both tree-walk at the same runtime speed, `--bundle` is the
practical bundler; `--aot`'s only edge is catching parse errors at build time.

### Mode 4 — native compile: `rakupp --exe demo.raku -o demo`

```
demo.raku ─► Lexer ─► Parser ─► Program (AST)
          ─► Codegen.transpileToCpp(Program) ─► C++ source
          ─► cc gen.cpp librakupp_rt.a ─► ./demo   (no interpreter inside)
```

`compileNative` parses the program to an AST and then
[`Codegen`](src/Codegen.cpp) walks it, emitting **C++ that implements the program
directly** — native control flow, native calls — calling the runtime only for
`Value` operations. For `demo.raku` it produces (verbatim):

```cpp
#include "Interpreter.h"
#include "Value.h"
using namespace rakupp;
static Interpreter RT;                 // supplies builtins, method dispatch, coercions

static Value u_square(Value v_n);
static Value u_square(Value v_n) {
    return applyArith("*", v_n, v_n);  // Raku `$n * $n`
    return Value::any();
}
static void __rakupp_register() { }    // (classes/enums would be registered here)

int main(int argc, char** argv) {
    /* … RT.setArgs(...) … */
    __rakupp_register();
    try {
        Value v_total = Value::integer(0LL);
        {                                          // for 1..5 { … }  — a real C++ loop
            long long __lo1 = (Value::integer(1LL)).toInt();
            long long __hi2 = (Value::integer(5LL)).toInt();
            for (long long __i3 = __lo1; __i3 <= __hi2; __i3++) {
                Value v__t0 = Value::integer(__i3);        // $_
                v_total = applyArith("+", v_total, u_square(v__t0));
            }
        }
        RT.callBuiltin("say", {v_total});
    } catch (const RakuError& e) { std::cerr << e.message << "\n"; return 1; }
    return 0;
}
```

That's then compiled with the system C++ compiler and linked against the
runtime. The loop is a native `for`, the sub call is a direct C++ call, and only
`Value` semantics (`applyArith`, `callBuiltin`) dip into the runtime. This is why
loops and recursion run several times faster than interpreted (fib ≈ level with
Rakudo).

Passing **`-O`** (`--exe -O`, also `-O2`/`-O3`/`-Os`) turns on `Codegen`'s own
optimizer *before* the C++ compiler sees the source: direct-arity calls (no
per-call `ValueList`), inline `int64` fast paths for arithmetic/comparison
(skipping the string-dispatch and re-boxing `applyArith` would do), and
native-bool conditions. Values stay boxed — the fast paths just avoid the slow
machinery — and anything unrecognized falls back to `applyArith`, so results are
identical. What each pass does and how much it buys (fib 165 → 66 ms) is in
[OPTIMIZATION.md](OPTIMIZATION.md). (The `-O…` suffix also selects the backend
C++ compiler's own optimization level.)

#### How codegen reuses the runtime instead of reimplementing it

The generator never rebuilds the object system or the built-ins in C++. It maps
Raku constructs onto a handful of runtime hooks:

| Raku | Generated C++ |
|---|---|
| `$a + $b`, `$a eqv $b`, … | `applyArith("op", a, b)` |
| `say`, `.map`, `.sort`, any method | `RT.callBuiltin("say", …)` / `RT.methodCall(inv, "map", …)` |
| a block / `-> $x {…}` / `* + 1` | `Value::closure([=](ValueList& a){ … })` |
| `@a[i]` / `%h{k}` (read / write) | `rtIndexGet(...)` / `rtIndexRef(...)` (autoviv) |
| `[+] …` | `rtReduce("+", …)` |
| `class` | register a `ClassInfo` at startup; methods → `Value::closure`; `$!x` → `rtAttrGet`/`rtAttrRef` |
| `multi` | one C++ fn per candidate + a dispatcher using `rtTypeMatch` |
| `enum` | global `Value::enumVal` constants |
| `gather`/`take` | push a collector onto `RT.gatherStack_`; `take` routes through `callBuiltin` |
| phasers, `CATCH` | reordered emission / a C++ `try`+`when`-chain |

#### The fallback that makes `--exe` total

`Codegen` throws `CodegenError` on any construct it can't yet transpile (mainly
grammars). `compileNative` catches it and **transparently falls back to Mode 2 (`--bundle`)
bundling for that whole program** — so `--exe` never refuses a program: it
native-compiles what it can and bundles the rest, always producing a correct
binary.

---

## 5. Why interpreter-first, and the compile split

Raku has genuinely dynamic features (`EVAL`, runtime grammars, `BEGIN`-time
code), so the reference implementation is VM-based and Raku++ started the same
way: get the language correct under Roast first. User-defined operators (`sub
infix:<…>` and friends, with precedence traits) *are* supported — they're
resolved during the single parse pass — but the deeper grammar-mutating layer
(macros, `RakuAST`, full slangs) is not (see
[METAPROGRAMMING.md](METAPROGRAMMING.md)). That keeps the language rakupp handles
static enough to compile ahead of time — which is what mode 3 exploits. The
remaining dynamic/heavy constructs (grammars) are exactly the ones that stay
bundled.

---

## 6. Build layout

```
CMakeLists.txt        # librakupp_rt (all of src/ except main.cpp) + the rakupp exe
src/main.cpp          # CLI: interpret / -e / --bundle / --aot / --exe + compile drivers
build/librakupp_rt.a  # the runtime, linked into rakupp AND into --bundle/--aot/--exe binaries
build/rakupp          # the CLI
tools/run-roast.raku  # the self-hosted Roast harness (run by rakupp)
tools/run-bench.raku  # the benchmark harness
tools/bench/*.raku    # benchmark programs
```

Re-run `cmake -S . -B build` after adding a source file (the glob is
`CONFIGURE_DEPENDS` but caches); then `cmake --build build`.
