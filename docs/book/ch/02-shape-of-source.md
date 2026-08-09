# The Shape of the Source

Before the mechanisms, the map. This chapter is the one to come back to when a
later chapter names a file and you want to know what else lives near it.

## The numbers

`src/` holds about **147,000 lines** of C++. That figure is misleading on its
own, because **79,400 of them are generated Unicode tables** — character names,
properties, collation weights, normalization data — emitted from the pinned UCD
and UCA 17.0 files in `tools/ucd/`. Nobody reads those, and nobody edits them.

The hand-written implementation is about **67,700 lines**, and it is very
unevenly distributed:

| File | Lines | What it is |
|---|---:|---|
| `Interpreter.cpp` | 20,787 | the tree walk, calls, dispatch, modules, concurrency |
| `Builtins.cpp` | 9,887 | named built-ins, `Test`, the head of the method chain |
| `Parser.cpp` | 7,210 | statements, expressions, declarations, interpolation |
| `MethodCallPart2.cpp` | 3,549 | the method chain, continued |
| `Regex.cpp` | 2,785 | the regex and grammar engine |
| `MethodCallPart3.cpp` | 2,652 | the method chain, continued |
| `Codegen.cpp` | 2,586 | the `--exe` transpiler |
| `Lexer.cpp` | 2,351 | tokenizer |
| `MethodCallTail.cpp` | 2,277 | the method chain, the end of it |
| `Interpreter.h` | 1,363 | the interpreter's own interface, plus the `rt*` helpers |
| `main.cpp` | 1,281 | the CLI and the four compile drivers |
| `Value.cpp` | 1,093 | coercions, comparison, `gist`, `flatten` |

Two shapes stand out and both are deliberate.

**`Interpreter.cpp` is enormous.** It is the tree walk, and the tree walk
touches everything: scopes, calls, operators, assignment, control flow, module
loading, the FFI marshaller, the concurrency runtime. Splitting it by topic
would mostly move `#include`s around, because the pieces share the interpreter's
private state rather than a clean interface.

**The method dispatcher is split across four files for one reason.** It used to
be a single 9,138-line function, `methodCallInner`, and that stopped being
compilable in a reasonable time. It is now four ordered *segments* —
`Builtins.cpp` holds the head, then `MethodCallPart2`, `MethodCallPart3`,
`MethodCallTail` — each returning `std::optional<Value>`, where `nullopt` means
"not handled here, try the next segment".

The critical property, stated in the source and worth repeating: **these are
segments, not categories.** The chain is order-sensitive. Later arms
deliberately catch what earlier ones decline. An arm belongs where its priority
is, not where it reads nicely. `MethodCallSegment.h` exists so that the four
files share one include prologue and cannot drift apart.

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
| `Interpreter.{h,cpp}` | the tree walk and nearly everything it reaches |
| `Builtins.cpp` | the built-in routine table and the method chain's head |
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
matter. Chapter 18 is entirely about what these fields hold and, more
importantly, what they must never hold.

**`rt*` functions are the compiled backend's vocabulary.** Anything named
`rtAdd`, `rtIndexRef`, `rtAttrGet`, `rtCallB` is a runtime entry point that
`Codegen` emits calls to. They are declared in `Interpreter.h` and are the
contract between the transpiler and the runtime — which is why they are
`inline` where the fast path matters. Chapters 25 to 27 are about them.

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
35 is about why the performance gate is there and what happens when it is
skipped.
