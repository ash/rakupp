# Windfall — plan

*Written 2026-10-05 at `a3b7644c`. A small-scale Grand Review: four read-only
reviewers, one each for the loop and sub kernels (`src/IntKernel.cpp`),
`--exe` codegen and native module bodies (`src/Codegen.cpp`,
`src/AotModules.cpp`), `--cnp` and `src/Jit.cpp`, and the interpreter hot
paths touched by the week's language-gap batches. The brief was low-hanging
fruit only: slow paths and wrong answers that are cheap to fix. Every row
below was measured on `build-arm64/rakupp` 5.2.1-62; rows marked "confirmed"
were re-run independently of the reviewer that found them.*

---

## How each task is done

1. **Probe first, on both engines.** Each task carries its probe. Run it
   before the change for the "before" number and the oracle answer, and after
   the change for the "after". The oracle is the same build with
   `RAKUPP_NO_KERNELS=1` (kernels), the interpreter (`--exe`), or
   `--cnp=off` (cnp); then `/opt/homebrew/bin/rakudo`.
2. **A wrong answer gets a test** in `t/` before the fix lands, one that
   fails on `a3b7644c`.
3. **Gates per batch:** Roast sweep after every change to `src/`; `t/aot`
   and `aot-battery` for any task in `Codegen.cpp` or `AotModules.cpp`;
   the `t/exe` gate for `--exe` tasks; the cnp differential gate for `--cnp`
   tasks. `perf-guard --check` once per batch, on a quiet machine, run alone.
4. **Measure, don't estimate.** A perf task closes with its before/after in
   the commit message. A task whose gain does not show in its probe is
   reverted, not kept for tidiness.

## Where it stands

W1 to W4 are done, except that `perf-guard --check` is still owed and three
W4 pieces are left (below). W3-5
and W3-6 were left as they are: each was under 1% of samples, and moving
`loopNest_` into ExecContext changes how it travels across a gather's
coroutine switch, which is no low-hanging change. W3-1 to W3-4, with kernels
off on both sides (1M iterations): element assignment 219 → 190 ms, a `Numeric`
parameter 243 → 222 ms, `~~ C` 260 → 250 ms, `~~ Real` 333 → 326 ms.
Measured against a build of `a3b7644c`, best of 5 interleaved unless noted:

| Task | Before | After |
|---|---:|---:|
| C1 + W2-6, kernel `.chars` / `.substr` (a per-slot cache) | CR LF counted twice; 100k-char scanner 1.7 s | right; 2 ms (kernels off: 84 ms) |
| C2, `--exe` typed parameters | unchecked, `Int()` not coerced, unpassed `Int $x?` Any | the interpreter's bind (inline test for Int/Str/Num/Rat/Bool) |
| C3 + W2-8, literal parameters | empty-type walk per bind, UB | literal multi fib(24) 102 → 87 ms |
| C4 + W2-5, `--exe` `for ^N` | `^2.5` stopped at 1; list built | counted; 3M calls 99 → 64 ms, `-O` 69 → 27 ms |
| C5, `return` in a `.map` assigned to an array | the map never ran | eager; `--exe` refuses the construct |
| W2-1, loop kernels under `--cnp` | `%% 7` loop 1.18 s | 0.07 s, as without `--cnp` |
| W2-2, `EXPR for ^$n` | 0.204 s | 0.004 s |
| W2-3, `--exe` `fib(Int $n)` | 90 ms | 7 ms |
| W2-4, `-O` for module bodies | `applyArithValue("*", …)` | `rtMul(…)` |
| W2-7, element `~=` | 1M appends 9.6 s | 0.015 s (kernels off: 0.155 s) |
| W2-9, multi-call argument copy | 2 copies + an allocation | 1 copy |

Found and fixed on the way:

- **The undo log built every entry before asking whether it was needed.**
  An entry copies the element and its key, so a write to a container
  already saved whole still copied a long string. W2-7's gain is mostly this.
- **`--exe` comparison lane.** `last if ++$c > 3` in a loop lane emitted C++
  that did not compile (`(() > (3LL))`): a refused operand did not refuse
  the comparison.
- **`--exe` fractional range.** `for 0..^$n` with `$n = 2.5` stopped at 1.
- **`--exe -O` module bodies.** A module routine's call to its own `sub uc`
  took the built-in `uc` once `-O` reached module bodies.
- **`--exe` `return` in a block**, and in `try { }` / `do { }`. It compiled to
  a C++ return from the block's lambda, so the routine went on. A routine with
  such a block is now an interpreter frame for its call (RtRoutineFrame) and
  the return is thrown at it; one from a block that outlived its routine is
  X::ControlFlow::Return.
- **`--exe` native parameters** bound as boxed and unchecked (`int8` took
  300, `uint8` -1, `int` a Str): they bind by the interpreter's rules now.
- **`--cnp` and `where`.** A `where`-constrained variable was written back
  unchecked (`my $w where * < 5` counted to 20); the cnp container guard now
  refuses what the loop kernels refuse.
- **`--exe` `++` / `--`** added 1: `"aa"++` died, a Bool did not saturate, a
  class's succ was never called. One rule now (Interpreter::stepValue).
- **`--exe` literal multi candidates** compared with `eqv`: `multi h(1)`
  refused True. One rule now (Interpreter::literalAccepts).

W4, against the commit before each round:

| Task | Before | After |
|---|---:|---:|
| W4-1, `$i++ while $i < 3M` / the same `repeat` | 0.22 s / 0.113 s | 7 ms / 3.5 ms |
| W4-2, `$s = $s ~ "x"` 100k in a kernel | 1.01 s | 1.6 ms |
| W4-3, undo log: 1M array stores / 1M hash updates (peak RSS) | 365 MB / 157 MB | 308 MB / 125 MB |
| W4-4, kernel entry, inner loop over an array, 200k entries | 205 ms | 187 ms |
| W4-5, `--cnp` `%%` / `+^` loop | 0.65 s | 0.087 s |
| W4-6, `--cnp` hot inner loop in a once-run outer one | 0.48 s | 0.043 s |
| W4-7, `--cnp` `.abs` with seven live variables | 0.165 s | 0.105 s |
| W4-8, `--exe` module sub, 1M calls | 73 ms | 64 ms |
| W4-9, `--exe` module sub with 3 params / method | 44 / 115 ms | 39.5 / 107 ms |
| W4-10, `--exe` literal multi fib(25) | 50 ms | 9.5 ms |
| W4-11, `--exe` `$x++;` as a statement | old value copied | not made |
| W4-12, interpreted literal multi fib(24) | 95 ms | 82 ms |
| W4-13, `.sort({ $^a cmp $^b })` over 200k Strs | 0.63 s | 0.086 s |
| W4-14, one-byte regex search, 2,000-char subject | 15.4 ms | 3.6 ms |

Tried and reverted, the gain did not show: hoisting long string literals in
`--exe` (W4-11's second half), and an inline register file for `--cnp`
kernel entry (W4-4's second half — the entry's cost is the slot lookups in
`runIfReady`, not the allocations).

Still open:
- `--cnp` kernel entry: `runIfReady` looks every slot up by name, per Env
  level, on every entry (about 2 µs for a 200k-entry inner loop).
- The interpreter's own `$s = $s ~ X` is O(n) per iteration (W4-2 fixed the
  kernel only).
- Three interpreter divergences from Rakudo, seen while fixing `--exe`
  natives: `int $x * 2` at 2**62 does not wrap; a BigInt into an `int`
  parameter says "72 bit wide" where Rakudo says 71; a wrong kind into a
  native parameter is X::TypeCheck::Binding::Parameter where Rakudo raises
  X::AdHoc ("cannot unbox").

## W1 — wrong answers (do first)

| # | What | Where | Probe → Rakudo | Raku++ | Fix |
|---|---|---|---|---|---|
| C1 | Kernel `.chars` counts CR LF as two | `IntKernel.cpp` `charsFn`, `substrFn`, `caseFn` | `my $s="a\r\nb"; my $n=0; for ^3 { $n += $s.chars }; say $n` → 9 | 12 (confirmed) | The ASCII fast path must also require no `\r`; then W2-6 replaces the rescan with the StrBody caches |
| C2 | `--exe` drops non-multi parameter type checks | `Codegen.cpp`, routine prologue | `sub f(Int $n) { $n }; my $v = "x"; say f($v)` → dies, X::TypeCheck::Binding::Parameter | prints `x` (confirmed) | Emit the bind check; a tag test for core types. Do before W2-3, so the kernel guard and the check agree |
| C3 | `p.type.back()` on an empty type string (UB) | `InterpreterCore.cpp:4054`, reached by literal params | `multi f(0) {…}` | UB (by reading) | Return from `typeCheckBindImpl` once the literal and Mu checks are done and `p.type` is empty; part of W2-8 |
| C4 | `--exe` `for ^$n` truncates a non-integer `$n` | `Codegen.cpp:227` (`.toInt()`) | `my $n = 2.5; for ^$n { print "$_ " }` → `0 1 2` | `0 1` (confirmed) | Fixed by W2-5's route through the Range branch |
| C5 | `return` in a `.map` block does not leave the sub | interpreter and `--exe` | `sub g($n) { my @x = (1,2).map({ return 5 if $_ == 2; $_ }); 0 }; say g(1)` → 5 | 0 (confirmed) | Engine fix, not perf. The `.map` is eager here (assigned to `@x`); find where the block's return is caught. May grow into its own item |

## W2 — hot paths, a few lines each

| # | What | Where | Measured | Fix |
|---|---|---|---|---|
| 1 | `--cnp` turns off every loop kernel | `IntKernel.cpp:2636` (`jit::on()` in the gate); `InterpreterCore.cpp:2178` builds the LoopGuard before `tryLoopKernel` | `for 1..20M { $s++ if $i %% 7 }` 0.07 s → 1.18 s with `--cnp=on` (confirmed) | Drop `jit::on()`; ask for the cnp site only when `tryLoopKernel` declines. The cnp gate may set `RAKUPP_NO_KERNELS=1` if it needs full coverage. Update CNP-PLAN's "where it stands" |
| 2 | `EXPR for ^$n` never reaches the loop kernel | `InterpreterCore.cpp:2811` requires `NK::Range` | `$s += $_ for ^$n` (3M) 0.21 s vs 0.01 s for `0..^$n` (confirmed) | Also accept `Unary` `^`, not postfix |
| 3 | `--exe`: an `Int` parameter keeps a sub out of the integer kernel | `Codegen.cpp:3646` `kernelParams` | `fib(Int $n)`, fib(30) in-binary: 90 ms vs 7 ms untyped (confirmed) | Accept `p.type == "Int"`; the entry guard `__kInt` already proves it. After C2 |
| 4 | `--exe -O` never reaches module native bodies | `Codegen.cpp:6093` `emitAotPass`; `main.cpp:989` | generated `return applyArithValue("*", v_sx, v_sx);` in a module sub under `-O` | Thread `optimize` through and set `g.optimize_` |
| 5 | `--exe` `for ^N` materialises the list | `Codegen.cpp:3466` `forStmt` | 3M iterations: 117 vs 67 ms (default), 65 vs 27.5 ms (`-O`) | Route `^` through the RangeExpr branch with `rtRangeVal(0, X, false, true)`; fixes C4 |
| 6 | Kernel `.chars` / `.substr` rescan the string every call | `IntKernel.cpp` ~1789, ~1922 | 100k-char scanner: 1.72 s vs 0.04 s with kernels off | Per-slot cache of ascii / crFree / grapheme count, reset by writers, seeded from StrBody for read-only outer strings |
| 7 | Kernel element `~=` copies the whole element | `IntKernel.cpp` `csappFn` ~2247 | `@a[0] ~= "x"` 100k: 0.115 s vs 0.016 s kernels off | Append in place via `el->s.mut()`; keep the `nfcNormalize` branch; take `v` by const ref |
| 8 | Literal params run the whole unresolved-type walk per bind | `InterpreterCore.cpp:4209` gate (7e584de8), `typeCheckBindImpl` | literal-multi recursion: `typeCheckBindImpl` 9.9% of samples, `coreEnumValue` 5.2% | Gate on `p.litVal && !whereVerified`; early return on empty `p.type` (C3) |
| 9 | Multi-sub calls copy the argument list twice | `InterpreterCore.cpp:7630–7633` (`sameArgs = as`, `hasNext` captures `as` by value) | `~RedispatchCtx` 4.0% of the literal-multi profile | `hasNext` reads `rc.sameArgs` through a pointer; move, don't copy, into `sameArgs` |

## W3 — this week's checks on every operation (3–5% each)

| # | What | Introduced | Fix |
|---|---|---|---|
| 1 | `arrayGrowGuard`: a by-name scope lookup on every `@a[i] = v` (4.8%) | be813251 | `lvalue()`'s Index path records base and old size only for a typed container, where it already sets `lastLvalueElemType` |
| 2 | `elemSmileyOf` walks the env chain on each element assign (3.2%) | 2026-09-24 | Gate behind a "some smiley container exists" flag |
| 3 | `typeObjectUserAccepts` on every `~~ Type` (3.4% user class, 1.6% core) | be813251 | Count of classes that declare ACCEPTS; skip at zero |
| 4 | `userShadowsCoreRole`: a 13-string `std::set` lookup per type check (3.0%) | c5326843 | One bool, set when a role with a core role's name is declared |
| 5 | `loopNest_` is its own thread_local, touched twice per iteration | f3b1fe01 | Move into ExecContext via the `tc` already in hand |
| 6 | `bindingParams` save/restore: TLS writes per bind | b4264fa5 | Catch in `bindParams`, where `&params` is in hand |

## W4 — medium effort

| # | What | Measured | Direction |
|---|---|---|---|
| 1 | Statement-modifier `while` and `repeat … while` stay out of the kernel | `$i++ while $i < $n` (3M) 0.167 s vs block `while` 0.006 s | Compile the modifier body as one expression statement; a do-while node for `repeat` |
| 2 | `$s = $s ~ X` is O(n) per iteration (kernel and generic) | 100k: 0.93 s vs 0.001 s for `~=` | Rewrite `SSet(slot, SCat(SVar(slot), R))` to `SApp(slot, R)` at kernel compile time; the interpreter twin after |
| 3 | Undo log peaks at ~2× the container | `@s[$_] = $_` over 1M: 363 MB vs 170 MB | `check = total`; hash keys as a pointer to the node's own key; collapse consecutive `Size` entries |
| 4 | Kernel and cnp entries allocate per entry | ~240 ns (kernel), ~250 ns (cnp) per entry of an inner loop | Inline arrays for ≤ 8 containers, `undo.reserve(16)`; a thread-local cnp frame; read-only outer Strs as a `const CowStr*` table instead of copies |
| 5 | `--cnp`: `%`, `%%`, `div`, bitwise, `min`/`max`, `cmp`/`leg` go through a string-dispatched helper | `%% 7` ~60 ns/iter (interpreter speed) vs ~3 ns for a stencilled op | Int/Int `switch` at the top of `rk_cnp_binop` now; leaf stencils later |
| 6 | `--cnp`: an inner hot loop stays interpreted once the outer loop compiles | `for 1..1 { while … 5M }`: 520 ms, 0 kernels entered, vs 24 ms alone | Compile the tripped loop as well; early return in `tick` once an enclosing site is Compiling or Ready |
| 7 | `--cnp`: calls spill and reload every register | ~6 ns per extra written slot per call; `.abs` 115 ns vs `abs()` 70 ns | No spill for pure methods; a dirty mask; hoist the `"&"+name` shadow check to entry |
| 8 | `--exe` calls into module routines resolve by name | ~55 ns vs ~9 ns for a local sub | `static const std::string` for `"&name"`; later a per-site cache keyed on a binding epoch |
| 9 | `--exe` native bodies read params, outers and `self` by name | — | `__env->findSelf()`; per-routine pad index for each param |
| 10 | `--exe` multi dispatch compares literals and types by string | recursive multi fib(25): ~213 ns/call | Inline Int/Str literal compares, core-type fast path, direct `__a[i]` without named args |
| 11 | `--exe` constant strings rebuilt per evaluation; postfix `++` as a statement copies | — | Hoist to `static const Value`; emit the prefix form when the value is sunk |
| 12 | Literal multi candidates re-evaluated per scoring, kept out of the dispatch cache | 0.70 µs/call vs 0.42 µs for the `where` twin | Cache the literal Value on the Param; a literal outcome bit in 944fda3b's cache entry. Also "simple once where is verified" for `bindParams`' fast path |
| 13 | Comparator sort on plain Strs still calls `applyArith(std::string)` per compare | 200k Strs: `{ $^a cmp $^b }` 0.57 s vs `.sort` 0.08 s | All-plain-Str items with `cmp`/`leg`: plain `.sort`'s order, with a reversed comparator for `$^b … $^a` (not a reversed result, which would break stability) |
| 14 | Regex prefilter steps one byte at a time | — | `memchr` for a single first byte, `memmem` for a leading literal |
| 15 | `--exe` compile time: header parse is 0.65 s of ~1.1 s `clang -O2 -c` | — | Reuse `--jit`'s PCH mode |

## For the maintainer

- **A precompiled header for `--exe` compiles (W4-15).** Measured: a trivial
  program's C++ compile 1.00 → 0.68 s with a PCH of Interpreter.h built with
  the same flags (0.77 s once, 37 MB). `--jit`'s PCH is opt-in for exactly this
  size; on by default it would leave 37 MB per build of rakupp in the cache.
  Not built: opt-in, default-on with pruning, or neither.

- **Default `--exe` dispatches every operator by string.** `fastBin` returns
  nothing without `-O` (`Codegen.cpp:3942`): a loop with `if $i %% 3` is
  136 ms by default and 27 ms with `-O`. Either the rt* fast operators become
  unconditional, or `-O` becomes the default for `--exe`. Either way the rt*
  slow path must call `applyArithValue`, not `applyArith`, so a Whatever
  arriving as a value keeps its smartmatch (`Codegen.cpp:1960` vs `:541`, an
  existing `-O` divergence).

## Checked and fine

`--cnp` off costs nothing measurable (one global load per loop entry);
negative results are cached in kernels, cnp and `--exe` kernel guards; the
where-aware cache never evaluates a `where` twice; `rtSinkStmt`, `&?ROUTINE`
and the read-only checks from 1e535cb8 cost nothing outside their own
constructs; 712df990's `DynRestore` is one compare per call; 4d39c136's
identity short-circuit; the plain `.new` path; the undo log is bounded and
compacts; Codegen itself is linear (4000 subs in 0.16 s).
