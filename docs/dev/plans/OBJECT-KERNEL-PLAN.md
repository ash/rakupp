# Plan: objects in loop kernels

*Written 2026-11-18, with phase 1's code. Prompted by the question "can we make
Raku++ 10 times faster?" and, once the answer was "only where the time is in
objects and dispatch, and only with a compiled tier for methods", by "let's
start trying". The measurements below were taken before any engine code.*

**The number a stranger can re-measure:**

```bash
cmake --build build -j
RAKUPP_NO_KERNELS=1 build/rakupp tools/bench/objects-steady.raku   # as before
build/rakupp                     tools/bench/objects-steady.raku
RAKUPP_KERNEL_TRACE=1 build/rakupp tools/bench/objects-steady.raku # says the loop compiled
```

`objects-steady` — 1M iterations of two `Point.new`, a method that makes a
Point, a method that reads four attributes — takes **0.02 s where it took
0.81 s**, 547M instructions retired where it retired 19.30G (Apple M4, the
tree at 22100928 beside this one). Rakudo 2026.09 takes 0.91 s; Bun, whose
JIT sinks the same allocations, 0.022 s.

That is one kernel. What the phase does for a program is set by how much of
its object code has the shape below, which is what the later phases widen.

---

## Where the time was

`/usr/bin/time -l`, instructions retired per iteration, 1M iterations, the
loop's own cost included (an empty counted loop is 115):

| loop body | instructions |
|---|---:|
| `my $p = Point.new(x => $i % 100, y => $i % 37)` | 6,570 |
| two such `.new` | 12,610 |
| `$acc += $p.x` (accessor, `$p` made outside) | 1,630 |
| `$acc += $p.len2` (four `$.x` reads and a call) | 5,180 |
| `my $q = $p.shift(1)` (a call and a `.new`) | 10,150 |
| all of `objects-steady` | 19,300 |
| the same arithmetic written out on `my` Ints | 320 |

Two things follow.

1. **Construction is two thirds of it.** The method call and the attribute
   reads — what "a compiled tier for methods" would have gone after first —
   are a third. Compiling methods and leaving `.new` alone could not have
   reached 1.5× on this kernel.
2. **The interpreter has nothing left to give per instruction.** It retires
   6.1 instructions a cycle here. The only gain is in not doing the work, and
   the last row says how much of it is not needed: the loop's objects are born
   and die inside one iteration, and nothing outside it ever sees them.

`--cnp` did not help (it refuses the loop at the class name, as the loop
kernels did) and `--exe -O` runs this program in 2.0 s, slower than the
interpreter, since its object code goes through the generic method call the
interpreter no longer takes.

## The design: an object that is never made

A loop kernel (`src/IntKernel.cpp`) runs a loop on a frame of `int64_t` and
`std::string` slots, each with the type its value had when the kernel was
compiled. Phase 1 lets a slot group stand for an object:

- **`my $p = CLASS.new(name => EXPR, …)`** makes no object. Each EXPR is
  compiled into a slot of its own type, and `$p` names the group
  (`VObj`: the class and one `VAttr` an attribute).
- **`$p.name`** where `name` is a public `$` attribute's accessor is a read of
  that slot.
- **`$p.m(ARGS)`** where `m` is a method whose body is one expression is that
  expression compiled in place: the arguments into slots, `$.x` and `$!x` as
  the invocant's slots, `self.other(…)` and `$.other` the same way, and a
  `CLASS.new(…)` in the body as another slot group — so `$p.shift(1).len2`
  never makes the Point `shift` answers either. An argument that is itself
  such an object (`$a.add($b)`, `$a.dot(Vec.new(…))`) is its slot group under
  the parameter's name, when the parameter is untyped or names its class.

The class of every such object is known from the `.new` that made it, so
there is no guess and no guard per call, and a body compiled in place sees
only its own parameters and its invocant (`LoopCx::floor`): a name of the
caller's is not in its scope, as it is not in the method's.

### What is asked, and when

*Decided once (the compile, at the loop's first entry):*

- the class is one `plainNewClass` answers 1 for and whose `PlainFill` is
  `ok` with `noHooks` — no `new`, `bless`, BUILD, TWEAK, POPULATE or DESTROY in
  the ancestry, every attribute a plain `$`, untyped or `Int` / `Num` / `Str` /
  `Bool` (phase 7), with no default or a literal one — and every attribute is stored under its bare name;
- every named argument is one an attribute takes, given once;
- the method is a plain `method` (not a multi, a submethod, a proto, wrapped,
  `is rw`, with a return type) with `$` positional parameters whose types the
  arguments already are, and its body mentions only its parameters, its own
  `my`s, `$.x` / `$!x`, `self`, literals, operators, `?? !!`, and method calls
  and `.new` of the same kind (`inlSafe`), as one expression or as statements
  (`inlSafeStmts`: phase 3); it does not call itself.

*Checked per entry:*

- each class name still finds that class from the scope the loop is entered
  in (`kernelClassIn`: what the scope binds the name to);
- no method compiled in place is wrapped;
- no thread is live, no custom metaobject, no language-revision switch;
- **when the symbol generation has moved** (a class, role or subset declared
  anywhere since the facts were last looked at): each class's fill is the plan
  it was compiled against and each `.name` it resolved finds what it found
  (`ObjFact`). They are looked at again rather than given up on: a program
  that declares a class in a routine moves the generation on every call.

*Bail is an exact re-run*, as for every kernel: an Int that outgrows int64 or
a zero divisor abandons the attempt and the loop runs the ordinary way from
the start, making its objects. Nothing the kernel did is visible, since an
object held as slots is by construction one nothing else can see.

### What refuses the kernel (the loop then runs as before)

An object used as a value in a way phases 3 and 5 do not cover: stored into
a container by subscript, passed to a routine or to a method the kernel does
not compile in place, interpolated, compared, returned. A method that is not an accessor
nor a body the kernel compiles in place (phase 3's statements included). An attribute read that was never given a value (it reads
as `Any`). A Str answered by a method called on a `.new` or on another call's
result (a `Seq` node has no string road; on a `my` it is fine).

## Phases

| | | status |
|---|---|---|
| 1 | Objects born and dead in the loop, as above | **landed with this file** — `t/regression/object-kernel.raku` |
| 2 | **Objects that come from outside and are only read.** `$p` made before the loop: a plain `$` variable holding an instance of a class whose attributes are plain untyped `$` ones under their own names (`readableClass`: the fill's test of the attributes; a `new`, BUILD or TWEAK of its own does not matter to a read). At each entry its class and each attribute's type are checked and the attributes copied into slots — the kernel runs no user code and writes no attribute, so they cannot change under it — and `$p.x`, `$p.len2`, `$p.dot($q)` compile as for an object the loop makes. A loop that assigns the variable, stores the object or calls a method that is not compiled in place refuses as before. `$acc += $p.len2` 5,181 → 123 instructions an iteration, `$acc += $p.x` 1,626 → 70. | **landed** |
| 3 | **Assignment and control flow.** `$p = OBJECT` for a `my` of the loop: the new attributes are worked out first, into slots of their own, then copied into the name's — when the class and each attribute's type are what the name already holds (`objCopy`). A name now owns its slots: `my $q = $p`, or a `my` given what a method answers from `self` or a parameter, copies the group (`objShared`), so `$q` still reads the old object after `$p` is assigned. `COND ?? A !! B` of two objects of one class and shape is one group both sides fill. A method's body may be statements — expressions and assignments to its own `my`s, `if` / `elsif` / `else`, `return VALUE` (`inlSafeStmts`): every `return` and the last statement store into one slot, or one group for an object (`InlRet`, the class read before anything is compiled by `objClassStmts`), and a new node (`KOp::Inl`) runs the body and takes the `return` mark off, so it ends that body and nothing around it. Refused, and the loop then runs as before: an assignment or `?? !!` that would change a class or an attribute's type, ends that answer two kinds of value, a Str, or a Rat, an `if` with no `else` as the last statement, a loop inside the body. **Not here:** assigning a variable made *before* the loop (`$p = $p.shift(1)` with `$p` outside) — the object has to exist when the loop ends, which is phase 5. 1M iterations of a conditional assignment, `clamp` with an early `return` and `norm1` with two `my`s: 31.82G → 1.01G instructions, 1.32 → 0.04 s (Rakudo 2026.09 1.16 s). | **landed** |
| 4 | **Writes.** `$!n = E`, `$!n OP= E`, `$!n++` in a body compiled in place, and `$p.n = E` through an `is rw` accessor, are stores to the attribute's slot when the value is of the slot's type (`attrStore`, `markWritten`). The slots are per *name*, so a write is only right while one name holds the object: every group carries whose object it is (`VObj::owner` — a name of the loop, an object of the program, nobody's, or a *copy* of what may be a name's, which is never written), and a name that gets a second holder (`my $q = $p`, `$q = $p.me`, `c ?? $p !! $q` given a name) is *aliased*; a loop in which one owner is both written and aliased is refused, whichever comes first. A method whose every end is `self` answers the invocant's own slots (`InlRet::direct`), so `$c.add(1).add(2)` chains. An object of the program: its attributes are written back in place when the loop ends (`LOuterObj::wr`; Int, Num and Bool only), and the loop is not entered when two of its names hold one object. Refused, the loop as before: an object that leaves the loop (phase 5b: it is a real object elsewhere), a variable the loop also replaces, an attribute never given a value, a value of another type, an accessor that is not `is rw`, a subscript that calls a method while attributes are written (the order of evaluation). 1M `$a.add($i % 100).add(1)`: 16.72G → 0.46G instructions, 0.66 → 0.02 s (Rakudo 2026.09 1.03 s). | **landed** |
| 5a | **Materialising at the loop's end: a variable of the program given a new object.** `$p = OBJECT` where `$p` was made before the loop and holds an object of OBJECT's class: the variable's slots (phase 2's) take the new attributes and a flag slot is set (`LOuterObj::dirty`); when the loop ends without a bail and the flag is set, the object is made from the slots by `plainFillRun` — the arguments its `.new` would have been given — and stored in the variable. A loop that never assigns leaves the very object there. OBJECT must be *fresh* (`VObj::fresh`): a `.new` on every road, through methods, `?? !!` and `return`s, and held by no name — so the object made at the end is one nothing else could have held, and identity is what it was. Checked per entry: the variable is a container a plain store is right for (`writableCell`: not typed, bound, readonly), and no second name of the loop's objects is the same container. Refused, the loop as before: another class or attribute type, a Str attribute (its NFC mark has no road back yet). `for ^1M { $p = $p.step }` 12.72G → 0.44G instructions, 0.51 → 0.01 s (Rakudo 2026.09 0.62 s). | **landed** |
| 5b | **Materialising where an object escapes.** `@a.push(OBJECT)` onto a plain array, and `$p = $name` into a variable of the program. The frame has object slots beside its integer and string ones (`KConts::ofr`). A fresh object is made at the push (`KOp::OMake`: the plain fill, its arguments read from the slots, no `.new` call, no user code — so a bail only drops it, and the push is taken back by the containers' undo log). A *name* that escapes anywhere holds the object itself from its declaration and each assignment on (`VObj::box`), so two pushes of it, a `my $q = $p` copy and the variable it is handed to are one object; which names those are is found by a first compile, which asks for a second (`needBox`, as an Int given a Rat does). Its attribute reads and method calls still go to the slots. A variable of the program that is pushed, or given a named object, holds its object in a slot too, taken at entry and stored back at the end if assigned. The array must hold nothing or objects (`LCont::objs`); no element of it is read in the loop (phase 6). Refused, the loop as before: an object that may or may not be a name's (`c ?? $p !! P.new(…)`, a method answering `self`) when it escapes, a Str attribute, a class not found by its name, an array of Ints or Strs. **Not here:** an object passed to a routine, stored with `@a[$i] = …` or `%h{$k} = …`, or the loop's value. 1M `@a.push(P.new(…).shift(1))`: 15.16G → 2.10G instructions, 0.69 → 0.13 s (Rakudo 2026.09 1.08 s) — what is left is the objects themselves. | **landed** |
| 6 | **Arrays of objects.** `for @points -> $p { … $p.len2 … }`: a class check and an attribute fetch per element, which needs object slots ([OBJECT-SLOTS-PLAN.md](OBJECT-SLOTS-PLAN.md)) to be a load rather than a hash probe. | open |
| 7 | **Typed attributes.** `has Int $.x` kept a class out of every phase, since the fill's plan did not cover a typed attribute. The plan now does, for `Int`, `Num`, `Str` and `Bool` with no smiley, coercion or `where` (`PlainFill::Slot::kind`): an attribute not given starts as its type object, a literal default must be of the type, and `plainFillRun` stores an argument only when it is a plain value of exactly that type — a wrong type (and its error), a Bool for an Int, an IntStr, a big Int, Nil, a type object all take `runAttrDefaults` as before. This is in the fill itself, so the interpreter's `.new` of such a class takes the fast fill too. In a kernel, `.new` requires the argument's kernel type to be the attribute's (anything else refuses the loop, and the generic path raises what it raises). `objects-steady` with `has Int $.x; has Int $.y`: 24.82G instructions before, 19.12G with kernels off, 0.59G with them on; 1.04 → 0.05 s (Rakudo 2026.09 0.97 s). **Not here:** `Rat`, user classes as attribute types, smileys (`Int:D`), subsets, natives, `@`/`%` attributes. | **landed** |
| 8 | **The same in `--cnp` and `--exe`.** Both refuse or box what this compiles; `--exe -O` on `objects-steady` is now 100× behind the interpreter. | open |

## What this does not do

It does not make a method call fast. A call the kernel cannot compile in
place is still the interpreter's, at 3–4k instructions, and a loop with one
such call is not a kernel at all. Phases 3–7 are about reach; the per-call
cost outside kernels stays with [DISPATCH-PERF-PLAN.md](DISPATCH-PERF-PLAN.md)
and V6-PLAN P2.

## Found on the way

- `my class Point` in a routine replaces a global `class Point` for the rest
  of the program once the routine has run (Rakudo keeps them apart). Not
  touched here; the regression file uses a lexical class of another name.
- Rakudo 2026.09 runs `Class.^find_method('m').wrap(…)` unwrapped; this engine
  applies the wrap, with the kernel on or off.
