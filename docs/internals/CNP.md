# `--cnp` from the inside — the stencils, and five worked examples

> `--cnp` is **work in progress** and the spelling is provisional. It and
> `--jit` are one feature with two backends; the expectation is that this one
> becomes the default and `--jit` goes. See
> [guide/JIT.md](../guide/JIT.md) before depending on the flag name.

Three pages, three jobs:

| | answers |
|---|---|
| [guide/JIT.md](../guide/JIT.md) | should I turn it on, what tiers up, what it is worth |
| [dev/plans/CNP-PLAN.md](../dev/plans/CNP-PLAN.md) | why it is built this way, what the four hard parts cost, what is next |
| **this page** | **what is in it**: every stencil, helper and operator index, and the path from Raku source to the bytes that run |

Everything below was read out of this build rather than remembered:
`v4.0.1-57-gd1316d5`, arm64-darwin, Release `-O3 -DNDEBUG`. The commands that
produced each transcript are shown with it, so any of it can be re-taken.

---

## 1. Build time and run time

A *stencil* is a snippet of machine code with holes in it, compiled when rakupp
was built and carried in the binary as bytes. Compiling a loop means copying
snippets into a page of memory and filling the holes with this run's values.
No compiler runs, nothing is written to disk, and the **first** run is the fast
one.

```
BUILD TIME (once, on the machine building rakupp)

  src/cnp/stencils.c  --cc-->  cnp_stencils.o  --cnp-extract-->  CnpStencils.cpp
  53 C functions               one object file                   53 byte arrays
  with undefined                                                 + their holes
  _JIT_* symbols                                                 (.rodata, 5,324 B)

RUN TIME (in the program that turned --cnp on)

  hot loop  --Jit.cpp-->  eligible?  --Cnp.cpp-->  IR ops  --CnpEmit.cpp-->  one mmap
            the counter,             the lowering          copy + patch      [ code ][ veneers ][ GOT ]
            the whitelist
```

Four files, and only one of them knows about more than its own layer:

| file | knows about | knows nothing about |
|---|---|---|
| `src/cnp/stencils.c` | Raku semantics of one operation | object formats, encodings |
| `tools/cnp-extract.cpp` | Mach-O and ELF | instruction encodings, Raku |
| `src/cnp/CnpEmit.cpp` | arm64/x86-64 encodings, executable memory | Raku |
| `src/Cnp.cpp` | Raku | everything under it |

They meet at [`src/cnp/CnpTable.h`](../../src/cnp/CnpTable.h) — an abstract
patch kind and which hole it wants — and at
[`src/cnp/CnpAbi.h`](../../src/cnp/CnpAbi.h), the one header both the C
stencils and the C++ emitter include, so the two cannot drift.

The extractor runs on the machine that *builds* rakupp, so it only ever reads
the host's own object format; there is no cross-format case. A table it could
not produce is **empty**, not wrong, and `--cnp` then says so and runs
interpreted:

```bash
rakupp -V
```

```
Cnp     copy-and-patch stencils for arm64
```

---

## 2. The register file

A kernel does **not** run on `Value` objects. It runs on a flat register file:
`r[k]` is an int64, `t[k]` says what that means, and anything that cannot be a
machine word lives in `boxes[k]` as a real `Value`.

| tag | value | `r[k]` holds |
|---|---:|---|
| `RK_T_INT` | 0 | a signed 64-bit integer |
| `RK_T_NUM` | 1 | the bit pattern of a `double` |
| `RK_T_BOOL` | 2 | 0 or 1 |
| `RK_T_BOX` | 3 | nothing — the value is `boxes[k]` |

The order is load-bearing: `t <= RK_T_NUM` is the single compare every
arithmetic stencil opens with, meaning "this is a machine number".

What a slot becomes at kernel entry, and the three things that cannot be a
register (`setReg`, [src/Cnp.cpp](../../src/Cnp.cpp)):

| | why |
|---|---|
| a bignum `Int` | cannot be an int64 at all |
| anything with an `enumName` | the name is the value's identity: `Less` kept as `-1` would print as `-1` the moment it was copied into another container |
| a `Str`, a `Rat`, a `Complex`, an object | stays a `Value` in `boxes[k]` |

`natBits`/`natFloat` are deliberately **not** excluded: they describe what
happens when something is *stored* into that container, and the harness already
refuses a native container a kernel would write. Reading one is fine.

The frame every stencil is handed:

| field | |
|---|---|
| `r`, `t` | the register file and its tags — also passed as arguments, so the hot paths never load through the frame |
| `boxes` | `Value[]`, one per register |
| `consts` | the kernel's constant pool: every `Str`, `Rat`, `Complex` and bignum literal |
| `interp` | the `Interpreter*` the helpers run against |
| `err` | `std::exception_ptr*` — where a helper stashes a throw |

**The rule the whole design rests on:** *no helper a stencil calls may throw.*
A code buffer has no unwind tables, so a throw crossing one reaches
`std::terminate`. Every `rk_cnp_*` function wraps its body in a catch-all, puts
the exception in the frame and returns a status; the stencil returns
`RK_CNP_ERR`; and because every stencil `musttail`-calls the next, the whole
kernel is **one machine frame**, so that return lands straight in the
trampoline, which is ordinary C++ and rethrows there.

---

## 3. The list

### 3.1 The 53 stencils

Sizes are this build's, in bytes of arm64 code (÷4 for instructions), and they
cover the **whole** stencil — the Int lane, the Num lane and the bail-out. The
path actually executed is a fraction of it. "Holes" is how many relocations the
extractor found in it.

`d`, `a`, `b` are register numbers; `imm` is a literal folded into the
instruction stream; `k` is an index into the constant pool; `op` is an index
into the operator table (§3.3).

**Constants and moves**

| stencil | holes | does | bytes |
|---|---:|---|---:|
| `rk_st_loadi` | 5 | `r[d] = imm`, tag Int | 28 |
| `rk_st_loadn` | 5 | `r[d] = imm`, tag Num (the bit pattern) | 32 |
| `rk_st_loadb` | 5 | `r[d] = 0/1`, tag Bool | 32 |
| `rk_st_move` | 6 | `r[d] = r[a]`; a boxed source bails — a `Value` copy runs a destructor | 48 |

**Arithmetic** — Int/Int first, then the lane where either side is a Num.
Anything else, *including an Int/Int that overflowed*, bails to the cold block.

| stencil | holes | does | bytes |
|---|---:|---|---:|
| `rk_st_add` | 11 | `r[d] = r[a] + r[b]`, overflow-checked | 156 |
| `rk_st_sub` | 11 | `r[d] = r[a] - r[b]` | 156 |
| `rk_st_mul` | 11 | `r[d] = r[a] * r[b]` | 164 |
| `rk_st_addi` | 13 | `r[d] = r[a] + imm` | 108 |
| `rk_st_incr` | 9 | `r[d] += imm`, in place: `$i++`, `$i--`, `$i += 3` | 84 |
| `rk_st_neg` | 9 | `r[d] = -r[a]` | 88 |
| `rk_st_not` | 6 | `r[d] = !r[a]`, tag Bool | 80 |

**Comparison, value form** — leaves a Bool in a register, for `my $b = $x < $y`.

| stencil | holes | does | bytes |
|---|---:|---|---:|
| `rk_st_cmplt` `cmple` `cmpgt` `cmpge` `cmpeq` `cmpne` | 8 each | `r[d] = (r[a] OP r[b])`, tag Bool | 140–144 |

**Comparison, fused with the branch** — no Bool is ever built. `while $i < $n`
is *one* stencil holding the test and the back edge.

| family | holes | does | bytes |
|---|---:|---|---:|
| `rk_st_jlt` `jle` `jgt` `jge` `jeq` `jne` | 8 each | branch to the target when `r[a] OP r[b]` | 132 |
| `rk_st_jlti` `jlei` `jgti` `jgei` `jeqi` `jnei` | 9–10 | the same against a literal Int | 80–84 |
| `rk_st_jnlt` `jnle` `jngt` `jnge` `jneq` `jnne` | 8 each | branch when **not** `r[a] OP r[b]` | 132 |
| `rk_st_jnlti` `jnlei` `jngti` `jngei` `jneqi` `jnnei` | 9–10 | the same against a literal Int | 80–84 |

The negated families exist because `while COND {…}` and `if COND {…}` both want
"branch **out** when the condition is false". They are written `!(a < b)` and
not `a >= b` **on purpose**: those are different questions once an operand is
NaN, and inverting the operator to save twelve stencils would have quietly
changed what a `Num` loop does at its edges
([`t/cnp/cases/nan-compare.raku`](../../t/cnp/cases/nan-compare.raku)).

**Control flow**

| stencil | holes | does | bytes |
|---|---:|---|---:|
| `rk_st_jmp` | 1 | unconditional branch — one instruction | 4 |
| `rk_st_jt` | 5 | branch if `r[a]` is true (nonzero; a box asks the interpreter) | 60 |
| `rk_st_jf` | 6 | branch if `r[a]` is false | 64 |
| `rk_st_jdef` | 4 | branch if `r[a]` is **defined** — `//` asks that, not truth, so `0 // 9` is `0`. An unboxed register is always defined, so the fast lane is an unconditional branch | 28 |
| `rk_st_ret` | 0 | the loop finished: `return RK_CNP_OK` | 8 |

**Cold stencils** — these call helpers, so they have frames. The emitter lays
them out past the end of the loop, where the prefetcher never goes unless a
guard actually failed.

| stencil | holes | does | bytes |
|---|---:|---|---:|
| `rk_st_binop` | 10 | `r[d] = r[a] op r[b]` through `applyArith` | 112 |
| `rk_st_unop` | 8 | `r[d] = op r[a]` | 104 |
| `rk_st_loadk` | 6 | `r[d] = consts[k]` — always boxed, always a helper: a `Str` or a `Rat` is a C++ object with a destructor | 96 |
| `rk_st_movebox` | 6 | `r[d] = r[a]` when the source is boxed | 96 |
| `rk_st_jtslow` | 5 | the boxed lane of `jt` | 120 |
| `rk_st_jfslow` | 5 | the boxed lane of `jf` | 120 |
| `rk_st_jdefslow` | 5 | the boxed lane of `jdef` | 120 |

**53 stencils, 5,324 bytes of template code in total** — the whole backend's
machine code, sitting in `.rodata` and costing nothing in a run that never
turns the flag on.

### 3.2 The helpers

The interface a stencil may reach, and the reason the no-throw rule is
affordable: the eligibility whitelist is small enough that this list is short.
Each is `extern "C"`, wrapped in a catch-all, returning a status.

| helper | called by | does |
|---|---|---|
| `rk_cnp_binop` | `rk_st_binop` | `applyArith(op, a, b)` — the interpreter's own operator dispatcher. `~=` into a boxed accumulator is special-cased to `rtCatAssign`, because the general path copies the string out and back and makes an append loop O(n²) |
| `rk_cnp_unop` | `rk_st_unop` | prefix `-`, `+`, `!`, `?`, `~`, `+^`, verbatim from what `Codegen` emits |
| `rk_cnp_loadk` | `rk_st_loadk` | copy a constant-pool entry into a register |
| `rk_cnp_move` | `rk_st_movebox` | copy a boxed register |
| `rk_cnp_truthy` | `rk_st_jtslow`, `rk_st_jfslow` | `Interpreter::boolify` |
| `rk_cnp_defined` | `rk_st_jdefslow` | `rtIsDefined` |

`CnpAbi.h` declares a seventh, `rk_cnp_cmp`; no stencil in this build
references it, so the extracted table names **six**
(`kHelperCount = 6`) — the cold lane of a comparison goes through
`rk_cnp_binop` and then a `jt`/`jf` pair instead.

Every one of them runs the **same** code an interpreted program would have
reached. That is what makes the differential gate meaningful rather than
circular: an overflow into a bignum, a `Rat` division, a coercion and a type
error are all decided by `applyArith`, not by a second opinion.

### 3.3 The operator table

One table, read from both ends: the lowering turns an operator into an index,
`rk_cnp_binop` turns the index back into the string `applyArith` dispatches on.
`RK_OP__COUNT` is 42, and a `static_assert` keeps the enum and the name table in
step.

| | operators |
|---|---|
| arithmetic | `+` `-` `*` `/` `%` `**` `div` `mod` `%%` `gcd` `lcm` |
| numeric comparison | `<` `<=` `>` `>=` `==` `!=` `<=>` |
| string comparison | `eq` `ne` `lt` `gt` `le` `ge` `leg` `cmp` |
| string | `~` `x` |
| bitwise | `+&` `+\|` `+^` `+<` `+>` |
| other infix | `min` `max` `^^` (and `xor`, its word form) |
| the unary pseudo-ops | `neg` `plus` `not` `so` `bnot` `str` |

`&&`, `and`, `||`, `or` and `//` are **not** here: they are control flow, not
operators. They lower to a branch and yield an **operand**, not a Bool.

### 3.4 The patch kinds

What the extractor hands the patcher, per hole. The `Sym` says *which* value
(`Cont`, `Target`, `Slow`, `Op0`…`Op3`, `Helper`); the `Patch` says how to write
it.

| kind | |
|---|---|
| `Call` | a direct branch — arm64 `BL`/`B` imm26, x86-64 `call`/`jmp` rel32 |
| `Adrp21` / `AddOff12` | arm64: the page of the value, and its offset in that page |
| `GotAdrp21` / `GotOff12` | the same, but of the GOT slot holding the value |
| `Rel32` / `GotRel32` | x86-64 rip-relative displacements |
| `Abs64` | a plain 64-bit word |

### 3.5 What the lowering accepts

The **eligibility** whitelist is `--jit`'s, unchanged (`Scan` in
[src/Jit.cpp](../../src/Jit.cpp)); see
[guide/JIT.md](../guide/JIT.md#what-tiers-up). The **lowering** is narrower
still, and refuses rather than guesses:

| | |
|---|---|
| statements | `ExprStmt`, `Block`, `IfStmt` with `elsif`/`else`, nested `while`/`until`/C-style `loop`, unlabelled `last`/`next` |
| literals | Int, Num, Rat, Complex, Str, Bool — a Rat, a Complex, a bignum or a string goes to the constant pool |
| variables | plain `$` scalars, `my $x` without a type or default |
| assignment | `=` and every compound form |
| operators | `+ - *` with fast lanes, everything else in §3.3 through `applyArith`, the six comparisons, prefix/postfix `++`/`--`, unary `- + ! not ? ~ +^` |
| short circuits | `&&` `and` `\|\|` `or` `//` as real branches yielding an operand |
| the ternary | `?? !!` |
| nothing else | and a `ListExpr` only in a C-style loop's header, where a comma is a sequence of side effects |

Two ceilings: 4,096 ops and 1,024 registers per kernel (`kMaxOps`, `kMaxRegs`).
A loop past either is refused, which costs nothing.

When it refuses, the reason is the string `--cnp=verbose` prints. What the
lowering itself can say:

```
an expression the lowering has no stencil for      a statement the lowering has no stencil for
operator 'OP'                                      prefix operator 'OP'
compound operator 'OP='                            a list in value position
an interpolated string                             assignment to a non-variable
++/-- on a non-variable                            a compound assignment to a declaration
a `last` outside the kernel's loops                a `next` outside the kernel's loops
the loop lowers to more ops than a kernel holds    the loop needs more registers than a kernel holds
too many variables                                 the assembler refused: …
```

Two more — `variable $x has no register` and `more variables than the register
pre-count found` — are internal invariants and should never be seen; if one is,
the pre-pass that counts declarations and the pass that hands out registers
have gone out of step.

---

## 4. Worked example 1 — a `while` loop, end to end

[`t/jit/cases/basic-while.raku`](../../t/jit/cases/basic-while.raku):

```raku
my $s = 0; my $i = 0;
while $i < 200000 { $s = $s + $i; $i = $i + 1 }
say "$s $i";
```

```bash
build/rakupp --cnp=verbose,stats t/jit/cases/basic-while.raku
```

```
[cnp] on — copy-and-patch for arm64, threshold 100
[cnp] loop at line 3 is eligible, 2 slot(s)
[cnp] loop at line 3 lowered to 16 ops, 10 registers, 16384 bytes
19999900000 200000
[cnp] examined 1, eligible 1, compiled 1, failed 0, kernels entered 1
```

**The ten registers.** Two slots (`$s`, `$i`) become registers 0 and 1 by
position; the pre-count reserves four more named registers as slack; the
lowering hands out temporaries above them. Registers 2–5 go unused in this
loop.

**The sixteen ops.** The hot ones come first, in order, and every cold block is
appended afterwards — so the vector is already partitioned and no renumbering is
needed.

| # | stencil | operands | |
|---:|---|---|---|
| 0 | `jnlti` | `a=r1` `imm=200000` | `!(r1 < 200000)` → exit at 6. Guard miss → 7 |
| 1 | `add` | `d=r8` `a=r0` `b=r1` | `$s + $i`. Overflow or a non-number → 11 |
| 2 | `move` | `d=r0` `a=r8` | the assignment to `$s`. Boxed source → 12 |
| 3 | `addi` | `d=r8` `a=r1` `imm=1` | `$i + 1` → 13 |
| 4 | `move` | `d=r1` `a=r8` | the assignment to `$i` → 15 |
| 5 | `jmp` | → 0 | the back edge |
| 6 | `ret` | | the loop ran out |
| 7 | `loadi` | `d=r6` `imm=200000` | *cold block for 0*: the literal has to become a register |
| 8 | `binop` | `d=r7` `a=r1` `b=r6` `op=<` | the comparison through `applyArith` |
| 9 | `jf` | `a=r7` | the same decision the fast lane made |
| 10 | `jfslow` | `a=r7` | …and its own boxed lane |
| 11 | `binop` | `d=r8` `a=r0` `b=r1` `op=+` | *cold block for 1* |
| 12 | `movebox` | `d=r0` `a=r8` | *cold block for 2* |
| 13 | `loadi` | `d=r9` `imm=1` | *cold block for 3* |
| 14 | `binop` | `d=r8` `a=r1` `b=r9` `op=+` | |
| 15 | `movebox` | `d=r1` `a=r8` | *cold block for 4* |

Those sixteen stencils are **1,224 bytes** of copied code. The `16384` in the
verbose line is the *mapping*: one 16 KB page, which is the minimum an `mmap`
can be, and which also holds the veneers and any GOT slots. (A page per kernel
is a real cost and pooling them is CNP-PLAN's P2.)

**Entry and exit.** The interpreter reaches an iteration boundary, checks that
no other Raku thread is live, binds the two containers, and calls the buffer.
`cnp::run` unboxes each slot into its register, runs the kernel, and writes the
**written** slots back — on both exits, including the one where the loop died,
because a `CATCH` outside is about to read them.

Two things the lowering does **not** do here are worth naming, because both
were bugs first:

- **The outermost loop's init is not emitted.** A kernel is entered *after* the
  interpreter has run `my $i = 0`. The first draft walked the init anyway and
  restarted the loop; the gate caught it as exactly one extra iteration.
- **`next` in a C-style `loop` jumps to the step, not to the test.** Rewriting
  a three-clause loop as a `while` would silently skip the increment.

## 5. Worked example 2 — one stencil, before and after the patch

This is the loop condition above, as the C compiler left it at build time:

```bash
otool -tvV build/generated/cnp_stencils.o | sed -n '/_rk_st_jnlti:/,/_rk_st_jnlei:/p'
```

```
_rk_st_jnlti:
   adrp x8, 0        ; ← hole: OP0, the register number
   ldr  x8, [x8]     ;   (an ADRP/LDR pair through the GOT)
   ldrb w9, [x2, x8] ; t[a]
   cmp  w9, #0x1
   b.eq 0xf68        ; → the Num lane
   cbnz w9, 0xf88    ; neither Int nor Num → the cold block
   ldr  x8, [x1, x8, lsl #3]   ; r[a]
   adrp x9, 0        ; ← hole: OP1, the loop bound
   ldr  x9, [x9]
   cmp  x8, x9
   b.ge 0xf84        ; !(a < k) → the branch target
   b    0xf64        ; ← hole: _JIT_CONT, the next stencil
   …the Num lane…
   b    0xf84        ; ← hole: _JIT_TARGET
   b    0xf88        ; ← hole: _JIT_SLOW
```

The three branches at the end go to their own addresses in the raw listing —
`b 0xf64` sits *at* `0xf64`. They are not infinite loops: they are relocations,
which is exactly what a hole is. The compiler had no idea what `_JIT_CONT` was,
so it emitted an ordinary branch, pointed it at itself and left a relocation
behind; the extractor lifted that into `(offset, Call, Cont)` and the patcher
writes the real displacement.

What the patcher does to the two ADRP/LDR pairs is where most of the win is.
The compiler had to assume an arbitrary symbol, so it reached it through the
GOT — two instructions and a **load**. By patch time the value is known, and if
it fits in 32 bits the same two slots hold `MOVZ` + `MOVK`:

```
adrp x9, <page>          movz x9, #0x0d40          ; 200000 & 0xffff
ldr  x9, [x9, #off]  →   movk x9, #0x0003, lsl #16 ; 200000 >> 16
```

No GOT slot, and no memory access at all on the hot path. Register numbers and
loop bounds almost always qualify; a `double`'s bit pattern or a large literal
does not, and falls back to a GOT slot in the same mapping.

The fold is refused unless the two instructions are **adjacent** and all three
register fields agree (`ADRP Xd`, then `LDR Xt, [Xn]` with `d == n == t`) — and
unless a low-half hole for the same symbol really sits at the next instruction.
Anything looser and the ADRP's scratch register might be something another
instruction in the stencil is still reading, which would be a wrong answer
rather than a slow one.

**Why nothing is ever out of range.** One mapping holds the lot:

```
[ stencil code, one block per op ] [ veneers ] [ GOT slots ]
```

so a branch between stencils is always inside the ±128 MB a `B` can express,
and an `ADRP` always reaches the GOT area, which is kilobytes away rather than
wherever the heap landed relative to the executable. A **veneer** handles the
other direction: a stencil calling `rk_cnp_binop` is calling into the host
executable, which may be anywhere, so it branches to a three-word thunk in the
buffer that loads the real address and jumps. It is cold, and a path that never
calls a helper never touches it.

The buffer is mapped read/write, patched, then flipped to read/execute with
`mprotect`, **per region** — which is what W^X asks for and what Apple silicon
enforces, with no entitlement and no `MAP_JIT`. (`MAP_JIT` plus
`pthread_jit_write_protect_np` is the fallback and deliberately not the default:
that call toggles write protection for every `MAP_JIT` mapping in the process
and for the *calling thread only*, so patching one kernel while another thread
runs a second one would be a race by construction.)

Two more for scale, at the ends of the range:

```
_rk_st_jmp:   b _rk_st_jmp      ; one instruction: the whole stencil is its hole
_rk_st_ret:   mov w0, #0x0      ; RK_CNP_OK …
              ret               ; … straight back to the trampoline
```

## 6. Worked example 3 — the cold lane, where a loop leaves its fast path

[`t/cnp/cases/int-overflow-loop.raku`](../../t/cnp/cases/int-overflow-loop.raku)
doubles an `Int` eighty times, which leaves int64 at iteration 63:

```raku
my $x = 1; my $i = 0; my $sum = 0;
while $i < 80 { $x = $x * 2; $sum = $sum + $x; $i = $i + 1 }
say $x; say $sum; say $x.WHAT.^name;
```

Eighty iterations is under the default threshold of 100, so the gate's spelling
is what forces it to lower:

```bash
build/rakupp --cnp=threshold=0,verbose,stats t/cnp/cases/int-overflow-loop.raku
```

```
[cnp] on — copy-and-patch for arm64, threshold 0
[cnp] loop at line 5 is eligible, 3 slot(s)
[cnp] loop at line 5 lowered to 21 ops, 11 registers, 16384 bytes
1208925819614629174706176
2417851639229258349412350
Int
[cnp] examined 1, eligible 1, compiled 1, failed 0, kernels entered 1
```

```bash
build/rakupp t/cnp/cases/int-overflow-loop.raku     # byte for byte the same
```

What happened at iteration 63: `rk_st_mul`'s `__builtin_mul_overflow` reported
an overflow, the stencil tail-called its cold block, `rk_cnp_binop` handed both
operands to `applyArith`, and `applyArith` promoted to a bignum exactly as it
does for an interpreted program. `setReg` could not fit the result in a
register, so it went to `boxes[k]` with tag `RK_T_BOX` — and every later
iteration took the cold lane too, because a boxed operand fails the very first
guard. The loop kept running; it just stopped being fast, which is the correct
trade.

The same shape covers the rest of Raku's arithmetic: `1/3` is a `Rat` and stays
one, `3.14` is a `Rat` literal and not a `Num`, a `Str` never leaves the box
array. Each has a case of its own in
[`t/cnp/cases/`](../../t/cnp/cases/) — `mixed-numeric`, `boxed-registers`,
`string-loop`, `enum-in-register`, `bool-and-shortcircuit`,
`ternary-and-unary`, `nan-compare`, `hoisted-throw`.

`hoisted-throw` is the one to read if you read one: a `div` by zero at
iteration five has to leave `i=5 n=22 d=0` behind for the `CATCH`, which means
carrying an error out of machine code that has no unwind tables — the status
return, the single frame and the write-back on both exits, all in one program.

## 7. Worked example 4 — the three ways a loop is turned down

Being refused costs nothing: the loop stays interpreted and behaves as it always
did. There are three different places it can happen, and `--cnp=verbose` names
each.

**(a) The whitelist, at the loop.** A call, a method, an index, a regex,
`return`, `die`, a phaser, `state`, a closure — checked once and never asked
again ([`t/jit/cases/ineligible.raku`](../../t/jit/cases/ineligible.raku)):

```
[cnp] loop at line 7 not eligible: an expression the kernel whitelist does not cover
```

**(b) The container guards, at the moment of entry.** A slot the kernel would
**write** is refused if its container has behaviour a direct assignment would
skip — a native width, a readonly binding, a coercion, a default, an `is rw`
write-through, `is dynamic`. A slot it only reads is exempt, which is what lets
a loop in a routine use its own parameters
([`t/jit/cases/typed-slot.raku`](../../t/jit/cases/typed-slot.raku), where
`my int8 $n` must wrap at eight bits and a kernel would not):

```
[cnp] loop at line 8 is eligible, 2 slot(s)
[cnp] loop at line 8 lowered to 17 ops, 10 registers, 16384 bytes
[cnp] slot $n is a native or readonly container the kernel would write — staying interpreted
44 300
[cnp] examined 1, eligible 1, compiled 1, failed 0, kernels entered 0
```

The kernel was built and then not entered — `compiled 1, kernels entered 0` is
what that looks like in the stats line.

**(c) Another Raku thread is live.** This is the one thing hoisting gives up,
and it was found by the corpus gate rather than by reasoning
([`t/cnp/cases/shared-flag.raku`](../../t/cnp/cases/shared-flag.raku)):

```raku
my $stop = False;
my $worker = start { my $n = 0; until $stop { $n = $n + 1 } };
loop (my $i = 0; $i < 500; $i++) { … }
$stop = True;
```

```
[cnp] loop at line 13 not entered while another thread is live — its variables are shared
[cnp] loop at line 17 not entered while another thread is live — its variables are shared
the worker saw the flag
main ran 500 times
```

The kernel would read `$stop` once into a register, and the write it is waiting
for would land in the container it stopped looking at. So a kernel is not
entered at all while `liveWorkers_` or `cuedLoads_` is above zero. The test
comes **before** the slot binding, because the interpreter asks again on every
iteration and a refusal has to cost one relaxed load; the site is not retired,
so a program that joins its workers gets its kernel back. It is conservative in
one direction — a loop inside a `start` block never runs a kernel, because that
block's own thread is counted.

There is a fourth, checked inside `cnp::run` because none of the harness's
guards can see it: **two names bound to a single container** would be two
registers here and one cell there, and the write-back would drop whichever went
last. A pairwise scan over the handful of slots catches it, and the site is
retired.

## 8. Worked example 5 — what it comes to here

Same machine as everything above, minimum of five runs, load average 8.56 while
they were taken — so these are factors, not percentages, and they belong in this
page rather than in BENCHMARKS.md until they are re-taken under the
quiet-machine protocol.

| | plain | `--cnp` |
|---|---:|---:|
| a 5M-iteration integer `while` | 961 ms | 46 ms |
| [`examples/mandel.raku`](../../examples/mandel.raku) | 120 ms | 76 ms |

These reproduce CNP-PLAN's 2026-09-19 figures (959 / 45 and 121 / 78) on a
differently loaded machine.

The Mandelbrot row reads as 1.6× and that is honest, but it needs reading:
`--exe -O` compiles the *whole* program ahead of time and the resulting binary
still takes 75 ms on this machine, so nearly all of that 120 ms is startup and
output rather than the loop. (CNP-PLAN, measuring the loop itself rather than
the program, puts it at roughly 50 ms interpreted against roughly 7 ms tiered.)
Its stats line says where the work went:

```bash
build/rakupp --cnp=stats examples/mandel.raku
```

```
[cnp] examined 3, eligible 1, compiled 1, failed 0, kernels entered 2211
```

One kernel — the innermost escape-time `loop (…; $k < 112; …)` — entered 2,211
times. The picture is 75 × 30 = 2,250 pixels, and the difference is the pixels
computed before the site passed the threshold. That count is also the answer to
whether kernel *entry* is expensive. It is not: a kernel costs about 15 µs to
produce (measured over 4,000 of them), and an entry is a slot bind, a register
file and the write-back.

## 9. Looking at it yourself

```bash
rakupp -V                                    # which stencils this binary has, and for what
rakupp --cnp=verbose,stats prog.raku         # every decision, then one summary line
rakupp --cnp=threshold=0 prog.raku           # lower every eligible loop on its first iteration
rakupp --bundle --cnp prog.raku -o prog      # bake it into a single file; RAKUPP_CNP steers one run
```

`--cnp=SPEC` takes `off`, `on`, `verbose`, `stats` and `threshold=N`. There is
no `sync`, because there is no background compile to wait for, and no
`nocache`, because there is nothing on disk.

The gate, which runs every program in `t/jit/cases`, `t/cnp/cases`,
`t/regression` and `examples` twice by the same binary — once plainly, once at
`threshold=0` — and compares stdout, stderr and exit status byte for byte:

```bash
build/rakupp t/jit/run.raku --cnp
```

A case that agrees only because nothing compiled is not evidence, so every case
whose first line is not `# JIT: refused` or `# CNP: refused` must also be shown
to have entered a kernel.

To read the machine code rather than the IR:

```bash
otool -tvV build/generated/cnp_stencils.o    # macOS; objdump -d elsewhere
sed -n '/kStencilNames/,$p' build/generated/CnpStencils.cpp
```

And `tools/run-engines.raku` prints the per-kernel picture across every engine,
with a column saying whether each kernel was refused, never counted, or built.

## 10. What it does not do

Short version; [CNP-PLAN.md](../dev/plans/CNP-PLAN.md) has the long one and the
phases.

- **It has run on one instruction set.** arm64. The x86-64 patcher is written,
  is known to be *wrong* rather than merely unexercised — CI entered a kernel
  and got the wrong answer — and is refused at startup until that is fixed.
  `RAKUPP_CNP_X86=1` lifts the gate for whoever fixes it.
- **It does not tier up threaded code**, for the reason in §7(c). That is a real
  functional gap against `--jit`, which does not hoist.
- **It reaches the most common loop shape, not the most common loop sources.**
  Only `while`, `until`, the C-style `loop` and a `for` over a Range of integers
  are counted at all. A `for` over an array, over `.kv`, over a lazy sequence or
  as a statement modifier is not refused — it is never examined — and neither is
  any `.map`.
- **It is not more general than `--jit`.** The 53 stencils are the ceiling.
- **It is not a speculating JIT.** No type feedback and no deopt path: the tags
  are checked per operation, which is what lets a guard miss fall back *within*
  the kernel instead of leaving it.
- **A kernel costs a page**, so forty kernels is about 640 KB of address space.

## See also

- [guide/JIT.md](../guide/JIT.md) — the user-facing view of both backends.
- [dev/plans/CNP-PLAN.md](../dev/plans/CNP-PLAN.md) — the design, the four costs
  it named, the gates and the phases.
- [internals/OPTIMIZATION.md](OPTIMIZATION.md) — the `-O` passes `--jit`'s
  kernels are emitted with.
- [guide/CLI.md](../guide/CLI.md) — the flag among the rest of the command line.
