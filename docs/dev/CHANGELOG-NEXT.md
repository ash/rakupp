# The next release's CHANGELOG entry, collected as work lands

*Emptied at the v5.5.0 tag: everything collected here is in
[CHANGELOG.md](../../CHANGELOG.md). This is not a CHANGELOG entry yet:
[RELEASING.md](RELEASING.md) step 2 writes that at the tag, with the tag's
own gate figures. Add each item as it lands, in the CHANGELOG's own shape —
what changed, what it measured and how, with the commit — so the entry starts
from this page. Move each item into CHANGELOG.md at the tag and delete it
here.*

*Covered so far: d7c6d9d5, 3e97932e and e30e2827 (FLOAT-HEAVY-ISSUES);
e24f05f1 (NUM-UNBOX-PLAN P0).*

---

### Faster

From docs/dev/findings/FLOAT-HEAVY-ISSUES-2026-10-22.md, issues 9, 11, 16, 18,
19 and 20. Figures are ns per call of that file's probe scripts, best of 3,
against a build of v5.5.0 in the same sitting, `--exe -O` unless they say
interpreted.

- **A lexical `my sub` is called directly** (d7c6d9d5). Every call, its own
  recursive ones included, found `&name` in the runtime env and went through
  callCallable; now a call site in its scope reaches the body through a weak
  handle, and takes the old path once that scope has gone. A recursion of
  2**10 leaves 210.6 → 42.5 µs (a top-level sub: 39.9).
- **Native types compile at the untyped code's speed** (d7c6d9d5). `+ - * /` on
  natives have a helper per operator instead of rtNativeArith's dispatch on
  the operator's text, a typed parameter that already conforms is not moved
  or looked up, and a native store compares no type names: `(num $a, num $b
  --> num)` 29.1 → 10.0 (untyped 9.7), `(Num $a, Num $b --> Num)` 22.3 →
  9.7, `num < num` 19.5 → 6.2, `(int $m --> int)` 10.6 → 5.5, a `num` float
  loop 28.5 → 9.5 (untyped 8.7).
- **`.elems`, `.defined` and a one-argument `.push` are answered at the call
  site** (d7c6d9d5), by the same tests methodCall's own first arms make:
  `@a.elems` 24.9 → 8.0, `$x.defined` 9.7 → 6.4, `my @a`, three `.push` and
  return 89.8 → 54.1.
- **A sub with a plain `@` or `%` parameter takes the direct call**
  (d7c6d9d5): `sub (@x)` 16.8 → 3.9 (`sub ($x)`: 3.8). Summing 10 Nums with
  `for @a -> $x` 168.6 → 148.8, with `while` and `@a[$i]` 170.6 → 155.2.
- **Prefix `-` has a Num and machine-Int arm in both engines** (d7c6d9d5): `-$x`
  12.9 → 5.2 compiled (`0e0 - $x`: 6.5), 143.5 → 128.6 interpreted (126.9).
- **A top-level `constant` holding a machine Int is a literal to the int
  kernels** (d7c6d9d5), compiled and interpreted: two constant references in a
  sub 9.2 → 1.5 compiled, 213.9 → 5.7 interpreted (two literals: 1.7 / 5.5).
- **A sub typed `num`, `int`, `Num`, `Int` or `Str` takes the interpreter's
  lean call path** (3e97932e; issue 17), when each argument already is of its
  parameter's type, and an argument that is not goes the general way. Against
  d7c6d9d5, interpreted: `(num $a, num $b --> num)` 387.1 → 152.6 (untyped
  144.8), `(Num $a, Num $b --> Num)` 360.8 → 148.3, `num < num` 245.2 → 143.1.
- **A loop kernel takes `int` and `num` variables** (3e97932e; issue 17), at
  full width: it bails where a result leaves int64, and the native path then
  wraps. Interpreted, the issue-15 float loops 1,024 → 292 and 2,332 → 581
  (untyped 282 and 407), and perf-guard's strscan, which counts in a `my
  int`, 96.4 → 6.0 ms.

### Fixed: silent wrong answers

- **Compiled `-$x` on 0e0 is -0** (d7c6d9d5), as interpreted and in Rakudo; it
  was `0 - $x`, which is +0. An object's `-$o` asks its `.Numeric`, not its
  `.Bridge`, as the interpreter does.
- **A List kept from a literal is written through in compiled code**
  (d7c6d9d5). `f(($a, 2))` with `sub f($x) { $x[0] = 5 }` writes $a. Three ways
  it did not: bundled for it, the program's routines were still native, and
  stored into the List's item instead of through it (a second write then died
  "Cannot modify an immutable List"); a native program handing such a List to
  a module's routine built it of values (the call now bundles the program);
  and a module routine keeping one did the same (it stays interpreted).
- **`fail` in a compiled routine returns its Failure** (d7c6d9d5). The builtin
  decides by the interpreter's routine frame, which compiled code never has,
  so every compiled sub that failed died with the exception: `q(-1) //
  "failed"` printed it and exited 1, with or without -O. In a block closure
  it is aimed at the routine's frame, as `return` is, and while a compiled
  `try` runs it still dies, so `(try q(-1)) // "caught: $!"` catches it.
- **`Num.Rat($epsilon)` walks as Rakudo's does** (d7c6d9d5): `1e-300.Rat(0)` was
  0/1 (Rakudo: 1/10**300), a fractional part below 1e-18 stopped a convergent
  early, and b/d past 2**53 was rounded twice. Where Rakudo's walk never ends
  (`7.01492021304424e-06.Rat(0)` grows Ints until memory runs out) this one
  answers the double's exact value. `.Rat(0)` is not the exact value in
  either engine: `0.1e0.Rat(0)` is 1/10, as in Rakudo, which the findings
  file had wrong. 13,222 (input, epsilon) pairs agree with Rakudo 2026.09.

- **A `$` store in compiled code resets on Nil and checks a declared type**
  (3e97932e), as `=` does interpreted: `my $r = try die "x"` left Nil (Any),
  `my @a = Nil` was `[]` (`[Any]`), `my Int $x; $x = "a"` stored the Str and
  `$x = Nil` the Nil. In a bundle, a module-mode routine's store into one of
  the interpreter's variables asks its declaration.
- **`-O` answers as plain `--exe` and the interpreter do** (3e97932e): `~` and
  an interpolation join two Strs NFC (`"e" ~ "\x[301]"` was two code points);
  `my ($a, $b) = $j` iterates the List in `$j` (it bound it to `$a`) and a
  Hash gives its pairs; `!=` between an unsigned native and a negative signed
  one is False, as in Rakudo; `my $bound := $q` is no longer compiled as a
  copy (it bundles).
- **Instant and Duration arithmetic answer Rakudo's types** (3e97932e):
  `-$duration` and `abs $duration` are Durations (were Rats), `0 - $duration`
  and `7 % $duration` Nums (were Durations), `-$instant` a Num; compiled, no
  operator kept the tag (`$d - 0` was a Rat).
- **`.push($s)` and `.unshift($s)` slip a Slip held in `$s`** (3e97932e), as
  Rakudo does; the `unshift` sub is the method (it answered the new length,
  slipped nothing and checked no element type).
- **`my ($k, $v) = %h` takes the pairs** (3e97932e); `$k` held the Hash.
- **`fail` in a block that another routine runs returns from the routine it
  was written in** (3e97932e), as a `return` does, in both engines: `sub q {
  ap({ fail "z" }); … }` returned the Failure from `ap`, and `q` sank it and
  died.
- **`f(($a, 2))` writes `$a` from a sub's second call on** (3e97932e): the
  interpreter's lean call path dropped what keeps the List's containers, and
  `$x[0] = 5` died "Cannot modify an immutable List".

- **A compiled routine applies its `--> T`** (e30e2827): `sub r(--> Int) {
  "x" }` answered "x" with or without -O, `--> Str()` did not coerce and
  `--> Int:D` passed Int. Every way out is checked, in top-level, lexical and
  anonymous subs, multi candidates, methods and pointy blocks, as the
  interpreter checks it; a value of a core type passes inline, at no cost
  measurable against the unchecked call (`--> Num` -O: 10.6 → 10.3 ns).
- **A write through a kept List into a typed variable is checked**
  (e30e2827), in both engines: `my Int $a; sub f($x) { $x[0] = "s" };
  f(($a, 2))` stored the Str, and a Nil there stayed Nil instead of
  resetting to the default.

- **A compiled loop's Num division by zero fails** (e24f05f1), as interpreted
  and in Rakudo, with X::Numeric::DivideByZero. The unboxed loop lane, on with
  `-O` and without, divided doubles and answered Inf: `while … { $t = 1e0 /
  $x }` with `$x` 0e0 returned Inf, and `$s = $s + 1e0 / $x` ran on where it
  dies. The lane bails on a zero divisor now, -0e0 and an Int 0 included.
- **A compiled loop of Nums alone leaves its lane correctly** (e24f05f1). A
  lane with no Int slot had no landing for a bail, so a Num `**` that
  underflows ended the loop with the values it held at that point: `$t = $x
  ** ($k + 1e0)` with `$x` 1e-200 answered 1e-200, where Rakudo gives an
  X::Numeric::Underflow Failure.

### Fixed: crashes

- **A worker thread's teardown no longer races the cycle collector**
  (e30e2827). A worker gave its count back at the end of its body, before its
  closure, ExecContext and frame pools were let go, and a collection on the
  main thread could walk containers that teardown was freeing: Roast's
  S07-hyperrace/basics.t segfaulted about one run in six. 300 runs, none.
- **`-O` compiles a `--> Int` sub of no parameters that is an int kernel**
  (e30e2827); its kernel guard named an `__a` its direct overload has not got.

### Left open

- Issues 2, 4 and 14 still bundle the mainline; 10 (a `use`d module's sub
  costs ~6× compiled) and 21 (no array-element loop lane) are unchanged.
- Found and open, the same on v5.5.0: `sub f(int $x) { $x * 2**62 }`
  answers a BigInt where Rakudo wraps; a `--> num` routine returning a Str
  hands it back where Rakudo dies; a `fail` in a block written at the
  mainline returns from the routine that runs it (Rakudo dies); and
  t/jit/run.raku's tier-up cases calls, elements, inline, methods and
  natives enter no kernel.
- t/exe/fuzz.raku's 20 `*-shadow` programs, which failed at parse time on
  v5.5.0, were not valid Raku (Rakudo refuses them too); the generator now
  writes valid ones, and all 232 programs agree.
