# The next release's CHANGELOG entry, collected as work lands

*Emptied at the v5.6.2 tag: everything collected here is in
[CHANGELOG.md](../../CHANGELOG.md). This is not a CHANGELOG entry yet:
[RELEASING.md](RELEASING.md) step 2 writes that at the tag, with the tag's
own gate figures. Add each item as it lands, in the CHANGELOG's own shape —
what changed, what it measured and how, with the commit — so the entry starts
from this page. Move each item into CHANGELOG.md at the tag and delete it
here.*

*Covered so far: c43f88fa (a metaop's step and the program's own operator);
00734769 (a `+` over the program's own class and every Int `+`); c77f05e7
(`~` and a user candidate over an object); 5ebbe282 (a multi whose
candidates all decline); fb43011a (an empty native array, packed); 300832c7
(a Rat's parts, a payload of their own); a90fb35e (objects and hashes leaner).*

---

### Faster

- **Objects and hashes take less memory** (a90fb35e). Every hash carried
  48 bytes for an aliasing generation and object keys almost none has, and
  every object 24 for a boxed value and a monitor's lock; both are behind
  one pointer now. An entry's dead flag moved into its hash (120 → 112
  bytes), and an object's attribute table holds exactly its attributes where
  it rounded up to a power of two. 200k two-attribute objects 609 → 512 bytes
  each (Rakudo 494); five attributes 1,444 → 1,009; two-key hashes 769 →
  663 (Rakudo 746). t/regression/hash-exact-first-chunk.raku agrees with
  Rakudo 2026.09; Roast 1,425 / 1,425.

- **A Rat takes 163 bytes, not 437** (300832c7). Its numerator and
  denominator were two separately allocated BigInts reached through the
  200-byte block every Value with a cold field carries; they are one 80-byte
  body of their own now, shared by every copy of the Rat, with the FatRat
  flag in it. 1M kept Rats: 437 → 163 bytes an element (Rakudo 211), one
  allocation each where there were three; that block is 168 bytes, not 200,
  for every other value. Rat arithmetic costs what it did (perf-guard's
  `rats` kernel unchanged), and keeping 300k Rats from a `.map` takes 24%
  fewer instructions. t/regression/rat-body-shared-and-written.raku agrees
  with Rakudo 2026.09; Roast 1,425 / 1,425.

- **A native array built by a loop keeps its elements as machine words**
  (fb43011a). Only `my int @a = ^N` and other list assignments made the
  packed form; a `my int @a` (or `num`) declared empty and grown by `push`
  or by index stayed 80-byte Values. It is packed from its declaration now,
  and a store at or past its end grows it. 1M pushed Ints: 87 → 24 MB peak,
  7.44G → 2.60G instructions; 1M stored by index 6.99G → 1.75G; the
  pushed row of tools/run-memory.raku 83 → 17 bytes an element, against
  Rakudo's 63. t/regression/native-array-grown-stays-packed.raku agrees
  with Rakudo 2026.09, interpreted, compiled and under `--cnp`.

- **A multi whose candidates all decline goes straight to the built-in**
  (5ebbe282; issue #47). Math::NumberTheory adds six `infix:<gcd>`
  candidates, none for two Ints, and every `$_ gcd $n` in its euler-phi
  scored all six before the built-in answered. The multi-dispatch cache now
  remembers "no candidate takes this shape", and takes candidates typed
  `Rat`, `FatRat`, `Complex` or `Bool`. euler-phi over 1 .. 3000 3.37 → 1.39 s
  (Rakudo 1.54); the issue #47 driver in run-bench 4.11 → 2.21 s interpreted
  and 3.23 → 1.29 s under `--exe`, against Rakudo's 3.33 s.
  t/regression/dispatch-cache-builtin-answers.raku agrees with Rakudo
  2026.09; perf-guard's new `multidecline` kernel 170 → 83 ms.

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

- **`~` written out takes a user candidate over an object or an enum**
  (c77f05e7). With `multi infix:<~>(Rt $a, Int $b) { "rt-cat" }` declared,
  `Rt.new ~ 1` was `Rt<…>1`, and a `(Color, Str)` candidate left `Red ~
  "x"` as "Redx": the arm for `~` concatenated an object through its Str
  before anything looked, where `**`, `+`, `[~]`, `Z~` and `~=` took the
  candidate. Where none takes the operands, they concatenate as before.
  t/regression/cat-user-candidate-object-operand.raku agrees with Rakudo
  2026.09.

### Fixed: crashes

### Left open

- Found with a90fb35e, the same before it: a class deriving Int loses its
  value in arithmetic — `class D is Int { method double { self * 2 } }`
  gives `D.new(21).double` 0 and `D.new(3) + 4` 4, where Rakudo 2026.09
  gives 42 and 7 (TODO.md, Correctness).

- Found with 5ebbe282, the same before it: a user candidate exactly as
  narrow as a core one is taken, where Rakudo calls the two ambiguous —
  `multi infix:<gcd>(Int:D $a, Int:D $b) { "mine" }; 4 gcd 6` is "mine",
  and Rakudo 2026.09 dies "Ambiguous call to 'infix:<gcd>(Int, Int)'"
  (TODO.md, Correctness).

