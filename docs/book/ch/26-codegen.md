# The Code Generator

Compiling a dynamic language to C++ invites one particular mistake: writing a
second implementation of the language inside the code generator, which then has
to be kept in agreement with the first one for ever. Raku++ declines that
trade, and declining it is why the generator is 10,620 lines against an
interpreter of 77,884 — `Codegen.cpp` against the seven `Interpreter*.cpp`
files.

`--exe` transpiles the AST into a self-contained C++ program. The interface is
one function:

```cpp
// src/Codegen.h
std::string transpileToCpp(Program& prog, bool optimize = false,
                           const std::string& srcPath = "",
                           const std::set<std::string>& moduleExports = {},
                           const std::string& srcText = "");
struct CodegenError {
    std::string msg; bool routineWide = false; int line = 0;
};
```

The design principle is stated in one sentence and everything follows from it:
**the generator never reimplements the runtime.** It emits C++ that calls the
same functions the interpreter calls. That is why the two produce byte-identical
output, and why a feature implemented once works in both.

## The mapping

| Raku | Generated C++ |
|---|---|
| `$a + $b`, `$a eqv $b` | `applyArithValue("op", a, b)`, or an inline `rt*` helper |
| `say`, any named builtin | `rtCallB(RT, __bfpN, "say", …)` |
| `.map`, `.sort`, any method | `RT.methodCall(inv, "map", …)` |
| a block, `-> $x {…}`, `* + 1` | `Value::closure([=](ValueList& a){ … })` |
| `@a[i]` / `%h{k}` read, write | `rtIndexGet(…)` / `rtIndexRef(…)` |
| `[+] …` | `rtReduce(RT, "+", …)` |
| `class` | register a `ClassInfo` at startup; methods become closures |
| `$!x` | `rtAttrGet` / `rtAttrRef` |
| `multi` | one C++ function per candidate, plus a `rtTypeMatch` dispatcher |
| `grammar` | a `ClassInfo` marked as a grammar, its rules installed at startup |
| `enum` | global `Value::enumVal` constants |
| `gather`/`take` | `RT.rtGather(closure)` |
| phasers, `CATCH` | reordered emission, a C++ `try` and a `when` chain |
| `use Foo` | `RT.rtUse("Foo", "")` — the interpreter loads the module |

`applyArithValue` is `applyArith` with one question asked first. A `*` that is
written in an expression curries it; a `Whatever` that merely arrives as a
value, out of a variable or a return, is an ordinary object and must not. The
generator knows which sites have a literal star, and emits this spelling at
the ones that do not.

The `rt*` family in `Interpreter.h` is the generator's vocabulary — 275 names
at this writing, where the first version of this chapter counted about sixty,
each the runtime's implementation of one construct:

```cpp
// src/Interpreter.h — a sample
Value  rtIndexGet(const Value& base, const Value& key, bool isHash);
Value& rtIndexRef(Value& base, const Value& key, bool isHash,
                  const char* stepOp = nullptr);
Value  rtArrayVal(const Value& v);       // list-assignment semantics
Value  rtSlipVal(const Value& v);        // |x as a list element
Value  rtHashLit(const ValueList& items);
Value  rtNamedPair(const std::string& k, Value v);
Value  rtReduce(Interpreter& I, const std::string& op, const Value& list,
                bool operands = false);
Value  rtTypedDefault(const char* type, char sigil);
ValueList rtMainArgs(const std::vector<std::string>& argv,
                     bool namedAnywhere = false /* … */);
```

Several of them exist *only* because the interpreter had the same logic inline
and the two would otherwise have drifted. `rtArrayVal` is the compiled twin of
`coerceArray`, with the same fresh-buffer rule (Chapter 12), and it was factored
out precisely so there is one definition of what `@a = expr` means.

## What is emitted

A generated file has five sections.

**A prologue** including `Interpreter.h` and defining `RT` — the runtime object
that owns the builtin table, the class registry and the method dispatcher. It
is a function-local static behind a macro, so the `Interpreter` is constructed
on first use, on the big-stack thread the program runs on, and not on the OS
main thread before `main`:

```cpp
static Interpreter& __rakupp_RT() { static Interpreter rt; return rt; }
#define RT __rakupp_RT()
```

**Static globals** for top-level variables. `my $total` becomes
`static Value v_stotal = Value::any();`, with the sigil encoded into the name
(`$` becomes `s`) so that `$x` and `@x` do not collide.

**Functions**, one per user sub. Their parameters arrive as a `ValueList`, and
positionals are pulled out by index:

```cpp
static Value u_square(ValueList __a) {
    /* … the int64 twin, when the sub has one (Chapter 27) … */
    try {
        Value v_sn = rtBoxedItem(rtPos(__a, 0));
        return applyArithValue("*", v_sn, v_sn);
    } catch (ReturnEx& __r) { if (__r.target) throw; return __r.v; }
}
```

**A registration function**, `__rakupp_register`, run before `main`'s body. It
resolves cached builtin pointers, registers `ClassInfo`s for the program's
classes, installs named regexes, and registers embedded modules.

**The main body**, `__rakupp_main_body`, which sets up `@*ARGS`, runs the
registration, and executes the mainline inside a `try` with the phaser and exit
handling around it. `main` itself only hands that function to
`rakuppMainOnBigStack`.

A sixth section appears when the program uses modules: the embedded module
table, and the native bodies of the modules' routines (Chapter 25).

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

A `next` or `last` written lexically inside the loop it leaves does not throw
at all: it is emitted as `continue` or `break`. The `catch` clauses are for the
ones that arrive from somewhere else — a callee, a closure, a labelled loop
further out — and they catch the *exception* forms rather than reading the
cooperative registers of Chapter 15. Compiled code can afford to: the
cooperative path exists to avoid a throw in the tree-walker's hot loop, and a
compiled loop's body is already native. A loop whose body can throw neither
gets no `try` at all.

`if`, `while`, `until` and the ternary become their C++ equivalents. Conditions
that are comparisons use the `bool`-returning helpers (`rtLtB`, `rtEqSB`)
rather than building a `Bool` `Value` and reading it back — a default, not an
`-O` feature.

## Closures

A block becomes a C++ lambda wrapped as a `Value`:

```cpp
// src/Value.h
static Value closure(std::function<Value(ValueList&)> fn) {
    Value v; v.t = VT::Code;
    auto c = makePayload<Callable>();
    c->ciM().builtin = [fn](Interpreter&, ValueList& a) -> Value {
        return fn(a);
    };
    v.setCode(std::move(c));
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
the code generator emits a shared cell instead: the local is declared as a
`std::make_shared<Value>`, the closure captures the pointer, and both sides
keep using the plain name through a reference.

## Classes and multis

A `class` becomes registration code: a `ClassInfo` built at startup, its
attributes filled in with precomputed defaults where possible
(`ClassAttr::defVal`), and its methods installed as closures. There is no C++
class, no vtable, and no inheritance in the generated code — the runtime's
object model does all of it, exactly as it does when interpreting.

A `grammar` is the same registration with `isGrammar` set. The regex engine
and the interpreter hooks it needs are in the runtime already, so a grammar no
longer sends a program to the fallback.

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
Value  rtSlurpyPos(const ValueList& a, size_t from, bool boxed = false);
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
- **`gather`/`take`** — `RT.rtGather`, a collector pushed on the execution
  context;
- **`use`** — `RT.rtUse`;
- **the `...` sequence operator** — `RT.seqOp` and `RT.seqOpGroups`;
- **hyper operators** — `rtHyperMethod` and the hyper core;
- **indirect method calls** — `rtIndirectMethod`: the generated site
  evaluates the name expression, and the runtime either calls the `Code` it
  produced (with the invocant first) or dispatches the name it stringifies
  to, through the one method dispatcher;
- **`nqp::` ops** — `rtNqpOp`, the same function the interpreter calls
  (Chapter 35).

Each of those is a place where "call the runtime" is not a compromise but the
correct answer: there is one implementation, so there is one behaviour.

## What it refuses

`CodegenError` is thrown for anything the generator cannot express, and the
driver answers by bundling the program. The main cases:

- **symbolic references** (`::($name)`) and `callframe`, which ask for names
  and frames that native locals do not have;
- `state` variables, and a `where` or `:D` constraint on a routine parameter;
- **roles and packages**, and a `CATCH` inside a routine or a `try` block;
- `no strict` in a unit whose declaration check could not finish (below);
- a few **NativeCall shapes** — `is native(&lib-sub)`, a library name computed by
  an expression, `is rw` out-parameters, and buffer or `CArray` parameters that
  need copy-back;
- anything involving a construct the generator has simply not learned.

Grammars were the first item of this list and are no longer on it.

For the *program*, the failure is still total rather than per-construct: one
refused construct and the file is bundled, mainline and all. What changed is
what the bundle holds. The paragraph that stood here called a hybrid binary,
some routines interpreted and others compiled, "a much larger design", because
the two halves would have to share an environment. That design was built for
module routines in October 2026, and its answer to the shared environment is
that the interpreter keeps the call: it binds the signature into a frame, and
the native body reads its parameters and every outer name out of that frame.

```cpp
// src/Codegen.h
std::string transpileModuleRoutine(SubDecl* d, const std::string& fnName,
                                   const AotNames& names,
                                   std::vector<int>* delegatedLines = nullptr,
                                   bool optimize = false);
```

It emits one function of the shape
`static bool fn(Interpreter&, Env* frame, Value& out)`. In this mode a refused
statement does not refuse the routine: the statement is serialized, read back
once at run time and executed by the interpreter in the same frame, with the
native locals it names copied in and back out. A `CodegenError` with
`routineWide` set is the exception — a write to a read-only parameter, whose
meaning depends on how the routine was bound — and leaves the whole routine
interpreted. A bundled fallback applies the same function to the program's own
routines, so a refusal costs the mainline and the routine it sits in, not
every sub in the file (Chapter 25).

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

(The generated site calls `callEnvFirstAmp`, the same lookup given the
`&name` key built once per site.) Everything else keeps the cached builtin
pointer, so the cost lands only on
names a module actually claims. The carve-out is the interpreter's own: a
**non**-exported module sub of a built-in's name stays module-private, so the
importer still reaches the built-in while the module's own code sees its own.

That fix is a good example of the general hazard in compiling a dynamic
language: **any decision made at compile time is a promise about the run-time
environment,** and every such promise needs a reason to be true.

### The other promise: every variable has a declaration

A variable compiles to a C++ local, which is only possible because Raku declares
its variables — and `no strict` is precisely the pragma that says it need not.
The generator emitted a reference to a local for the name anyway, so *any*
program using the pragma failed in the C++ compiler:

```
nostrict.rakupp.gen.cpp:35:9: error: use of undeclared identifier 'v_szz'
```

Not a `CodegenError`, so not a fallback either — `--exe` simply could not build
these programs. The reason to believe the promise now comes from the pass that
already answers this exact question, `DeclCheck` (Chapter 39): `findLaxVars`
returns the names a lexically lax region auto-vivifies, and those compile to
`RT.laxVarRef("$x")` — a slot in the live environment — instead of a local.

What makes the set safe to act on is the pass's *source-text backstop*: a name
survives into it only if the unit spells a declaration for it nowhere, and a
name the generator never declares cannot collide with one it does. When the
check could not finish (`EVAL` and friends stand it down) the set may be short,
so the generator throws `CodegenError` and the program is bundled — the same
answer as any other construct it cannot promise anything about.

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
