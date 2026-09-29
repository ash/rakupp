# The Shape of the Source

Before the mechanisms, the map. This chapter is the one to come back to when a
later chapter names a file and you want to know what else lives near it.

## The numbers

`src/` holds about **274,000 lines** of C++. That figure is misleading on its
own, because **93,000 of them are generated**: the Unicode tables — character
names, properties, collation weights, normalization data, emitted from the
pinned UCD and UCA 17.0 files in `tools/ucd/` — and the JavaScript runtime,
which is written as JavaScript in `src/js-rt/` and baked into one C++ file so a
binary carries the runtime it was built with. Nobody reads those, and nobody
edits them where they land.

The hand-written implementation is about **182,000 lines**, and it is very
unevenly distributed:

| File | Lines | What it is |
|---|---:|---|
| `InterpreterCore.cpp` | 28,737 | the hot paths of the tree walk: `eval`, `exec`, operators, assignment, calls |
| `Parser.cpp` | 16,741 | statements, expressions, declarations, interpolation |
| `MethodCallPart2.cpp` | 10,222 | the method chain, continued |
| `Builtins.cpp` | 8,964 | the built-ins' helpers, `methodCall`, the head of the method chain |
| `Interpreter.cpp` | 7,559 | construction, the mainline run, the precompiled-module cache |
| `InterpreterModules.cpp` | 7,034 | module loading, `EVAL`, declarations |
| `MethodCallPart3.cpp` | 6,671 | the method chain, continued |
| `InterpreterBinding.cpp` | 5,642 | closures, signatures and binding, NativeCall |
| `Regex.cpp` | 5,428 | the regex and grammar engine |
| `Lexer.cpp` | 5,017 | tokenizer |
| `MethodCallTail.cpp` | 4,999 | the method chain, the end of it |
| `Codegen.cpp` | 4,652 | the `--exe` transpiler |
| `Interpreter.h` | 4,003 | the interpreter's own interface, plus the `rt*` helpers |
| `main.cpp` | 3,670 | the CLI, the compile drivers and the tooling subcommands |

Two shapes stand out and both are deliberate.

**The interpreter is one class in seven files.** The tree walk touches
everything — scopes, calls, operators, assignment, control flow, module
loading, the FFI marshaller, the concurrency runtime — and the pieces share the
interpreter's private state rather than a clean interface, so the files are cut
for compile time, not by concept. `InterpreterCore.cpp` holds every function
the `perf-guard` kernels spend their time in, whatever it does, and the
definition of `tctx_`, the per-thread execution state. The hot functions have to
share a file: in separate files they no longer inline into each other, and a
`thread_local` read from a file that does not define it goes through a call on
every access — measured, that cost 14–35% on the kernels. The other six files
(`Interpreter`, `…Modules`, `…Binding`, `…Calls`, `…Regex`, `…Operators`) hold
the rest in source order, and `InterpreterParts.h` declares what they share.
`tools/source-helpers/` holds the scripts that made the cut, and the plan they
followed.

**The method dispatcher is split across six files for the same reason.** It used
to be a single 9,138-line function, `methodCallInner`, and that stopped being
compilable in a reasonable time. It is now six ordered *segments* —
`Builtins.cpp` holds the head, then `MethodCallPart1b`, `MethodCallPart1c`,
`MethodCallPart2`, `MethodCallPart3`, `MethodCallTail` — each returning
`std::optional<Value>`, where `nullopt` means "not handled here, try the next
segment". `registerBuiltins`, which fills the built-in routine table, is cut the
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

Everything except `main.cpp` and `Repl.cpp` compiles into `librakupp_rt.a`.
The REPL lives in the executable rather than the library on purpose: nothing
about an interactive session should be linked into the standalone binaries the
compiling modes produce.

## What each file is for

**Front end**

| File | Role |
|---|---|
| `Token.h` | the token struct: kind, text, position, `spaceBefore` |
| `Lexer.{h,cpp}` | source text to a flat `vector<Token>` |
| `Ast.h` | every node type, and the `NK` tag enum |
| `Parser.{h,cpp}` | tokens to a `Program`; the live user-operator tables |
| `Pod.{h,cpp}` | the `$=pod` DOM |

**Values**

| File | Role |
|---|---|
| `Value.{h,cpp}` | the fat tagged struct, `CowStr`, `ClassInfo`, `Callable` |
| `IStr.h` | the interned-string field |
| `MethodName.h` | `MName`, the packed method name used by the dispatch chain |
| `BigInt.{h,cpp}` | arbitrary-precision integers, base 10^9 |
| `IntOps.h` | portable overflow-checked arithmetic and bit intrinsics |

**Execution**

| File | Role |
|---|---|
| `Interpreter.h`, `Interpreter*.cpp` | the tree walk and nearly everything it reaches; `InterpreterCore.cpp` is its hot half |
| `InterpreterParts.h` | what the `Interpreter*.cpp` files share |
| `Builtins.cpp` | the built-ins' helpers and the method chain's head |
| `BuiltinsRegister*.cpp` | the built-in routine table, filled in five pieces |
| `BuiltinsSupply.cpp`, `BuiltinsNqp.cpp` | supplies and the `--exe` built-in natives; the `nqp::` ops |
| `BuiltinsParts.h` | what the `Builtins*.cpp` and `MethodCallPart1b/1c.cpp` files share |
| `MethodCall*.cpp` | the rest of the method chain |
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
| `AstEmit.cpp` | `--aot`: C++ that rebuilds the AST |
| `AstSerial.{h,cpp}` | the binary AST format behind the precompiled parse |
| `AstDump.cpp` | `--dump-ast` |
| `SlimScan.{h,cpp}` | `--slim`: the feature scan over a parsed program |
| `FeatureGate.cpp` | the `X::Feature::NotBuilt` a cut feature throws |
| `ucd_seam.h` | the accessors the cuttable Unicode tables sit behind |
| `stubs/` | one throwing stand-in per cuttable feature |
| `Lint.{h,cpp}` | `--lint`, static analysis over the parsed tree |
| `Highlight.{h,cpp}` | `--highlight`, parse-aware syntax colouring |
| `Profiler.{h,cpp}` | `--profile`, the routine-level wall-time profiler |
| `Repl.{h,cpp}` | the interactive session |

**Boundaries**

| File | Role |
|---|---|
| `Ffi.{h,cpp}` | the libffi backend: loader, ABI probe, type registry |
| `rakupp_ext.h` | the C ABI extension modules compile against |
| `ExtApi.cpp` | the host side of that ABI |
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

`CMakeLists.txt` builds `librakupp_rt` from all of `src/` except `main.cpp`,
then the `rakupp` executable. The glob is `CONFIGURE_DEPENDS`, but CMake caches
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
| the showcase interpreters | JavaScript, Perl, Python and Lisp, written in Raku |

The release checklist in `docs/dev/RELEASING.md` gates on all of them. Chapter
40 is about why the performance gate is there and what happens when it is
skipped.
