# Issue #63: the report said `clone`, the bug was the element type check

[Issue #63](https://github.com/ash/rakupp/issues/63) (melezhik, 3 Sep 2026)
reported that `.clone` "fails when we clone objects in a recursion": a cloned
object's `@.data` kept overwriting the original's. The golf is reproduced in
`t/regression/issue63-typed-container-elements.raku`.

## `clone` was right

Rakudo 2026.08 prints the report's output **byte for byte** once the program is
made runnable. `clone` is shallow: it copies the attribute *values*, so a
container attribute is the *same* Array in the original and the clone, and

```raku
method run-task($i) { self.data = [$i] }
```

is a **list assignment into that shared Array**, not a replacement of it. Both
engines therefore show the original's `data` changing. Verified three ways —
the reporter's program, a container-identity probe (`$a.data === $b.data` is
`True` on both), and a four-level recursion with `.clone(:twiddle)` (identical
output on both engines). The way to give a clone its own container is a twiddle:
`self.clone(data => [|@.data])`.

## What was actually broken

The reporter's class declares `has Str @.data` and stores `Int`s in it. Rakudo
stops at that line:

```
Type check failed for an element of @!data; expected Str but got Int (0)
```

rakupp enforced **no element type at all**, anywhere: not on an attribute, not
on a lexical, not through any of the ways a value enters a container. `.of` on
a typed *attribute* answered `(Mu)`, because the declared type never reached the
slot. So the program ran past the point Rakudo stops it, and the confusing
output was what the user saw instead of the error that names the real mistake.

Fixed, all oracle-checked against Rakudo 2026.08:

- **the declared type reaches the slot** — `has Str @.d` / `has Int %.h` seed an
  `Array[Str]` / `Hash[Int]`, so `.of` answers, and a missing element reads back
  as the element type object rather than `(Any)`;
- **every way in is checked** — element and slice assignment, list assignment,
  `.push` / `.append` / `.unshift` / `.prepend`, `.splice` (with Rakudo's own
  `X::TypeCheck::Splice`, and the array left untouched when it fails), a value
  passed to `.new`, and a declared attribute default;
- **the predicate is nominal conformance, not `~~`** — a role, a user class
  hierarchy and a `subset`'s `where` are all honoured, while a Junction (which
  smart-matches `Int`) is an illegal `Int` element, as in Rakudo;
- **a PARAMETERISED element type constrains twice** — `my Array[Int] @a` takes an
  `Array[Int]` and rejects both an `Array[Str]` and a plain `Array`, which is
  Rakudo's rule. Treating `Array[Int]` as one opaque nominal name instead is
  what the first cut did, and it killed `roast S06-currying/positional.t` at its
  `my Array[Int] @AoAoI` declaration — the file went 164/166 → 71/71. The same
  fix made the value/key split of a hash's `ofType` bracket-depth-aware: a
  parameterised element type carries its own commas *inside* its brackets, so
  `my Hash[Int,Str] @a` was being cut to `Hash[Int`;
- **exempt values** — a `Nil` RESETS the slot (and in a typed container to the
  element *type object*: `my Bool @r = Nil` is one `(Bool)`, which also fixes a
  standing `[Any]` divergence); the matching type object is legal, a supertype's
  is not; a `Failure` is **not** exempt — Rakudo fails that assignment;
- **`natCheck`'s two bugs**, uncovered by giving it a boxed twin: it read the
  *raw* argument, so `my int @a; @a.append([1,2])` was rejected where Rakudo
  appends two ints, and it treated a `Nil` as a store.

And one adjacent bug that had to be fixed because the new check turned it from a
wrong element count into a crash: **`push`/`unshift` stored a Slip instead of
slipping it.** `@a.push(Empty)` added one element where Rakudo adds none, and
`@a.push(slip(7,8))` added one where Rakudo adds two. (`append`/`prepend`
already flattened correctly.) That alone took
`roast S02-types/undefined-types.t` from 45/49 to **49/49**.

## Measurement

Full Roast, `--workers=1 ROAST_TIMEOUT=30`, both legs on this machine against
the same Roast checkout (baseline = `v3.25.0-2-g8dfd647`, built in a throwaway
worktree so the shared tree was never stashed):

| | baseline | with the fix |
|---|---:|---:|
| Files fully passing | 654 / 1,464 | **656 / 1,464** |
| Assertions (all declared) | 200,084 | **200,128** |
| `t/run.raku` | 637 + 1 failing | **638 / 638** |

**Zero removals from the `[PASS]` list** (and zero against the archived
`v3.25.0-union.list`); additions are `S02-types/undefined-types.t` (the Slip
fix) and `S17-supply/min.t` (a known timing flipper).

Per-file arithmetic, which is the gate that matters here — 12 files gained
**+45**, and the only loss is `S32-list/pick.t` −1, proven noise by running it
three times on *each* binary (3/4/4 baseline, 3/3/4 with the fix):

```
S02-types/array.t                70/80  -> 71/80     S12-attributes/instance.t  127/145 -> 134/145
S02-types/undefined-types.t      45/49  -> 49/49     S14-roles/parameter-subtyping.t 16/22 -> 17/22
S09-hashes/objecthash.t          16/33  -> 20/33     S17-supply/min.t             8/10  -> 10/10
S09-typed-arrays/arrays.t        59/84  -> 72/84     S32-array/push.t            49/56  -> 51/56
S09-typed-arrays/hashes.t        30/47  -> 38/47     S32-array/unshift.t         74/76  -> 75/76
S12-attributes/clone.t           40/44  -> 41/44     S32-hash/perl.t             36/55  -> 37/55
```

The headline total is why the per-file pass is not optional: the FIRST cut of
this change also showed zero PASS-list removals and a *green* formal gate, while
`S06-currying/positional.t` had silently lost 93 assertions. A partial file is in
neither list, so only the per-file join sees it. (`docs/status/COUNTING.md` warns
about exactly this case.)

## Verified, and deliberately left open

Each was oracle-checked in the same session; none is on the path of the report.

| # | Repro | Rakudo 2026.08 | rakupp |
|---|---|---|---|
| 1 | `C.new(a => 1;)` — a `;` inside an argument list | "Default constructor for 'C' only takes named arguments" (the pair became positional) | accepted as a named argument |
| 2 | `my @a; @a.push(a => 1)` | `[]` — a bare pair in an argument list is a NAMED argument, and `push` ignores it | `[:a(1)]` — pushed as a positional |
| 3 | `my @a; @a.push(1\|2)` | `[any(1, 2)]` — one Junction element | `[1, 2]` — flattened |
| 4 | `my int @a = 1,2,3; @a.splice(1,1,"x")` | `X::AdHoc`, "This type cannot unbox to a native integer" | stores `"x"` unchecked (a NATIVE element type; the boxed path is fixed) |
| 5 | `my %h{Int}; %h{"s"} = 1` — object-hash KEY type | `X::TypeCheck::Binding::Parameter` | accepted (only the VALUE half is checked) |
| 6 | `sub f(Int @x) {}; my @y = "s"; f(@y)` | `X::TypeCheck::Binding::Parameter` — the array's own element type is part of the binding | accepted |
| 7 | `my Str @a; say @a.raku` | `Array[Str].new()` | `[]` — the parameterisation is not rendered (pre-existing for lexicals; attributes now match lexicals) |
| 8 | `$obj.attr := [...]` | compile-time "Cannot use bind operator with this left-hand side" | the same message, at RUN time |
| 9 | `class C { has Int @.d is rw }; C.new.d = ["x"]` | type error | accepted — the accessor-assignment path takes the attribute's sigil only for a **variable or `self`** invocant, so an rvalue invocant falls to the scalar path and overwrites the slot. The restriction is deliberate (evaluating an arbitrary invocant twice could double a side effect); `$c.d = ["x"]` on a variable is checked. |

## Reproducing

```sh
build-arm64/rakupp t/regression/issue63-typed-container-elements.raku   # PASS
raku               t/regression/issue63-typed-container-elements.raku   # PASS — every
                                                                        # assertion is
                                                                        # oracle-verified
```
