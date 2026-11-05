# The next release's CHANGELOG entry, collected as work lands

*Emptied at the v5.6.2 tag: everything collected here is in
[CHANGELOG.md](../../CHANGELOG.md). This is not a CHANGELOG entry yet:
[RELEASING.md](RELEASING.md) step 2 writes that at the tag, with the tag's
own gate figures. Add each item as it lands, in the CHANGELOG's own shape —
what changed, what it measured and how, with the commit — so the entry starts
from this page. Move each item into CHANGELOG.md at the tag and delete it
here.*

*Covered so far: c43f88fa (a metaop's step and the program's own operator).*

---

### Faster

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

- Found with c43f88fa, the same before it: a `multi infix:<+>` over the
  program's own class makes every Int `+` in the program slower, about 3.8×
  written out and 2.3× in `[+]`, because the candidate is taken for one that
  may name a core subset when it is declared above the class
  (SPEED-BACKLOG 53).
