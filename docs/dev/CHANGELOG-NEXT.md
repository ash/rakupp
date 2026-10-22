# The next release's CHANGELOG entry, collected as work lands

*Emptied at the v5.5.0 tag: everything collected here is in
[CHANGELOG.md](../../CHANGELOG.md). This is not a CHANGELOG entry yet:
[RELEASING.md](RELEASING.md) step 2 writes that at the tag, with the tag's
own gate figures. Add each item as it lands, in the CHANGELOG's own shape —
what changed, what it measured and how, with the commit — so the entry starts
from this page. Move each item into CHANGELOG.md at the tag and delete it
here.*

*Covered so far: d7c6d9d5 (FLOAT-HEAVY-ISSUES).*

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

### Fixed: crashes

### Left open

- Issues 2, 4 and 14 still bundle the mainline; 10 (a `use`d module's sub
  costs ~6× compiled), 17 (a `num` sub off the interpreter's lean call path)
  and 21 (no array-element loop lane) are unchanged.
- On v5.5.0 as on this branch: t/exe/fuzz.raku fails 20 of its `*-shadow`
  shape programs at parse time ("Lexical symbol '$ca' is already bound to an
  outer symbol"); t/jit/run.raku's tier-up cases calls, elements, inline,
  methods and natives enter no kernel; and t/exe/run.raku finds -O answering
  differently on nfc-join-window.raku and
  typed-subtree-native-and-rat-leaves.raku. Compiled, `my $r = try die "x"`
  leaves $r Nil (interpreted and in Rakudo: Any).
