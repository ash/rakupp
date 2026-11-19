# The next release's CHANGELOG entry, collected as work lands

*Emptied at the v5.10.0 tag: everything collected here is in
[CHANGELOG.md](../../CHANGELOG.md). This is not a CHANGELOG entry yet:
[RELEASING.md](RELEASING.md) step 2 writes that at the tag, with the tag's
own gate figures. Add each item as it lands, in the CHANGELOG's own shape —
what changed, what it measured and how, with the commit — so the entry starts
from this page. Move each item into CHANGELOG.md at the tag and delete it
here.*

*Covered so far: the `my class` shadowing fix.*

---

### New

### Faster

### Fixed: silent wrong answers

- A `my class` (or `my role`) declared in a routine or block no longer takes
  the name from a global type of the same name for good: once
  `sub f { my class Point {} }` had run, `Point` named f's class everywhere
  and the program's own `class Point` could not be reached. The type
  registry holds one type per name; the shadowed one is now put back when the
  declaring scope leaves, with its `temp`s, whether it returned or died, and
  in order through recursion and nested scopes (registerDeclaredType).
  t/regression/my-class-shadow.raku, which Rakudo 2026.09 passes too.

### Fixed: crashes

### Behaves like Rakudo

### Platforms and packaging

### Tools

### Left open

- A closure or method that outlives the routine which declared a shadowing
  `my class` finds the outer type under that name, where Rakudo finds the
  routine's own; and a method of the outer class, called while the routine
  runs, finds the routine's class. Both follow from one registry entry per
  name.
