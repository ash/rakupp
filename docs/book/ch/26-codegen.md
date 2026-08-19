# The Code Generator

`--exe` transpiles the AST into a self-contained C++ program. The interface is
one function:

```cpp
// src/Codegen.h
std::string transpileToCpp(Program& prog, bool optimize = false,
                           const std::string& srcPath = "",
                           const std::set<std::string>& moduleExports = {});
struct CodegenError { std::string msg; };
```

The design principle is stated in one sentence and everything follows from it:
**the generator never reimplements the runtime.** It emits C++ that calls the
same functions the interpreter calls. That is why the two produce byte-identical
output, and why a feature implemented once works in both.

## The mapping

| Raku | Generated C++ |
|---|---|
| `$a + $b`, `$a eqv $b` | `applyArith("op", a, b)`, or an inline `rt*` helper |
| `say`, any named builtin | `rtCallB(RT, __bfpN, "say", …)` |
| `.map`, `.sort`, any method | `RT.methodCall(inv, "map", …)` |
| a block, `-> $x {…}`, `* + 1` | `Value::closure([=](ValueList& a){ … })` |
| `@a[i]` / `%h{k}` read, write | `rtIndexGet(…)` / `rtIndexRef(…)` |
| `[+] …` | `rtReduce("+", …)` |
| `class` | register a `ClassInfo` at startup; methods become closures |
| `$!x` | `rtAttrGet` / `rtAttrRef` |
| `multi` | one C++ function per candidate, plus a `rtTypeMatch` dispatcher |
| `enum` | global `Value::enumVal` constants |
| `gather`/`take` | push a collector onto the execution context |
| phasers, `CATCH` | reordered emission, a C++ `try` and a `when` chain |
| `use Foo` | `RT.rtUse("Foo")` — the module is still interpreted |

The `rt*` family in `Interpreter.h` is the generator's vocabulary — about sixty
functions, each the runtime's implementation of one construct:

```cpp
// src/Interpreter.h — a sample
Value  rtIndexGet(const Value& base, const Value& key, bool isHash);
Value& rtIndexRef(Value& base, const Value& key, bool isHash);
Value  rtArrayVal(const Value& v);       // list-assignment semantics
Value  rtSlipVal(const Value& v);        // |x as a list element
Value  rtHashLit(const ValueList& items);
Value  rtNamedPair(const std::string& k, Value v);
Value  rtReduce(const std::string& op, const Value& list);
Value  rtTypedDefault(const char* type, char sigil);
ValueList rtMainArgs(const std::vector<std::string>& argv);
```

Several of them exist *only* because the interpreter had the same logic inline
and the two would otherwise have drifted. `rtArrayVal` is the compiled twin of
`coerceArray`, with the same fresh-buffer rule (Chapter 12), and it was factored
out precisely so there is one definition of what `@a = expr` means.

## What is emitted

A generated file has five sections.

**A prologue** including `Interpreter.h` and defining one static `Interpreter
RT` — the runtime object that owns the builtin table, the class registry and the
method dispatcher.

**Static globals** for top-level variables. `my $total` becomes
`static Value v_stotal;`, with the sigil encoded into the name (`$` becomes `s`)
so that `$x` and `@x` do not collide.

**Functions**, one per user sub. Their parameters arrive as a `ValueList`, and
positionals are pulled out by index:

```cpp
static Value u_square(ValueList __a) {
    Value v_sn = rtPos(__a, 0);
    return applyArith("*", v_sn, v_sn);
}
```

**A registration function**, `__rakupp_register`, run before `main`'s body. It
resolves cached builtin pointers, registers `ClassInfo`s for the program's
classes, installs named regexes, and registers embedded modules.

**`main`**, which sets up `@*ARGS`, runs the registration, and executes the
mainline inside a `try` with the phaser and exit handling around it.

## Control flow

Raku control flow becomes C++ control flow wherever it can.

A `for` over a literal range becomes a native counting loop with the topic
rebuilt per iteration, wrapped in a `try` that catches the control exceptions:

```cpp
for (long long __i = __lo; __i <= __hi; __i++) {
    Value v__t0 = Value::integer(__i);
    try { /* body */ }
    catch (const NextEx&) { continue; }
    catch (const LastEx&) { break; }
}
```

Note that compiled code catches the *exception* forms of `next` and `last`
rather than reading the cooperative registers of Chapter 15. It can afford to:
the cooperative path exists to avoid a throw in the tree-walker's hot loop, and
a compiled loop's body is already native.

`if`, `while`, `until` and the ternary become their C++ equivalents. Conditions
that are comparisons use the `bool`-returning helpers (`rtLtB`, `rtEqSB`)
rather than building a `Bool` `Value` and reading it back — a default, not an
`-O` feature.

## Closures

A block becomes a C++ lambda wrapped as a `Value`:

```cpp
// src/Value.h
static Value closure(std::function<Value(ValueList&)> fn) {
    Value v; v.t = VT::Code; v.code = std::make_shared<Callable>();
    v.code->builtin = [fn](Interpreter&, ValueList& a) { return fn(a); };
    return v;
}
```

This is where the dual nature of `Callable` pays off. A `Callable` is *either* an
AST body plus a closure environment, *or* a C++ function. The interpreter builds
the first kind; the code generator builds the second. Everything downstream —
`.map`, `.sort`, `callsame`, `&f` as a value, passing a block to a user routine
— treats them identically, because it only ever calls through `callCallable`.

The capture is by value (`[=]`), so a compiled closure captures copies of the
`Value`s it uses. That is the same observable behaviour as the interpreter's
environment capture for reads; for a closure that *mutates* a captured variable
the code generator has to emit a shared slot instead.

## Classes and multis

A `class` becomes registration code: a `ClassInfo` built at startup, its
attributes filled in with precomputed defaults where possible
(`ClassAttr::defVal`), and its methods installed as closures. There is no C++
class, no vtable, and no inheritance in the generated code — the runtime's
object model does all of it, exactly as it does when interpreting.

A `multi` becomes one C++ function per candidate plus a dispatcher that scores
with `rtTypeMatch`. That is a simplification of the interpreter's
`scoreCandidate`, which is the honest reason a few multi shapes are on the
fallback list.

## Signatures

The generator has to bind arguments without the interpreter's `bindParams`, so
there is a small family of binding helpers:

```cpp
// src/Interpreter.h
Value  rtPos(const ValueList& a, size_t idx);
bool   rtHasPos(const ValueList& a, size_t idx);
Value  rtNamed(const ValueList& a, const std::string& key);
bool   rtHasNamed(const ValueList& a, const std::string& key);
Value  rtSlurpyPos(const ValueList& a, size_t from);
Value  rtSlurpyNamed(const ValueList& a);
size_t rtPosCount(const ValueList& a, size_t from = 0);
Value& rtPosRef(ValueList& a, size_t i);   // `is rw`: a ref into the list
```

A signature is compiled into a sequence of these. Defaults become `if
(!rtHasPos(…)) …`; a slurpy becomes one call; `is rw` binds a reference into the
caller-visible argument list.

## Sub-language pieces the generator hands back to the runtime

Some constructs are emitted as a single runtime call rather than transpiled,
because transpiling them would mean duplicating an engine:

- **regexes and substitutions** — `RT.regexMatch`, `RT.substApply`;
- **`gather`/`take`** — a collector pushed on the execution context;
- **`use`** — `RT.rtUse`;
- **the `...` sequence operator** — `RT.seqOp` and `RT.seqOpGroups`;
- **hyper operators** — `rtHyperMethod` and the hyper core;
- **`nqp::` ops** — `rtNqpOp`, the same function the interpreter calls
  (Chapter 34).

Each of those is a place where "call the runtime" is not a compromise but the
correct answer: there is one implementation, so there is one behaviour.

## What it refuses

`CodegenError` is thrown for anything the generator cannot express, and the
driver answers by bundling the whole program. The main cases:

- **grammars**, which need the full engine plus the interpreter hooks;
- a few **NativeCall shapes** — `is native(&lib-sub)`, a library name computed by
  an expression, `is rw` out-parameters, and buffer or `CArray` parameters that
  need copy-back;
- anything involving a construct the generator has simply not learned.

The failure is total-per-program rather than per-construct: one refused
construct bundles the entire file. That is a deliberate simplification — a
hybrid binary that interpreted some routines and compiled others would need the
two halves to share an environment, which is a much larger design.

## The correctness constraint on resolving names at compile time

The generator resolves a named call at compile time, deciding whether it is a
user sub or a built-in. **A `use`d module can take that name away.**

```raku
# lib/OnlySub.rakumod
unit module OnlySub;
sub val() is export { 'from-module' }
```

The interpreter's `evalCall` looks in the environment *before* the builtin table
(Chapter 16), so the exported sub wins. Compiled code never did that lookup and
printed the built-in `val`'s answer instead — a silent divergence.

So the generator is given the module graph's exported names, and for those it
emits a call that looks in the environment first:

```cpp
// src/Interpreter.h
Value callEnvFirst(const std::string& name, ValueList args);
```

Everything else keeps the cached builtin pointer, so the cost lands only on
names a module actually claims. The carve-out is the interpreter's own: a
**non**-exported module sub of a built-in's name stays module-private, so the
importer still reaches the built-in while the module's own code sees its own.

That fix is a good example of the general hazard in compiling a dynamic
language: **any decision made at compile time is a promise about the run-time
environment,** and every such promise needs a reason to be true.

## Inspecting the output

```sh
rakupp --cpp prog.raku          # print the generated C++
rakupp --cpp -O prog.raku       # …with the optimizer on
rakupp --exe -o prog prog.raku  # compile it
```

`--cpp` is the debugging tool for everything in this chapter and the next. When
compiled and interpreted output differ, the first step is to read the emitted
C++ for the construct that differs; it is ordinary C++, and the divergence is
usually visible.
