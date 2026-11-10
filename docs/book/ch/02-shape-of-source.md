# The Shape of the Source

Before the mechanisms, the map. This chapter is the one to come back to when a
later chapter names a file and you want to know what else lives near it.

## The numbers

`src/` holds about **348,000 lines** of C++. That figure is misleading on its
own, because **102,000 of them are generated**. Most of that, 89,000 lines, is
the Unicode tables — character names, properties, collation weights,
normalization data, emitted from the pinned UCD and UCA 18.0 files in
`tools/ucd/`. The rest is the JavaScript runtime, which is written as
JavaScript in `src/js-rt/` and baked into one C++ file so a binary carries the
runtime it was built with; the index of docs.raku.org pages that `--doc-links`
resolves names against; and three small tables read off Rakudo — the ancestry
of the `X::` exceptions, the `RakuAST` class names, and the grammar service's
Raku half. Nobody reads those, and nobody edits them where they land.

The hand-written implementation is about **247,000 lines**, and it is very
unevenly distributed:

| File | Lines | What it is |
|---|---:|---|
| `InterpreterCore.cpp` | 33,040 | the hot paths of the tree walk: `eval`, `exec`, operators, assignment, calls |
| `Parser.cpp` | 18,459 | statements, expressions, declarations, interpolation |
| `MethodCallPart2.cpp` | 11,198 | the method chain, continued |
| `Codegen.cpp` | 10,620 | the `--exe` transpiler |
| `Builtins.cpp` | 10,491 | the built-ins' helpers, `methodCall`, the head of the method chain |
| `InterpreterModules.cpp` | 9,945 | module loading, `EVAL`, declarations |
| `InterpreterBinding.cpp` | 9,768 | closures, signatures and binding, multi dispatch, NativeCall |
| `Interpreter.cpp` | 9,624 | construction, the mainline run, the precompiled-module cache |
| `MethodCallPart3.cpp` | 7,918 | the method chain, continued |
| `InterpreterCalls.cpp` | 6,917 | calls, method invocation, lvalues and the assignment helpers |
| `Interpreter.h` | 6,639 | the interpreter's own interface, plus the `rt*` helpers |
| `Regex.cpp` | 6,100 | the regex and grammar engine |
| `MethodCallTail.cpp` | 5,834 | the method chain, the end of it |
| `Lexer.cpp` | 5,735 | tokenizer |
| `main.cpp` | 4,838 | the CLI, the compile drivers and the tooling subcommands |

Two shapes stand out and both are deliberate.

**The interpreter is one class in seven files.** The tree walk touches
everything — scopes, calls, operators, assignment, control flow, module
loading, the FFI marshaller, the concurrency runtime — and the pieces share the
interpreter's private state rather than a clean interface, so the files are cut
for compile time, not by concept. `InterpreterCore.cpp` holds every function
the `perf-guard` kernels spend their time in, whatever it does, and the
per-thread execution state behind `tctx()`. The hot functions have to share a
file: in separate files they no longer inline into each other, and the state
was then a `thread_local` with a destructor, which a file that does not define
it reaches through a call on every access — measured, that cost 14–35% on the
kernels. (The state is reached through a constant-initialized pointer now,
which costs the same from any file.) The other six files
(`Interpreter`, `…Modules`, `…Binding`, `…Calls`, `…Regex`, `…Operators`) hold
the rest in source order, and `InterpreterParts.h` declares what they share.
`tools/source-helpers/` holds the scripts that made the cut, and the plan they
followed; `budget.raku` there fails `t/run.raku` when a source file, a function
or a widely included header grows past its recorded ceiling, so the next
59,000-line file is noticed long before it is one.

**The method dispatcher is split across six files for the same reason.** It used
to be a single 9,138-line function, `methodCallInner`, and that stopped being
compilable in a reasonable time. It is now six ordered *segments* —
`Builtins.cpp` holds the head, then `MethodCallPart1b`, `MethodCallPart1c`,
`MethodCallPart2`, `MethodCallPart3`, `MethodCallTail` — each returning
`std::optional<Value>`, where `nullopt` means "not handled here, try the next
segment". `MethodCallEntry.cpp` is not a seventh segment: it answers the
commonest questions about a plain core value before the chain is entered, each
answer the one the chain's own arm would give. `registerBuiltins`, which fills the built-in routine table, is cut the
same way: five pieces in the `BuiltinsRegister*.cpp` files, each calling the
next, so the registrations still run in one order.

The critical property, stated in the source and worth repeating: **these are
segments, not categories.** The chain is order-sensitive. Later arms
deliberately catch what earlier ones decline. An arm belongs where its priority
is, not where it reads nicely. `MethodCallSegment.h` gives `MethodCallPart2`,
`MethodCallPart3` and `MethodCallTail` one include prologue, so they cannot
drift apart; `MethodCallPart1b` and `1c` share `BuiltinsParts.h` with the head.

## The library boundary

```
  src/main.cpp        the rakupp CLI          ─┐
                                               ├─► both link librakupp_rt.a
  generated stub.cpp  a --exe/--aot binary    ─┘
```

Nearly everything compiles into `librakupp_rt.a`. What does not falls into two
groups.

The first is what only the command line can reach, and it is linked into the
`rakupp` executable alone:

```
main.cpp             Repl.cpp              Codegen.cpp
codegen/Js.cpp       JsRuntimeSrc.cpp      codegen/Arduino.cpp
DocLinks.cpp         raku_docs_index_gen.cpp
```

plus one generated file, `EmbeddedTools.cpp`, which holds the Raku programs
behind `rakupp install`, `doc` and `upgrade` as byte arrays. The reason is the same for each: nothing
about an interactive session, a transpiler or an installer should be linked
into the standalone binaries the compiling modes produce. `Codegen.cpp` was in
the runtime until the tier-up JIT stopped calling it by name; it is the biggest
object in the tree, and no `--exe` binary transpiles.

The second is four archives of their own, which the CLI and an ordinary `--exe`
binary link whole and a `--slim` binary may replace with a throwing stub
(Chapter 29):

| Archive | Holds |
|---|---|
| `rakupp_ucd_names` | character names and sequences |
| `rakupp_ucd_coll` | the collation table |
| `rakupp_ucd_props` | scripts, blocks, bidi and the property tables |
| `rakupp_parse` | the lexer, the parser, the `RakuAST` view and `--fmt` |

An optional build of the same sources, `librakupp`, is the shared library an
embedding host loads; it always carries all four.

## What each file is for

**Front end**

| File | Role |
|---|---|
| `Token.h` | the token struct: kind, text, position, `spaceBefore` |
| `Lexer.{h,cpp}` | source text to a flat `vector<Token>` |
| `Ast.h` | every node type, and the `NK` tag enum |
| `Parser.{h,cpp}` | tokens to a `Program`; the live user-operator tables |
| `Parser*.cpp` | three pieces split out of `Parser.cpp` for its size |
| `Slang.h` | what an activated slang hands the lexer and the parser |
| `Pod.{h,cpp}` | the `$=pod` DOM |

**Values**

| File | Role |
|---|---|
| `Value.{h,cpp}` | the fat tagged struct, `CowStr`, `ClassInfo`, `Callable` |
| `IStr.h` | the interned-string field |
| `MethodName.h` | `MName`, the packed method name used by the dispatch chain |
| `BigInt.{h,cpp}` | arbitrary-precision integers, base 10^9 |
| `IntOps.h` | portable overflow-checked arithmetic and bit intrinsics |
| `Ref.h`, `BodyPool.h` | the intrusive reference count, and where a `Value`'s out-of-line blocks live |
| `ValueVec.h`, `ValueHash.h` | the array and the hash behind a `Value` |
| `Cycle.{h,cpp}` | the backup cycle collector |

**Execution**

| File | Role |
|---|---|
| `Interpreter.h`, `Interpreter*.cpp` | the tree walk and nearly everything it reaches; `InterpreterCore.cpp` is its hot half |
| `InterpreterParts.h` | what the `Interpreter*.cpp` files share |
| `Builtins.cpp` | the built-ins' helpers and the method chain's head |
| `BuiltinsRegister*.cpp` | the built-in routine table, filled in five pieces |
| `BuiltinsSupply.cpp`, `BuiltinsNqp.cpp` | supplies and the `--exe` built-in natives; the `nqp::` ops |
| `BuiltinsParts.h` | what the `Builtins*.cpp` and `MethodCallPart1b/1c.cpp` files share |
| `MethodCall*.cpp` | the rest of the method chain, and the answers given before it |
| `IntKernel.cpp` | the kernels: subs over integers and loops over plain scalars, run on machine values instead of `Value`s |
| `Jit.{h,cpp}`, `Cnp.{h,cpp}`, `cnp/` | the two tier-up back ends, `--jit` and `--cnp` |
| `Coro.{h,cpp}` | stackful coroutines, which `gather` is built on |
| `DeclCheck.{h,cpp}`, `CallCheck.{h,cpp}` | the two whole-unit checks run before a program starts |
| `BuiltinsShared.h` | helpers the split forced out of file scope |
| `Runtime.{h,cpp}` | the shared entry points, and the big-stack thread |
| `IOSpec.cpp` | `IO::Spec::*` path algorithms |

**Engines**

| File | Role |
|---|---|
| `Regex.{h,cpp}` | regex compilation, the matcher, `GrammarMatcher` |
| `LtmNfa.{h,cpp}` | the declarative-prefix NFA for longest-token matching |
| `Unicode.{h,cpp}` | normalization, grapheme segmentation, collation, properties |
| `unicode_*_gen.cpp` | the generated tables those read |
| `unicode_names.cpp` | character names — the single largest file in the tree |

**Back ends and tooling**

| File | Role |
|---|---|
| `Codegen.{h,cpp}` | `--exe`: AST to C++ |
| `codegen/Js.{h,cpp}` | `--target=js`: AST to JavaScript |
| `codegen/Arduino.{h,cpp}` | `--target=arduino`: a subset of Raku to one sketch |
| `AstEmit.cpp` | `--aot`: C++ that carries the serialised AST |
| `CompileTime.{h,cpp}` | the build-time run that gives a binary its constants |
| `AotModules.{h,cpp}` | native routine bodies for the modules an `--exe` binary embeds |
| `AstSerial.{h,cpp}` | the binary AST format behind the precompiled parse |
| `AstDump.cpp` | `--dump-ast` |
| `SlimScan.{h,cpp}` | `--slim`: the feature scan over a parsed program |
| `FeatureGate.cpp` | the `X::Feature::NotBuilt` a cut feature throws |
| `ucd_seam.h` | the accessors the cuttable Unicode tables sit behind |
| `stubs/` | one throwing stand-in per cuttable feature |
| `Lint.{h,cpp}` | `--lint`, static analysis over the parsed tree |
| `Highlight.{h,cpp}` | `--highlight`, parse-aware syntax colouring |
| `Profiler.{h,cpp}` | `--profile`, the routine-level wall-time profiler |
| `Fmt.{h,cpp}` | `--fmt`, the source formatter |
| `DocLinks.{h,cpp}` | `--doc-links`, each construct linked to its docs.raku.org page |
| `Lsp.{h,cpp}`, `LspIndex.{h,cpp}` | `--lsp`, the language server |
| `JupyterKernel.{h,cpp}` | `--jupyter`, the interpreter as a Jupyter kernel |
| `McpServer.{h,cpp}` | `--mcp`, the interpreter over the Model Context Protocol |
| `RaceDetector.{h,cpp}` | `--detect-races`, a happens-before race detector |
| `Sandbox.{h,cpp}`, `SandboxOs.{h,cpp}` | `--sandbox`: the policy, and the OS confinement under it |
| `RakuAst*.cpp` | the `RakuAST` classes, and the view built over the parsed tree |
| `Repl.{h,cpp}` | the interactive session |

**Boundaries**

| File | Role |
|---|---|
| `Ffi.{h,cpp}` | the libffi backend: loader, ABI probe, type registry |
| `include/rakupp/rakupp_ext.h` | the C ABI extension modules compile against |
| `ExtApi.cpp` | the host side of that ABI |
| `include/rakupp/rakupp.h` | the C API a host embeds the interpreter through |
| `EmbedApi.cpp` | the host side of that one |
| `Platform.h` | the Windows/POSIX split, in one place |

## Reading conventions in the source

Three habits recur, and knowing them saves a lot of confusion.

**Comments explain *why*, and often carry the measurement.** A comment in this
tree is rarely a restatement of the code. It is much more often the reason the
obvious version was rejected, sometimes with a number attached:

```cpp
// src/Value.h — the CowStr rationale, trimmed
// Value is copied by value everywhere ... so holding a bare std::string
// meant a long string was memcpy'd on each of those. The cost is
// O(length) per OPERATION, which makes any pure-Raku tokenizer O(n^2):
// JSON::Fast spent 13.9 s on a 421 KB document that Rakudo parses in
// 50 ms, and the profile was all copying, not parsing.
```

When this book explains a decision, it is usually expanding a comment like that
one.

**A "decided once" field is a fact about the syntax, not a cached result.**
Several node types carry a small mutable field that starts at a sentinel and is
written on first evaluation:

```cpp
// src/Ast.h
template <typename T> struct DecidedOnce {
    std::atomic<T> v;
    operator T() const { return v.load(std::memory_order_relaxed); }
    DecidedOnce& operator=(T x) {
        v.store(x, std::memory_order_relaxed); return *this;
    }
};
```

The atomic is not for synchronisation. It is there because a node is shared
between threads, every writer computes the same idempotent answer, and a plain
field would make that a data race that ThreadSanitizer correctly reports.
Relaxed atomics make it defined at plain-load cost on the architectures that
matter. Chapter 19 is entirely about what these fields hold and, more
importantly, what they must never hold.

**`rt*` functions are the compiled backend's vocabulary.** Anything named
`rtAdd`, `rtIndexRef`, `rtAttrGet`, `rtCallB` is a runtime entry point that
`Codegen` emits calls to. They are declared in `Interpreter.h` and are the
contract between the transpiler and the runtime — which is why they are
`inline` where the fast path matters. Chapters 26 to 28 are about them.

## Building it

```sh
cmake -S . -B build
cmake --build build -j
```

`CMakeLists.txt` builds `librakupp_rt` and the four feature archives from
`src/`, then the `rakupp` executable from the CLI-only files and all five. The
install step gives `rakupp` a second name, `raku++`, a symbolic link to the
same binary (on Windows the installers make it). The glob is `CONFIGURE_DEPENDS`, but CMake caches
it, so re-run the configure step after adding a source file.

The compiler matters more than usual here. Clang produces a binary between 1.2
and 2 times faster than GCC's on this codebase — the method dispatch chain and
the tree walk are both inlining-sensitive in ways GCC handles less well — so
Clang is what ships and GCC is kept as a portability gate. Link-time
optimisation and `-mcpu=native` were both measured and both did nothing.

## The test surface

| Where | What |
|---|---|
| Roast | the Raku specification suite, run by `tools/run-roast.raku` |
| `t/run.raku` | the local suite: examples and showcases, byte-compared to golden output |
| `t/regression/` | one file per fixed bug |
| `t/stress/` | concurrency and memory stress, also run under TSan and ASan |
| `tools/perf-guard.raku` | the performance gate, compared against a recorded baseline |
| the showcase interpreters | JavaScript, Perl, Python, Lisp and Forth, written in Raku |

The release checklist in `docs/dev/RELEASING.md` gates on all of them. Chapter
40 is about why the performance gate is there and what happens when it is
skipped.
