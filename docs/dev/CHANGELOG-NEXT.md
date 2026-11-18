# The next release's CHANGELOG entry, collected as work lands

*Emptied at the v5.9.0 tag: everything collected here is in
[CHANGELOG.md](../../CHANGELOG.md). This is not a CHANGELOG entry yet:
[RELEASING.md](RELEASING.md) step 2 writes that at the tag, with the tag's
own gate figures. Add each item as it lands, in the CHANGELOG's own shape —
what changed, what it measured and how, with the commit — so the entry starts
from this page. Move each item into CHANGELOG.md at the tag and delete it
here.*

*Covered so far: f6a2ecb6 and 0444d23b.*

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
