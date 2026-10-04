# Plan: kernels past scalars

*Written 2026-10-02, after 68b2e991 and d3c06426. Those two commits made loop
kernels (`src/IntKernel.cpp`): a `for` over an integer Range, a `while` /
`until`, or a C-style `loop` whose body uses only plain Int and Str variables
runs on a frame of `int64_t` and `std::string` slots and writes the variables
back when the loop ends. Sub kernels gained typed signatures and statement
bodies, and `--exe` gained Str lanes and `given`/`when`. This plan covers
what is still open, in the order the evidence suggests. Benchmark and gate
runs are not tasks here; every task below names the gates it needs.*

---

## The contract every task keeps

The design that made the first two commits safe is the one each widening has
to keep. Nothing here relaxes it.

1. **Decided once, checked per entry.** The shape is compiled on the first
   entry; the facts that can change between entries (a value's type, a
   container's traits, a re-bound routine, a shadowed operator, an `augment`,
   another live thread) are checked at every entry, never per iteration.
2. **Bail is an exact re-run.** A kernel that meets anything it cannot answer
   exactly abandons the attempt, and the loop or call runs the ordinary way
   from the start. That is sound only while nothing the kernel did is
   visible. Scalars meet that because the kernel works on copies. Task 1 is
   the first one that writes into containers in place, which is why it needs
   an undo log.
3. **The oracle is the same build with `RAKUPP_NO_KERNELS=1`, then Rakudo.**
   A difference from Rakudo that the kernels-off build shares is pre-existing
   and gets its own task; a difference between kernels on and off is a bug in
   the kernel.
4. **Every Num operation rounds on its own.** The interpreter never fuses a
   multiply and an add. Kernel nodes are separate functions, so nothing fuses
   in the interpreter; generated C++ is compiled with `-ffp-contract=off`
   (68b2e991). Any new emission keeps that true.

## Where the time is

Measured 2026-10-02, interleaved best of 7 against b2a63b6c (the run-bench
programs) and best of 5 (the 28-variation study). A row is open when its loop
falls back to the generic path because of one construct.

| Workload | Interp | What keeps it out of a kernel |
|---|---:|---|
| arraypush | 125 ms | `@a.push`, `@a[$i]` read and write |
| hashfill | 90 ms | `%h{"key$i"} = …`, `for %h.values` |
| rats | 169 ms | Rat arithmetic, `.denominator` |
| hash | 13.6 ms | `%h{$_ % 1000}++` |
| variation: array read in a loop | 33.6 ms | `@a[$_ % 100]` |
| variation: hash increment | 36.3 ms | `%h{$_ % 100}++` |
| variation: Num accumulator | 64.8 ms | `$x += 0.5e0` |
| variation: Rat accumulator | 73.3 ms | `$x += 0.5` |
| variation: `**` | 74 ms | `$_ ** 2` |
| variation: `.chars` (now in) | 69.9 ms | — (d3c06426) |

In-vocabulary loops ran 10–23× faster in the same study, and a loop with one
construct outside ran at 0.9–1.0×. So the gain for a program is set by the
vocabulary, not by size: the tasks below widen it.

---

## Task 1: arrays and hashes, with an undo log

**Status (2026-10-04).** Done except two items: `%h{EXPR}:exists` and `for @a
-> $x` as a source. In: reads, stores, `+= -= *= ~=`, `++`/`--` (a hole or a
missing key counts from the identity, a postfix one answers it), `@a.push`,
`.elems` on both kinds, Int and Str elements and keys. The undo log is bounded
by the containers, not the iterations: once it outgrows the containers it
covers, each is saved as it was before the loop (a copy with the log taken
back on the copy) and logs no more; a bail restores in place, so a Proxy or a
live Pair into an element still reaches it. A subscript with a side effect is
refused (the generic path evaluates a store's value before its subscript, and
Rakudo the other way round). Gate: `t/regression/loop-kernel-containers.raku`,
28 on Rakudo, kernels off and kernels on. Interleaved min cycles against the
build before it: arraypush −87%, hashfill −56%, hash −48% (−60% at 1M
iterations), a two-iteration container loop entered 200k times −92%; fib,
mainwhen, loopsum, streq, objects level or better. Two traps met: a
`std::string` and two pointers added to KRun cost mainwhen 2% in the scalar
loop (one pointer to a side struct is level), and `snprintf` for an Int key
was half the hash kernel's time.

**What.** Inside a loop kernel:

- reads `@a[EXPR]` and `%h{EXPR}`;
- writes `@a[EXPR] = …`, `op=`, `++`/`--`;
- `@a.push(EXPR)` and `@a.elems`;
- `%h{EXPR}++`, `%h{EXPR} = …`, `%h{EXPR}:exists`;
- as a source, `for @a -> $x` over a plain Array.

Element values are read as plain Int or Str and checked on every read; a
read that finds anything else bails.

**The undo log.** These writes land in the container itself, so a later bail
has something to take back. Each write records how to reverse it before it
happens:

- an element store logs `(array, index, old Value)`;
- a push logs the length before it;
- a hash store logs `(hash, key, old Value or absent)`.

On a bail the log is replayed backwards, which puts every container back
exactly as it was, and the loop then re-runs from the start. On success the
log is dropped. A loop that never writes a container keeps no log.

**Entry checks**, per container, beside the scalar ones:

- a plain Array: not a List, not shaped, typed, native, lazy, or `is default`;
- a plain Hash: no Set/Bag/Mix, not object-keyed, not typed;
- no two names for one container;
- no other thread live.

**Traps to settle first, by probing Rakudo and the kernels-off build:**

- An element can be a bound container (`@a[0] := $x`). A store has to go
  where the generic store goes, into the container the element holds, and
  the undo log restores that container. The pre-existing `$c := @a[0];
  $c += 1` bug (chip spawned) shows this path is not settled yet; fix it
  before relying on it.
- Writing past the end auto-vivifies the elements in between. Reading past
  the end gives Any, which is not an Int, so the read bails.
- A hash key from an Int is its decimal text.
- If `ValueHash` keeps insertion order, `.keys` output depends on it. Undoing
  an insertion must restore the exact internal order: erase the new key, and
  check that erasing the last-inserted key restores the old iteration order.
- A missing key read by `++` is Any, which becomes 1. That has to match the
  generic path, warnings included: there are none for `++` on Any.
- `for @a` aliases its loop variable to the element. A kernel that writes
  `$_` has to write the element, as the generic path does, or refuse.

**Expected reach:** arraypush, hash, the array/hash variations, the first and
last loops of hashfill. The hashfill key `"key$i"` is an interpolation, which
d3c06426 already compiles.

**Gates:** Roast; a new `t/regression/loop-kernel-containers.raku` with
bails placed after container writes (an overflow on the last iteration, a
non-Int element halfway), checked against kernels-off and Rakudo; the
differential battery.

## Task 2: Num slots

*Done 2026-10-03 in the interpreter.* A Num travels as its bits in the int64
frame and in what a node returns, so `Param`, `Lit`, `Set` and `?? !!` carry
it unchanged; `+ - * / **`, the comparisons, `++ --`, `+= -= *= /=`, prefix
`-`, a Num as text (`~`, interpolation, `.Str`) and a Num as a condition
(`NTruth`, which `cexpr` puts in front of every condition, since `-0e0`'s bits
are not zero) are nodes of their own. An Int meeting a Num is converted
exactly as numFastArith converts it, `(double)i`, so an Int variable may meet a
Num too; an Int literal is converted when the kernel is compiled. A zero
divisor and a power that underflows bail. `.Num` of an Int is the same
conversion. `#pragma clang fp contract(off)` heads the file. Measured
(best of 5 against 954f1af7): the Num variation 59.7 → 4.7 ms, a mixed
Int/Num loop 166 → 10 ms, a Newton iteration in a `while` 95 → 18 ms. The
`--exe` half is below.

**What.** A third slot type, `double`. Num literals (the `e` spelling; `0.5`
is a Rat), `+ - * /` over Nums, the six comparisons, `++`/`--`, `+=`/`-=`/
`*=`/`/=`, Num in `?? !!` and in conditions (true when not zero). An Int
literal that a double holds exactly (|v| ≤ 2⁵³) may meet a Num; an Int
variable meeting a Num is refused for now, because the conversion and the
comparison rules need probing first.

**Semantics.**

- IEEE where Raku agrees; probe Rakudo for NaN in each comparison and for
  `-0e0`.
- Division by zero bails and leaves the answer to the generic path, Failure
  or Inf.
- `/` of two Ints is a Rat and is refused here.
- Write-back stores `.n`. The entry check: `t == Num`, no `natFloat`
  (num32), no NumStr tag.
- Add `#pragma clang fp contract(off)` to IntKernel.cpp as a guard, even
  though every operation is its own function today.

**--exe:** the F64 lane exists, but it types a slot F64 only when a Num literal
reaches it inside the loop. A slot that only ever holds a Num read from
outside is typed I64, and its entry guard sends the loop to the boxed path.
Seed the lane type from the declaration's initializer as well: `my $x =
0e0` outside the loop.

**Gates:** Roast; Num cases in both regression files, including one that
depends on fusing (the 68b2e991 case) and one with NaN; t/exe for the lane
change.

*`--exe`, 2026-10-03:* a top-level `my $x = <Num literal>` (every top-level
declaration of the name) seeds the slot F64 (`topNumInit_`, `uSeed`); the
guard still decides at run time. Three pre-existing lane divergences surfaced
while testing it and are fixed in the type pass: `$x = 1` into an F64 slot
came back `1e0` (an `=` of an integer into an F64 slot now refuses the lane,
unless it is a `my num`); `$b = $_ > 1` stored 1 where Raku stores True (an
`=` of a comparison, `!`, `&&` or `||` refuses it); and `&&` / `||` in value
position computed 0 or 1 where Raku returns an operand (they are laned only
where their truth is all that is read).

## Task 3: Rat slots

*Done 2026-10-03 in the interpreter, as EXACT slots rather than Rat slots.*
The workloads that mattered start from an Int: `my $t = 0; … $t += $r` (the
`rats` kernel and the Rat variation both), and Raku turns that Int into a Rat
on the first `+=`. So a slot is an exact number, an Int or a Rat: two integer
slots, the numerator and a denominator that is 0 for an Int. An exact node
returns the numerator and leaves the denominator in the word in front of the
loop frame (`kDenSlot`, `fr[-1]`). Arithmetic is in 128 bits, reduced by a
binary gcd (`xratio`), a Rat with a denominator of 1 stays a Rat, an Int `/`
an Int is a Rat; a part past int64 or a zero divisor bails, so the generic
path decides the BigInt Rat or the Num a large denominator spills to. An Int
variable that is given a Rat (`=`, `+= -= *= /=`) makes the first compile
refuse with `again`, and the loop is compiled again with that name promoted
(`LoopCx::promote`, at most five compiles); write-back stores an Int in place
and anything else as a new value in the plain container. Rat literals are
reduced at compile time. `.numerator`, `.denominator`, Rats as text and as
conditions are in. Exact numbers need `__int128` (`kHasExact`); a Rat meeting
a Num, `%`, `div`, `**` and `min`/`max` on Rats are refused. Measured (best of
5 against 954f1af7): `rats` 176 → 25.5 ms, the Rat variation 70 → 17 ms; a
harmonic sum outgrows 64-bit denominators within 50 terms and stays generic.

Two placement findings, both kept in the code: a `den` register in `KRun`
cost `fib` 5% (it never uses it), and a frame slot for it that moved every
other slot by one cost `mainwhen` 4%. And code added to IntKernel.cpp moved
`fib` by up to ±5% with nothing on its path changed, so its call path
(`callFn`, `condFn`) is aligned to 64 bytes (`KHOT`); aligning every `binFn`
instance as well cost `mainwhen` 8%, so `binFn` is not.

**What.** A Rat as an `int64_t` numerator and denominator, normalized after
every operation by gcd, with a positive denominator. Rat literals, `+ - *`
over Rat and Int, `/` over any two (an Int `/` an Int is a Rat), the
comparisons, `.numerator` / `.denominator`. Any overflow bails, and so does
a denominator the generic path would turn into a Num: the generic path then
decides, which keeps Rakudo's Rat-to-Num degradation out of the kernel
entirely.

**Write-back** builds the BigInt pair the Value carries (`ratN`/`ratD` in
`ValueExt`). It allocates, but only once per loop. The entry check: a Rat that
is not a FatRat and whose numerator and denominator both fit in an int64.

**Probe first:** how the generic path normalizes (sign, zero numerator),
what `0.1 + 0.2` stores, and what `.raku` prints for a Rat the kernel built,
which must be byte-identical.

**Expected reach:** rats (169 ms), the Rat variation.

## Task 4: `**` and a few pure methods

*Done 2026-10-03, interpreter and `--exe`.* `**` on Ints (`pow_ovf` in
IntOps.h, shared by both), on Nums (`std::pow`, with applyArith's underflow
rule), `min` and `max` on Ints, and the methods `.abs .sign .Str .Num .Int .ord
.uc .lc .substr` (the last four ASCII only, else bail; `.substr` bails where
the generic path fails). The native lanes took `**`, `min` and `max`; the
methods stay interpreter-only. Measured (best of 5): the `**` variation 70.9 →
4.9 ms interpreted and 52 → 3.9 ms native, a `min`/`max` loop 242 → 14 ms
interpreted and 3.4 native, `.substr`/`.ord`/`.uc` in a loop 260 → 11 ms.
`abs` and `chr` as subs are not done: a call to a built-in by name resolves
nowhere a kernel looks.

- **`**` on Ints.** A non-negative exponent: repeated squaring with overflow
  bails. A negative exponent is a Rat and bails, and so does a negative
  exponent on a Rat slot until task 3 decides otherwise.
- **Methods**, gated as `.chars` is (entered only while no built-in type is
  `augment`ed):
  - on Int: `.abs`, `.sign`, `.Str`;
  - on Str: `.ord`, `.uc` / `.lc` (ASCII only, else bail), and `.substr`
    with Int arguments inside the string, grapheme-aware like `.chars`
    (ASCII fast, else bail).

  Each one is the generic path's own function, called from the node.
- `min` / `max` infixes, `abs`, `chr` of an ASCII code point.

## Task 5: --exe parity for d3c06426

The interpreter took these in d3c06426; the native backend does not have them:

- *Grown 2026-10-03:* the interpreter's exact slots (task 3), the methods
  of task 4, and a Num `/=`. The lanes took `**`, `min` and `max` only.

- **Lanes:** Int to Str in the Str lane (`~`, `~=`, interpolation with Int
  and Str parts), Str arms in `?? !!`, `.chars` (the same `graphemeCount`),
  `True` / `False` in conditions. `^N` and top-level `while` / `loop` lanes
  already exist.
- **Sub twins (`planKernels`):** `Int` / `Int:D` parameters, `is copy`,
  `--> Int`, and statement bodies (locals, `while`, `for`, assignments,
  `return` inside loops). The twin emitter `kStmts` takes only the
  pure-expression shape today; emitting the statement shape as C++ is the
  same work `ltail` does in the interpreter.
- **Callees:** the twins resolve callees at compile time. A program that
  mentions `wrap` gets none; re-binding (`my &g` assigned, `&a :=`) needs the
  same answer: refuse twins for a call through a `&`-variable, as the
  interpreter's `callsBound` would decline them.

**Gates:** t/exe (both `-O` and plain); `t/regression/loop-lanes-native.raku`
extended with the new shapes.

## Task 6: Str in sub kernels

**What.** Str parameters and a Str result: `sub tag(Str $s) { "<$s>" }`. A sub
frame then needs string slots of its own per activation. A string array on
the call path costs constructor and destructor work on every call, so only a
kernel that uses strings would carry one, and a pure-Int kernel's call path
(fib) must not change. Measure fib first and after; the 12% the KRun flag
layout cost (see the 68b2e991 notes) shows how sensitive that path is.

**Before building:** find a workload where it matters. No run-bench program
and none of the 28 variations has a hot Str-returning sub. Without one, this
task waits.

## Task 7: closures in --exe (the original plan's item d)

Anonymous subs (`-> $x { $x * 2 }`, `sub ($x) {…}` held in a variable) get no
twin in `--exe`; the interpreter shares one kernel across every closure made
from a body (`Stmt::kernelHome`). The native backend would need the same: an
int64 twin per closure body, entered from the closure's Value function, with
the callee checks above.

## Task 8: documentation

- *Done 2026-10-03:* `--help` (`src/main.cpp`) describes `RAKUPP_NO_KERNELS`
  for subs and loops over Ints, Nums and Strs, and for `--exe` at compile time.
- `docs/status/BENCHMARKS.md`: its prose and its mutsu comparisons
  ("Against mutsu, `--exe` wins all seventeen and the interpreter fourteen",
  the loopsum row) predate both commits. Rewrite them from the next release
  run's numbers, in the page's own tone: numbers and mechanisms, no
  editorializing.
- `docs/dev/plans/UNBOX-PLAN.md`: the lanes now run without `-O`, have a Str
  type, `given`/`when` and `^N`. Add a dated section rather than editing the
  history out.

---

## Found on the way, tracked apart

Pre-existing, each reproduced with the kernels off and on the clean base
build, each with its own spawned task:

- the block form of `for` over a Range with BigInt ends iterates once;
- four `--exe` divergences: `:=` aliasing, `$x++` on a readonly parameter,
  `~=` NFC, a `where` container;
- `t/exe`'s two `-O` disagreements: `failure-is-concrete-for-smiley`,
  `pair-and-list-are-immutable`;
- parser leniencies where Rakudo refuses: `when 0 { … } when 1 { … }` on one
  line, and `augment` redefining an existing method;
- a user `sub postfix:<++>` is ignored by `$x++`. Rakudo honours it.

## Order

| | | Reach | Size |
|---|---|---|---|
| **1** | arrays and hashes, undo log | arraypush, hash, hashfill, 2 variations | large |
| ~~2~~ | Num slots (+ the F64 lane seeding) — done 2026-10-03 | Num variation, numeric examples | small |
| ~~3~~ | Rat slots (exact slots) — done 2026-10-03 | rats, Rat variation | medium |
| ~~4~~ | `**`, pure methods — done 2026-10-03 | `**` variation, string munging loops | small |
| **5** | --exe parity for d3c06426 | the native column of all of the above | medium |
| **8** | documentation | — | small |
| **6** | Str in sub kernels | none measured yet | medium; waits for a workload |
| **7** | closures in --exe | `map`/`grep` bodies compiled natively | medium |

Tasks 2 and 4 are small and independent; either can land first while
task 1's bound-element bug is fixed. Task 5 follows each interpreter task
rather than waiting for all of them: a widening the native backend does not
have shows up as interp faster than `--exe`, which run-bench reports.

## Gates, every task

- `rakupp tools/run-roast.raku` after every `src/` change: 1,424/1,424 files
  and 218,420 assertions without skip/todo.
- The task's regression file, green on Rakudo, with kernels on, and with
  `RAKUPP_NO_KERNELS=1`; checked with `RAKUPP_KERNEL_TRACE=1` that the cases
  meant to run as kernels do. A program-wide filter (a lexical
  `infix:<…>`, an `augment`) silently turns every later kernel off, so
  those cases go last in a file.
- `t/exe/run.raku` for any Codegen change, run from a scratch worktree build:
  another session relinking `build/rakupp` mid-gate stops it.
- A/B against a clean build of the previous commit, interleaved, with stress
  workloads that try to make the new path lose: tiny loops entered often,
  declines and bails on every entry. Below 10%, compare minimum `cycles
  elapsed` (`/usr/bin/time -l`) over 12–20 interleaved runs; wall time on the
  benchmark machine is too noisy for that.
