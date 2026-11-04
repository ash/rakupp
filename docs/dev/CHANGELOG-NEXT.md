# The next release's CHANGELOG entry, collected as work lands

*Emptied after the v5.6.0 tag: everything collected here is in
[CHANGELOG.md](../../CHANGELOG.md). This is not a CHANGELOG entry yet:
[RELEASING.md](RELEASING.md) step 2 writes that at the tag, with the tag's
own gate figures. Add each item as it lands, in the CHANGELOG's own shape —
what changed, what it measured and how, with the commit — so the entry starts
from this page. Move each item into CHANGELOG.md at the tag and delete it
here.*

*Covered so far: 39f73f24 (`is copy` of a constant, issue #218).*

---

### Faster

### Fixed: silent wrong answers

### Fixed: crashes

- **An `is copy` parameter given a constant is writable, in every shape**
  (39f73f24; issue #218). `my constant X = 500; sub f($n is copy) { $n =
  $n - 1 }; f(X)` died "Cannot assign to a readonly variable or a value":
  a constant's value is readonly, as a sigilless `\y` parameter's is, and
  the mark came along into the copy. The simple path `is copy` joined in
  5321f41c kept it, b7c47a57's native wrap put it back (`int32 $n is copy
  = X`), and a named parameter, a default and a slurpy (`*@`, `+@`, `**@`,
  `*%`) never cleared it, so a sub, a method, a multi, a pointy block and a
  `for` loop's all died, and `sub g(\y) { f(y) }` on any argument.
  t/regression/is-copy-of-a-readonly-argument.raku agrees with Rakudo
  2026.09, interpreted and under `--exe`.

### Left open

- Found with 39f73f24, the same before it: a slurpy hash's values are
  writable, so `sub f(*%h) { %h<a> = 1 }; f(:a(5))` lives where Rakudo dies
  "Cannot assign to an immutable value"; and a constant handed to a slurpy
  that is not `is copy` dies "Cannot assign to a readonly variable or a
  value" when its element is assigned, where Rakudo, and rakupp for a
  literal, say "Cannot assign to an immutable value".
