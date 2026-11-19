# The next release's CHANGELOG entry, collected as work lands

*Emptied at the v5.9.0 tag: everything collected here is in
[CHANGELOG.md](../../CHANGELOG.md). This is not a CHANGELOG entry yet:
[RELEASING.md](RELEASING.md) step 2 writes that at the tag, with the tag's
own gate figures. Add each item as it lands, in the CHANGELOG's own shape —
what changed, what it measured and how, with the commit — so the entry starts
from this page. Move each item into CHANGELOG.md at the tag and delete it
here.*

*Covered so far: f6a2ecb6, 0444d23b, and fb3f9db0 to 3b004277.*

---

### New

### Faster

- **A loop holds the objects it makes and never lets out as slots**
  ([OBJECT-KERNEL-PLAN.md](plans/OBJECT-KERNEL-PLAN.md) phases 1 to 5 and 7).
  `my $p = Point.new(x => A, y => B)` in a loop a kernel runs made an object
  an iteration, 6,570 instructions each, and any class name kept the loop out
  of a kernel. For a class whose `.new` is the plain fill, the loop kernel now
  keeps each attribute in a frame slot, reads `$p.x` as the slot and compiles
  a one-expression method in place, objects passed to it and made by it
  included; an object used any other way keeps the loop interpreted. An
  object made before the loop and only read in it (phase 2) has its
  attributes copied into slots at each entry: `$acc += $p.len2` 5,181 → 123
  instructions an iteration. Such a `my` may be assigned another object of
  its class (`$p = $p.shift(1)`), `?? !!` may choose between two, and a
  method's body may be statements — `my`, `if`, `return` — when every end
  answers one kind of value (phase 3): a loop of a conditional assignment,
  a `clamp` with an early `return` and a `norm1` of two `my`s, 31.82G → 1.01G
  instructions for 1M iterations (1.32 → 0.04 s; Rakudo 2026.09 1.16 s).
  A variable made before the loop may be given a fresh object of its class
  in it (`$p = $p.step`, phase 5): the object is made once, from the slots,
  when the loop ends — 1M such steps 12.72G → 0.44G instructions (0.51 →
  0.01 s; Rakudo 0.62 s). And an object may leave the loop — `@a.push($p)`,
  `@a.push(P.new(…))`, `$outside = $named`: it is made where it is pushed, or
  where its name is declared, by the plain fill with no `.new` call; 1M
  `@a.push(P.new(…).shift(1))` 15.16G → 2.10G instructions (0.69 → 0.13 s;
  Rakudo 1.08 s). An attribute typed `Int`, `Num`, `Str` or `Bool` no longer
  keeps a class out (phase 7): the plain fill takes an argument that is
  exactly of that type and leaves every other one, with its check and its
  error, to the general construction — so a plain `.new` of such a class is
  faster outside a kernel too. `objects-steady` with `has Int $.x`: 24.82G →
  19.12G instructions with kernels off, 0.59G with them on (1.04 → 0.05 s;
  Rakudo 0.97 s). And an attribute may be written (phase 4): `$!n = …`,
  `$!n++`, `$!n += …` in a method and `$p.n = …` through an `is rw` accessor
  are stores to the slot, for an object only one name holds; an object made
  before the loop gets its attributes back when the loop ends. 1M
  `$a.add($i % 100).add(1)` on an accumulator: 16.72G → 0.46G instructions
  (0.66 → 0.02 s; Rakudo 1.03 s).
  `objects-steady` 770 → 23 ms (19.30G → 0.55G instructions; Rakudo 2026.09
  907 ms), `objects` 82 → 6.6 ms (Rakudo 197), best of six on the M4 at a load
  of 3; `multiwhere-steady`, `fib` and `loopsum` unchanged.
  `t/regression/object-kernel.raku`.
- **`--exe` compiles an EVAL of a literal natively** (0444d23b). Every EVAL
  sent its program to the bundling fallback, since the evaluated code cannot
  see a native program's variables and routines; it was the first construct
  to bundle in 265 of Roast's 1,425 files. Now only an EVAL that could ask for
  one of them refuses — code that is not a literal, reads the program's
  names, may use its operators, reads `$!`, `OUTER::` or a package-qualified
  name, or would raise an error whose wording depends on the scope — and
  `$v.raku.EVAL` round trips compile too. The note names the line and the
  reason. Roast files compiled natively: 525 → 545 of 1,425, each of the 20
  giving the interpreter's results. `t/regression/exe-eval-literal-native.raku`,
  `exe-eval-reads-program.raku`.

- **Objects and `where` multis at steady state, at or under Rakudo's time**
  (fb3f9db0 to 3b004277; V6-PLAN number 1). `tools/bench/objects-steady.raku`
  1.42 → 0.77 s against Rakudo 2026.09's 0.91 (1.56× → 0.84×; 33.07G →
  19.29G instructions retired) and `multiwhere-steady.raku` 1.33 → 0.81 s
  against 0.82 (1.64× → 1.00×; 33.45G → 20.89G), interleaved best of six at a
  load of 9–12. perf-guard against the tree before: `method` −32%, `attrread`
  −24%, `privmeth` −26%, `multimeth` −25%, `multiwhere` −43%, `objnew` −22%,
  every other kernel within the run's spread. Seven changes:
  - `my $x = <method call>` takes the declaration lane (fb3f9db0): the
    declaration alone was about 4,100 instructions.
    `t/regression/decl-call-lane.raku`.
  - A hash's entry chunks come from the slab pool (43a88ae2): a malloc and a
    free fewer for every object and small hash. `hash-chunk-slab.raku`.
  - A plain class's default construction is planned once (d190a0b6):
    `Point.new(x => 3, y => 4)` 6.97k → 5.03k instructions.
    `plain-class-new-fill.raku`.
  - `$!x` and `$.x` are read by what the node has learned of the class
    (664e8acf): about 700 instructions a read before. `attr-read-site.raku`.
  - `$obj.name(…)` on a user object skips methodCall's ladder, by the same
    per-site cache (185b0182). `method-call-site.raku`.
  - A plain method takes a lean call lane, as a plain sub does, and a sub
    called from a method body takes its own again (185b3d2a).
    `plain-method-lane.raku`.
  - `where * > 0` of an Int is compared without calling it, on a multi
    candidate and on an only method (3b004277): `method m(Int $x where * > 0)`
    12.8k → 5.6k instructions a call. `where-int-compare.raku`.

### Fixed: silent wrong answers

- **A `for` loop's `$_` parameter is the topic under `--exe`** (f6a2ecb6):
  `for 1, 2, 3, 4 -> $_, $n { say .succ }` printed 1 1 compiled, where the
  interpreter and Rakudo print 2 4 — a loop with two or more parameters bound
  `$_` but read the outer topic. Roast's S32-io/io-special.t died on it once
  compiled natively. `t/regression/exe-for-topic-parameter-native.raku`.

### Fixed: crashes

### Behaves like Rakudo

### Platforms and packaging

### Tools

### Left open
