# The next release's CHANGELOG entry, collected as work lands

*Emptied at the v5.6.3 tag: everything collected here is in
[CHANGELOG.md](../../CHANGELOG.md). This is not a CHANGELOG entry yet:
[RELEASING.md](RELEASING.md) step 2 writes that at the tag, with the tag's
own gate figures. Add each item as it lands, in the CHANGELOG's own shape —
what changed, what it measured and how, with the commit — so the entry starts
from this page. Move each item into CHANGELOG.md at the tag and delete it
here.*

*Covered so far: 28257097 (a `constant` type alias in a signature, issue #220),
02e52fa2 (`Value` 80 → 64 bytes), 5d52a248 (string literals keep their Value),
f0376fa6 (kernels share stored strings; `"…"` literals cached),
9b294b72 (`%h.elems` without listing the hash),
f3096c7d and 7a09c596 (`return-rw` use-after-frees; ASan sees the pools' frees),
578572c6 (a finished worker is not a running one).*

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
- **A string literal is built once** (5d52a248). Its value is made on the
  literal's first evaluation and copied after that, so a literal over 15 bytes
  no longer allocates each time it runs. Evaluating a 36-byte literal 200,000
  times takes 200,000 fewer mallocs and 12–19% fewer instructions in the
  interpreter. Loops the integer kernels compile are unchanged: their string
  lane still makes a new value per element stored. perf-guard is level, and
  `prepend` gets back the 2% of instructions the 64-byte `Value` cost it.
  `t/regression/str-literal-shared-value.raku` checks that a write through a
  copy never reaches the literal.
- **A long string stored many times is stored once** (f0376fa6).
  - The integer kernels used to build a new string for every element they
    stored. Now they share the one they read, from a variable, an element or
    a literal.
  - A double-quoted `"…"` with nothing interpolated is now the cached literal
    as well; until this change only `'…'` was.
  - Pushing a 36-byte literal 200,000 times takes 4,536 mallocs where it took
    404,533, 5.9 ms where it took 15.3, and 23 MB of memory where it took 48.
  - `"…" xx 200_000` takes 6.3 ms where it took 27.8.
  - perf-guard −1.32% (`prepend` −19.7%, `sharedappend` −19.0%).
- **`%h.elems` is the hash's size** (9b294b72). `.elems`, `.end` and `.Numeric`
  of a hash built a Pair for every entry to count them, one allocation each,
  on every call. `say %h.elems` of a 200,000-key hash makes 4,595 mallocs
  where it made 204,626. A Map, Set, Bag or Mix, a role mixed in, a subclass
  and an augmented `Hash.elems` answer as before.
  `t/regression/hash-elems-size.raku`.

### Fixed: silent wrong answers

- **`return-rw` wrote into, or read from, the frame it had just left**
  (7a09c596). When a routine returns, its frame goes back to a pool and its
  variables are cleared. Two paths used one of those variables afterwards.
  - Assigning through an `is rw` call wrote the value into the returning
  routine's own copy of a sigilless parameter.
  `sub raw-id(\c) is rw { return-rw c }; sub set(\c, $v) { raw-id(c) = $v; c }`
  does that.
  - A `return-rw` of the routine's own local was read back only after the
  frame had been cleared.

  On most systems the freed memory still held the old bytes, so nothing
  showed. musl's allocator reuses it sooner:
  `t/regression/rw-sub-lvalue.raku` crashed with SIGSEGV on linux-aarch64-musl
  in CI. Both bugs are older than this release's other changes, and ASan
  found them only once it could see the engine's own pools free memory
  (f3096c7d: an ASan build now hands every slab and list block back to the
  allocator).
- **Right after `await start {…}`, a worker still exiting counted as running
  code** (578572c6). For as long as its thread took to tear down:
  - `my $b := $a` would not share `$a`'s cell, and `$b.VAR.default` died;
  - a callee did not see a `my $*X` declared later in its caller's frame.

  Both depended on timing, which is why CI's Linux legs failed
  `scalar-alias-keeps-type.raku` or `dynamic-var-memo.raku` now and then.
  The checks that ask whether another thread can be running code now count
  worker bodies, which end when the body returns.
  `t/regression/worker-teardown-is-not-a-running-body.raku` makes the
  teardown slow on purpose (`RAKUPP_WORKER_EXIT_DELAY`).

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
