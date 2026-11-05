# The next release's CHANGELOG entry, collected as work lands

*Emptied at the v5.6.2 tag: everything collected here is in
[CHANGELOG.md](../../CHANGELOG.md). This is not a CHANGELOG entry yet:
[RELEASING.md](RELEASING.md) step 2 writes that at the tag, with the tag's
own gate figures. Add each item as it lands, in the CHANGELOG's own shape —
what changed, what it measured and how, with the commit — so the entry starts
from this page. Move each item into CHANGELOG.md at the tag and delete it
here.*

*Covered so far: c43f88fa (a metaop's step and the program's own operator);
00734769 (a `+` over the program's own class and every Int `+`).*

---

### Faster

- **A `multi infix:<+>` over the program's own class no longer slows every
  Int `+` in the program** (00734769). Its candidate is noted when the sub
  is hoisted, above the class it names, so the class could still have been
  a subset of Int, and every core-operand `+` went through the candidates'
  scoring: 3.8× on a `$t = $t + $_` loop, 2.3× in `[+]`. The operator is
  now decided again once the class or subset is declared. Retired
  instructions, before / after / with no such multi: the loop over 5M Ints
  31.4G / 8.17G / 8.20G, `[+]` 21.0G / 8.87G / 8.89G; perf-guard's new
  `userplus` kernel 259 → 73 ms. A subset of Int a hoisted candidate names
  still keeps the operator armed (t/regression/user-infix-filter-after-
  declarations.raku, green on Rakudo 2026.09).

### Fixed: silent wrong answers

- **A reduce or a zip meets the program's own candidate for a built-in
  operator** (c43f88fa). With `multi infix:<lcm>(Rat:D, Int:D)` declared,
  `2.0 lcm 6` took the candidate but `[lcm] 2.0, 6` was the built-in's 6,
  and so were `[gcd]`, `[\lcm]`, `[+]`, `[max]`, `Z+` and `X+` over any
  candidate the built-in could also answer for, a `where` candidate over two
  Ints among them. Each step of a metaop now does what the operator written
  out does. Math::NumberTheory's `carmichael-lambda(360)`, which ends in
  `[lcm]` over a Rat, is 12.0 as on Rakudo, not 12.
  t/regression/metaop-core-operands-user-infix.raku agrees with Rakudo
  2026.09, interpreted and compiled; `[+]` over Ints keeps its fold, also in
  a program whose `+` candidates take its own classes.

### Fixed: crashes

### Left open

- Found with 00734769, the same before it: a user `~` candidate over an
  object is never taken. With `multi infix:<~>(Rt $a, Int $b) { "rt-cat" }`
  declared, `Rt.new ~ 1` is `Rt<…>1`, where Rakudo 2026.09 answers
  "rt-cat"; the same shape over `**` or `+` takes its candidate (TODO.md,
  Correctness).
