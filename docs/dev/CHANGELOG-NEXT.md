# The next release's CHANGELOG entry, collected as work lands

*Emptied at the v5.5.0 tag: everything collected here is in
[CHANGELOG.md](../../CHANGELOG.md). This is not a CHANGELOG entry yet:
[RELEASING.md](RELEASING.md) step 2 writes that at the tag, with the tag's
own gate figures. Add each item as it lands, in the CHANGELOG's own shape —
what changed, what it measured and how, with the commit — so the entry starts
from this page. Move each item into CHANGELOG.md at the tag and delete it
here.*

*Covered so far: 93f9a4af and c7a12943.*

---

### Faster

### Fixed: silent wrong answers

- **C's `cond ? a : b` is a compile error, as in Rakudo** (93f9a4af). A lone
  `?` ended the expression and `? a` began a new statement, so
  `my $s = $n > 2 ? "many" : "few"` set `$s` to True; other shapes died of
  "Malformed radix number" or "expected )". It is now Rakudo's X::Obsolete,
  "Unsupported use of ? and : for the ternary conditional operator".

### Fixed: crashes

### Behaves like Rakudo

- **An undeclared routine in the main program is a compile-time report with
  its line** (c7a12943): `say length("Riga")` printed a run-time "Undefined
  routine 'length'" at line 0, and now prints Rakudo's ===SORRY!===
  "Undeclared routine: length used at line 1. Did you mean 'elems', 'chars',
  'codes'?", every such name at once, with Rakudo's fixed hints (`local`,
  `foreach`, `quit`, a phaser in lower case, …). `len` is no longer "Did you
  mean 'leg'?".

### Left open
