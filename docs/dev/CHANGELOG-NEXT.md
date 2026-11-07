# The next release's CHANGELOG entry, collected as work lands

*Emptied at the v5.6.3 tag: everything collected here is in
[CHANGELOG.md](../../CHANGELOG.md). This is not a CHANGELOG entry yet:
[RELEASING.md](RELEASING.md) step 2 writes that at the tag, with the tag's
own gate figures. Add each item as it lands, in the CHANGELOG's own shape —
what changed, what it measured and how, with the commit — so the entry starts
from this page. Move each item into CHANGELOG.md at the tag and delete it
here.*

*Covered so far: 28257097 (a `constant` type alias in a signature, issue #220),
02e52fa2 (`Value` 80 → 64 bytes).*

---

### Faster

- **Less memory: a `Value` is 64 bytes, not 80** (02e52fa2; VALUE32-PLAN
  design C). A Str of up to 15 bytes now lives in the value's own 16-byte
  string slot, and a longer one is a pointer to a shared body. The slot held a
  32-byte `std::string` and a pointer before. Peak memory, measured against
  the commit before:
  - a million Ints built by `.map`: 164 → 134 MB;
  - a million short Strs: 164 → 134 MB;
  - a million Rats: 242 → 164 MB;
  - 200,000 two-element arrays: 82 → 72 MB;
  - `hashfill`: 52 → 45 MB.

  perf-guard is level (geomean −0.49% over 27 kernels; instructions −0.04%).
  Roast's allocations are within run-to-run noise. A string of 16 to 22 bytes
  now costs an allocation where it cost none: `textsplit`, which keeps 20,000
  of them, uses 26.3 MB where it used 23.6, and runs 9% faster.
  t/run.raku 1,992 of 1,992; Roast 1,425 of 1,425; Roast under ASan finds
  nothing new.

### Fixed: silent wrong answers

- **A `constant` naming a type is that type in a signature** (28257097;
  issue #220). `my constant Alias = Foo::Bar; sub g(Alias $x) { }` gave a
  parameter whose `.type` was a type named Alias, `=== Foo::Bar` False,
  and a signature rendered `:(Alias $x)`; Rakudo resolves the constant at
  compile time and answers Foo::Bar. The binder and the return check
  already resolved the name per call; `.type`, the parameter's and the
  signature's rendering, `--> Alias`, `.returns` and `.signature.returns`
  now resolve it in the routine's closure too, so a role's method in `class
  RI does R[Int]` has an `Int $x`. Found beside it, the binder dropped a
  parameterization: `constant AI = Array[Int]; sub f(AI $x)` took an untyped
  Array, and `multi m(AI $x)` won over `multi m(@x)` for one; both refuse
  it now, as Rakudo does. t/regression/constant-type-alias-in-signature.raku
  agrees with Rakudo 2026.09, interpreted, under `--jit`, `--cnp` and
  `--exe`.

### Fixed: crashes

### Platforms and packaging

### Left open

- Found with 28257097, the same before it: an attribute typed by a
  parameterized alias, `constant AI = Array[Int]; has AI $.a`, takes an
  untyped Array (the attribute resolver keeps the bare name, which
  NativeCall's `Pointer[void]` aliases lean on), and `my AI $z` answers
  `.VAR.of` AI where Rakudo says Array[Int]. `multi n(Array[Int] $x)`
  written out never takes a typed array, where Rakudo's does. A role
  method's signature leaves out its invocant (`:(Int $x, *%_)` against
  Rakudo's `:(RI $:: Int $x, *%_)`), and `&c:(Foo)` renders as `&c`.
