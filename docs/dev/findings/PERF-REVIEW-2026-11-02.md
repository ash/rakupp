# Why Raku++ is not Fortran: the run-time work a compiler would have finished

*2026-11-02, an independent review at 5.5.0-346-gfe9237a8 (a clean tree), on
an Apple M4, macOS 26.6, Release build with `-g`. C is Apple clang 21 `-O2
-ffp-contract=off`; gfortran was not installed, and
[FORTRAN-GAP-2026-10-24.md](FORTRAN-GAP-2026-10-24.md) measured gfortran and
clang within 6–17% of each other on kernels of this kind. Times are the best
of 3 wall-clock runs, less process start-up (about 9 ms for rakupp, 6.5 ms
for C), divided by the kernel's operation count. Other sessions were running
(load average 3–13), so read the ratios, not the last digit. The kernels and
the instruments are in [perf-review-2026-11/](perf-review-2026-11/).*

The question put: Raku++ has spent months on native speed, and it is still
not near Fortran. Leaving aside the big `Value` and copy semantics, what work
does the engine do while a program runs that it could stop doing **without
dropping any of Raku's rules?**

---

## The short answer

- **Where a program is shaped like Fortran, written with native types and
  compiled with `--exe -O`, Raku++ is already in Fortran's neighbourhood.**
  A dot product runs at 2.1× C, a matmul at 8×, a stencil at 9× (C
  vectorizes it, Raku++ does not), integer recursion at 2.3×, scalar loops at
  1.0–1.05×. The box is gone there.
- **Everywhere else the gap is 25× to 900×.** It is not the arithmetic, and
  mostly not the 80-byte `Value` as such. It is five kinds of run-time work
  that a Fortran compiler finishes before the program starts:
  1. **Names resolved at run time.** A method by name through about 1,800
     string comparisons, an attribute through a hash map, a callee through a
     hash walk of the scope chain on every call.
  2. **The call and block protocol, paid in full.** About twenty pieces of
     bookkeeping on every plain call, for features the routine does not use.
  3. **Declared types checked, not used.** `int` and `num` make code slower
     in every mode, by up to 49×.
  4. **A native interior with a boxed perimeter.** The compiler emits a plain
     C++ function for a native routine, then boxes its arguments at every
     call site anyway.
  5. **Cliffs between seven evaluators.** Each fast path has its own
     whitelist. One construct outside it sends the whole loop or routine to
     the slowest evaluator.
- **None of the five is required by the language.** Each is the engine
  paying for a possibility the program does not use, and in every case
  whether the program uses it is visible in the source. Rakudo agrees on the
  sharpest case: it lowers lexicals to locals and answers
  `callframe(1).my<$x>` with `LoweredAwayLexical` (§2.2).
- **Why the effort has not converged.** The speed work so far added fast
  paths on top of a generic engine, and each path re-derives its own facts
  under its own whitelist. Fortran's speed is not a fast path. It is the
  absence of a generic path. To get there without dropping a rule, invert the
  default: the statically known case is the representation, and the dynamic
  case is built on demand when the program actually asks for it.

---

## 1. The measurements

### 1.1 Fortran-shaped kernels

A kernel here is a small benchmark program built around one inner loop. Each
is written twice: untyped, as most Raku is written, and in the Fortran form,
with `int`/`num` scalars, `num @` arrays and C-style `loop`. The six, and
what the table counts as one operation of each:

- **intloop**: `s = (s + i*i) % 1000003`, 20M times; one iteration.
- **numloop**: the Leibniz series for π, 20M terms; one iteration.
- **dot**: the dot product of two 1,000-element float arrays, repeated
  5,000 times; one multiply-add.
- **matmul**: a 120×120 float matrix multiply over flat arrays; one
  multiply-add (120³).
- **stencil**: 1-D Jacobi relaxation, each point set to the mean of its two
  neighbours, 2,000 points, 400 rounds of two sweeps; one point update.
- **fib**: recursive `fib(27)`; one call.

Each column is one way of running the kernel. C is the reference in
[ref.c](perf-review-2026-11/ref.c). `--exe -O` compiles the program to a
binary, "interp" runs it in the ordinary interpreter, and `--cnp` runs it
with the copy-and-patch loop tier. "IntKernel off" is the untyped program
interpreted with `RAKUPP_NO_KERNELS=1`, which switches off the interpreter's
`int64`/`double` fast paths in [IntKernel.cpp](../../../src/IntKernel.cpp)
(no relation to the benchmark kernels) and leaves the plain tree-walker. A
cell is nanoseconds per operation; the figure in brackets is that cell over
the C column, so (53×) means 53 times C's time. An empty cell was not
measured; `--cnp` compiles loops only, so it has nothing to take in fib.

| kernel (ops) | C | `--exe -O` untyped | `--exe -O` native | interp untyped | interp native | interp untyped, IntKernel off | `--cnp` untyped | `--cnp` native |
|---|--:|--:|--:|--:|--:|--:|--:|--:|
| intloop (20M iterations) | 3.0 | 3.2 (1.05×) | 3.2 (1.05×) | 5.8 (1.9×) | 5.8 (1.9×) | 76 (25×) | 6.0 | 5.8 |
| numloop (20M) | 0.80 | 0.80 (1.0×) | **39 (49×)** | 7.1 (9×) | 8.7 (11×) | 93 (116×) | 7.1 | 8.6 |
| dot (5M multiply-adds) | 0.49 | 26 (53×) | 1.05 (2.1×) | 98 (200×) | 171 (350×) | 97 | 39 (80×) | 5.6 (11×) |
| matmul (1.73M) | 0.49 | 35 (71×) | 3.9 (8×) | 208 (425×) | 372 (760×) | | 45 (92×) | 11 (22×) |
| stencil (1.6M updates) | 0.11 | 39 (350×) | 1.0 (9×) | 261 (2,400×) | 485 (4,400×) | 261 | 58 | 94 |
| fib (calls) | 0.56 | 1.3 (2.3×) | 1.3 (2.3×) | 4.0 (7×) | 4.0 | 122 (218×) | | |

The "IntKernel off" columns show the tree-walker underneath: the interpreter
is near C on intloop, numloop and fib only because IntKernel recognises
those shapes and runs them on `int64`/`double` instead of `Value`s. The
native-compiled dot, matmul and stencil figures come from 100× longer runs,
since at the scale shown they finish inside the start-up time.

### 1.2 Calls and objects: Raku as it is usually written

The cost of one iteration of `for ^3_000_000 { $s += EXPR }`, in ns:

| EXPR | `--exe -O` | interp | interp, IntKernel off |
|---|--:|--:|--:|
| `3` (the loop alone) | 0.3 | 0.7 | 35 |
| `sum($_, 2)` with `sub sum($a, $b) { $a + $b }` | 9 | 3.7 | 162 |
| `sum(1e0, 2e0)` with `sub sum(num $a, num $b --> num)` | **25** | **193** | 297 |
| `$p.x`, a public attribute | **127** | 72 | 71 |
| `$p.sum`, `method sum { $!x + $!y }` | 78 | 254 | 254 |

An n-body step written with a class (five bodies, ten pair updates and five
`.advance` calls; [nbody.raku](perf-review-2026-11/nbody.raku)) costs
**35 ns in C, 16,500 ns under `--exe -O` (470×) and 31,000 ns interpreted
(890×)**. `--cnp` is 1.00× the interpreter on it.

### 1.3 What the tables say before any profile

- **Native declarations make code slower in every mode.** numloop is 49× C
  in its native form and 1.0× untyped under `--exe -O`. dot, matmul and
  stencil are 1.7–1.9× slower interpreted when native. A `num` sub is 2.8×
  slower than an untyped one compiled and 52× slower interpreted.
- **The compiled accessor is slower than the interpreted one:** 127 ns
  against 72.
- **The engines are islands.** The same loop costs 5.8 ns when IntKernel
  takes it and 76 ns when it does not. IntKernel declines dot's loop with
  "container @a holds neither a plain Int nor a plain Str", so a loop over
  `Num` elements gets nothing from a fast path that handles `Num` scalars.

---

## 2. Where the time goes: five kinds of work

### 2.1 Names resolved at run time

**Methods.** Compiled `$p.x` is `RT.methodCall(v_sp, "x", ValueList{})`.
That builds a `std::string` and an empty `ValueList`, and enters
`methodCall` → `methodCallInner` → `methodCallPart1b` → `Part1c` →
`Part2`, where the accessor's arm finally answers. The chain tests the name
against about 1,800 string literals across
[Builtins.cpp](../../../src/Builtins.cpp) and
[MethodCallPart1b/1c/2/3/Tail.cpp](../../../src/MethodCallPart2.cpp), in
priority order.

- In the compiled n-body profile, the chain and its `memcmp`/`strlen` are
  **57% of all samples**. `___chkstk_darwin` adds 0.8–2.4% more: the
  dispatcher's stack frame is large enough to need a stack probe on every
  call.
- In compiled `$p.x`, the chain is about 60%.
- The interpreter avoids part of this at its own call site
  (`InterpreterCore.cpp:31929` → `plainAccessorSlot`). Compiled code takes
  the general path, which is why it is slower than the interpreter.
- Even `plainAccessorSlot` ([Builtins.cpp:6156](../../../src/Builtins.cpp))
  makes three lookups by name per call: `findAttr(m)`,
  `findMethodForCall(m)` and `attrs.find(slot)`.

**Attributes.** An object is `ObjectData { shared_ptr<ClassInfo> cls;
ValueMap attrs; … }` ([Value.h:2341](../../../src/Value.h)): a hash map
keyed by attribute name. Compiled `$!x` is `rtAttrRef(__self, "x")`
([InterpreterBinding.cpp:4864](../../../src/InterpreterBinding.cpp)), which
builds a `std::string`, hashes it and probes the map on every access. In
Fortran, `b%x` is one load at a fixed offset.

**Callees.** Every call to a program's own sub runs
`tctx().cur->find(callAmpName(c))`
([InterpreterCore.cpp:26620](../../../src/InterpreterCore.cpp)), and
`Env::find` ([Interpreter.h:1068](../../../src/Interpreter.h)) walks the
scope chain, probing a string-keyed map and then the pad's names at each
level. There is no call-site cache on this path. In fib with IntKernel off it
is 9% of the time.

**Also by name:**
- `env->define("$_", …)` on every call to a body that may use the topic;
- a type object carries its class as a *name* (`classes_.find(inv.s)` in
  `methodCall`);
- compiled native stores pass the variable's name and type as strings
  (`rtNativeStoreK(v, 2, "int", "$k", …)`);
- compiled code reads a native global as `__nGI(v_sn, "$n")`.

**What Raku requires here.**
- Lexical names are static: Rakudo resolves them at compile time, and
  "Undeclared routine" is a compile-time error. The dynamic forms
  (`::("&name")`, `EVAL`, the pseudo-packages) are explicit in the source.
- Methods are late-bound, but late binding is not lookup by name on every
  call. It means a lookup whose result can change when the class changes:
  `augment`, `.^add_method`, `but`, a role mixin. A per-call-site cache keyed
  on (class, class epoch) honours all of these. `g_symbolGen` is already the
  epoch `plainAccessorSlot` keys on.
- Attributes are fixed once a class is composed. Rakudo's P6opaque gives
  each one a slot index. A mixin makes a new class, with its own layout.

### 2.2 The call and block protocol, paid in full

`callPlainSub` ([InterpreterCore.cpp:8050](../../../src/InterpreterCore.cpp))
is the fast lane for a plain sub. Per call it does:

- a `redispatchFloor` save, set and restore;
- a `DepthGuard`;
- a pooled `Env`, released with cycle-collector notes;
- a `shared_ptr` parent, a layout and a `pad.resize`;
- per parameter, `wrapNative`/`dropNativeTags`, `readonly` and `itemizeScalar`;
- an atomic `padLive.fetch_or`;
- `define("$_")` by name when the body may use the topic;
- a dynamic-epoch check;
- a `cur`/`curStateEnv` swap;
- a `dynStack` push, which bumps a `thread_local` serial;
- a `callFrames` push of five fields;
- save and restore of `curLine_`, `curBlockVal`/`curRoutineVal`,
  `frameTop`, `curRoutineFrame`, `curRoutineEnv` and `curStmtExpr`;
- `declStmts`, `callEnv`, `fatalPragma` and the `t_fatalTry` thread-local;
- `captureBodyEnds`;
- a `runAotBody` probe;
- a `returning` poll after every statement;
- `drainTempRestores`;
- a Failure test on the result that may run `let` restores;
- `leanReturn`.

Its callers add `callSpecialName`, the by-name `find`,
`declaredTypeOutranksRoutine`, a `ValueList` for the arguments, `plainSubShape`
and a `tryIntKernel` probe, which is still entered under
`RAKUPP_NO_KERNELS=1`.

The fib profile with IntKernel off, by bucket:

| | share |
|---|--:|
| the call protocol above, with its thread-local reads | ~45% |
| resolving the callee by name | ~9% |
| building and freeing the argument list | ~7% |
| the five operations the program asked for (`<`, `?? !!`, `-`, `-`, `+`) | ~35% |

So about 60% of a call goes on things fib does not ask for. The loop body
has the same shape:
- `execPlainBlock` ([InterpreterCore.cpp:1082](../../../src/InterpreterCore.cpp))
  takes the scope `shared_ptr` by value, swaps it in and out, constructs a
  `Value last`, tests the result for `Failure` by comparing an interned name,
  and drains temp restores.
- `runLoopBody` ([InterpreterCore.cpp:1514](../../../src/InterpreterCore.cpp))
  runs `safePoint()`, `gatherProbePoint()`, `loopPhaserMask`, a
  `firstBody` swap and a `curLoopFrame`/`curGivenFrame` swap on every
  iteration.

Every `eval` starts with an atomic load of `pendingSubscriptsAny_`
([InterpreterCore.cpp:30237](../../../src/InterpreterCore.cpp)), a guard for
one bug, a subscript evaluated twice, paid by every node. An expression
statement writes `curLine_` twice.

**What Raku requires here.** Very little, and Rakudo shows it:

```
$ rakudo -e 'sub f { say CALLER::<$x> }; my $x = 5; f()'
Cannot access '$x' through CALLER, because it is not declared as dynamic
$ rakudo -e 'sub f { say callframe(1).my<$x> }; my $x = 5; f()'
(LoweredAwayLexical)
```

A callee can reach its caller's *dynamic* variables only: `$*x`, `is
dynamic`, and `$_`, `$/` and `$!`. Each of those is visible in the caller's
source. Rakudo lowers every other lexical to a local, gone from
introspection. Raku++ keeps every lexical reachable (`callframe(1).my<$x>`
answers 5), which costs more than the language asks.

Everything else in the list belongs to one of two groups:
- **Features the routine's own source either uses or does not:** phasers,
  `temp`/`let`, the topic, `&?ROUTINE`, `samewith`/`nextsame`, a `return`
  from a nested block, `use fatal`, `state`, and closures that capture
  locals.
- **Features a callee can observe:** backtraces, `$*` lookup and
  `callframe`. One stack record per call serves all three: a pointer to the
  routine, the line and the parent record. Rakudo's frames are that.

### 2.3 Declared types are checked, not used

A native declaration should hand the engine a fact. Today each mode turns it
into checks.

- **Interpreted.** Each store into a native goes through
  `enforceTypedAssign`, `varWhereCheck`, `wrapNative` and
  `compoundSrcNative`, and each read through the slot and `Env::find`.
  - In the interpreted native dot product those are about 8% of samples.
  - IntKernel declines a sub with `num` parameters, so that sum is 193 ns
    against 3.7 ns untyped.
- **Compiled, a lane bug.** In
  [Codegen.cpp:8537](../../../src/Codegen.cpp), a declared-`num` slot
  starts the lane as F64 only if the loop does *not* write it. A written
  `my num $g` starts as I64 and widens only "the moment anything float
  reaches it". `$g = -$g` brings no float, so the lane guards
  `rtIntSlot(v_sg)`. The guard fails on every entry, and the loop runs boxed
  through `applyArith("+", …)`, string-dispatched arithmetic. That is
  numloop's 49× C, against 1.0× for the same loop untyped. It reproduces in
  one line:

  ```raku
  my num $g = 1e0; my num $s = 0e0;
  loop (my int $k = 0; $k < 10; $k++) { $s += $g; $g = -$g }   # rtIntSlot
  # with `$g = 0e0 - $g`, or with `my $g`, the guard is rtNumSlot
  ```

- **Compiled, native `int` loop variables.** These carry `add_ovf` and bail
  on overflow. Native `int` wraps, in Rakudo and in Raku++'s own boxed path,
  so the check buys nothing, and the side exit blocks vectorization.

**What Raku requires here.** A native's semantics are machine semantics.
A `num` is a double, and dividing it by zero throws: Rakudo dies "Attempt to
divide 1 by zero using /" on `my num` operands. That is one compare. An
`int` wraps.

### 2.4 A native interior with a boxed perimeter

Under `--exe -O`, `sub sum(num $a, num $b --> num)` becomes

```cpp
static double n_sum(double nv_sa, double nv_sb) { return nv_sa + nv_sb; }
```

which is exactly C. But its call site, where `$s` is `my num` and both
arguments are `num` literals, is

```cpp
cycleMaybeCollect();
try {
    v_ss = rtNativeStoreLike(v_ss, applyArith("+", v_ss, u_sum(Value::number(1), Value::number(2))), "$s", false);
} catch (const NextEx& __e) { … } catch (const LastEx& __e) { … } catch (const RedoEx& __e) { … }
```

and `u_sum` checks both `Value`s with `rtTypedFast`, itemizes them, unboxes
them, calls `n_sum`, boxes the result and wraps it all in a `try` for
`ReturnEx`. The native code exists. Every boundary converts into it and out
of it.

The same perimeter shows elsewhere:
- **Untyped arrays.** `@a[$i] * @b[$i]` compiles to two `rtIndexGet` calls
  returning `Value`s by value, then `rtMul`, `rtAdd` and a `Value`
  assignment. Five or six 80-byte constructions and destructions per
  element: 26 ns, against 1.05 ns once the arrays are `num @`. Nothing in
  the program says the elements are `Num`. The engine never learns it either.
- **Native loops.** The native stencil re-reads the loop bound through
  `__nGI(v_sn, "$n")` on every iteration, since the compiler cannot see that
  nothing writes the global. Every element access is bounds-checked, with a
  side exit to the boxed path, so clang cannot vectorize the loop.
  - That is the 8–9× left on matmul and stencil.
  - A Fortran compiler hoists the bound and the checks out of the loop.
- **Every iteration of a compiled loop calls `cycleMaybeCollect()`.**
  [Codegen.cpp:5070](../../../src/Codegen.cpp) emits it.

### 2.5 Cliffs between seven evaluators

The same semantics are implemented by:

1. the tree-walker (`eval`/`exec` on `Value`);
2. the interpreter's typed operand evaluator (`fusedTypedAssign`/
   `typedOperand` on `TNum`);
3. IntKernel's sub and loop kernels (`int64`, `double`, `std::string`);
4. `--cnp`'s copy-and-patch loop tier;
5. `--jit`'s run-time C++;
6. `--exe`'s boxed C++ (`rt*` on `Value`);
7. `--exe -O`'s unboxed lanes and `n_` routines.

`--target=js` makes an eighth.

Each decides for itself what it understands, so each has its own refusals
and its own bugs, and every refusal falls all the way back:
- dot's loop is refused by IntKernel, which takes `Num` scalars but not
  `Num` elements, so it runs on the tree-walker at 98 ns an element;
- the `num` lane bug sends a whole loop to string-dispatched arithmetic;
- `--cnp` compiles loops, so a program whose time is in method calls inside
  the loop gets 1.00×.

A user cannot predict which side of a cliff a loop lands on. Each new fast
path also adds a whitelist that has to track the other six.

---

## 3. What was checked and is not the problem

- **Free-threading.** With `RAKUPP_GIL=1` against the default,
  interleaved, best user time of 5: fib 0.86 against 0.86 s, intloop 1.50
  against 1.52 s.
  - The `SlotStripe`/`liveWorkers_` tests on each variable access cost
    nothing measurable single-threaded.
  - The atomic reference counts in [Ref.h](../../../src/Ref.h) were not
    measured. That needs a rebuild, and a scalar `Value` has no payload to
    count.
- **Opcode dispatch.** [IR-EXPERIMENT.md](../experiments/IR-EXPERIMENT.md)
  measured it at 0.3 ns a node. Nothing here contradicts that.
- **Arithmetic, libm and bigints.** FORTRAN-GAP found libm at 0.3% and
  bigints at 0%. The scalar loops here run at C speed once compiled.

---

## 4. What Raku actually requires at run time

| rule | what it costs once types and names are known | where Raku++ pays more |
|---|---|---|
| `Int` overflows into a bigint | one overflow-flag branch per op | (fine; the lanes do this) |
| native `int` wraps | nothing | lanes check for overflow and bail (§2.3) |
| `num` division by zero throws | one compare | (fine) |
| `Failure` propagates | a tag test where a value can be a Failure | (mostly fine) |
| methods bind late | one guard per call site on (class, epoch) | a 1,800-name ladder per call (§2.1) |
| attributes are per class | a load at an index | a hash probe by name (§2.1) |
| lexicals are static | a frame slot, often a register | a scope-chain hash walk for callees (§2.1) |
| a callee sees its caller's dynamics | one frame record per call | a full `Env` with every lexical reachable (§2.2) |
| phasers, `temp`/`let`, topic, redispatch | nothing, unless the routine's source uses them | checked or set up on every call (§2.2) |
| an array grows when written past its end | one bounds check, hoistable out of a counted loop | a check per element, with a side exit (§2.4) |
| `:=`, `is rw` and `\` containers | a cell where the source binds one | (fine; cells are already opt-in) |

The column on the right is what separates Raku++ from Fortran on these
programs. The column in the middle is close to what a Fortran compiler
emits.

---

## 5. What to do, in order

**A. Local fixes (days), each with a measured target:**

1. **Seed declared-float slots as F64 whether or not the loop writes them**
   ([Codegen.cpp:8537](../../../src/Codegen.cpp)). numloop native should go
   from 49× to the untyped form's 1.0×, since its lane is the same loop
   (*estimate*).
2. **Native `int` slots in lanes wrap rather than bail.** Fewer side exits,
   and room for clang to vectorize.
3. **Call `n_*` directly from a call site whose arguments are already native
   or literal,** and keep the result native. The `num` sum should go from
   25 ns to about 1, the cost of the `n_sum` it already emits (*estimate*).
4. **Hoist native reads of globals out of a call-free native loop,** and
   version the loop on one bounds check at entry (FORTRAN-GAP item 5).
   Targets the 8–9× left on matmul and stencil.
5. **A compiled `$obj.name` with no arguments gets a call-site cache:** key
   (class, `g_symbolGen`), value "plain accessor, attribute name". A hit
   skips the ladder. Fixes the compiled accessor being slower than the
   interpreted one (127 against 72 ns). Item 8 then takes it to a load.

**B. Structural (weeks), each keeping every rule:**

6. **Resolve a call to a lexical sub when the program is parsed,** into the
   frame and pad slot of its `&name` container. Pointing at the container
   rather than the Routine keeps `:=` rebinding right, and `wrap` changes
   the Routine in place, so it stays right too. Where frame shapes are not
   static, a per-site cache keyed on the defining `Env` does the same.
   Removes fib's 9%.
7. **A per-routine "needs" mask, computed once.** It records:
   - phasers;
   - `temp`/`let`;
   - use of `$_`, `$/` or `$!`;
   - closures that capture locals;
   - `&?ROUTINE` and `callframe`;
   - redispatch;
   - a `return` from a nested block;
   - `use fatal`;
   - `state`;
   - dynamic variables it declares.

   `callPlainSub` runs only the pieces the mask names. A frame record of
   routine, line and parent serves backtraces and `$*` lookup. An `Env` is
   built only where a closure captures one or EVAL needs one. This is
   [IR-EXPERIMENT.md](../experiments/IR-EXPERIMENT.md)'s first
   recommendation, now with Rakudo's `LoweredAwayLexical` as the licence.
   Addresses up to fib's 45%.
8. **Inline caches for methods, and slots for attributes.**
   - A call site caches (class, epoch) → resolved `Callable` or attribute
     slot, consulted before the built-in ladder.
   - Built-in types get a perfect-hash table from method name to arm in
     place of 1,800 comparisons in sequence. The arms keep their priority
     order inside each type's table.
   - A composed class lays its attributes out as a vector, with the
     name → index map on `ClassInfo`. Rakudo's P6opaque is this design.
   - `augment`, `add_method`, `but` and mixins bump the epoch.

   Targets the 57% of the compiled n-body.

**C. Representation (months):**

9. **Element kinds for untyped arrays.** An Array whose elements are all
   `Num`, or all `Int`, is stored packed. The `PK::Packed` body already
   exists for native arrays. The first store that does not fit converts the
   array to the general form; so do a bind (`:=`), an element container
   handed out, and `holdsCells`. A lane tests the kind once at loop entry.
   Should take untyped dot from 26 ns toward the native 1.05, with no change
   to the program (*estimate*).
10. **One fact layer, shared by every evaluator.** DeclCheck already walks
    every lexical scope. Extend that walk to record, on the AST:
    - resolved lexical coordinates;
    - declared and inferred types;
    - the needs mask from item 7;
    - escape: whether a closure captures the variable, or `:=` or `\` binds
      it;
    - whether an expression can run user code.

    The seven evaluators then read one set of facts, where today each derives
    its own under its own whitelist. That removes the class of bug in §2.3,
    and the class of cliff in §2.5 where one evaluator's whitelist is
    narrower than another's.
    This is not the bytecode IR that IR-EXPERIMENT measured and rejected.
    It is the "unboxed typed registers" that file names as the one thing
    that would revive one, stored where every engine can read it.

**Not worth doing, by the measurements:** removing the free-threading checks
(§3), and flattening the tree for dispatch speed (IR-EXPERIMENT).

---

## 6. Two defects found on the way

1. **A `num` written in a loop compiles to an `int` lane** (§2.3,
   [Codegen.cpp:8537](../../../src/Codegen.cpp)). The program's answer is
   right, because the guard sends it to the boxed path. It costs 49× on
   numloop. A loop that negates a `num` in FORTRAN-GAP's native-typed
   program may be part of why that program is slower than its untyped twin
   (not checked).
2. **Compiled `$obj.attr` takes the general dispatch path and misses the
   interpreter's accessor fast path:** 127 ns compiled against 72 ns
   interpreted (§2.1).

---

## Appendix: re-measuring

Build a Release binary with `-g` in a second build directory, so `sample`
prints `File.cpp:LINE`:

```bash
cmake -S . -B build-g -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_FLAGS="-g"
cmake --build build-g --target rakupp -j10
```

From [perf-review-2026-11/](perf-review-2026-11/):

```bash
cc -O2 -ffp-contract=off ref.c -o ref        # C: ./ref intloop|numloop|dot|matmul|fib|stencil
../../../../build-g/rakupp --exe -O dot.raku -o dot.O
./tm.sh "dot exe-O" ./dot.O                  # best of 3, with the program's checksum
RAKUPP_NO_KERNELS=1 ./tm.sh "dot nok" ../../../../build-g/rakupp dot.raku
```

- **Per-op figures.** The 100× runs scale the outer repeat or the size with
  `sed`: dot's `5000` → `500000`, matmul's `120` → `600`, stencil's `400`
  → `40000`, and for C `fib(27)` → `fib(37)`.
- **Checksums.** Every form prints the C checksum, to the last digit, in
  every mode.
- **Profiles.** Run the program, `sleep 1`, then `sample <pid> 2 -mayDie -f
  out.sample`. `python3 -I selfline.py out.sample 40 func` (or `line`)
  prints self time per function or per source line, leaving out the
  waiting main thread.
- **Rakudo** is `/Users/ash/.rakubrew/versions/moar-2026.09/install/bin/rakudo`.
