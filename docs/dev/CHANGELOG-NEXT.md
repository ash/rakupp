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
