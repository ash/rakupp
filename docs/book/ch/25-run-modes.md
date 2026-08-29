\part{The Back Ends}

# Four Ways to Run a Program

The same binary will run the same Raku source four different ways, and the
choice is not academic: one mode is for editing and re-running, one for handing
someone a program that needs no interpreter installed, one for cutting start-up
time, one for throughput. They share the entire front end and diverge
completely after it.

One front end, four back ends. This chapter takes one small program through all
four and shows exactly what each produces.

```raku
# demo.raku
sub square($n) { $n * $n }
my $total = 0;
for 1..5 { $total += square($_) }
say $total;
```

The CLI in `main.cpp` picks a mode from the flags and calls one of four
drivers.

## Mode 1 — interpret

```
demo.raku ─► Lexer ─► Parser ─► Program (AST)
          ─► Interpreter.run(Program)  ─► walk the tree, node by node
```

`main` reads the source and calls `rakuppRunBigStack`, which lexes, parses, and
hands the `Program` to `Interpreter::run`. Evaluation is the recursive
`eval`/`exec` of Chapter 13: the `for` loop iterates and re-executes its body
block, `square($_)` looks the sub up in the environment chain and calls it,
`$total += …` re-dispatches through `applyArith`.

Nothing is cached or compiled — the same AST nodes are re-interpreted on every
iteration. Startup is about two milliseconds. This is the only mode that runs
every construct in the language, so it is the one the language is developed
against.

## Mode 2 — `--bundle`

```
demo.raku ─► (embedded verbatim as bytes) ─► generated stub.cpp
          ─► cc stub.cpp librakupp_rt.a ─► ./demo
```

The driver does **not** parse the program at build time. It emits a small stub
that embeds the source as a byte array and calls the runtime:

```cpp
static const unsigned char SRC[] = { 115,117,98,32,… };
int main(int argc, char** argv) {
    std::string src(reinterpret_cast<const char*>(SRC), SRC_LEN);
    /* … collect argv … */
    return rakupp::rakuppRunBigStack(src, args, "demo.raku", exe);
}
```

then compiles it and links against the runtime. The result is standalone — no
`rakupp` needed on the target machine — but at run time it still lexes, parses
and tree-walks. It *is* the interpreter in a box. The win is distribution and a
roughly ten-millisecond start; run time equals interpreting.

## Mode 3 — `--aot`

```
demo.raku ─► Lexer ─► Parser ─► Program        ← parsed at BUILD time
          ─► AstEmit.emitAstProgram(Program) ─► C++ that rebuilds the AST
          ─► cc gen.cpp librakupp_rt.a ─► ./demo
```

This is genuine ahead-of-time work: the program is **parsed at build time**, so
parse errors are reported then rather than at run time, and `AstEmit` emits one
small builder function per AST node:

```cpp
static ExprPtr e0() { auto n = std::make_unique<IntLit>(1LL); return n; }
static StmtPtr s3() { auto n = std::make_unique<ExprStmt>(); n->e = e2();
                      return n; }
// … one builder per node …
int main(int argc, char** argv) {
    Program prog;
    prog.stmts.push_back(s3());  /* … */
    return rakupp::rakuppRunProgramBigStack(prog, args, "demo.raku", exe, "");
}
```

The generated `main` reconstructs the identical `Program` and hands it to
`rakuppRunProgram`, which interprets it with **no lexing or parsing at run
time**.

Because the interpreter runs the embedded tree, `--aot` handles the **whole
language, grammars included**. If `AstEmit` ever meets a node it cannot rebuild,
it throws `AstEmitError` and the driver falls back to mode 2.

The trade-off is the generated code size: one builder function per node means a
few hundred lines of Raku become tens of thousands of lines of C++, and it
builds about ten times slower than bundling. Since both tree-walk at the same
speed, `--bundle` is the practical bundler; `--aot`'s only real edge is catching
parse errors at build time.

## Mode 4 — `--exe`

```
demo.raku ─► Lexer ─► Parser ─► Program
          ─► Codegen.transpileToCpp(Program) ─► C++ source
          ─► cc gen.cpp librakupp_rt.a ─► ./demo   (no interpreter inside)
```

`Codegen` walks the tree and emits C++ that **implements the program directly**
— native control flow, native calls — calling the runtime only for `Value`
operations. For `demo.raku`, abridged:

```cpp
#include "Interpreter.h"
using namespace rakupp;
static Interpreter RT;
static Value v_stotal = Value::any();     // top-level `my $total`
static const BuiltinFn* __bfp0 = nullptr; // say — resolved once at startup

static Value u_square(ValueList __a) {
    Value v_sn = rtPos(__a, 0);
    return applyArith("*", v_sn, v_sn);
}
static void __rakupp_register() { __bfp0 = RT.builtinPtr("say"); }

int main(int argc, char** argv) {
    __rakupp_register();
    try {
        v_stotal = Value::integer(0LL);
        {                                        // for 1..5 — a real C++ loop
            long long __lo = Value::integer(1LL).toInt();
            long long __hi = Value::integer(5LL).toInt();
            for (long long __i = __lo; __i <= __hi; __i++) {
                Value v__t0 = Value::integer(__i);           // $_
                try { v_stotal = applyArith("+", v_stotal,
                                            u_square(ValueList{v__t0})); }
                catch (const NextEx&) { continue; }
                catch (const LastEx&) { break; }
            }
        }
        rtCallB(RT, __bfp0, "say", ValueList{v_stotal});
    } catch (const RakuError& e) { std::cerr << e.message << "\n"; return 1; }
    return 0;
}
```

The loop is a native `for`, the sub call is a direct C++ call, and only value
semantics dip into the runtime. This is the only mode whose runtime performance
differs, and the only one the optimizer touches.

## Comparing them

| | parse when | runs how | whole language | speed |
|---|---|---|---|---|
| interpret | run time | tree walk | yes | baseline |
| `--bundle` | run time | tree walk | yes | baseline |
| `--aot` | build time | tree walk | yes | baseline |
| `--exe` | build time | native C++ | falls back | several times faster on hot loops |

The important row is the last column of the third row: **three of the four modes
run at the same speed**, because three of them are the same tree walk. Bundling
and AOT buy distribution and build-time error reporting, not throughput.

## The fallback that makes `--exe` total

`Codegen` throws on any construct it cannot transpile:

```cpp
// src/Codegen.h
struct CodegenError { std::string msg; };
```

mainly grammars, indirect method calls (`."$name"()`, whose name no generated
call site evaluates), and a handful of NativeCall shapes that need copy-back the
generated call site cannot express. The driver catches it and **transparently
bundles the whole program instead**.

So `--exe` never refuses a program. It native-compiles what it can and bundles
the rest, always producing a correct binary. The user is told which happened.

## What every mode shares

**One runtime.** Everything except the CLI is in `librakupp_rt.a` — plus four
feature archives a compiled binary may leave out, which is Chapter 29. A feature
implemented once behaves identically in all four modes. `Value` is the shared
currency, `callBuiltin`/`callCallable` the shared calling convention,
`rtIndexRef`/`rtAttrRef`/`rtArrayVal` the shared container operations, and
`callNative` the shared FFI.

**One big stack.** Every entry point runs the program body on a thread with a
large reserved stack, including a compiled binary's `main`, so the recursion
budget matches across modes.

**One compile-time gate.** Before any mode does its own work, the parsed unit is
asked whether every variable in it is declared, and a program that fails is
refused rather than started — so a typo on line 90 is not discovered after 89
lines of output, and `--exe` reports it itself instead of emitting C++ that names
an identifier it never declared. `--aot` and `--bundle` inherit it at build time;
a `--bundle` binary, which parses its embedded source at run time, is checked
again then, since that is the interpreter path. The pass, and why it is careful
to the point of standing down, is in Chapter 38.

**Modules are always interpreted.** `Codegen` emits a call to
`Interpreter::rtUse`, a thin mirror of the interpreter's `use` handling, which
calls the same `loadModule`. Only the *main program* is compiled; a compiled
binary still loads and interprets its modules (Chapter 32) — though it can
carry their serialised ASTs inside itself, which is the next section.

## Carrying modules inside a binary

A standalone binary that needs its modules' source files on the target machine
is not very standalone. So the compiling modes resolve the `use` graph at build
time and embed each module's serialised AST:

```cpp
// src/Interpreter.h
struct BundledModule { std::string name, blob, finish, src; };
std::vector<BundledModule> collectModuleGraph(
    const Program& prog, const std::vector<std::string>& searchPath,
    std::set<std::string>* exportsOut = nullptr);
void rakuppRegisterModule(const std::string& name, const char* blob,
                          size_t blobLen, const std::string& finish);
```

Registration happens once at startup, and `loadModule` consults the embedded
table before it looks at the disk.

A module that cannot be found, parsed or serialised is **simply left out**
rather than failing the build — the binary falls back to loading it from disk,
which is also what has to happen for anything only a running program can name
(`require ::($x)`, a computed `use lib`).

`--bundle` needs one extra thing, and it is a subtle one. It parses the main
program at *run* time, and that parse has to scan each `use`d module for the
operators it declares (Chapter 6) — otherwise a program using an imported
operator stops parsing once the module tree is gone. So bundled binaries also
carry the module **sources**:

```cpp
// src/Parser.h
void rakuppRegisterModuleSource(const std::string& name,
                                const char* src, size_t len);
const std::string* rakuppEmbeddedModuleSource(const std::string& name);
```

`--exe` and `--aot` parse at build time and never need it.

## A fifth target

The same runtime compiled to **WebAssembly** is Raku.js: the interpreter running
in a browser tab, with the same `Value` semantics and no server. It is mode 1
with a different host — but the host removes the filesystem, the threads and the
dynamic loader, which changes enough to be worth its own chapter. That is
Chapter 31, at the end of this part.
