# Set, SetHash, Bag, BagHash, Mix, MixHash and the set operators — semantics sheet

Provenance: Rakudo tag `2026.08`, files `src/core.c/QuantHash.rakumod` (116
lines), `Setty.rakumod` (241), `Set.rakumod` (186), `SetHash.rakumod` (326),
`Baggy.rakumod` (647), `Bag.rakumod` (166), `BagHash.rakumod` (280),
`Mixy.rakumod` (181), `Mix.rakumod` (167), `MixHash.rakumod` (224), the
thirteen `set_*.rakumod` operator files (1,896 lines together) and
`core.d/operators.rakumod` (the 6.d removal of `(<+)`), all read in full on
2026-09-23; `Rakudo/QuantHash.rakumod` (1,904) read for what the constructors,
coercers and each operator accept and produce, not for how; the `Set`, `Bag`,
`Mix` and `-Hash` coercers of `Any`, `Iterable`, `Map`, `Nil` and `Failure`.
Out of scope: hyper-operator dispatch beyond `>>` on a QuantHash,
`classify`/`categorize` on non-Baggy types, the `unique`/`repeated` family.
Oracle: Homebrew Rakudo v2026.08 on macOS. Compared against Raku++
4.0.1-84-ga4291988 (build-arm64, 2026-09-23). Format and legend:
[README.md](README.md).

Where this sits against the declared spec: Rakudo passes all 22 Roast files
of this area on this machine (`S02-types/{set,sethash,bag,baghash,mix,mixhash,
baggy,set-iterator,bag-iterator,mix-iterator,mixed_multi_dimensional}.t` and
`S03-operators/set_{elem,union,intersection,difference,symmetric_difference,
addition,multiply,subset,proper_subset,equality}.t`; `set_precedes.t` lives
under `6.c/` and is not in either list); Raku++ passes 7 (the three iterator
files, `mixed_multi_dimensional.t`, `set_intersection.t`, `set_multiply.t`,
`set_union.t`). The rules below are what those files and real programs lean
on: which arguments become elements and which become weights, the identity
of the empty sentinels, what a subscript answers and returns, how the six
mutation paths of the hash forms coerce and remove, what each operator
returns for each pair of operand kinds, and the exact exception types. 17 of
the 44 items are neither fully documented nor fully asserted by Roast. Four
Rakudo behaviours are recorded as bugs and ten as quirks; step two should
not imitate the bugs.

Every probe ran with `alarm 10` and standard input closed, in a fresh
sandbox directory per engine. Because the types are unordered, every probe
prints `.sort`, `.keys.sort`, or `.gist` (which sorts by the elements'
gists); SB-14 says which orderings are guaranteed. One probe (SB-30) waits
three seconds on hung threads and says so.

## A. Construction

### SB-01  `.new`, the `set`/`bag`/`mix` subs, the empty sentinels, named arguments   D:partial R:partial V:spec
`Set.new(...)` and `set(...)` build a Set from positional arguments;
duplicates collapse by `WHICH`, so `1`, `"1"` and `1.0` are three
elements. Every empty immutable construction returns the same object: `Set.new`,
`Set.new()`, `set()`, `().Set` are all `=:=`, and so are the Bag and Mix ones;
every `SetHash.new` is a fresh object. `set;` without parentheses is a
compile-time `X::Comp::AdHoc`, `set<a b c>` an `X::Syntax::Confused`. A named
argument to the subs (`set(a => 1)`, an unquoted key) is `X::Multi::NoMatch`;
a named argument to `.new` is silently ignored (`Set.new(a => 1)` is empty,
`Bag.new(a => 3, "b")` is `Bag(b)`). A quoted-key Pair is positional and
becomes an ELEMENT of type Pair, not a weight: `set(("a" => 1),)` has one Pair
element, `Bag.new("a" => 3)` has the Pair with weight 1, `Mix.new("a" => 3,
"a" => 3)` the Pair with weight 2, `bag(<a b> => 1)` the Pair
`("a", "b") => 1`; such a set answers `<a>` False but `.AT-KEY("a" => 1)` and
`("a" => 1) (elem)` True. `.Set` on a Set is the identity, so the Pair stays a
Pair element.
```
say Set.new(<a b a>).gist, " ", set(<a b a>).gist, " ", Set.new(1, "1", 1.0).elems, " ", set().elems, " ", Set.new.elems, " ", (Set.new =:= set()), " ", (Set.new() =:= set()), " ", SetHash.new.elems, " ", (SetHash.new =:= SetHash.new), " ", bag(<a b a>).gist, " ", mix(<a b a>).gist, " ", Bag.new.gist, " ", (Bag.new =:= bag()), " ", (Mix.new =:= mix()), " ", (().Set =:= set()), " ", (().Bag =:= bag()), " ", (().Mix =:= mix()), " ", (().SetHash =:= SetHash.new), " ", (try EVAL('set;')) // $!.^name, " ", (try EVAL('set<a b c>')) // $!.^name, " ", (try EVAL('set(a => 1).elems')) // $!.^name, " ", (try EVAL('bag(a => 1, "b").elems')) // $!.^name, " ", Set.new(a => 1).elems, " ", Bag.new(a => 3, "b").gist, " ", set(("a" => 1), ("b" => 0)).keys.sort.map(*.^name).join(","), " ", set(("a" => 1),).elems, " ", set(("a" => 0),).elems, " ", Set.new("a" => 0).elems, " ", set(("a" => 1),).keys[0].raku, " ", Bag.new("a" => 3).keys[0].raku, " ", Bag.new("a" => 3).values.raku, " ", Mix.new("a" => 3, "a" => 3).pairs.raku, " ", bag(<a b> => 1).keys[0].raku, " ", set(("a" => 1),)<a>, " ", set(("a" => 1),).AT-KEY("a" => 1), " ", (("a" => 1) (elem) set(("a" => 1),)), " ", SetHash.new(:a, :b).keys.map(*.^name).raku, " ", Set.new(:a, "b").gist, " ", set(("a" => 1),).Set.gist, " ", set(("a" => 1),).Set.keys[0].^name
# rakudo 2026.08: Set(a b) Set(a b) 3 0 0 True True 0 False Bag(a(2) b) Mix(a(2) b) Bag() True True True True True False X::Comp::AdHoc X::Syntax::Confused X::Multi::NoMatch X::Multi::NoMatch 0 Bag(b) Pair,Pair 1 1 1 :a(1) :a(3) (1,).Seq ((:a(3)) => 2,).Seq ("a", "b") => 1 False True True ().Seq Set(b) Set(a => 1) Pair
```
rakupp 4.0.1-84: differs — `Set.new(1, "1", 1.0)` has one element (keys are conflated by value), no empty sentinel (`Set.new =:= set()` is False), `set;` and `set<a b c>` are accepted, a named argument to `set`/`bag` is taken as an element, and `Set.new(a => 1)` has one element.

### SB-02  What `.new` iterates and what the subs flatten                   D:partial R:yes V:spec
`.new` with ONE argument iterates it if it is an Iterable that is not
itemized (`Set.new([1,2])` has 2 elements, `Set.new($[1,2])` one, a Hash
gives its Pairs as elements, a Range its numbers, a Seq its values); with
several arguments each argument is one element (`Set.new((1,2),(3,4))` and
`Set.new(<a b>, <c d>)` have 2; `Bag.new(<a b>, <a b>)` has 2 elements of
weight 1 each, because two equal Lists are distinct objects). The `set`/
`bag`/`mix` subs flatten one level of Iterables (`set([1,2],[3,4])` has 4,
`set((1,2),(3,4))` 4, `set({a => 1, b => 2})` 2 Pairs) but keep an itemized
argument whole (`set($[1,2])`, `set(${...})` have 1). A QuantHash argument is
never flattened: `set(set(<a b>))` and `Set.new(bag(<a b>))` have one element
of type Set/Bag. Type objects, `Nil` and `Mu` are ordinary elements
(`Set.new(Int)` is `Set((Int))`, `set(Nil)` is `Set(Nil)` and `Nil (elem)` it);
an Int and its IntStr allomorph are two elements (`Set.new(1, <1>)`).
```
say Set.new([1,2],[3,4]).elems, " ", set([1,2],[3,4]).elems, " ", set([1,2]).elems, " ", set($[1,2]).elems, " ", Set.new([1,2]).elems, " ", Set.new($[1,2]).elems, " ", set((1,2),(3,4)).elems, " ", Set.new((1,2),(3,4)).elems, " ", set({a=>1,b=>2}).elems, " ", set(${a=>1,b=>2}).elems, " ", set({a=>1,b=>2}.hash).elems, " ", Set.new({a=>1}).elems, " ", Set.new({a=>1}).keys[0].^name, " ", Set.new(set(<a b>)).elems, " ", set(set(<a b>)).elems, " ", Set.new(bag(<a b>)).keys[0].^name, " ", set(<a b>, <c d>).elems, " ", Set.new(<a b>, <c d>).elems, " ", Set.new((<a b>, <c d>)).elems, " ", Set.new(<a b>).elems, " ", Bag.new(<a b>, <a b>).elems, " ", Bag.new(<a b>, <a b>).values.raku, " ", set(1..3).elems, " ", Set.new(1..3).elems, " ", Set.new((1,2).Seq).elems, " ", set((1,2).Seq, (3,)).elems, " ", Set.new(bag(<a b>).pairs).elems, " ", Set.new(Int).gist, " ", set(Int, Str).elems, " ", Set.new(Nil).elems, " ", Set.new(Nil).keys[0].raku, " ", set(Nil).gist, " ", (Nil (elem) set(Nil)), " ", set(Mu).elems, " ", Set.new(1, <1>).gist
# rakudo 2026.08: 2 4 2 1 2 1 4 2 2 1 2 1 Pair 1 1 Bag 4 2 2 2 2 (1, 1).Seq 3 3 2 3 2 Set((Int)) 2 1 Nil Set(Nil) True 1 Set(1 1)
```
rakupp 4.0.1-84: differs — `Bag.new(<a b>, <a b>)` has one element of weight 2 (equal Lists are conflated).

### SB-03  `new-from-pairs`: a Pair's value is the weight                     D:yes R:partial V:quirk
`new-from-pairs` (and every `.Set`/`.Bag`/`.Mix` coercion of a list, see
SB-05) reads a Pair as `element => weight` and a non-Pair as an element of
weight 1. Set: the element is included when the value is true (`"b" => ""`
and `"a" => 0` are skipped). Bag: the value is coerced with `.Int` (2.9 gives
2, `"3"` 3, `1e0` the Int 1, `True` 1), repeated keys add up, and a
non-positive weight is SKIPPED, never subtracted: `(a => 2, a => -2).Bag` is
`Bag(a(2))` and `(a => 3, a => -2).Bag` is `Bag(a(3))`. Mix: the value is
coerced with `.Real` (`"3"` gives the Int 3, `2.9e0` stays a Num), repeated
keys are summed algebraically and a key whose sum reaches zero is removed
(`(a => -1, a => 1).Mix` is `Mix()`). An Array of Pairs as the sole argument
is iterated. With no arguments the immutable forms return the empty
sentinel. A named argument is ignored.
```
say Set.new-from-pairs("a" => 1, "b" => 0, "c").gist, " ", Bag.new-from-pairs("a" => 2, "a" => 3, "b" => 0, "c" => -1, "d").gist, " ", Mix.new-from-pairs("a" => 0.5, "a" => -0.5, "b" => -1, "c" => 1/3, "d").gist, " ", Set.new-from-pairs(<a b>).gist, " ", SetHash.new-from-pairs("a" => "x", "b" => "").gist, " ", BagHash.new-from-pairs("a" => "3").gist, " ", MixHash.new-from-pairs("a" => "2.5").gist, " ", Bag.new-from-pairs("a" => 2.9).gist, " ", Bag.new-from-pairs("a" => 1e0).values.raku, " ", Mix.new-from-pairs("a" => 1e0).pairs.raku, " ", Bag.new-from-pairs("a" => True).gist, " ", Set.new-from-pairs([a => 1]).gist, " ", Bag.new-from-pairs(("a" => 2, "b" => 1)).gist, " ", Set.new-from-pairs.gist, " ", Bag.new-from-pairs.raku, " ", (Set.new-from-pairs =:= set()), " ", Set.new-from-pairs("a" => 0).elems, " ", Mix.new-from-pairs("a" => "3").pairs.raku, " ", BagHash.new-from-pairs("a" => 2.9).pairs.raku, " ", Mix.new-from-pairs("a" => 2.9e0).pairs.raku, " ", Set.new-from-pairs(a => 1).elems, " ", (a => 2, a => -2).Bag.gist, " ", (a => 3, a => -2).Bag.gist, " ", (a => -1, a => 1).Mix.gist, " ", (a => -1, a => 1, "b").Mix.gist, " ", Bag.new-from-pairs("a" => -5, "a").gist, " ", BagHash.new-from-pairs("a" => 0).elems
# rakudo 2026.08: Set(a c) Bag(a(5) d) Mix(b(-1) c(0.333333) d) Set(a b) SetHash(a) BagHash(a(3)) MixHash(a(2.5)) Bag(a(2)) (1,).Seq (:a(1e0),).Seq Bag(a) Set(a) Bag(a(2) b) Set() bag() True 0 (:a(3),).Seq (:a(2),).Seq (:a(2.9e0),).Seq 0 Bag(a(2)) Bag(a(3)) Mix() Mix(b) Bag(a) 0
```
The skipped negative weight is the quirk: a Bag never subtracts during
construction, while a Mix does.
rakupp 4.0.1-84: differs — `MixHash.new-from-pairs("a" => "2.5")` truncates to 2, `(a => 2, a => -2).Bag` is empty and `(a => 3, a => -2).Bag` is `Bag(a)` (negative weights are subtracted), the empty sentinel is not shared, and a named argument becomes an element.

### SB-04  Weights that are refused                                          D:no R:partial V:quirk
Refusals are THROWN (not Failures), by `new-from-pairs`, the `.Bag`/`.Mix`
coercers of lists and hashes, and the two-list `STORE`. Bag/BagHash: a
non-numeric Str `X::Str::Numeric`; `Inf`, `-Inf`, `NaN`, the Num `1e400` and
the string `"1e400"` `X::Numeric::CannotConvert`; a Complex
`X::Numeric::Real`; the Bool type object `X::Multi::NoMatch`. Mix/MixHash: a
non-numeric Str `X::Str::Numeric`; `Inf`, `-Inf`, `NaN` and `1e400`
`X::OutOfRange` with `.what` "Value", `.got` the number and `.range`
`-Inf^..^Inf`; a Complex `X::Numeric::Real`; the Str type object contributes
weight 0 (with a warning) and nothing is added. The quirk: the STRING `"Inf"`
passes the Mix check (only a Num is tested) and produces `Mix(a(Inf))`, and a
Range as a Bag weight counts its elements (`"a" => 1..2` gives `a(2)`).
```
say do { sub T(&c) { my $r = do { c() }; CATCH { default { return "T:" ~ .^name } }; $r ~~ Failure ?? do { $r.so; "F:" ~ $r.exception.^name } !! $r.gist }; (T({ Bag.new-from-pairs("a" => "x") }), T({ Bag.new-from-pairs("a" => Inf) }), T({ Bag.new-from-pairs("a" => NaN) }), T({ Bag.new-from-pairs("a" => 3i) }), T({ Mix.new-from-pairs("a" => "x") }), T({ Mix.new-from-pairs("a" => Inf) }), T({ Mix.new-from-pairs("a" => -Inf) }), T({ Mix.new-from-pairs("a" => NaN) }), T({ Mix.new-from-pairs("a" => 3i) }), T({ ("a" => 3i).Bag }), T({ ("a" => "x").Mix }), T({ Bag.new-from-pairs("a" => "3") }), T({ %(a => "x").Bag }), T({ %(a => Inf).Mix }), T({ %(a => 2i).Mix }), T({ quietly Bag.new-from-pairs("a" => Bool) }), T({ quietly Mix.new-from-pairs("a" => Str) }), T({ Bag.new-from-pairs("a" => 1..2) }), T({ Bag.new-from-pairs("a" => "1e400") }), T({ Mix.new-from-pairs("a" => "Inf") }), T({ Mix.new-from-pairs("a" => 1e400) }), T({ Bag.new-from-pairs("a" => 1e400) }), T({ BagHash.new-from-pairs("a" => "x") }), T({ MixHash.new-from-pairs("a" => NaN) }), T({ BagHash.new.STORE(<a>, ("x",)) }), T({ MixHash.new.STORE(<a>, (Inf,)) })).join(" ") }
# rakudo 2026.08: T:X::Str::Numeric T:X::Numeric::CannotConvert T:X::Numeric::CannotConvert T:X::Numeric::Real T:X::Str::Numeric T:X::OutOfRange T:X::OutOfRange T:X::OutOfRange T:X::Numeric::Real T:X::Numeric::Real T:X::Str::Numeric Bag(a(3)) T:X::Str::Numeric T:X::OutOfRange T:X::Numeric::Real T:X::Multi::NoMatch Mix() Bag(a(2)) T:X::Numeric::CannotConvert Mix(a(Inf)) T:X::OutOfRange T:X::Numeric::CannotConvert T:X::Str::Numeric T:X::OutOfRange T:X::Str::Numeric T:X::OutOfRange
say (try Mix.new-from-pairs("a" => Inf)) // $!.^name ~ ":" ~ $!.what ~ ":" ~ $!.got ~ ":" ~ $!.range
# rakudo 2026.08: X::OutOfRange:Value:Inf:-Inf^..^Inf
```
rakupp 4.0.1-84: differs — every non-Str refusal is `X::Numeric::CannotConvert` (no `X::OutOfRange`, no `X::Numeric::Real`), a Bool type object, a Range weight and the string `"1e400"` are silently dropped, `"Inf"` becomes an empty Mix, and the two-list `STORE` stores the raw `"x"` and `Inf`; the second probe died at `.what` (the exception has no such attribute).

### SB-05  The coercers `.Set`, `.Bag`, `.Mix` and their `-Hash` forms       D:yes R:yes V:spec
On an Iterable the coercer FLATTENS recursively (`((1,2),(3,4)).Set` and
`([1,2],[3,4]).Set` have 4 elements, `(1, (2, (3, 4))).Set` 4; an itemized
`$[1,2]` stays one Array element) and then applies the Pair rule of SB-03:
`("a" => 2, "b" => 0, "c", "a").Bag` is `Bag(a(3) c)`, `(a => 0).Set` is empty.
On a Hash the keys become elements and the values weights (a Map key is a
Str; an object hash keeps its key objects: `(:{1 => 3}).Bag` has an Int key,
`{1 => 3}.Bag` a Str key); `{a => 2.5}.Bag` truncates to `a(2)`. A Range
coerces to its numbers. Any other value is one element: `42.Set`, `"ab".Set`,
`(a => 1).Set` is `Set(a)` and `(a => 0).Set` empty (a lone Pair is still a
weight), `(1 => 2).Bag` is `Bag(1(2))` with an Int key. Type objects coerce to
a one-element collection holding themselves (`Int.Set` is `Set((Int))`,
`Set.Set` is `Set((Set))`, `Any.Set` `Set((Any))`); `Nil.Set` is `Set(Nil)`;
`Mu` has no coercer (`X::Method::NotFound`).
```
say <a b a>.Set.gist, " ", <a b a>.Bag.gist, " ", <a b a>.Mix.gist, " ", ("a" => 2, "b" => 0, "c", "a").Set.gist, " ", ("a" => 2, "b" => 0, "c", "a").Bag.gist, " ", ("a" => 2.5, "b" => -1, "c").Mix.gist, " ", {a => 2, b => 0}.Set.gist, " ", {a => 2, b => 0}.Bag.gist, " ", {a => 2.5, b => -1}.Mix.gist, " ", {a => 2.5}.Bag.gist, " ", {a => "x", b => ""}.Set.gist, " ", (:{ 1 => "x", 2 => "" }).Set.gist, " ", (:{ 1 => 3 }).Bag.keys[0].^name, " ", {1 => 3}.Bag.keys[0].^name, " ", (1..3).Set.gist, " ", (1..3).Bag.gist, " ", do { my @a = <x y>; my %h = a => 1; (@a, %h).Set.gist }, " ", ((1,2),(3,4)).Set.elems, " ", ([1,2],[3,4]).Set.elems, " ", ($[1,2],).Set.elems, " ", ($[1,2],).Set.keys[0].^name, " ", (a => 1).Set.gist, " ", (a => 0).Set.gist, " ", (a => 2).Bag.gist, " ", 42.Set.gist, " ", "ab".Set.gist, " ", (1, (2, 3)).Set.elems, " ", (1, (2, (3, 4))).Set.elems, " ", (1, $(2, 3)).Set.elems, " ", (1, [2, 3]).Bag.elems, " ", <a b a>.Set.^name, " ", {a => 1}.SetHash.^name, " ", (1,2).Seq.Set.elems, " ", (:a, :!b, :3c, :0d, :e<meow>, :f(""), "g").Set.gist, " ", Int.Set.gist, " ", List.Set.gist, " ", Nil.Set.gist, " ", Nil.Bag.gist, " ", Nil.SetHash.gist, " ", Any.Set.gist, " ", Set.Set.gist, " ", Set.Bag.gist, " ", Str.Bag.raku, " ", (try Mu.Set.gist) // $!.^name, " ", (Nil (elem) Nil.Set), " ", (Any (elem) Any.Set), " ", Set.list.raku, " ", Set.Set.elems, " ", (1 => 2).Bag.gist, " ", (1 => 2).Bag.keys[0].^name
# rakudo 2026.08: Set(a b) Bag(a(2) b) Mix(a(2) b) Set(a c) Bag(a(3) c) Mix(a(2.5) b(-1) c) Set(a) Bag(a(2)) Mix(a(2.5) b(-1)) Bag(a(2)) Set(a) Set(1) Int Str Set(1 2 3) Bag(1 2 3) Set(a x y) 4 4 1 Array Set(a) Set() Bag(a(2)) Set(42) Set(ab) 3 4 2 3 Set SetHash 2 Set(a c e g) Set((Int)) Set((List)) Set(Nil) Bag(Nil) SetHash(Nil) Set((Any)) Set((Set)) Bag((Set)) (Str=>1).Bag X::Method::NotFound True True (Set,) 1 Bag(1(2)) Int
```
rakupp 4.0.1-84: differs — `(1, (2, (3, 4))).Set` has 3 elements (one level of flattening only) and `Mu.Set` answers `Set((Mu))`.

### SB-06  Conversions between the six types, snapshots, the sentinels        D:partial R:partial V:spec
`.Set`/`.SetHash` of a Bag or Mix keep the keys with a positive weight (a
Mix's negative weights are dropped: `("a" => -1).Mix.Set` is empty; a
positive fraction is kept); `.Bag`/`.BagHash` of a Mix keep the keys whose
`.Int` weight is positive (2.7 gives 2, 0.4 and negatives vanish, a weight of
10**20 stays exact); `.Mix`/`.MixHash` of a Bag keep the weights; a Set gives
weight 1 everywhere. `.Setty`/`.Baggy`/`.Mixy` map to the same mutability:
on type objects `Set.Setty`/`Bag.Setty`/`Mix.Setty` are `Set`, the `-Hash`
ones `SetHash`, `Set.Baggy` `Bag`, `SetHash.Baggy` `BagHash`, and so on; on
instances they convert (`$bag.Setty` is `$bag.Set`) or return the invocant
(`$bag.Baggy === $bag`). Converting an immutable to its own type returns the
same object (`$bag.Bag === $bag`, `$set.Set === $set`); converting an empty
collection to an immutable type returns the sentinel (`bag().Set =:= set()`,
`SetHash.new.Set =:= set()`, `BagHash.new.Mix =:= mix()`). A conversion FROM a
hash form is a snapshot: mutating the SetHash afterwards does not change the
Set taken from it, `.clone` is independent, and `.SetHash` of a Set is a
fresh object every time.
```
say do { my $b = <a a b>.Bag; ($b.Set.gist, $b.SetHash.gist, $b.BagHash.gist, $b.Mix.gist, $b.MixHash.gist, $b.Bag === $b, $b.Setty.gist, $b.Baggy === $b, $b.Mixy.^name, $b.Set.^name, $b.SetHash.^name).join(" ") }, " | ", do { my $m = ("a" => 2.7, "b" => -1, "c" => 0.4, "d" => 1).Mix; ($m.Set.gist, $m.SetHash.gist, $m.Setty.gist, $m.Mixy === $m, $m.Mix === $m, $m.MixHash.gist, $m.Bag.^name).join(" ") }, " | ", do { my $s = <a b>.Set; ($s.Bag.gist, $s.Bag<a>, $s.Mix.gist, $s.SetHash.gist, $s.Setty === $s, $s.Baggy.gist, $s.Mixy.gist, $s.BagHash.gist, $s.MixHash.gist, $s.Set === $s, $s.Set =:= $s).join(" ") }, " | ", do { my $sh = SetHash.new(<a>); my $s = $sh.Set; $sh<b> = True; ($s.gist, $sh.gist).join(" ") }, " | ", do { my $bh = BagHash.new(<a>); my $b = $bh.Bag; my $m = $bh.Mix; my $c = $bh.clone; $bh<a> = 5; ($b.gist, $m.gist, $c.gist, $bh.gist).join(" ") }, " | ", do { my $s = set(<a>); my $sh = $s.SetHash; $sh<b> = True; ($s.gist, $sh.gist).join(" ") }, " | ", do { my $s = SetHash.new(<a>); my $c = $s.clone; $c<b> = True; ($s.gist, $c.gist, $s.SetHash =:= $s, $s.clone === $s).join(" ") }, " | ", do { my $b = bag(<a>); ($b.Bag =:= $b, $b.Set === set(<a>), $b.Mix === mix(<a>), bag().Set =:= set(), bag().Mix =:= mix(), mix().Bag =:= bag(), mix().Set =:= set(), set().Bag =:= bag(), set().Mix =:= mix(), set().BagHash.^name, ("a" => -1).Mix.Set.gist, ("a" => -0.5).Mix.SetHash.gist, ("a" => 10**20).Mix.Bag.gist, set(<a>).Bag =:= set(<a>).Bag, SetHash.new.Set =:= set(), BagHash.new.Bag =:= bag(), MixHash.new.Mix =:= mix(), BagHash.new.Mix =:= mix(), set().SetHash.elems, set().SetHash.^name, bag().BagHash.^name, mix().MixHash.^name).join(" ") }, " | ", do { my $m = mix(<a>); my $mh = $m.MixHash; $mh<a> = 5; ($m.gist, $mh.gist).join(" ") }, " | ", Set.Setty.^name, " ", SetHash.Setty.^name, " ", Bag.Setty.^name, " ", BagHash.Setty.^name, " ", Mix.Setty.^name, " ", MixHash.Setty.^name, " ", Set.Baggy.^name, " ", SetHash.Baggy.^name, " ", Bag.Baggy.^name, " ", Mix.Baggy.^name, " ", MixHash.Baggy.^name, " ", Set.Mixy.^name, " ", SetHash.Mixy.^name, " ", Bag.Mixy.^name, " ", BagHash.Mixy.^name, " ", Mix.Mixy.^name
# rakudo 2026.08: Set(a b) SetHash(a b) BagHash(a(2) b) Mix(a(2) b) MixHash(a(2) b) True Set(a b) True Mix Set SetHash | Set(a c d) SetHash(a c d) Set(a c d) True True MixHash(a(2.7) b(-1) c(0.4) d) Bag | Bag(a b) 1 Mix(a b) SetHash(a b) True Bag(a b) Mix(a b) BagHash(a b) MixHash(a b) True False | Set(a) SetHash(a b) | Bag(a) Mix(a) BagHash(a) BagHash(a(5)) | Set(a) SetHash(a b) | SetHash(a) SetHash(a b) False False | False True True True True True True True True BagHash Set() SetHash() Bag(a(100000000000000000000)) False True True True True 0 SetHash BagHash MixHash | Mix(a) MixHash(a(5)) | Set SetHash Set SetHash Set SetHash Bag BagHash Bag Bag BagHash Mix MixHash Mix MixHash Mix
```
rakupp 4.0.1-84: differs — `.Set`/`.SetHash`/`.Setty` of a Mix keep the negatively weighted keys, the empty conversions are not the sentinels (`bag().Set =:= set()` and `SetHash.new.Set =:= set()` are False), and `Mix.Baggy`/`MixHash.Baggy` answer `Mix`/`MixHash`.

### SB-07  `.Bag` of a Mix truncates the weights of the source Mix              D:no R:no V:bug
Calling `.Bag` or `.BagHash` on a Mix or MixHash modifies the SOURCE: every
weight of 1 or more is replaced in place by its `.Int` (2.7 becomes 2, the
`.total` changes from 0.5 to 1.4 in the probe), while weights below 1 and
negative ones are left alone in the source (they are simply absent from the
result). `.Set`, `.SetHash` and `.MixHash` do not touch the source. Do not
imitate: a conversion must never change its invocant; the result must be
computed on a copy.
```
say do { my $m = ("a" => 2.7, "b" => -1.5).Mix; my $g1 = $m.gist; my $b = $m.Bag; my $g2 = $m.gist; ($g1, $b.gist, $g2, $m<a>.raku, $m.total).join(" ") }, " | ", do { my $m = ("a" => 2.7).MixHash; my $b = $m.BagHash; ($m.gist, $b.gist).join(" ") }, " | ", do { my $m = ("a" => 2.7).Mix; my $b = $m.BagHash; ($m.gist, $b.gist).join(" ") }, " | ", do { my $m = ("a" => 2.7).MixHash; my $b = $m.Bag; ($m.gist, $b.gist).join(" ") }, " | ", do { my $m = ("a" => 2.7).Mix; my $s = $m.Set; my $h = $m.MixHash; ($m.gist, $s.gist, $h.gist).join(" ") }, " | ", do { my $m = ("a" => 2.7, "b" => -1, "c" => 0.4).Mix; my $g1 = $m.gist; my $b = $m.Bag; ($g1, $b.gist, $m.gist, $m.total).join(" ") }, " | ", do { my $m = ("a" => 0.4).Mix; my $b = $m.Bag; ($b.gist, $m.gist, $m.elems, $m<a>).join(" ") }
# rakudo 2026.08: Mix(a(2.7) b(-1.5)) Bag(a(2)) Mix(a(2) b(-1.5)) 2 0.5 | MixHash(a(2)) BagHash(a(2)) | Mix(a(2)) BagHash(a(2)) | MixHash(a(2)) Bag(a(2)) | Mix(a(2.7)) Set(a) MixHash(a(2.7)) | Mix(a(2.7) b(-1) c(0.4)) Bag(a(2)) Mix(a(2) b(-1) c(0.4)) 1.4 | Bag() Mix(a(0.4)) 1 0.4
```
rakupp 4.0.1-84: differs, correctly — the source Mix keeps `a(2.7)` and its total; keep that.

### SB-08  Lazy sources: Failures from constructors, throws from operators      D:no R:partial V:quirk
`Set.new(^Inf)`, `(^Inf).Set`, `Bag.new-from-pairs(1..*)`, `set(*..*)`,
`MixHash.new(lazy 1..3)`, `(lazy 1..3).Bag`, `mix(1..*)` and the rest of the
constructor/coercer family RETURN a Failure whose exception is
`X::Cannot::Lazy` with `.what` the type name and `.action` "coerce";
`classify-list` on a lazy list fails the same way with action "classify".
`(elem)`/`(cont)` with a lazy right side return a Failure with `.what` ""
and `.action` "(elem)" — and an INFINITE Range is such a lazy right side
(`1 (elem) 1..*`, `1.5 (elem) (1..*)`, `"a" (elem) ("a"..*)`; only a finite
Int range is tested arithmetically, so `10**42 (elem) 0..10**42` is True at
once). The value-producing operators THROW `X::Cannot::Lazy` for a lazy
operand on either side (`(|)`, `(&)`, `(-)`, `(^)`, `(+)`, `(.)`, also
`set(1) (-) (1..*)`), while the Boolean `(<=)`, `(==)` and `(<)` throw
`X::Multi::Ambiguous` for a lazy Seq (the quirk). `.set`, `.add` and `.unset`
on a hash form given an infinite list never return (measured in SB-30).
```
say do { sub T(&c) { my $r = do { c() }; CATCH { default { return "T:" ~ .^name } }; $r ~~ Failure ?? do { $r.so; "F:" ~ $r.exception.^name ~ ":" ~ $r.exception.what ~ ":" ~ $r.exception.action } !! $r.gist }; (T({ Set.new(^Inf) }), T({ (^Inf).Set }), T({ Bag.new-from-pairs(1..*) }), T({ set(*..*) }), T({ MixHash.new(lazy 1..3) }), T({ (lazy 1..3).Bag }), T({ 1 (elem) (1..*).map({$_}) }), T({ Set.new(1..*) }), T({ (1..*).BagHash }), T({ SetHash.new-from-pairs(1..*) }), T({ mix(1..*) }), T({ 1 (elem) 1..* }), T({ 1.5 (elem) (1..*) }), T({ (1..*) (cont) 1 }), T({ "a" (elem) ("a"..*) }), T({ BagHash.new.classify-list({$_}, 1..*) })).join(" ") }, " ", (try (set(1) (|) (1..*).map({$_})).elems) // $!.^name, " ", (try ((1..*).map({$_}) (-) set(1)).elems) // $!.^name, " ", (try (set(1) (&) (1..*).map({$_})).elems) // $!.^name, " ", (try (set(1) (^) (1..*).map({$_})).elems) // $!.^name, " ", (try (set(1) (+) (1..*).map({$_})).elems) // $!.^name, " ", (try (set(1) (.) (1..*).map({$_})).elems) // $!.^name, " ", (try set(1) (<=) (1..*).map({$_})) // $!.^name, " ", (try set(1) (==) (1..*).map({$_})) // $!.^name, " ", (try set(1) (<) (1..*).map({$_})) // $!.^name, " ", (try ((1..*) (|) set(1)).elems) // $!.^name, " ", (try (set(1) (-) (1..*)).elems) // $!.^name, " ", (10**42 (elem) 0..10**42), " ", (1 (elem) 1..3), " ", (2.5 (elem) 1.5..4)
# rakudo 2026.08: F:X::Cannot::Lazy:Set:coerce F:X::Cannot::Lazy:Set:coerce F:X::Cannot::Lazy:Bag:coerce F:X::Cannot::Lazy:Set:coerce F:X::Cannot::Lazy:MixHash:coerce F:X::Cannot::Lazy:Bag:coerce F:X::Cannot::Lazy::(elem) F:X::Cannot::Lazy:Set:coerce F:X::Cannot::Lazy:BagHash:coerce F:X::Cannot::Lazy:SetHash:coerce F:X::Cannot::Lazy:Mix:coerce F:X::Cannot::Lazy::(elem) F:X::Cannot::Lazy::(elem) F:X::Cannot::Lazy::(elem) F:X::Cannot::Lazy::(elem) F:X::Cannot::Lazy:BagHash:classify X::Cannot::Lazy X::Cannot::Lazy X::Cannot::Lazy X::Cannot::Lazy X::Cannot::Lazy X::Cannot::Lazy X::Multi::Ambiguous X::Multi::Ambiguous X::Multi::Ambiguous X::Cannot::Lazy X::Cannot::Lazy True True True
```
rakupp 4.0.1-84: differs — the first three refusals are thrown rather than returned, and `set(*..*)` builds a huge finite Set (`*..*` is a range of native-Int limits) that filled the output; the rest of the line is unmeasured. Measured separately: the operators with a lazy operand do throw `X::Cannot::Lazy`.

### SB-09  Parameterized types `Set[T]`, `keyof`, `of`                          D:yes R:partial V:spec
`Set[Str]` is a distinct type named `Set[Str]` whose `.keyof` is Str (plain
`Set.keyof` is Mu); `.of` is Bool for Setty, UInt for Baggy, Real for Mixy on
both type objects and instances. An element that does not match the
constraint throws `X::TypeCheck::Binding` (`.expected` the constraint,
`.got` the value) at construction, `new-from-pairs`, `add`, `set`, subscript
assignment and `++` on an autovivified `SetHash[Int]`; no coercion is
attempted (`Set[Int].new("1")` throws) unless the constraint is a coercion
type (`Set[Int()].new("3")` holds the Int 3). `.raku` and `.gist` carry the
type: `Set[Str].new("a")`, `Bag[Str].new-from-pairs("a"=>2)`, `().Bag[Str]`,
`Set[Str](a)`; the WHICH prefix is `Set[Str]|`, so `Set[Int].new(1) === set(1)`
is False and `eqv` is False. `Set[Int].new` is a real empty object, not the
sentinel. A set operator keeps the LEFT operand's parameterization
(`Set[Int] (|) Set` is a `Set[Int]`, `Set (|) Set[Int]` a plain `Set`) and
checks the right operand's elements against it (`Set[Int].new(1) (|)
set("x")` throws `X::TypeCheck::Binding`). Conversions drop the
parameterization (`Set[Int].new(1).Bag` is a plain `Bag`, keyof Mu).
Smartmatch: `Set[Int].new(1) ~~ Set` and `~~ Set[Int]` True, `set(1) ~~
Set[Int]` False.
```
say Set.of.^name, " ", SetHash.of.^name, " ", Bag.of.^name, " ", BagHash.of.^name, " ", Mix.of.^name, " ", MixHash.of.^name, " ", Set.keyof.^name, " ", Set[Str].keyof.^name, " ", Set[Str].^name, " ", Bag[Int].new(1,2).gist, " ", (try Set[Int].new(<a b>)) // $!.^name, " ", Bag[Str].new-from-pairs("a" => 2).raku, " ", Set[Str].new(<a>).raku, " ", (try do { my $s = SetHash[Str].new; $s{42} = True; $s }) // $!.^name, " ", Set[Str].new(<a>).WHICH.Str.substr(0,9), " ", Mix[Int].new(1).of.^name, " ", (try Bag[Int].new-from-pairs("a" => 1)) // $!.^name, " ", (try BagHash[Int].new.add("a")) // $!.^name, " ", Set[Int].new(1).keyof.^name, " ", set(1).keyof.^name, " ", Set[Int].new(1) === set(1), " ", Set[Int].new(1) eqv set(1), " ", (Set[Int].new(1) (|) set(2)).^name, " ", (try Set[Int].new(1) (|) set("x")) // $!.^name, " ", (set(1) (|) Set[Int].new(2)).^name, " ", (try (Set[Int].new(1) (|) Set[Str].new("x")).^name) // $!.^name, " ", Set[Int].new.raku, " ", Set[Str].new.gist, " ", Bag[Str].new.raku, " ", MixHash[Str].new.raku, " ", Set[Int].new(1).SetHash.^name, " ", Set[Int].new(1).Bag.^name, " ", Set[Int].new(1).Bag.keyof.^name, " ", Set[Int].new(1).Bag.raku, " ", Set[Int].new =:= set(), " ", (Set[Int].new(1) (==) set(1)), " ", Set[Int].new(1) ~~ Set, " ", Set[Int] ~~ Set, " ", Set[Int].new(1) ~~ Set[Int], " ", set(1) ~~ Set[Int], " ", Set[Int].new(1) ~~ Set[Str], " ", Set[Str].new(<a>).gist, " ", (try SetHash[Int].new.set("a")) // $!.^name, " ", do { my SetHash[Int] $s; (try { $s<a>++; "ok" }) // $!.^name }, " ", do { my SetHash[Int] $s; $s{1}++; $s.gist ~ " " ~ $s.^name }, " ", (try Set[Int].new("1")) // $!.^name, " ", Set[Cool].new(1, "a").elems, " ", (try Set[Int()].new("3").keys[0].^name) // $!.^name
# rakudo 2026.08: Bool Bool UInt UInt Real Real Mu Str Set[Str] Bag[Int](1 2) X::TypeCheck::Binding Bag[Str].new-from-pairs("a"=>2) Set[Str].new("a") X::TypeCheck::Binding Set[Str]| Real X::TypeCheck::Binding X::TypeCheck::Binding Int Mu False False Set[Int] X::TypeCheck::Binding Set X::TypeCheck::Binding Set[Int].new() Set[Str]() ().Bag[Str] ().MixHash[Str] SetHash Bag Mu (1=>1).Bag False True True True True False False Set[Str](a) X::TypeCheck::Binding X::TypeCheck::Binding SetHash[Int](1) SetHash[Int] X::TypeCheck::Binding 2 Int
say (try Set[Int].new(<a b>)) // $!.^name ~ ":" ~ $!.expected.^name ~ ":" ~ $!.got.raku
# rakudo 2026.08: X::TypeCheck::Binding:Int:"a"
```
rakupp 4.0.1-84: differs — the parameterization survives only in `.^name`: `.raku`/`.gist`/WHICH show the plain type, `SetHash[Str]{42} = True`, `Bag[Int].new-from-pairs("a" => 1)`, `SetHash[Int].new.set("a")`, `Set[Int].new(1) (|) set("x")` and `my SetHash[Int] $s; $s<a>++` are accepted, `BagHash[Int].new.add("a")` answers a path, `Set[Int].new(1) === set(1)` and `set(1) ~~ Set[Int]` are True, the operator result is a plain `Set`, an autovivified `SetHash[Int]` is a Hash, and `Set[Int()].new("3")` refuses instead of coercing; the second probe died at `.expected`.

### SB-10  `%h is Set`, `STORE`, and the uninitialised `is Set` variable         D:partial R:partial V:bug
`my %s is Set = <a b a>` is a real Set (`.^name` Set); assigning again,
`:delete` and `%s<a> = False` are `X::Assignment::RO`. `my %s is SetHash`
can be re-assigned (`%s = <c d>` replaces the contents), so can `is BagHash`.
`is Bag = a => 2, b => 0` and `is Set = a => 2, b => 0` apply the Pair rule
(`Bag(a(2))`, `Set(a)`); `is MixHash = a => 1, b => 0, c => 2` omits b. `is
Set[Int]` type-checks (`X::TypeCheck::Binding`). A single value (`= 42`) and
an empty list (`= ()`, giving `Set()`, not the sentinel) both work; `%h :=
set <a b>` binds a Set. The bug: `my %h is Set;` WITHOUT an initializer
yields an unusable object — `.elems`, `.gist`, `.raku`, `.Bool`, `<a>`
throw `X::AdHoc`, `.keys` is empty, it is not the sentinel, and a later
assignment is `X::Assignment::RO`; `my %h is Bag;` likewise (`.raku`,
`.total` throw). `my %h is SetHash;` is fine. `STORE` with two lists
(`BagHash.new.STORE(<a b c>, (1, 2, 4))`, `SetHash` with Bools, `MixHash`
with Reals) zips objects and weights, sums repeated keys, drops a zero,
equals the `Z=>` form, and a shorter value list is `X::Multi::NoMatch`;
`STORE` with one list replaces the contents; `Set.new.STORE(<a b>)` is
`X::Assignment::RO`. A `Set`-typed Hash slot accepts a Set and refuses a
List (`X::TypeCheck::Assignment`).
```
say do { my %s is Set = <a b a>; (%s.^name, %s.gist, %s<a>, %s<z>, (try { %s = <c>; "ok" }) // $!.^name, (try { %s<a>:delete; "ok" }) // $!.^name, (try { %s<a> = False; "ok" }) // $!.^name).join(" ") }, " | ", do { my %s is SetHash = <a b>; %s = <c d>; (%s.gist, (%s<c>:delete), %s.gist, (%s<d> = False), %s.elems).join(" ") }, " | ", do { my %b is Bag = <a a b>; (%b.gist, %b<a>, %b<z>, (try { %b = <c>; "ok" }) // $!.^name).join(" ") }, " | ", do { my %m is MixHash = a => 1, b => 0, c => 2; (%m.gist, (%m<b>:exists), %m.elems).join(" ") }, " | ", do { my %s is Set[Int] = 1,2; (%s.keyof.^name, %s.gist, (try { my %t is Set[Int] = <a>; "ok" }) // $!.^name).join(" ") }, " | ", do { my %s is Set = 42; %s.gist }, " | ", do { my %h := set <a b>; (%h.^name, %h<a>).join(" ") }, " | ", do { my %h is Bag = a => 2, b => 0; %h.gist }, " | ", do { my %h is Set = a => 2, b => 0; %h.gist }, " | ", do { my %h is Set; ((try %h.elems) // $!.^name, (try %h.gist) // $!.^name, %h.keys.raku, (%h =:= set()), (try { %h = <a>; "ok" }) // $!.^name, %h.^name).join(" ") }, " | ", do { my %h is Bag; ((try %h.raku) // $!.^name, (try %h.total) // $!.^name).join(" ") }, " | ", do { my %h is Set = (); (%h.gist, (%h =:= set())).join(" ") }, " | ", do { my %h is SetHash; (%h<a>++).raku ~ " " ~ %h.gist }, " | ", do { my %h is Set = <a b>; %h.keys.sort.raku ~ " " ~ set().keys.raku }, " | ", do { my %h is BagHash = <a a>; %h = <b>; %h.gist }, " | ", BagHash.new.STORE(["a","b","c"], [1,2,4]).gist, " ", SetHash.new.STORE(<a b c>, (True, False, True)).gist, " ", MixHash.new.STORE(<a b>, (0.5, -1)).gist, " ", (BagHash.new.STORE(["a","b"], [1,2]) eqv BagHash.new.STORE(["a","b"] Z=> [1,2])), " ", do { my $s = SetHash.new(<a b>); $s.STORE(<c>); $s.gist }, " ", (try Set.new.STORE(<a b>)) // $!.^name, " ", BagHash.new.STORE(<a>, (0,)).gist, " ", (try BagHash.new.STORE(<a b>, (2,)).gist) // $!.^name, " ", MixHash.new.STORE(<a a>, (1, 2)).gist, " ", BagHash.new.STORE(<a>, (2.9,)).gist, " ", do { my %h is Set = <a>; my %g := %h; %g.^name }, " ", do { my Set %h; %h<x> = set(<a>); %h<x>.gist ~ " " ~ ((try { %h<y> = <a>; "ok" }) // $!.^name) }
# rakudo 2026.08: Set Set(a b) True False X::Assignment::RO X::Assignment::RO X::Assignment::RO | SetHash(c d) True SetHash(d) False 0 | Bag(a(2) b) 2 0 X::Assignment::RO | MixHash(a c(2)) False 2 | Int Set[Int](1 2) X::TypeCheck::Binding | Set(42) | Set True | Bag(a(2)) | Set(a) | X::AdHoc X::AdHoc ().Seq False X::Assignment::RO Set | X::AdHoc X::AdHoc | Set() False | Bool::False SetHash(a) | ("a", "b").Seq ().Seq | BagHash(b) | BagHash(a b(2) c(4)) SetHash(a c) MixHash(a(0.5) b(-1)) True SetHash(c) X::Assignment::RO BagHash() X::Multi::NoMatch MixHash(a(3)) BagHash(a(2)) Set Set(a) X::TypeCheck::Assignment
```
rakupp 4.0.1-84: differs — `is Set[Int]` throws `X::TypeCheck::Binding::Parameter`, an uninitialised `my %h is Set;` is a usable empty Set that accepts assignment (keep that), `Set.new.STORE(<a b>)` stores, a zero or fractional weight in the two-list `STORE` becomes an extra element (`BagHash(0 a)`, `BagHash(2.9 a)`), and a short value list is accepted.

## B. Introspection and stringification

### SB-11  Set: keys, values, pairs, kv, antipairs, elems, total, minpairs, maxpairs, default   D:yes R:yes V:spec
`keys` is a Seq of the elements, `values` a Seq of `True` once per element,
`pairs` a Seq of `elem => True`, `kv` the interleaving, `antipairs` and
`invert` `True => elem`; `elems` and `total` are the element count (Int);
`minpairs` and `maxpairs` are all the pairs; `default` is `False`; `+$set`,
`.Int`, `.Num`, `.Real` are the count, `.Numeric` an Int; `.list`/`.List`
are Lists of the pairs; on the empty set every Seq is empty and total 0; a
value pulled from `.values` is a plain Bool, not a container; `.keys = ...`
is `X::Assignment::RO`. SetHash answers the same shapes (its values are
Proxies, SB-24).
```
say do { my $s = set <b a>; ($s.keys.sort.raku, $s.values.raku, $s.pairs.sort.raku, $s.kv.sort.raku, $s.antipairs.sort(*.value).raku, $s.elems, $s.total, $s.minpairs.sort.raku, $s.maxpairs.sort.raku, $s.default.raku, $s.Bool, +$s, $s.Int, $s.Numeric.^name, $s.Num, $s.Real, $s.keys.^name, $s.values.^name, $s.pairs.^name, $s.kv.^name, $s.list.^name, $s.List.^name, $s.list.sort.raku, set().keys.raku, set().values.raku, set().pairs.raku, set().minpairs.raku, set().total, set().elems, SetHash.new(<a>).values.raku, SetHash.new(<a>).kv.raku, SetHash.new(<a>).antipairs.raku, $s.invert.sort.raku, $s.list.elems, $s.elems.^name, $s.total.^name, $s.pairs.sort[0].key.^name, $s.pairs.sort[0].value.^name, set(<a>).values[0].VAR.^name, set(<a>).minpairs.^name, (try { $s.keys = <c>; "ok" }) // $!.^name).join(" ") }
# rakudo 2026.08: ("a", "b").Seq (Bool::True, Bool::True).Seq (:a, :b).Seq (Bool::True, Bool::True, "a", "b").Seq (Bool::True => "a", Bool::True => "b").Seq 2 2 (:a, :b).Seq (:a, :b).Seq Bool::False True 2 2 Int 2 2 Seq Seq Seq Seq List List (:a, :b).Seq ().Seq ().Seq ().Seq ().Seq 0 0 (Bool::True,).Seq ("a", Bool::True).Seq (Bool::True => "a",).Seq (Bool::True => "a", Bool::True => "b").Seq 2 Int Int Str Bool Bool Seq X::Assignment::RO
```
rakupp 4.0.1-84: differs — `.Numeric` is a Num, `set().minpairs` is a List and `set(<a>).minpairs` a List where Rakudo answers Seqs.

### SB-12  Bag: weights, kxxv, minpairs by weight                              D:yes R:yes V:spec
`values` are the Int weights, `pairs`/`kv`/`antipairs`/`invert` carry them;
`elems` is the number of distinct keys, `total` (and `+$bag`, `.Int`,
`.Numeric`) the sum of weights; `minpairs`/`maxpairs` select by weight and
return Lists (`(:b(1),)`, `(:c(3),)`, all ties); `default` is 0; `kxxv` is a
Seq of each key repeated weight times; `.list`/`.List`/`.Seq` have one Pair
per key; a value pulled from an immutable Bag's `.values` is a plain Int,
from a BagHash a Proxy; `.values = ...` is `X::Assignment::RO`; the empty bag
has total 0 and empty Seqs.
```
say do { my $b = bag <b a a c c c>; ($b.keys.sort.raku, $b.values.sort.raku, $b.pairs.sort.raku, $b.kv.sort.raku, $b.antipairs.sort.raku, $b.invert.sort.raku, $b.elems, $b.total, +$b, $b.Int, $b.minpairs.raku, $b.maxpairs.raku, $b.default, $b.kxxv.sort.raku, $b.kxxv.^name, $b.Bool, bag().Bool, $b.values.^name, $b.total.^name, bag().total, $b.list.elems, $b.List.elems, $b.Seq.elems, $b.Numeric, bag(<a b>).minpairs.sort.raku, bag(<a a b b>).maxpairs.sort.raku, bag().minpairs.raku, bag().kxxv.raku, bag().antipairs.raku, BagHash.new(<a a>).kxxv.raku, $b.list[0].^name, bag(<a>).values[0].VAR.^name, BagHash.new(<a>).values[0].VAR.^name, (try { $b.values = 1,2; "ok" }) // $!.^name).join(" ") }
# rakudo 2026.08: ("a", "b", "c").Seq (1, 2, 3).Seq (:a(2), :b(1), :c(3)).Seq (1, 2, 3, "a", "b", "c").Seq (1 => "b", 2 => "a", 3 => "c").Seq (1 => "b", 2 => "a", 3 => "c").Seq 3 6 6 6 (:b(1),) (:c(3),) 0 ("a", "a", "b", "c", "c", "c").Seq Seq True False Seq Int 0 3 3 3 6 (:a(1), :b(1)).Seq (:a(2), :b(2)).Seq () ().Seq ().Seq ("a", "a").Seq Pair Int Proxy X::Assignment::RO
```
rakupp 4.0.1-84: differs — `.kxxv` is a List, `bag().kxxv` and `BagHash.kxxv` are Lists, and a BagHash's `.values[0]` is a plain Int rather than a Proxy.

### SB-13  Mix: Real totals, negative weights, `kxxv`                          D:partial R:partial V:spec
Weights are Reals kept as given (Rat, Num, Int); `total` sums them exactly
(`1.5 + -2 + 1/3` is the Rat -1/6, printed -0.166667; `0.1 + 0.2` is 0.3;
`1/3 + 2/3` is 1), its type follows the weights (Int for whole weights, Rat,
Num when any weight is a Num); `+$mix`, `.Numeric` are the total, `.Int`
truncates it; `elems` counts keys; `minpairs`/`maxpairs` select by weight
including negatives; `default` is 0; `.Bool` is True whenever a key exists,
even when every weight is negative and the total is -1; `.kxxv` returns a
Failure (`X::AdHoc`) on Mix and MixHash.
```
say do { my $m = ("a" => 1.5, "b" => -2, "c" => 1/3).Mix; ($m.pairs.sort.raku, $m.total, $m.total.^name, +$m, $m.Int, $m.Numeric.^name, $m.elems, $m.minpairs.raku, $m.maxpairs.raku, $m.default, $m.Bool, ("a" => -1).Mix.Bool, ("a" => -1).Mix.total, $m.values.sort.raku, $m.antipairs.sort.raku, mix().total, ("a" => 1e0).Mix.total.^name, ("a" => 0.1, "b" => 0.2).Mix.total.raku, mix(<a a>).total.^name, ("a" => 1/3).MixHash.total.^name, ("a" => 1.5).MixHash.total, ("a" => 1.5, "b" => 1.5).Mix.minpairs.sort.raku, mix().minpairs.raku, ("a" => -1).Mix.Numeric, ("a" => 1/3, "b" => 2/3).Mix.total, ("a" => 0.5, "b" => 0.5e0).Mix.total.^name, ("a" => 1/2).Mix.Int, ("a" => -1/2).Mix.Int, $m.kxxv.^name).join(" ") }
# rakudo 2026.08: (:a(1.5), :b(-2), :c(<1/3>)).Seq -0.166667 Rat -0.166667 0 Rat 3 (:b(-2),) (:a(1.5),) 0 True True -1 (-2, <1/3>, 1.5).Seq (-2 => "b", <1/3> => "c", 1.5 => "a").Seq 0 Num 0.3 Int Rat 1.5 (:a(1.5), :b(1.5)).Seq () -1 1 Num 0 0 Failure
```
rakupp 4.0.1-84: differs — `.kxxv` on a Mix is `X::Method::NotFound`, so the line died at its last field; the totals before it matched.

### SB-14  gist, Str, raku and what ordering is guaranteed                     D:partial R:partial V:spec
`.gist` is `Type(` + elements + `)`: the elements' gists SORTED as strings,
a Baggy element with weight ≠ 1 printed as `key(weight)` before sorting (so
`Bag(a(2) b)`, `Set(1 10 2)`, `Set(A B a b)`, `Set((Int) Nil x)`); the gist
ordering is the only guaranteed one. `.Str` joins the same element texts
with spaces in storage order (NOT sorted; a key containing a space is
indistinguishable in it). `.raku` (storage order): Set `Set.new("a","b")`,
the empty sentinel `set()`; SetHash `SetHash.new("a")` / `SetHash.new()`;
Bag `("a"=>2).Bag`, sentinel `bag()`, empty BagHash `().BagHash`, BagHash
`("a"=>1).BagHash`; Mix `("a"=>2).Mix`, `mix()`, `().MixHash`; a
parameterized type `Set[Str].new("a")`, `Set[Str].new()`,
`Bag[Str].new-from-pairs("a"=>2)`, `().Bag[Str]`; a weight prints with its
own `.raku` (`1e0`, `0.5`), a gist with its own gist (`0.333333`). The type
objects gist `(Set)` and raku `Set`. A mixin keeps a ValueObjAt WHICH. A
SetHash that contains itself prints nested without looping.
```
say set(<b a>).gist, "|", set(<b a>).Str.words.sort.join(" "), "|", set(<b a>).raku.comb(/\w+/).sort.join(","), "|", set().raku, "|", set().gist, "|", set().Str.raku, "|", SetHash.new.raku, "|", SetHash.new.gist, "|", SetHash.new(<a>).raku, "|", bag(<b a a>).gist, "|", bag(<b a a>).Str.words.sort.join(" "), "|", bag(<a a>).raku, "|", bag().raku, "|", bag().gist, "|", BagHash.new.raku, "|", BagHash.new(<a>).raku, "|", BagHash.new(<a>).gist, "|", mix(<a a>).raku, "|", mix().raku, "|", MixHash.new.raku, "|", ("a" => 0.5).MixHash.raku, "|", ("a" => 0.5).Mix.gist, "|", ("a" => -1).Mix.gist, "|", ("a" => 1/3).Mix.gist, "|", (1, "1", $[1]).Set.gist, "|", Bag[Str].new(<a a>).raku, "|", Set[Str].new(<a>).raku, "|", Mix[Str].new-from-pairs("a" => 0.5).raku, "|", Set[Str].new.raku, "|", Bag[Str].new.raku, "|", Set[Str].new(<a>).gist, "|", ("a" => 1e0).Mix.gist, "|", ("a" => 1e0).Mix.raku, "|", set(1, 2, 10).gist, "|", bag(10 => 1, 9 => 2).gist, "|", set(<b B a A>).gist, "|", mix(<a>).Str, "|", ("a" => 2.5).Mix.Str, "|", set("a b", "c").gist, "|", set("a b", "c").Str.chars, "|", Set.gist, "|", Set.raku, "|", SetHash.gist, "|", BagHash.raku, "|", Set.new(1, <1>).elems, "|", bag(<a b b>).raku.comb(/\w+/).sort.join(","), "|", MixHash.new(<a b b>).raku.comb(/\w+/).sort.join(","), "|", set(Nil, Int, "x").gist, "|", set(Nil, Int, "x").raku.comb(/\w+/).sort.join(","), "|", set(1.5, 1/3, 1e0).gist, "|", set(<a b>, <a c>).gist, "|", bag("a b" => 2).gist, "|", set("").gist, "|", set("").raku, "|", ("" => 2).Bag.gist, "|", set(<a b>).gist.^name, "|", (set(<a>) but role {}).WHICH.^name, "|", do { my %sh is SetHash; %sh ,= 1; (%sh.elems, %sh.gist ~~ /SetHash\(1\s+SetHash/ ?? "nested" !! "flat", ?(%sh.raku ~~ /SetHash/)).join(" ") }
# rakudo 2026.08: Set(a b)|a b|Set,a,b,new|set()|Set()|""|SetHash.new()|SetHash()|SetHash.new("a")|Bag(a(2) b)|a(2) b|("a"=>2).Bag|bag()|Bag()|().BagHash|("a"=>1).BagHash|BagHash(a)|("a"=>2).Mix|mix()|().MixHash|("a"=>0.5).MixHash|Mix(a(0.5))|Mix(a(-1))|Mix(a(0.333333))|Set(1 1 [1])|Bag[Str].new-from-pairs("a"=>2)|Set[Str].new("a")|Mix[Str].new-from-pairs("a"=>0.5)|Set[Str].new()|().Bag[Str]|Set[Str](a)|Mix(a)|("a"=>1e0).Mix|Set(1 10 2)|Bag(10 => 1 9 => 2)|Set(A B a b)|a|a(2.5)|Set(a b c)|5|(Set)|Set|(SetHash)|BagHash|2|1,2,Bag,a,b|1,2,MixHash,a,b|Set((Int) Nil x)|Int,Nil,Set,new,x|Set(0.333333 1 1.5)|Set(a b c)|Bag(a b => 2)|Set()|Set.new("")|Bag((2))|Str|ValueObjAt|2 nested True
```
rakupp 4.0.1-84: differs — `(1, "1", $[1]).Set` has one element, parameterized types print as the plain type, a mixin's WHICH is an ObjAt, and the self-referencing SetHash prints flat.

### SB-15  hash, Hash, Map, Capture, fmt                                       D:partial R:partial V:spec
`.hash` is an OBJECT hash typed by the weight kind — `Hash[Bool,Mu]` for
Setty, `Hash[UInt,Mu]` for Baggy, `Hash[Real,Mu]` for Mixy (the name shows a
third `Any` slot): keys keep their objects (Int 1 and Str "1" stay two
keys), assigning an out-of-range value to it is `X::TypeCheck::Assignment`
(-1 or "x" into a Bag's hash), 5 is fine; `.Hash` is `Hash[Any,Mu]` with
unconstrained values; both are copies (mutating them does not touch a
SetHash). `.Map` is a plain Map whose keys are the elements' STRINGS, so 1 and
"1" collide (elems 1; for a Bag the later one wins, no summing) and a
missing key is Nil. `.Capture` is `\(:a(2), :b(1))`. `.fmt("%s")` formats
keys, `.fmt("%s=%s", sep)` key and weight, the default format is `%s\t%s`
joined by newlines. On a type object `.hash` and `.Hash` are `{}`, `.Capture`
`\()`, `.Map` throws `X::AdHoc`.
```
say do { my $s = set(1, "a"); ($s.hash.^name, $s.hash.keys.map(*.^name).sort.raku, $s.hash<a>.raku, $s.Hash.^name, $s.Map.^name, $s.Map.keys.sort.raku, $s.Map<1>, $s.Capture.^name, $s.fmt("%s").split("\n").sort.raku, $s.fmt("%s=%s", ",").split(",").sort.raku, $s.fmt.split("\n").sort.map(*.subst("\t", "<TAB>")).raku, $s.hash.of.^name, $s.hash.keyof.^name, $s.Hash.of.^name, $s.hash{1}.raku, $s.Map{1}.raku, $s.Map.elems).join(" ") }, " | ", do { my $b = bag <a a b>; ($b.hash.^name, $b.hash.sort.raku, $b.Hash.^name, $b.Map.raku, $b.Capture.raku, $b.fmt("%s:%s", ";").split(";").sort.raku, (try { $b.hash<a> = -1; "ok" }) // $!.^name, (try { $b.hash<a> = "x"; "ok" }) // $!.^name, (try { $b.Hash<a> = "x"; "ok" }) // $!.^name, (try { $b.hash<a> = 5; "ok" }) // $!.^name, $b.hash.of.^name, $b.hash<z>.raku, $b.Map<z>.raku).join(" ") }, " | ", do { my $m = ("a" => 0.5).Mix; ($m.hash.^name, $m.hash.raku, $m.Map.raku, (try { $m.hash<a> = "x"; "ok" }) // $!.^name, ("a" => -2).Mix.hash.raku, set().hash.raku, bag().Hash.raku, mix().Map.raku, $m.hash.of.^name, $m.Hash.of.^name).join(" ") }, " | ", do { my $s = set($[1,2]); ($s.hash.keys[0].^name, $s.Map.keys[0].^name, $s.Map.keys[0], $s.Hash.keys[0][1]).join(" ") }, " | ", do { my $s = SetHash.new(<a>); my $h = $s.hash; $h<b> = True; ($s.gist, $h.sort.raku).join(" ") }, " | ", do { my $s = set(1, "1"); ($s.hash.elems, $s.Map.elems, $s.Hash.elems).join(" ") }, " | ", do { my $b = bag(1, "1"); ($b.Map.elems, $b.Map.raku).join(" ") }, " | ", Set.hash.raku, " ", Bag.hash.raku, " ", Set.Capture.raku, " ", (try Set.Map.raku) // $!.^name, " ", (try Set.Hash.raku) // $!.^name
# rakudo 2026.08: Hash[Bool,Mu,Any] ("Int", "Str").Seq Bool::True Hash[Any,Mu] Map ("1", "a").Seq True Capture ("1", "a").Seq ("1=True", "a=True").Seq ("1<TAB>True", "a<TAB>True").Seq Bool Mu Any Bool::True Bool::True 2 | Hash[UInt,Mu,Any] (:a(2), :b(1)).Seq Hash[Any,Mu] Map.new((:a(2),:b(1))) \(:a(2), :b(1)) ("a:2", "b:1").Seq X::TypeCheck::Assignment X::TypeCheck::Assignment ok ok UInt Any Nil | Hash[Real,Mu,Any] (my Real %{Mu} = :a(0.5)) Map.new((:a(0.5))) X::TypeCheck::Assignment (my Real %{Mu} = :a(-2)) (my Bool %{Mu}) (my Any %{Mu}) Map.new Real Any | Array Str 1 2 2 | SetHash(a) (:a(Bool::True), :b(Bool::True)).Seq | 2 1 2 | 1 Map.new(("1" => 1)) | {} {} \() X::AdHoc {}
```
rakupp 4.0.1-84: differs — `.hash` is an untyped Hash that conflates `1` and `"1"` (elems 1), value assignment to it is `X::Assignment::RO`, `.Map` sums (`"1" => 2`), and `Set.Map` throws `X::Hash::Store::OddNumber`.

### SB-16  WHICH, `===`, `eqv`, `==`, `cmp`                                      D:no R:partial V:spec
Set, Bag and Mix are value types: `.WHICH` is a `ValueObjAt` of the form
`Set|` + a 40-hex digest (44 characters), so `===` holds for the same
elements in any order and, for Baggy, the same weights (`bag(<a a>) ===
bag(<b a a>)`, `mix(<a>) === ("a" => 1.0).Mix`); different types never
`===` (`set(<a>) === bag(<a>)`, `bag(<a a>) === ("a" => 2).Mix` False); a
subclass prefixes its own name (`MySet|`) and a mixin breaks `===`.
SetHash/BagHash/MixHash have an ObjAt WHICH (identity), so two equal ones
are not `===` and `.unique` keeps both. `eqv` requires the same type and
`(==)` equality: `set eqv SetHash`, `set eqv bag`, `bag eqv mix` are all
False, `SetHash.new(<a>) eqv SetHash.new(<a>)` True. `==` is numeric
(`set(<a b>) == set(<c d>)` True, `bag(<a a>) == 2` True). `cmp` compares the
SORTED pairs (`set(<a b>) cmp set(<a c>)` Less, `set(<a>) cmp bag(<a>)`
Same because `True cmp 1` is Same, a QuantHash against a plain Iterable
compares pairs to items), `<=>` the totals; `before`/`after` and `sort`
follow `cmp`. An Array element is an identity key (`set($[1]) === set($[1])`
False), a Range a value key.
```
say set(<a b>).WHICH.^name, " ", set(<a b>).WHICH.Str.substr(0,4), " ", set(<a b>).WHICH.Str.chars, " ", (set(<a b>) === set(<b a>)), " ", (set(<a b>) === set(<a>)), " ", (bag(<a a b>) === bag(<b a a>)), " ", (bag(<a b>) === bag(<a a b>)), " ", (mix(<a>) === ("a" => 1).Mix), " ", (("a" => 1.0).Mix === ("a" => 1).Mix), " ", (("a" => 1e0).Mix === ("a" => 1).Mix), " ", (set(<a>) === bag(<a>)), " ", (bag(<a a>) === ("a" => 2).Mix), " ", SetHash.new(<a>).WHICH.^name, " ", (SetHash.new(<a>) === SetHash.new(<a>)), " ", (BagHash.new(<a>) === BagHash.new(<a>)), " ", (set(<a b>) eqv set(<b a>)), " ", (set(<a>) eqv SetHash.new(<a>)), " ", (set(<a>) eqv bag(<a>)), " ", (bag(<a>) eqv mix(<a>)), " ", (SetHash.new(<a>) eqv SetHash.new(<a>)), " ", (BagHash.new(<a>) eqv BagHash.new(<a>)), " ", (MixHash.new(<a>) eqv MixHash.new(<a>)), " ", (bag(<a>) eqv bag(<a a>)), " ", (bag(<a>) eqv BagHash.new(<a>)), " ", (set(<a b>) == set(<c d>)), " ", (bag(<a a>) == 2), " ", (set(<a b>) cmp set(<a c>)), " ", (set(<a b>) cmp set(<a b>)), " ", (set(<a b>) cmp (<a b>)), " ", (bag(<a a>) <=> bag(<b>)), " ", (set() === set()), " ", (set() eqv set()), " ", (set() =:= set()), " ", (set() === Set.new), " ", do { class MySet is Set {}; (MySet.new(<a>) === set(<a>)) ~ " " ~ MySet.new(<a>).WHICH.Str.substr(0,6) ~ " " ~ (MySet.new(<a>) eqv set(<a>)) ~ " " ~ (MySet.new(<a>) === MySet.new(<a>)) }, " ", ((set(<a>), set(<a>)).unique.elems), " ", ((SetHash.new(<a>), SetHash.new(<a>)).unique.elems), " ", (set(1) === set("1")), " ", ("a Str|b Str|c".Set.WHICH eq <a b c>.Set.WHICH), " ", bag(<a>).WHICH.Str.substr(0,4), " ", mix(<a>).WHICH.Str.substr(0,4), " ", (set(<a>) === (set(<a>) but role {})), " ", (set(<a>) eqv (set(<a>) but role {})), " ", (set(<a>) eqv Set), " ", (Set eqv Set), " ", (set($[1]) === set($[1])), " ", (set(1..2) === set(1..2)), " ", (bag(<a a b>).WHICH eq bag(<a b b>).WHICH), " ", (set(<a>) cmp set(<b>)), " ", (bag(<a a>) cmp bag(<a>)), " ", (set(<a>) cmp bag(<a>)), " ", (set(<a>) cmp "a"), " ", (set(<a>) before set(<b>)), " ", (sort(set(<b>), set(<a>)).map(*.gist).raku)
# rakudo 2026.08: ValueObjAt Set| 44 True False True False True True True False False ObjAt False False True False False False True True True False False True True Less Same More More True True True True False MySet| False True 1 2 False False Bag| Mix| False False False True False True False Less More Same Same True ("Set(a)", "Set(b)").Seq
```
rakupp 4.0.1-84: differs — the WHICH is 7 characters (`Set|` plus a counter, not a digest), `MySet.new(<a>) === set(<a>)` is True while `MySet === MySet` is False, `SetHash`es are `.unique`d as one, `set(1) === set("1")` is True, a mixin does not break `===`, `set($[1]) === set($[1])` is True, `set(<a b>) cmp <a b>` is Same, and `sort` on two sets prints the pairs.

### SB-17  ACCEPTS and smartmatch                                             D:partial R:yes V:spec
A type object on the right tests the role/class (`~~ Set`, `~~ Setty`, `~~
QuantHash`, `~~ Associative` True; `~~ Iterable`, `~~ Positional`, `~~ Map`,
`~~ Cool` False; a Bag is not a Setty; `Set ~~ Setty:D` False). An instance
on the right coerces the LEFT side to its own kind and tests `(==)`: a Set
on the right coerces to Set, so a list, a Seq, a Hash (truthy keys) and a
Bag with any weights all match when the keys agree (`bag(<a a b>) ~~
set(<a b>)` True) and a Str matches a one-element set of itself (`"a" ~~
set(<a>)` True, `42 ~~ set(42)` True, `"42" ~~ set(42)` False); a Bag on the
right coerces to Bag, so weights count (`set(<a b>) ~~ bag(<a a b>)` False,
`{a => 2} ~~ bag(<a a>)` True); a Mix on the right coerces to Mix
(`bag(<a>) ~~ ("a" => 0.5).Mix` False). `Set ~~ set()` is False (the type
object becomes a one-element set), `Nil ~~ set()` and `Any ~~ set()` False,
`().Set ~~ set(1)` False. The MRO is `Set, Any, Mu`; the roles `Setty,
QuantHash, Associative` and for Mix `Mixy, Baggy, QuantHash, Associative`.
```
say (set(<a b>) ~~ set(<b a>)), " ", (set(<a>) ~~ set(<a b>)), " ", ("a" ~~ set(<a b>)), " ", ("a" ~~ set(<a>)), " ", (<a b> ~~ set(<a b>)), " ", (<a b a> ~~ set(<a b>)), " ", ((1,2).Seq ~~ set(1,2)), " ", (bag(<a a b>) ~~ set(<a b>)), " ", (set(<a b>) ~~ bag(<a a b>)), " ", (set(<a b>) ~~ bag(<a b>)), " ", (bag(<a b>) ~~ mix(<a b>)), " ", (mix(<a b>) ~~ bag(<a b>)), " ", (("a" => 0.5).Mix ~~ bag(<a>)), " ", (bag(<a>) ~~ ("a" => 0.5).Mix), " ", ({a => 1, b => 1} ~~ set(<a b>)), " ", ({a => 1, b => 0} ~~ set(<a>)), " ", ({a => 2} ~~ bag(<a a>)), " ", ({a => 2} ~~ set(<a>)), " ", (set(<a b>) ~~ Set), " ", (set(<a b>) ~~ Setty), " ", (set(<a b>) ~~ QuantHash), " ", (bag(<a>) ~~ Set), " ", (bag(<a>) ~~ Setty), " ", (SetHash.new(<a>) ~~ Setty), " ", (set(<a>) ~~ SetHash), " ", (SetHash.new(<a>) ~~ set(<a>)), " ", (set(<a>) ~~ SetHash.new(<a>)), " ", (set() ~~ set()), " ", ((1,2,3).Set.ACCEPTS(().Set)), " ", (().Set ~~ set(1)), " ", (42 ~~ set(42)), " ", ("42" ~~ set(42)), " ", (Set ~~ Set), " ", (Set ~~ set()), " ", (set() ~~ Set), " ", (Nil ~~ set()), " ", (Any ~~ set()), " ", (set(<a>) ~~ Associative), " ", (set(<a>) ~~ Iterable), " ", (set(<a>) ~~ Positional), " ", (set(<a>) ~~ Map), " ", (set(<a>) ~~ Any), " ", (set(<a>) ~~ Cool), " ", (mix(<a>) ~~ Baggy), " ", (mix(<a>) ~~ Bag), " ", (Mix ~~ Baggy), " ", (set(<a>) ~~ Setty:D), " ", (Set ~~ Setty:D), " ", ((a => 1) ~~ set(<a>)), " ", ((a => 1) ~~ set(("a" => 1),)), " ", (set(<a>).ACCEPTS(SetHash.new(<a>))), " ", (bag(<a>).ACCEPTS(<a>)), " ", (bag(<a a>).ACCEPTS(<a a>)), " ", (bag(<a a>).ACCEPTS(<a>)), " ", (mix(<a>).ACCEPTS({a => 1.0})), " ", (set(<a>) ~~ (set(<a>), set(<b>)).any), " ", (set(<a>) ~~ Set | Bag), " ", (Setty ~~ QuantHash)
# rakudo 2026.08: True False False True True True True True False True True True False False True True True True True True True False False True False True True True False False True False True False True False False True False False False True False True False True True False True False True True True False True True True True
say (Set.^mro.map(*.^name).raku), " ", (Set.^roles.map(*.^name).raku), " ", (Mix.^roles.map(*.^name).raku), " ", (MixHash.^mro.map(*.^name).raku), " ", (Setty ~~ QuantHash), " ", (Mixy ~~ Baggy), " ", (QuantHash ~~ Associative)
# rakudo 2026.08: ("Set", "Any", "Mu").Seq ("Setty", "QuantHash", "Associative").Seq ("Mixy", "Baggy", "QuantHash", "Associative").Seq ("MixHash", "Any", "Mu").Seq True True True
```
rakupp 4.0.1-84: differs — a Hash on the left is not coerced (`{a => 1, b => 1} ~~ set(<a b>)` and `{a => 2} ~~ bag(<a a>)` False), `"42" ~~ set(42)` True, `~~ Map` and `~~ Cool` True, `(a => 1) ~~ set(<a>)` False while `(a => 1) ~~ set(("a" => 1),)` True, `mix(<a>).ACCEPTS({a => 1.0})` False, `Setty ~~ QuantHash` False, and `.^roles` does not exist (the second probe died).

## C. Subscripts and mutation

### SB-18  Subscripts on the immutable types                                   D:yes R:yes V:spec
`$set<a>` is a Bool (True/False), `$bag<a>` an Int (0 when absent),
`$mix<a>` the Real weight (0, an Int, when absent; a Rat/Num/Int weight
keeps its type); a multi-key subscript is a List of those; `:exists`,
`:p`/`:k`/`:v` adverbs work (`:p` of a missing key is `()`); `$s{*}` gives
all values; the zen slice `$s<>`/`$s{}` is the collection itself. Assigning
to a key (existing or new), `:delete`, `:delete:exists`, `.keys = `, `.values
= `, `DELETE-KEY`, `ASSIGN-KEY` are `X::Assignment::RO`; `++` is
`X::Multi::NoMatch` (the value is not a container: `$s<a>.VAR` is a Bool);
binding a key (`:=`) is `X::Bind`. On the type objects `Set.AT-KEY`
and `Bag.AT-KEY` are `X::Assignment::RO` and `Set.EXISTS-KEY` False.
```
say do { my $s = set <a b>; my $b = bag <a a b>; my $m = ("a" => 0.5).Mix; ($s<a>.raku, $s<z>.raku, $s{"a"}, $s<a b z>.raku, ($s<a>:exists), ($s<z>:exists), (try { $s<a> = True; "ok" }) // $!.^name, (try { $s<z> = True; "ok" }) // $!.^name, (try { $s<a>:delete }) // $!.^name, (try { $s<a>++; "ok" }) // $!.^name, $b<a>.raku, $b<z>.raku, $b<a>.^name, $b<a z>.raku, ($b<z>:exists), (try { $b<a> = 5; "ok" }) // $!.^name, (try { $b<a>++; "ok" }) // $!.^name, (try { $b<a>:delete }) // $!.^name, $m<a>.raku, $m<z>.raku, (try { $m<a> = 1; "ok" }) // $!.^name, (try { $m<a>:delete }) // $!.^name, $s{*}.sort.raku, $b{*}.sort.raku, ($s<a>:p).raku, ($s<z>:p).raku, ($b<a>:k).raku, ($b<z>:k).raku, ($b<a>:v).raku, ($s<a z>:exists).raku, ($b<a>:!exists), $s<>.gist, $s{}.gist, (try $s<a>:delete:exists) // $!.^name, $s<a>.VAR.^name, $b<a>.VAR.^name, (try { $s<a> := True; "ok" }) // $!.^name, $s.AT-KEY("a"), $s.EXISTS-KEY("a"), (try $s.DELETE-KEY("a")) // $!.^name, (try $s.ASSIGN-KEY("a", True)) // $!.^name, $s<a b>.^name, $s{"a", "z"}.raku, $b{"a", "z"}.raku, ($s<z>:exists:p).raku, ($b<a>:exists:kv).raku, $s<a>.WHAT.gist, $b<z>.WHAT.gist, $m<z>.WHAT.gist, ("a" => 1/3).Mix<a>.WHAT.gist, ("a" => 1e0).Mix<a>.WHAT.gist, ("a" => 3).Mix<a>.WHAT.gist, (try Set.AT-KEY("a")) // $!.^name, (try Set.EXISTS-KEY("a")) // $!.^name, (try Bag.AT-KEY("a")) // $!.^name).join(" ") }
# rakudo 2026.08: Bool::True Bool::False True (Bool::True, Bool::True, Bool::False) True False X::Assignment::RO X::Assignment::RO X::Assignment::RO X::Multi::NoMatch 2 0 Int (2, 0) False X::Assignment::RO X::Multi::NoMatch X::Assignment::RO 0.5 0 X::Assignment::RO X::Assignment::RO (Bool::True, Bool::True).Seq (1, 2).Seq :a () "a" () 2 (Bool::True, Bool::False) False Set(a b) Set(a b) X::Assignment::RO Bool Int X::Bind True True X::Assignment::RO X::Assignment::RO List (Bool::True, Bool::False) (2, 0) () ("a", Bool::True) (Bool) (Int) (Int) (Rat) (Num) (Int) X::Assignment::RO False X::Assignment::RO
```
rakupp 4.0.1-84: differs — `++` on a Set or Bag key and `:=` into a key are `X::Assignment::RO`, `DELETE-KEY`/`ASSIGN-KEY` succeed, and `Set.AT-KEY`/`.EXISTS-KEY` are `X::Method::NotFound`.

### SB-19  Which values are the same element                                   D:partial R:partial V:spec
Membership is by `WHICH`: `1`, `"1"`, `1.0`, `1e0` and the IntStr `<1>` are
five elements (`1/1` and `1.00` are the Rat `1.0`); two `[1]` Arrays and a
`(1,)` List are three elements found only by the same object (`$s.AT-KEY(@a)`
True for the stored array, `[1]` False); a Pair is a value (`"a" => 1` twice
is one element, `"a" => 1.0` and `"a" => 2` are others); type objects are
elements; `Nil` passed among several `.new` arguments arrives as `Any`;
Sets, Bags and Ranges are values (`Set.new(set(<a>), bag(<a>), set(<a>),
SetHash.new(<a>), SetHash.new(<a>))` has 4: the two Sets merge, the two
SetHashes do not; `Set.new(1..2, 1..2, ^2)` has 2 and `0..1` is not `^2`);
Dates, Versions and Complexes are values; equal Rats are one element, two
Nums that differ by rounding are two; `True`, `1` and `"True"` are three;
a multi-key subscript with a list is a slice, `AT-KEY` takes the object.
```
say do { my $s = Set.new(1, "1", 1.0, 1e0, [1], [1], (1,), "a" => 1, "a" => 1, Int, Str, Any, Nil, <1>); ($s.elems, $s.keys.map(*.^name).sort.raku, $s.AT-KEY(1), $s.AT-KEY("1"), $s.AT-KEY(1.0), $s.AT-KEY([1]), $s.AT-KEY((1,)), $s.AT-KEY("a" => 1), $s.AT-KEY(Int), $s.AT-KEY(Any), $s.AT-KEY(Nil).raku, (Nil (elem) $s), (1 (elem) $s), ("1" (elem) $s), (1e0 (elem) $s), (<1> (elem) $s), $s.AT-KEY(<1>), $s.AT-KEY(1 => 1), $s.AT-KEY(2), $s.AT-KEY(1/1), $s.AT-KEY(1.00), $s.AT-KEY("1" => 1), $s.AT-KEY(True), $s.AT-KEY(Set), $s.AT-KEY(Mu), $s.AT-KEY("a" => 2), $s.AT-KEY("a" => 1.0)).join(" ") }, " | ", do { my $b = bag(1, "1", <1>, 1.0); ($b.elems, $b{1}, $b{"1"}, $b.AT-KEY(<1>), $b{1.0}, $b.keys.map(*.^name).sort.raku).join(" ") }, " | ", do { my @a = 1; my @b = 1; my $s = Set.new(@a, @b, @a); ($s.elems, $s.AT-KEY(@a), $s.AT-KEY(@b), $s.AT-KEY([1]), (@a (elem) $s), ([1] (elem) $s), ((1,) (elem) $s), (@a (elem) (@a,))).join(" ") }, " | ", do { my $s = set(1, 2); ($s{1, 2}.raku, $s{1 .. 3}.raku, $s{$(1, 2)}, $s.AT-KEY((1, 2)), $s{(1, 2)}.raku).join(" ") }, " | ", do { my $s = Set.new(set(<a>), bag(<a>), set(<a>), SetHash.new(<a>), SetHash.new(<a>)); ($s.elems, $s.AT-KEY(set(<a>)), $s.AT-KEY(SetHash.new(<a>)), $s.AT-KEY(bag(<a>)), $s.AT-KEY(mix(<a>))).join(" ") }, " | ", do { my $s = Set.new(1..2, 1..2, ^2, "a".."b"); ($s.elems, $s.AT-KEY(1..2), $s.AT-KEY(^2), $s.AT-KEY(0..1)).join(" ") }, " | ", set("a", "a".Str, "A".lc, "a" ~ "").elems, " ", set(1, 1 + 0, 2 - 1, 10**30, 10**30 + 0).elems, " ", set(0.1 + 0.2, 0.3).elems, " ", set(0.1e0 + 0.2e0, 0.3e0).elems, " ", set(1/3, 2/6).elems, " ", set(True, 1, "True").elems, " ", set(Date.new("2020-01-01"), Date.new("2020-01-01")).elems, " ", set(Version.new("1.0"), Version.new("1.0")).elems, " ", set(1i, 1i, Complex.new(0, 1)).elems, " ", Set.new((1, 2), (1, 2)).elems, " ", set({ 1 }, { 1 }).elems, " ", set(Bag.new(<a>), Bag.new(<a>)).elems, " ", set(BagHash.new(<a>), BagHash.new(<a>)).elems, " ", Set.new(1..2, 1..2).elems, " ", set("a" => 1, "a" => 1).elems, " ", set(Int, Int).elems, " ", set(<1>, 1).elems, " ", set("a", "a".Str).keys[0].^name
# rakudo 2026.08: 12 ("Any", "Array", "Array", "Int", "Int", "IntStr", "List", "Num", "Pair", "Rat", "Str", "Str").Seq True True True False False True True True Bool::False False True True True True True False False True True False False False False False False | 4 1 1 1 1 ("Int", "IntStr", "Rat", "Str").Seq | 2 True True False True False False True | (Bool::True, Bool::True) (Bool::True, Bool::True, Bool::False) False False (Bool::True, Bool::True) | 4 True False True False | 3 True True False | 1 2 1 2 1 3 1 1 1 2 2 1 2 1 1 1 2 Str
```
rakupp 4.0.1-84: differs — the line died at an undefined value early on (a missing key answered Any); measured elsewhere: Int 1 and Str "1" are one key (SB-01), equal Lists one key (SB-02).

### SB-20  SetHash: assignment, `++`/`--`, `:delete`, `set`, `unset`              D:partial R:yes V:quirk
Assigning to a key stores the Bool of the value and returns that Bool:
`= 2`, `= "x"`, `= -1` add (True), `= 0`, `= Nil`, `= False` remove (False).
`++` returns the OLD Bool and adds; `--` returns the old Bool and removes;
`--` on a missing key returns False and adds nothing. `:delete` returns
True when the key was present, False otherwise. `.set(x)` and `.unset(x)`
return Nil; they iterate their argument: a Str is one element, a list adds
each item, an Array flattens, a Range adds its numbers, `""` and `Nil` are
elements, `()` adds nothing; `.unset("abc")` removes the element "abc", not
letters. The quirk: `.set` of a Set/Bag/Mix argument iterates the
QuantHash's PAIRS, so `.set(set(<a b>))` adds the two Pairs `a => True` and
`b => True` as elements (use `.keys`). `.clear`, `.add`, `.remove` do not
exist (`X::Method::NotFound`). A slice assignment `<a b> = True, False`
returns the Bools.
```
say do { my $s = SetHash.new(<a b>); (($s<a> = False).raku, $s.gist, ($s<c> = 2).raku, $s.gist, ($s<c> = 0).raku, ($s<d> = Nil).raku, $s.gist, ($s<a>++).raku, $s.gist, ($s<a>++).raku, ($s<b>--).raku, $s.gist, ($s<z>--).raku, $s.gist, ($s<a>:delete).raku, ($s<a>:delete).raku, $s.gist, ($s<a> = "x").raku, $s<a>.raku, $s.set("q").raku, $s.gist, $s.set(<r s>).raku, $s.gist, $s.unset(<q r>).raku, $s.gist, $s.set(<a b>.Set).raku, $s.gist, $s.unset("a").raku, $s.gist, ($s<a> -= 1).raku, $s.gist, (try $s.clear) // $!.^name, ($s<s> += 1).raku, $s.gist, (try $s.add("x")) // $!.^name, (try $s.remove("x")) // $!.^name, ($s<s> = -1).raku, $s<s>.raku, $s.set(bag(<x>).keys).raku, $s.gist, $s.unset(<x s>).raku, $s.set([<m n>]).raku, $s.gist, $s.set(("m" => 1),).raku, $s.gist, $s.set("abc".comb).raku, $s.gist, $s.unset("abc").raku, $s.gist, $s.set(1..2).raku, $s.gist, $s.unset(mix(<m>).keys).raku, ($s<z>:exists), $s.set(Nil).raku, $s.gist, $s.unset(Nil).raku, $s.gist, $s.set("").raku, $s.gist, ($s{""}:exists), $s.set(()).raku, $s.gist, ($s<a b> = True, False).raku, $s.gist, $s.set(<a b>).WHAT.gist, $s.unset(<a b>).WHAT.gist).join(" ") }
# rakudo 2026.08: Bool::False SetHash(b) Bool::True SetHash(b c) Bool::False Bool::False SetHash(b) Bool::False SetHash(a b) Bool::True Bool::True SetHash(a) Bool::False SetHash(a) Bool::True Bool::False SetHash() Bool::True Bool::True Nil SetHash(a q) Nil SetHash(a q r s) Nil SetHash(a s) Nil SetHash(a a => True b => True s) Nil SetHash(a => True b => True s) Bool::True SetHash(a a => True b => True s) X::Method::NotFound Bool::True SetHash(a a => True b => True s) X::Method::NotFound X::Method::NotFound Bool::True Bool::True Nil SetHash(a a => True b => True s x) Nil Nil SetHash(a a => True b => True m n) Nil SetHash(a a => True b => True m m => 1 n) Nil SetHash(a a => True b b => True c m m => 1 n) Nil SetHash(a a => True b b => True c m m => 1 n) Nil SetHash(1 2 a a => True b b => True c m m => 1 n) Nil False Nil SetHash(1 2 Nil a a => True b b => True c m => 1 n) Nil SetHash(1 2 a a => True b b => True c m => 1 n) Nil SetHash( 1 2 a a => True b b => True c m => 1 n) True Nil SetHash( 1 2 a a => True b b => True c m => 1 n) (Bool::True, Bool::False) SetHash( 1 2 a a => True b => True c m => 1 n) Nil Nil
```
rakupp 4.0.1-84: differs — assignment returns the raw value (2, 0, "x") instead of its Bool, `++` returns 0/1, `:delete` returns the weight then Any, `.set(set(<a b>))` adds the Set as one element, `$s<s> = -1` returns -1, and `.add("x")` on a SetHash returns a path (a stray IO method).

### SB-21  BagHash: `Int()` weights, zero removes, `add`/`remove`                D:partial R:yes V:spec
Assignment coerces with `Int()`: 2.9 stores 2, `"3"` 3, `1e0` 1, `True` 1,
`10**20` exact; 0, `False`, `Nil`, `Any` (the last two with a warning) and
any negative value remove the key (the assignment still returns what was
assigned, -3); `"x"` throws `X::Str::Numeric`, `Inf`/`NaN`
`X::Numeric::CannotConvert`, a Complex `X::Numeric::Real`. `++` returns the
old weight; `--` at 1 removes; `--` on a missing key returns 0 and creates
nothing; `-= 1` at 1 removes; `+= 2` on a missing key stores 2; `*= 1.5` on 2
stores 3. `:delete` returns the weight, 0 when absent. `.add(x)` and
`.remove(x)` return Nil, take one element or an iterable, add or subtract 1
per item, delete a key that reaches 0 and ignore a missing key on remove;
`.add(Nil)` adds Nil as a key; `.add(bag(...))`/`.add(set(...))` add the
PAIRS as keys (as in SB-20; use `.kxxv` or `.keys`). `.set` does not exist. A
slice assignment `<a b> = 1, 2` returns the list.
```
say do { my $b = BagHash.new(<a a b>); (($b<a> = 5).raku, $b<a>.raku, ($b<a> = 0).raku, $b.gist, ($b<b> = -3).raku, $b.gist, ($b<c> = 2.9).raku, $b<c>.raku, ($b<d> = "3").raku, $b.gist, (try { $b<e> = "x"; "ok" }) // $!.^name, (try { $b<e> = Inf; "ok" }) // $!.^name, (try { $b<e> = NaN; "ok" }) // $!.^name, (try { $b<e> = 2i; "ok" }) // $!.^name, ($b<e>:exists), ($b<c>++).raku, $b<c>.raku, ($b<c>--).raku, ($b<z>--).raku, ($b<z>:exists), $b<z>.raku, ($b<d>--).raku, ($b<d>--).raku, ($b<d>:exists), ($b<d> -= 1).raku, $b<d>.raku, ($b<d>:exists), ($b<c>:delete).raku, ($b<c>:delete).raku, $b.gist, $b.add("q").raku, $b.add(<q r>).raku, $b.gist, $b.remove("q").raku, $b.gist, $b.remove(<q r z>).raku, $b.gist, ($b<a> = True).raku, ($b<a> = False).raku, $b.gist, (try $b.set("x")) // $!.^name, ($b<w> += 2).raku, $b<w>.raku, ($b<w> *= 1.5).raku, $b<w>.raku, ($b<v> = 1e0).raku, $b<v>.raku, ($b<u> = 10**20).raku, $b.add(bag(<a a>).kxxv).raku, $b.gist, $b.add(("a" => 5),).raku, $b.gist, $b.remove(bag(<a a a>).kxxv).raku, $b.gist, $b.add(Nil).raku, $b.AT-KEY(Nil), ($b<u> = 0).raku, $b.add(1..3).raku, $b.elems, (quietly { $b<x> = Nil }).raku, (quietly { $b<x> = Any }).raku, ($b<x>:exists), $b.add(<y y>).raku, $b<y>.raku, $b.remove("y").raku, $b<y>.raku, $b.add(bag(<t t>)).raku, $b.gist, $b.add(set(<s>)).raku, $b.keys.grep(Pair).elems, $b.add(<a>).WHAT.gist, ($b<a b> = 1, 2).raku, $b<a>.raku, $b<b>.raku).join(" ") }
# rakudo 2026.08: 5 5 0 BagHash(b) -3 BagHash() 2 2 3 BagHash(c(2) d(3)) X::Str::Numeric X::Numeric::CannotConvert X::Numeric::CannotConvert X::Numeric::Real False 2 3 3 0 False 0 3 2 True 0 0 False 2 0 BagHash() Nil Nil BagHash(q(2) r) Nil BagHash(q r) Nil BagHash() 1 0 BagHash() X::Method::NotFound 2 2 3 3 1 1 100000000000000000000 Nil BagHash(a(2) u(100000000000000000000) v w(3)) Nil BagHash(a => 5 a(2) u(100000000000000000000) v w(3)) Nil BagHash(a => 5 u(100000000000000000000) v w(3)) Nil 1 0 Nil 7 0 0 False Nil 2 Nil 1 Nil BagHash(1 2 3 Nil a => 5 t => 2 v w(3) y) Nil 3 Nil (1, 2) 1 2
```
rakupp 4.0.1-84: differs — a negative weight is stored (`b(-3)`), a zero weight stays as a key (`d(0) e(0)`), the assignment echoes the uncoerced value (2.9, `"3"`), `"x"`, `Inf`, `NaN` and a Complex are accepted, `--` on a missing key creates it, `:delete` returns Any, `.add` answers a path and adds nothing, and the line died at a stringified Any.

### SB-22  MixHash: `Real()` weights, zero removes, Inf and NaN accepted        D:partial R:partial V:quirk
Assignment coerces with `Real()`: `1/2` stays a Rat, `-1` is kept (negative
weights are legal), `2.5e0` stays a Num, `"3.5"` and `"1/3"` become Rats,
`True` 1; `0`, `0.0`, `-0.0`, `1e-400` (which is `0e0`), `False` and `Nil`
remove the key; `"x"` throws `X::Str::Numeric`, a Complex
`X::Numeric::Real`. The quirk: `Inf` and `NaN` are ACCEPTED by assignment
(construction refuses them, SB-04); the total then becomes `Inf` or `NaN`
until the key is deleted. `--` on a missing key stores -1 (the key now
exists), `++` from -1 reaches 0 and removes, `-= 1` on a missing key stores
-1; `:delete` returns the weight, 0 when absent; `*=` and `/=` work on the
key; `.add`/`.remove` do not exist. A large Int divided stays exact (`Rat`).
```
say do { my $m = MixHash.new(<a a b>); (($m<a> = 1/2).raku, $m<a>.raku, ($m<a> = -1).raku, $m.gist, ($m<a> = 0).raku, $m.gist, ($m<c> = 2.5e0).raku, $m<c>.raku, ($m<d> = "3.5").raku, $m<d>.raku, (try { $m<e> = "x"; "ok" }) // $!.^name, (try { $m<e> = 2i; "ok" }) // $!.^name, (try { $m<e> = Inf; "ok" }) // $!.^name, $m<e>.raku, (try { $m<f> = NaN; "ok" }) // $!.^name, $m<f>.raku, ($m<z>--).raku, $m<z>.raku, ($m<z>:exists), ($m<z>++).raku, ($m<z>:exists), ($m<b>--).raku, ($m<b>:exists), ($m<b> -= 1).raku, $m<b>.raku, ($m<d>:delete).raku, ($m<d>:delete).raku, (try $m.add("x")) // $!.^name, (try $m.remove("x")) // $!.^name, ($m<b> = 0.0).raku, ($m<b>:exists), ($m<g> = 1e-400).raku, ($m<g>:exists), ($m<h> = -0.0).raku, ($m<h>:exists), ($m<i> = True).raku, ($m<i>:exists), ($m<i> = False).raku, ($m<i>:exists), ($m<j> = "1/3").raku, ($m<c> *= 2).raku, ($m<c> /= 4).raku, $m<c>.raku, $m.total.raku, $m.elems, ($m<e>:delete).raku, ($m<f>:delete).raku, $m.total.raku, (quietly { $m<k> = Nil }).raku, ($m<k>:exists), ($m<l> = 10**20).raku, ($m<l> /= 3).raku, $m<l>.WHAT.gist).join(" ") }
# rakudo 2026.08: 0.5 0.5 -1 MixHash(a(-1) b) 0 MixHash(b) 2.5e0 2.5e0 3.5 3.5 X::Str::Numeric X::Numeric::Real ok Inf ok NaN 0 -1 True -1 False 1 False -1 -1 3.5 0 X::Method::NotFound X::Method::NotFound 0.0 False 0e0 False 0.0 False 1 True 0 False <1/3> 5e0 1.25e0 1.25e0 NaN 4 Inf NaN 1.5833333333333333e0 0 False 100000000000000000000 <100000000000000000000/3> (Rat)
```
rakupp 4.0.1-84: differs — `"3.5"` and `"1/3"` are stored as Strs, no refusal for `"x"` or a Complex, `True`/`False` are stored as Bools, `:delete` returns Any and the total is wrong afterwards, `.add` returns a path.

### SB-23  Autovivification of an undefined variable                           D:no R:yes V:spec
A `my SetHash $s` (undefined) autovivifies on `$s<a>++` (returns False, then
`SetHash(a)`), on `$s<a> = 2` (True), on a slice assignment, on reading
`$s<a>` (False, and `$s` is now defined) and on `:exists` it stays undefined
(False). `my BagHash $b; $b<a>--` returns 0 and leaves an empty BagHash;
`my MixHash $m; --$m<a>` stores -1; `$m<a> += 0.5` works. The immutable
types refuse: `my Set $s; $s<a> = True` and a read `$s<a>` are
`X::Assignment::RO` and `$s` stays undefined, `Bag`'s `++` and `= 0` too;
`:delete` on an undefined Set or BagHash is a silent Nil and vivifies
nothing; binding `:=` into an undefined Set is `X::TypeCheck::Assignment`; a
`QuantHash`-typed variable is `X::AdHoc`, a `Setty`- or `Baggy`-typed one
`X::Multi::NoMatch`.
```
say do { my SetHash $s; my $r = ($s<a>++).raku; ($r, $s.^name, $s.gist).join(" ") }, " | ", do { my BagHash $b; my $r = ($b<a>--).raku; ($r, $b.gist, $b<a>.raku, $b.defined).join(" ") }, " | ", do { my MixHash $m; my $r = (--$m<a>).raku; ($r, $m.gist).join(" ") }, " | ", do { my Set $s; (try { $s<a> = True; "ok" }) // $!.^name ~ ":" ~ $s.^name ~ ":" ~ $s.defined }, " | ", do { my Bag $b; (try { $b<a>++; "ok" }) // $!.^name }, " | ", do { my SetHash $s; ($s<a> = 2).raku ~ " " ~ $s.gist }, " | ", do { my Mix $m; (try { $m<a>:delete; "ok" }) // $!.^name ~ ":" ~ $m.defined }, " | ", do { my SetHash $s; ($s<a>:exists) ~ " " ~ $s.defined ~ " " ~ $s<a>.raku ~ " " ~ $s.defined }, " | ", do { my BagHash $b; $b<a>.raku ~ " " ~ $b.defined ~ " " ~ $b<a>.^name }, " | ", do { my Set $s; (try $s<a>.raku) // $!.^name }, " | ", do { my Set $s; ($s<a>:exists) ~ " " ~ ((try $s<a>:delete) // $!.^name).raku }, " | ", do { my SetHash $s; $s<a b> = True, False; $s.gist }, " | ", do { my MixHash $m; $m<a> += 0.5; $m<b> -= 1; $m.gist }, " | ", do { my BagHash $b; ($b<a>:delete).raku ~ " " ~ $b.defined }, " | ", do { my Bag $b; (try { $b<a> = 0; "ok" }) // $!.^name }, " | ", do { my Set $s; (try { $s<a> := True; "ok" }) // $!.^name }, " | ", do { my QuantHash $q; (try { $q<a>++; "ok" }) // $!.^name }, " | ", do { my Setty $q; (try { $q<a>++; "ok" }) // $!.^name }, " | ", do { my Baggy $q; (try { $q<a>++; "ok" }) // $!.^name }
# rakudo 2026.08: Bool::False SetHash SetHash(a) | 0 BagHash() 0 True | -1 MixHash(a(-1)) | X::Assignment::RO:Set:False | X::Assignment::RO | Bool::True SetHash(a) | ok | False False Bool::False True | 0 True Int | X::Assignment::RO | False "Any" | SetHash(a) | MixHash(a(0.5) b(-1)) | Nil False | X::Assignment::RO | X::TypeCheck::Assignment | X::AdHoc | X::Multi::NoMatch | X::Multi::NoMatch
```
rakupp 4.0.1-84: differs — the vivified object is a plain Hash (`{a => 1}`), a read does not vivify, `:delete` returns Any, and the role-typed variables vivify silently.

### SB-24  Live values, kv and pairs of the hash forms; hyper over a QuantHash   D:no R:yes V:quirk
On SetHash/BagHash/MixHash the values handed out by `.values`, `.kv` and
`.pairs` are live Proxies: assigning 0 (or `--` to 0) through `$_`, the `kv`
value, or `.value` of the pair removes the key, `-1` keeps a SetHash key
(truthy), `42` sets a weight, `2.9` stores 2, `"x"` is `X::Str::Numeric`, and
`.value = 0; .value = 1` restores; a bound alias (`my \p = $s<a>`) stays
live, an assignment copy (`my $p = $s<a>`, `my @v = $s.values`) does not. On
Set/Bag/Mix `$_ = 5 for .values` is `X::AdHoc`, `.value =` and a `kv` value
`X::Assignment::RO`, `.pairs[0].key =` `X::Assignment::RO`. The hyper `>>`
on a SetHash/BagHash applies the operation to the live values and returns
the ORIGINAL contents (`$s>>--` empties `$s` and returns `SetHash(a b c)`);
on an immutable it returns a new collection with the mapped weights and
leaves the invocant alone (`$bag>>.&{ 3 }` is `Bag(a(3) b(3))`, a false result
drops the key); the quirk: on a Mix the mapped weight is truncated to Int
(`2.5` gives `Mix(a(2))`) while a MixHash keeps `2.5`.
```
say do { my $s = <a b>.SetHash; for $s.values { $_ = 0 }; my $g1 = $s.gist; $s = <a>.SetHash; $_ = -1 for $s.values; my $g2 = $s.gist; $s = <a>.SetHash; for $s.pairs { .value = 0; .value = 1 }; my $g3 = $s.gist; $s = <a b>.SetHash; for $s.kv -> \k, \v { v-- }; ($g1, $g2, $g3, $s.gist).join(" ") }, " | ", do { my $b = <a a a>.BagHash; $_-- for $b.values; my $g1 = $b.gist; .value = 42 for $b.pairs; my $g2 = $b.gist; $_ = -1 for $b.values; my $g3 = $b.gist; $b = <a>.BagHash; my $e = (try { $_ = "x" for $b.values; "ok" }) // $!.^name; my $e2 = (try { .value = 2.9 for $b.pairs; "ok" }) // $!.^name; ($g1, $g2, $g3, $e, $e2, $b.gist).join(" ") }, " | ", do { my $m = <a a>.MixHash; .value = -1.5 for $m.pairs; my $g1 = $m.gist; for $m.kv -> \k, \v { v = 0 }; ($g1, $m.gist).join(" ") }, " | ", do { my $b = bag <a>; ((try { $_ = 5 for $b.values; "ok" }) // $!.^name, (try { .value = 5 for $b.pairs; "ok" }) // $!.^name, (try { for $b.kv -> \k, \v { v = 5 }; "ok" }) // $!.^name, (try { $b.pairs[0].key = "z"; "ok" }) // $!.^name, (try { $_ = 0 for set(<a>).values; "ok" }) // $!.^name, (try { .value = 0 for set(<a>).pairs; "ok" }) // $!.^name, (try { $_ = 5 for mix(<a>).values; "ok" }) // $!.^name).join(" ") }, " | ", do { my $s := <a b c>.SetHash; ($s>>--).gist ~ " " ~ $s.gist }, " | ", do { my $b := <a a b>.BagHash; ($b>>--).gist ~ " " ~ $b.gist }, " | ", do { my $b := <a a b>.Bag; ($b>>.&{ 3 }).gist ~ " " ~ $b.gist }, " | ", do { my $s := <a b>.Set; ($s>>.&{ 0 }).gist ~ " " ~ $s.gist }, " | ", do { my $m := ("a" => 1.5).Mix; ($m>>.&{ 2.5 }).gist ~ " " ~ ($m>>.&{ 0 }).gist ~ " " ~ $m.gist }, " | ", do { my $m := ("a" => 1.5).MixHash; ($m>>.&{ 2.5 }).gist ~ " " ~ $m.gist }, " | ", do { my $s := <a b>.Set; (try $s>>.&{ "x" }.gist) // $!.^name }, " | ", do { my $s = SetHash.new(<a b>); my @v = $s.values; @v[0] = False; $s.elems }, " | ", do { my $s = SetHash.new(<a>); my $v := $s.values[0]; $v = False; my $e = $s.elems; $v = True; $e ~ " " ~ $s.gist }, " | ", do { my $s = SetHash.new(<a>); my $p = $s<a>; $p = False; $s.elems }, " | ", do { my $s = SetHash.new(<a>); my \p = $s<a>; p = False; my $e = $s.elems; p = True; $e ~ " " ~ $s.gist }, " | ", do { my $b = BagHash.new(<a a>); my \p = $b<a>; p = 0; my $e = $b.elems; p = 3; $e ~ " " ~ $b.gist }
# rakudo 2026.08: SetHash() SetHash(a) SetHash(a) SetHash() | BagHash(a(2)) BagHash(a(42)) BagHash() X::Str::Numeric ok BagHash(a(2)) | MixHash(a(-1.5)) MixHash() | X::AdHoc X::Assignment::RO X::Assignment::RO X::Assignment::RO X::AdHoc X::Assignment::RO X::AdHoc | SetHash(a b c) SetHash() | BagHash(a(2) b) BagHash(a) | Bag(a(3) b(3)) Bag(a(2) b) | Set() Set(a b) | Mix(a(2)) Mix() Mix(a(1.5)) | MixHash(a(2.5)) MixHash(a(1.5)) | Set(a b) | 2 | 0 SetHash(a) | 1 | 0 SetHash(a) | 0 BagHash(a(3))
```
rakupp 4.0.1-84: differs — assigning through the values of a Bag/Set/Mix is accepted, a bound alias of a value or subscript is not live, `$s>>--` leaves the SetHash unchanged, `>>--` on a BagHash leaves `b(0)`, and the hyper on an immutable returns a plain list.

### SB-25  pick, roll, pickpairs                                             D:yes R:yes V:spec
`pick` with no count returns one element (Nil when empty); `pick(n)` a Seq
of at most n DISTINCT draws (n above the count is capped, 0 or negative
gives an empty Seq, a fraction is truncated, `*`/`Inf` gives all, a Callable
receives the element count on a Set and the total on a Bag; NaN throws
`X::Numeric::CannotConvert`). On a Bag a pick is weighted and without
replacement, so `pick(*)` lists every key weight times (`("a","a","b")`) and
`pick(5)` is capped at the total. `roll(n)` draws with replacement, `roll(*)`
is an infinite lazy Seq, `roll` of an empty collection is Nil and `roll(n)`
empty, `roll(NaN)` throws even on an empty set. `pickpairs` gives
`elem => weight` (Set: `elem => True`) once per key, a Pair without count.
None of them changes the invocant.
```
say do { my $s = set <a b c>; my $b = bag <a a b>; ($s.pick.^name, $s.pick(2).elems, $s.pick(*).elems, $s.pick(*).^name, $s.pick(10).elems, $s.pick(0).elems, $s.pick(-1).elems, $s.pick(2.9).elems, $s.pick(Inf).elems, $s.pick({ $_ - 1 }).elems, (try $s.pick(NaN)) // $!.^name, $s.roll.^name, $s.roll(5).elems, $s.roll(*)[^7].elems, $s.roll(0).elems, $s.roll(-2).elems, $s.roll(2.5).elems, (try $s.roll(NaN).elems) // $!.^name, set().roll.raku, set().pick.raku, set().roll(3).raku, set().pick(*).raku, (try set().roll(NaN).raku) // $!.^name, $b.pick(*).sort.raku, $b.pick(*).elems, $b.pick(2).elems, $b.pick(-2.5).elems, $b.pick(2.5).elems, $b.pick({ $_ / 3 }).elems, $b.roll(4).elems, $b.roll(*)[^5].elems, bag().roll.raku, bag().pick(2).raku, bag().roll(*).raku, $b.pickpairs(*).sort.raku, $b.pickpairs(5).elems, $b.pickpairs(-1).elems, (try $b.pick(NaN)) // $!.^name, (try $b.roll(NaN)) // $!.^name, $s.pickpairs(*).sort.raku, $s.pickpairs.^name, $s.pickpairs(2).^name, bag(<a a a>).roll(3).raku, bag(<a a a>).pick(*).raku, bag(<a a a>).pick(5).raku, $s.pick(1).^name, $s.roll(1).^name, $b.pick(*).^name, bag().pick.raku, $b.pick({ $_ }).elems, $b.pickpairs({ $_ }).elems, $s.pickpairs({ $_ }).elems, $b.roll({ $_ }).elems, set().pick({ $_ }).raku, bag(<a a a>).pickpairs(*).raku, bag(<a a a>).pickpairs.raku, bag(<a a a>).roll(0).raku, bag(<a a a>).roll(Inf)[^2].raku, $s.roll(Inf)[^2].elems, $s.pick(*).sort.raku, $s.total, $b.total).join(" ") }
# rakudo 2026.08: Str 2 3 Seq 3 0 0 2 3 2 X::Numeric::CannotConvert Str 5 7 0 0 2 X::Numeric::CannotConvert Nil Nil ().Seq ().Seq X::Numeric::CannotConvert ("a", "a", "b").Seq 3 2 0 2 1 4 5 Nil ().Seq ().Seq (:a(2), :b(1)).Seq 2 0 X::Numeric::CannotConvert X::Numeric::CannotConvert (:a, :b, :c).Seq Pair Seq ("a", "a", "a").Seq ("a", "a", "a").Seq ("a", "a", "a").Seq Seq Seq Seq Nil 3 2 3 3 ().Seq (:a(3),).Seq :a(3) ().Seq ("a", "a") 2 ("a", "b", "c").Seq 3 3
```
rakupp 4.0.1-84: differs — the NaN count throws `X::AdHoc` instead of `X::Numeric::CannotConvert`; everything else matches.

### SB-26  What the immutable types and Mix refuse: grab, pick on Mix, classify-list   D:yes R:yes V:spec
`grab` and `grabpairs` on Set, Bag and Mix throw `X::Immutable` with
`.method` the method name and `.typename` the type; so do `classify-list`
and `categorize-list` on Bag and Mix (Setty has neither: `X::Method::NotFound`).
`pick` on a Mix or MixHash, `grab` on a MixHash and `kxxv` on either
RETURN a Failure whose exception is `X::AdHoc` (the messages name the
method); `grabpairs`, `roll` and `pickpairs` work on a Mix. `BagHash`/
`MixHash.classify-list(mapper, list)` and `categorize-list` add 1 per
classification to the invocant and return it (`=== self`), accept a
Callable, an Array or a Hash mapper (a missing hash key classifies under
`(Any)`), refuse a mapper that returns an Iterable
(`X::Invalid::ComputedValue`) and accept an empty list.
```
say do { sub T(&c) { my $r = do { c() }; CATCH { default { return "T:" ~ .^name } }; $r ~~ Failure ?? do { $r.so; "F:" ~ $r.exception.^name ~ ":" ~ $r.exception.message } !! $r.gist }; (T({ set(<a>).grab }), T({ set(<a>).grabpairs }), T({ bag(<a>).grab(2) }), T({ bag(<a>).grabpairs }), T({ mix(<a>).grab }), T({ mix(<a>).grabpairs }), T({ mix(<a>).pick }), T({ mix(<a>).pick(2) }), T({ MixHash.new(<a>).grab }), T({ MixHash.new(<a>).grab(2) }), T({ MixHash.new(<a>).kxxv }), T({ mix(<a>).kxxv }), T({ MixHash.new(<a>).pick }), T({ MixHash.new(<a>).grabpairs }), T({ mix(<a>).roll }), T({ mix(<a>).pickpairs }), T({ set(<a>).pickpairs }), T({ bag(<a a>).pickpairs }), T({ bag(<a>).classify-list({$_}, <a>) }), T({ mix(<a>).categorize-list({$_}, <a>) }), T({ BagHash.new.classify-list({ $_ %% 2 ?? "even" !! "odd" }, ^5) }), T({ MixHash.new.categorize-list({ ($_, "all") }, <a b>) }), T({ BagHash.new.classify-list({ ($_, $_) }, <a>) }), T({ set(<a>).grab(*) }), T({ SetHash.new.grab }), T({ set(<a>).categorize-list({$_}, <a>) }), T({ mix(<a>).pick(*) }), T({ BagHash.new.classify-list(<x y>, 0, 1, 1) }), T({ BagHash.new.classify-list({ $_ }, <a b>, <c>) }), T({ MixHash.new.classify-list({$_}, ()) }), T({ Mix.new.pick }), T({ mix().roll }), T({ BagHash.new.categorize-list({ () }, <a>) }), T({ MixHash.new.grab(*) }), T({ mix(<a>).grab(*) }), T({ set(<a>).grabpairs(2) }), T({ BagHash.new(<a>).classify-list({$_}, <a>).gist }), T({ MixHash.new.classify-list(%(a => "x"), <a b>).gist }), T({ my $b = BagHash.new; $b.classify-list({ $_ }, <a>) === $b })).join(" ") }
# rakudo 2026.08: T:X::Immutable T:X::Immutable T:X::Immutable T:X::Immutable T:X::Immutable T:X::Immutable F:X::AdHoc:.pick is not supported on a Mix, maybe use .roll instead? F:X::AdHoc:.pick is not supported on a Mix, maybe use .roll instead? F:X::AdHoc:.grab is not supported on a MixHash F:X::AdHoc:.grab is not supported on a MixHash F:X::AdHoc:.kxxv is not supported on a MixHash F:X::AdHoc:.kxxv is not supported on a Mix F:X::AdHoc:.pick is not supported on a MixHash, maybe use .roll instead? a => 1 a a => 1 a => True a => 2 T:X::Immutable T:X::Immutable BagHash(even(3) odd(2)) MixHash(a all(2) b) T:X::Invalid::ComputedValue T:X::Immutable (Any) T:X::Method::NotFound F:X::AdHoc:.pick is not supported on a Mix, maybe use .roll instead? BagHash(x y(2)) T:X::Invalid::ComputedValue MixHash() F:X::AdHoc:.pick is not supported on a Mix, maybe use .roll instead? (Any) BagHash() F:X::AdHoc:.grab is not supported on a MixHash T:X::Immutable T:X::Immutable BagHash(a(2)) MixHash((Any) x) True
say (try set(<a>).grab) // $!.^name ~ ":" ~ $!.method ~ ":" ~ $!.typename, " ", (try bag(<a>).grabpairs) // $!.^name ~ ":" ~ $!.method ~ ":" ~ $!.typename, " ", (try mix(<a>).classify-list({$_}, <a>)) // $!.^name ~ ":" ~ $!.method ~ ":" ~ $!.typename
# rakudo 2026.08: X::Immutable:grab:Set X::Immutable:grabpairs:Bag X::Immutable:classify-list:Mix
```
rakupp 4.0.1-84: differs — the Mix refusals are thrown, not returned as Failures, `kxxv` is `X::Method::NotFound`, `set(<a>).categorize-list` is `X::Immutable`, a mapper returning an Iterable is accepted, and `X::Immutable` has no `.method`/`.typename` (the second probe died).

### SB-27  grab and grabpairs on the hash forms                               D:partial R:yes V:quirk
`SetHash.grab` removes and returns one element (Nil when empty), `grab(n)`
a Seq of up to n, `grab(*)` all in random order. `BagHash.grab(n)` removes n
weighted draws (the total drops by n, a key is deleted at 0), `grab(*)`
returns total elements; `grabpairs` removes whole keys and returns `key =>
weight` (a Pair without count, Nil when empty). A negative or zero count
gives an empty Seq, 2.5 grabs 2, NaN throws `X::Numeric::CannotConvert`; a
Callable count receives the total for `grab` and the key count for
`grabpairs`. Weights above 64 bits grab fine. The removal is LAZY: right after
`grab(*)` or `grabpairs(*)` returns, the invocant still holds everything,
and each element leaves as the Seq is consumed.
The quirk: `grabpairs` on an EMPTY SetHash returns the Pair `(Nil) => True`
rather than Nil.
```
say do { my $s = SetHash.new(<a b c>); my $g = $s.grab; ($g.^name, $s.elems, $s.grab(5).elems, $s.elems, $s.grab.raku, $s.grab(2).raku, $s.grab(*).raku).join(" ") }, " | ", do { my $b = BagHash.new(<a a a b>); my @g = $b.grab(2); (@g.elems, $b.total, $b.grab(*).elems, $b.total, $b.elems, $b.grab.raku, $b.grab(3).raku).join(" ") }, " | ", do { my $b = BagHash.new(<a a a>); ($b.grabpairs.raku, $b.gist, $b.grabpairs.raku).join(" ") }, " | ", do { my $b = BagHash.new(<a a>); ($b.grab.raku, $b.gist, $b.grab.raku, $b.gist, $b.grab.raku).join(" ") }, " | ", do { my $m = MixHash.new(<a a b>); my @g = $m.grabpairs(*); (@g.elems, $m.elems, $m.grabpairs.raku).join(" ") }, " | ", do { my $s = SetHash.new(<a>); ($s.grabpairs.raku, $s.grabpairs.raku, $s.grabpairs(2).raku, SetHash.new(<a b>).grabpairs(*).sort.raku).join(" ") }, " | ", do { my $b = BagHash.new(<a a>); ($b.grab(-1).raku, $b.grab(0).raku, $b.grab(2.5).elems, $b.total, (try $b.grab(NaN)) // $!.^name).join(" ") }, " | ", do { my $b = BagHash.new(<a a a a>); ($b.grab({ $_ / 2 }).elems, $b.total, $b.grabpairs({ $_ }).elems, $b.elems).join(" ") }, " | ", do { my $b = ("foo" => 10**19).BagHash; ($b.grab(1).raku, $b<foo>).join(" ") }, " | ", do { my $s = SetHash.new(<a b c>); ($s.grab(2).^name, $s.grab(*).^name, $s.grabpairs(1).^name, $s.grabpairs.^name, $s.grab(1).elems).join(" ") }, " | ", do { my $b = BagHash.new(<a a b>); my $seq = $b.grab(*); my $e = $b.total; ($e, $seq.elems, $b.total).join(" ") }, " | ", do { my $s = SetHash.new(<a b>); my $seq = $s.grab(*); my $e = $s.elems; ($e, $seq.elems, $s.elems).join(" ") }, " | ", do { my $s = SetHash.new(<a b>); my $seq = $s.grabpairs(*); my $e = $s.elems; ($e, $seq.elems, $s.elems).join(" ") }, " | ", do { my $b = BagHash.new(<a a b>); my $seq = $b.grabpairs(*); my $e = $b.elems; ($e, $seq.elems, $b.elems).join(" ") }
# rakudo 2026.08: Str 2 2 0 Nil ().Seq ().Seq | 2 2 2 0 0 Nil ().Seq | :a(3) BagHash() Nil | "a" BagHash(a) "a" BagHash() Nil | 2 0 Nil | :a (Nil) => Bool::True ().Seq (:a, :b).Seq | ().Seq ().Seq 2 0 X::Numeric::CannotConvert | 2 2 1 0 | ("foo",).Seq 9999999999999999999 | Seq Seq Seq Pair 1 | 3 3 0 | 2 2 0 | 2 2 0 | 2 2 0
```
rakupp 4.0.1-84: differs — `grab(-1)` returns the elements, `grab(2.5)` grabs nothing, NaN is not refused, a Callable count is ignored, `grabpairs` on an empty SetHash returns Nil (the sane answer; keep it), and `grab(*)` is eager (the invocant is empty before the Seq is read).

### SB-28  roll on a Mix uses the positive weights only                        D:partial R:yes V:spec
A Mix rolls by its positive weights; negative weights are ignored, so
`("a" => 2, "b" => -5).Mix.roll(3)` is always `a`; a Mix whose weights are
all ≤ 0 rolls Nil, `roll(n)` and `roll(*)` on it are empty; fractional
weights work; `roll(0)`/`roll(-1)` are empty, 2.7 rolls 2, NaN throws; a
Callable count receives the positive total and is not called when it is 0.
```
say do { my $m = ("a" => 2, "b" => -5).Mix; ($m.roll, $m.roll(3).raku, $m.roll(*)[^3].raku, $m.total, ("a" => -1).Mix.roll.raku, ("a" => -1).Mix.roll(2).raku, ("a" => -1, "b" => -2).MixHash.roll(*).raku, ("a" => 0.5).Mix.roll, ("a" => 1/3, "b" => -1).MixHash.roll, mix().roll.raku, mix().roll(2).raku, ("a" => 2, "b" => -5).Mix.roll({ $_ * 2 }).raku, $m.roll(0).raku, $m.roll(-1).raku, $m.roll(2.7).elems, (try $m.roll(NaN)) // $!.^name, ("a" => 10**30, "b" => -10**30).Mix.roll, ("a" => 1e-10).Mix.roll, mix().roll(*).raku, ("a" => -1).Mix.roll(*).raku, ("a" => -1).Mix.roll({ $_ }).raku, ("a" => 2).Mix.roll({ $_ }).raku, $m.roll(1).^name, mix().roll({ 5 }).raku).join(" ") }
# rakudo 2026.08: a ("a", "a", "a").Seq ("a", "a", "a") -3 Nil ().Seq ().Seq a a Nil ().Seq ("a", "a", "a", "a").Seq ().Seq ().Seq 2 X::Numeric::CannotConvert a a ().Seq ().Seq ().Seq ("a", "a").Seq Seq ().Seq
```
rakupp 4.0.1-84: differs in one field — NaN throws `X::AdHoc`.

## D. The set operators

### SB-29  `(elem)`, `∈`, `∊`, `∉`, `(cont)`, `∋`, `∍`, `∌` and what the right side means   D:partial R:yes V:spec
`a (elem) b` is True when a's WHICH is found: in a QuantHash by key
presence (a Mix key with a negative weight IS an element; a MixHash key
set to 0 is gone); in a Hash by a truthy value (`"a" (elem) {a => 0}` False;
an Int against a plain Hash is False because the hash keys are strings,
against an object hash True); in a List/Array/Seq by walking (`[1] (elem)
[[1],[2]]` False, identity); in a finite Int Range arithmetically, in any
other Range by walking (`2 (elem) 1.5..4` False, `1.0 (elem) 1..3` False:
a Rat is not an Int); anything else is coerced with `.Set`, so a Str is ONE
element (`"a" (elem) "ab"` False, `"ab" (elem) "ab"` True), `1 (elem) 1` True,
`1 (elem) "1"` False, `1 (elem) Nil` False, `Nil (elem) (Nil,)` True, `Set
(elem) Set` True (the type object's set holds itself), `1 (elem) (1 => 2)`
True (the Pair is a weight). `(cont)` swaps the operands; `∉`/`∌` negate;
`!(elem)` negates; `∊` and `∍` are aliases. A Failure on either side throws.
Values from `.values` of a hash form are the weights, not elements.
```
say (1 (elem) (1,2)), " ", (1 ∈ (1,2)), " ", (3 ∉ (1,2)), " ", ((1,2) (cont) 2), " ", ((1,2) ∋ 2), " ", ((1,2) ∌ 3), " ", (1 !(elem) (1,2)), " ", ((1,2) !(cont) 3), " ", ("a" (elem) set(<a>)), " ", ("a" (elem) bag(<a>)), " ", ("a" (elem) ("a" => -1).Mix), " ", ("a" (elem) ("a" => 0).MixHash), " ", ("a" (elem) {a => 1}), " ", ("a" (elem) {a => 0}), " ", ("a" (elem) {a => "x"}), " ", (1 (elem) {1 => 1}), " ", (1 (elem) :{1 => 1}), " ", ("1" (elem) :{1 => 1}), " ", (1 (elem) :{1 => 0}), " ", ("a" (elem) "a"), " ", ("a" (elem) "ab"), " ", ("ab" (elem) "ab"), " ", (1 (elem) 1), " ", (1 (elem) "1"), " ", (1 (elem) 1..3), " ", (2 (elem) 1.5..4), " ", (1 (elem) (1..3).map({$_})), " ", ("b" (elem) "a".."c"), " ", (1 (elem) [1,2]), " ", ([1] (elem) [[1],[2]]), " ", (1 (elem) Nil), " ", (Nil (elem) (Nil,)), " ", (1 (elem) Any), " ", (Int (elem) (Int,)), " ", (1 (elem) ()), " ", (1 (elem) set()), " ", (1 (elem) Set), " ", (1 (elem) Bag), " ", (Set (elem) Set), " ", (Set (elem) (Set,)), " ", (try 1 (elem) Failure.new) // $!.^name, " ", (try Failure.new (elem) (1,2)) // $!.^name, " ", (1 (elem) ($ = :42foo,)), " ", (1e0 (elem) (1,2)), " ", (1 (elem) (1e0,)), " ", (<1> (elem) (1,)), " ", (1 (elem) (<1>,)), " ", ("a" (elem) ("a" => 1,)), " ", (("a" => 1) (elem) ("a" => 1,)), " ", ("a" (elem) (a => 1).Set), " ", (1 (elem) 1.5), " ", (1 (elem) 1..1), " ", (1.0 (elem) 1..3), " ", (1e0 (elem) 1..3), " ", (0 (elem) ^3), " ", (3 (elem) ^3), " ", (2 (elem) 1^..^3), " ", (1 (elem) 1^..^3), " ", (1 (elem) Bool), " ", (True (elem) (True,)), " ", ("a" (elem) SetHash.new(<a>)), " ", ("a" (elem) BagHash.new(<a>)), " ", (1 (elem) 3..1), " ", (2 (elem) (1..3).reverse), " ", (1 (elem) (1 => 2)), " ", ((1 (elem) (1,)).^name), " ", ("a" (elem) SetHash.new(<a>).values), " ", (True (elem) SetHash.new(<a>).values), " ", (1 (elem) bag(<a>).values)
# rakudo 2026.08: True True True True True True False True True True True False True False True False True False False True False True True False True False True True True False False True False True False False False False True True X::AdHoc X::AdHoc False False False False False False True True False True False False True False True False False True True True False True True Bool False True True
say (1 ∊ (1,2)), " ", ((1,2) ∍ 2), " ", (3 ∊ (1,2)), " ", ((1,2) ∍ 3)
# rakudo 2026.08: True True False False
```
rakupp 4.0.1-84: differs — `2 (elem) 1.5..4`, `1 (elem) (<1>,)`, `"a" (elem) ("a" => 1,)`, `1.0 (elem) 1..3` and `1e0 (elem) 1..3` are True, `("a" => 1) (elem) ("a" => 1,)` is False, a Failure operand answers Any, and `∊`/`∍` do not parse (the second probe died at compile time).

### SB-30  A Junction operand: Boolean operators autothread, `(|)` `(&)` `(+)` `(.)` never return   D:no R:no V:bug
`1 (elem) any(1,2)`, `any(1,5) (elem) (1,2)`, `(<=)`, `(<)`/`⊂` and `(==)`
with a Junction operand return a Junction that collapses as expected
(`so all(1,2) (elem) set(1)` False, `so set(1) (==) (1&2)` False). The bug:
`set(1) (|) any(2,3)`, `(&)`, `(+)` and `(.)` with a Junction on either side
NEVER return (the probe gives up after 3 s on four spinning threads), and
`(-)`/`(^)` throw `X::AdHoc`. A Junction INSIDE a list is a single element
(`1 (elem) (1|2,)` False, `set(1|2)` has one Junction key). Do not imitate
the hang: autothread the four operators like the Boolean ones. This probe
is timing-dependent (3 s). Also measured here: `.set`/`.add`/`.unset` of a
hash form given an infinite list never return (SB-08).
```
say do { my @ops = &infix:<(|)>, &infix:<(&)>, &infix:<(+)>, &infix:<(.)>, &infix:<(-)>, &infix:<(^)>; my @p = @ops.map(-> &op { start { (try op(set(1), any(2,3)).^name) // $!.^name } }); @p.push(start { SetHash.new.set(1..*); "done" }, start { BagHash.new.add(1..*); "done" }, start { SetHash.new.unset(1..*); "done" }); await Promise.anyof(Promise.allof(@p), Promise.in(3)); (@p.map({ .status ~~ Kept ?? .result !! "HANG" }), (1 (elem) any(1,2)).^name, (so 1 (elem) any(1,2)), (so 1 (elem) all(1,2)), (so any(1,5) (elem) (1,2)), (so none(1,5) (elem) (1,2)), (any(1,2) (<=) set(3)).^name, (so any(1,2) (<=) set(1,3)), (set(1) (==) any(1,2)).^name, (so set(1) (==) (1|2)), (so set(1) (==) (1&2)), ((1|2) ⊂ (1,2,3)).^name, (so (1|2) ⊂ (1,2,3)), (1 (elem) (1|2,)), set(1|2).elems, set(1|2).keys[0].^name, bag(1|2, 1|2).elems, (so all(<a b>) (elem) set(<a b c>)), (so set(<a b>) (cont) all(<a b>)), (so set(<a b>) ∋ none(<c>)), (1 (elem) any((1,2),(3,4))).^name, (so 1 (elem) all((1,2),(3,4)))).join(" ") }
# rakudo 2026.08: HANG HANG HANG HANG X::AdHoc X::AdHoc HANG HANG HANG Junction True False True False Junction True Junction True False Junction True False 1 Junction 1 True True True Junction False
```
rakupp 4.0.1-84: differs, correctly, on the hang: the four operators autothread (Junction results); `(-)` autothreads too, `(^)` answers a Set, `.set(1..*)` finishes ("done": the range is finite here), `so set(1) (==) (1&2)` is True, and `set(1|2)` has two Int keys (the junction is expanded into elements).

### SB-31  `(|)` / `∪` union                                                   D:partial R:yes V:spec
Result kind: Set with Set is Set; Set with Bag is Bag; anything with Mix
is Mix; weights are the MAXIMUM per key, so `set(<a b>) ∪ bag(<b b c>)` is
`Bag(a b(2) c)`, `bag(<a a b>) ∪ ("b" => 0.5).Mix` is `Mix(a(2) b)`, and
negative Mix weights survive as the maximum (`a => -10`, `b => -20`).
Mutability follows the LEFT operand: `SetHash ∪ bag` is a BagHash, `bag ∪
SetHash` a Bag, `BagHash ∪ mix` a MixHash, a plain list on the left gives
the immutable kind of the right (`<a> ∪ BagHash` is a Bag). A non-QuantHash
operand is coerced with the rule of SB-05: a list flattens, a Hash contributes
its truthy keys (as a Set) or its values as weights when the other side is
a Bag/Mix (`bag(<a>) ∪ {a => 3}` is `Bag(a(3))`, `{a => -1} ∪ mix()` is
`Mix(a(-1))`), a Pair is a weight (`set(<a>) ∪ (b => 0)` is `Set(a)`,
`bag(<a>) ∪ (a => 3)` `Bag(a(3))`), a Str, Int or Nil is one element (`"ab"
∪ "cd"` is `Set(ab cd)`, `Nil ∪ 1` `Set(1 Nil)`), a type object is one element
(`Set ∪ Set` is `Set((Set))`, `bag() ∪ Bag` `Bag((Bag))`). `set() ∪ set()` is the
sentinel. The reduction `[(|)]` with no arguments is `Set()`, with one
QuantHash returns it unchanged (a SetHash stays a SetHash), with one list
spreads the list as arguments; with three or more it upgrades the kind
across all arguments and keeps the first argument's mutability (`[(|)]
bag(<a a>), bag(<a>), <a a a>` is `Bag(a(3))`: the list is coerced to a Bag).
`Z(|)`, `X(|)`, `(|)=` and the `∪` alias work; `1 (|) ($ = :42foo,)` is
`Set(1 foo)`.
```
say (set(<a b>) (|) set(<b c>)).gist, " ", (set(<a b>) ∪ bag(<b b c>)).gist, " ", (set(<a b>) ∪ bag(<b b c>)).^name, " ", (SetHash.new(<a b>) (|) bag(<b b c>)).^name, " ", (bag(<a b>) (|) SetHash.new(<c>)).^name, " ", (bag(<a a b>) (|) mix(<a b b>)).gist, " ", (bag(<a a b>) (|) ("b" => 0.5).Mix).gist, " ", (("a" => -10, "b" => -30).Mix (|) ("b" => -20, "c" => -10).Mix).pairs.sort.raku, " ", (<a b c> (|) <c d>).gist, " ", (<a b c> (|) <c d>).^name, " ", ({a => 1, b => 0} (|) {b => 1}).gist, " ", ({a => 1} (|) <c d>).gist, " ", (:{1 => "x", 2 => ""} (|) (3,)).gist, " ", ("ab" (|) "cd").gist, " ", (42 (|) 42).gist, " ", (Nil (|) 1).gist, " ", (Any (|) 1).gist, " ", (Set (|) Set).gist, " ", (Set (|) set()).gist, " ", (set() (|) Bag).gist, " ", (bag() (|) Bag).gist, " ", ((set() (|) set()) =:= set()), " ", (SetHash.new (|) set()).^name, " ", (set() (|) SetHash.new).^name, " ", (SetHash.new (|) SetHash.new).^name, " ", (set(<a>) (|) SetHash.new(<b>)).^name, " ", (bag(<a>) (|) BagHash.new(<b>)).^name, " ", (BagHash.new(<a>) (|) bag(<b>)).^name, " ", (mix(<a>) (|) BagHash.new(<b>)).^name, " ", (BagHash.new(<a>) (|) mix(<b>)).^name, " ", (SetHash.new(<a>) (|) mix(<b>)).^name, " ", (SetHash.new(<a>) (|) <b>).^name, " ", (<a> (|) SetHash.new(<b>)).^name, " ", (<a> (|) BagHash.new(<b>)).^name, " ", ([(|)]).gist, " ", ([(|)] set(<a>)).gist, " ", ([(|)] <a b>).gist, " ", ([(|)] {a => 1}).gist, " ", ([(|)] SetHash.new(<a>)).^name, " ", ([(|)] bag(<a>), <b>, mix(<c>)).gist, " ", (infix:<(|)>()).gist, " ", (infix:<(|)>(<a b>)).gist, " ", (infix:<(|)>(bag(<a a>))).gist, " ", ((1,2) (|) (2,3) (|) (3,4)).gist, " ", (1 (|) ($ = :42foo,)).gist, " ", do { my $s = set(); $s (|)= 5; $s.gist }, " ", ((1..3, 1..3) Z(|) (2..4, 2..5)).gist, " ", ([(|)] bag(<a a>), bag(<a>), <a a a>).gist, " ", ([(|)] SetHash.new(<a>), bag(<b>)).^name, " ", ([(|)] set(<a>), BagHash.new(<b>), <c>).^name, " ", ([(|)] BagHash.new(<a>), set(<b>), mix(<c>)).^name, " ", ([∪] <a>, <b>, <c>).gist, " ", (set(<a>) ∪ set(<b>) ∪ set(<c>)).gist, " ", (set(<a>) (|) bag(<a a>) (|) set(<a>)).gist, " ", (Set (|) Bag).gist, " ", (set(<a>) (|) Nil).gist, " ", ((<a>, <b>) (|) <c>).gist, " ", ("a b" (|) "c").gist, " ", ((1..3) (|) (2..4)).gist, " ", (set(<a>) (|) (a => 0)).gist, " ", (set(<a>) (|) (b => 0)).gist, " ", (set(<a>) (|) (b => 0).Set).gist, " ", (bag(<a>) (|) (a => 3)).gist, " ", (bag(<a>) (|) {a => 3}).gist, " ", (bag(<a a a>) (|) {a => 2}).gist, " ", (mix(<a>) (|) {a => 0.5}).gist, " ", (mix(<a>) (|) {a => -0.5}).gist, " ", ({a => -1} (|) mix()).gist, " ", ((a => -1).Mix (|) set()).gist, " ", (set() (|) (a => -1).Mix).gist, " ", ((a => -1).Mix (|) set(<a>)).gist
# rakudo 2026.08: Set(a b c) Bag(a b(2) c) Bag BagHash Bag Mix(a(2) b(2)) Mix(a(2) b) (:a(-10), :b(-20), :c(-10)).Seq Set(a b c d) Set Set(a b) Set(a c d) Set(1 3) Set(ab cd) Set(42) Set(1 Nil) Set((Any) 1) Set((Set)) Set((Set)) Set((Bag)) Bag((Bag)) True SetHash Set SetHash Set Bag BagHash Mix MixHash MixHash SetHash Set Bag Set() Set(a) Set(a b) Set(a) SetHash Mix(a b c) Set() Set(a b) Bag(a(2)) Set(1 2 3 4) Set(1 foo) Set(5) (Set(1 2 3 4) Set(1 2 3 4 5)) Bag(a(3)) BagHash Bag MixHash Set(a b c) Set(a b c) Bag(a(2)) Set((Bag) (Set)) Set(Nil a) Set(a b c) Set(a b c) Set(1 2 3 4) Set(a) Set(a) Set(a) Bag(a(3)) Bag(a(3)) Bag(a(3)) Mix(a) Mix(a) Mix(a(-1)) Mix(a(-1)) Mix(a(-1)) Mix(a)
```
rakupp 4.0.1-84: differs — the Mix union drops a key with a negative weight (`:c(-10)` missing), `set() ∪ set()` is not the sentinel, `[(|)]` with no arguments is `(Any)` and with a Hash argument returns the Hash, and `(a => -1).Mix ∪ set(<a>)` is `Mix()` instead of taking the maximum 1.

### SB-32  `(&)` / `∩` intersection                                            D:partial R:yes V:bug
Keys present on both sides, weights the MINIMUM (`bag(<a a b>) ∩ bag(<a b b
c>)` is `Bag(a b)`, `bag(<a a>) ∩ ("a" => 0.5).Mix` `Mix(a(0.5))`, a negative
Mix weight is the minimum: `(a => -42).Mix ∩ mix(<a>)` is `Mix(a(-42))`,
`Mix(a(2)) ∩ Mix(a(-3))` `Mix(a(-3))`); the result kind and mutability follow
SB-31 (`set ∩ bag` is a Bag, `SetHash ∩ bag` a BagHash, `<a a> ∩ bag(<a>)` a
Bag of `a`); a list on the right of a Bag is a Bag (`bag(<a a a>) ∩ <a a>`
is `Bag(a(2))`); the empty results are the sentinels; `Set ∩ Set` is
`Set((Set))`, `Set ∩ set()` empty. `[(&)]` with no arguments is `Set()`, with
one list spreads it (`[(&)] <a b>` is `"a" ∩ "b"`, empty), with three or more
takes the minimum across all (`Bag(a(7))`), and a Set among Mixes makes the
result a Mix with Set semantics for that argument (`(a => -42).Mix, <b>.Set,
(:42a).Bag` gives `Mix()`). The bug: two plain Hashes take a fast path that
ignores the values' truthiness — `{a => 1, b => 0} ∩ {a => 1, b => 1}` is
`Set(a b)` and `{b => 0} ∩ {b => 0}` is `Set(b)`, while every other operand
combination (`.Set` first, a list, an object hash, `(|)`, `(-)`, `(^)`,
`(+)`, `(.)`, `(<=)`, `(elem)`) treats `b => 0` as absent. Do not imitate:
a false value is not a member.
```
say (set(<a b c>) (&) set(<b c d>)).gist, " ", (set(<a b>) ∩ bag(<a b b>)).gist, " ", (set(<a b>) (&) bag(<a b b>)).^name, " ", (bag(<a a b>) (&) bag(<a b b c>)).gist, " ", (bag(<a a>) (&) mix(<a a a>)).gist, " ", (bag(<a a>) (&) ("a" => 0.5).Mix).gist, " ", (("a" => -42).Mix (&) mix(<a>)).gist, " ", (("a" => -42).Mix (&) bag()).gist, " ", (("a" => 2).Mix (&) ("a" => -3).Mix).gist, " ", (<a b c> (&) <b c d>).gist, " ", ({a => 1, b => 0} (&) {a => 1, b => 1}).gist, " ", ({a => 1} (&) <a b>).gist, " ", (:{1 => 1, 2 => 0} (&) :{1 => 1, 2 => 1}).gist, " ", (42 (&) 42).gist, " ", (42 (&) 43).gist, " ", ("a" (&) "a").gist, " ", (Set (&) Set).gist, " ", (Set (&) set()).gist, " ", (set() (&) Set).gist, " ", (SetHash.new(<a>) (&) set()).^name, " ", (set(<a>) (&) SetHash.new).^name, " ", (SetHash.new(<a>) (&) bag(<a>)).^name, " ", (bag(<a>) (&) SetHash.new(<a>)).^name, " ", (BagHash.new(<a>) (&) mix(<a>)).^name, " ", (mix(<a>) (&) BagHash.new(<a>)).^name, " ", (SetHash.new(<a>) (&) mix(<a>)).^name, " ", (set(<a>) (&) MixHash.new(<a>)).^name, " ", (bag(<a>) (&) <a a>).gist, " ", (<a a> (&) bag(<a>)).gist, " ", (<a a> (&) bag(<a>)).^name, " ", (mix(<a>) (&) {a => 2}).gist, " ", ([(&)]).gist, " ", ([(&)] <a b>).gist, " ", ([(&)] bag(<a a>)).gist, " ", ([(&)] <a b c>, <b c d>, <c d e>).gist, " ", ([(&)] (:42a).Bag, (:7a).Bag, (:43a).Bag).gist, " ", ([(&)] (a => -42).Mix, <b>.Set, (:42a).Bag).gist, " ", ((1..3, 1..3) Z(&) (2..4, 1..4)).gist, " ", (1 (&) ($ = :42foo,)).gist, " ", (set(<a b>) ∩ set(<b>) ∩ set(<b c>)).gist, " ", (Nil (&) Nil).gist, " ", (Any (&) Any).gist, " ", ((1,2) (&) (2,3)).^name, " ", ([(&)] SetHash.new(<a>), <a>).^name, " ", ([(&)] set(<a>), BagHash.new(<a>), <a>).^name, " ", ([(&)] BagHash.new(<a>), set(<a>), mix(<a>)).^name, " ", (bag(<a a a>) (&) <a a>).gist, " ", (bag(<a>) (&) {a => 5}).gist, " ", (set(<a>) (&) {a => 5}).gist, " ", (set(<a>) (&) (a => 5)).gist, " ", (set(<a>) (&) (a => 5).Bag).gist, " ", (bag(<a a>) (&) (a => 1.5).Mix).gist, " ", (bag(<a a>) (&) (a => 1.5).Mix).^name, " ", (("a" => -1).Mix (&) ("a" => -2).Mix).gist, " ", (("a" => -1).Mix (&) set(<a>)).gist, " ", (("a" => -1).Mix (&) <a>).gist, " ", (set(<a>) (&) (a => -1).Mix).gist, " ", (bag(<a>) (&) (a => -1).Mix).gist, " ", ((a => -1).Mix (&) bag(<a>)).gist, " ", (set(<a b>) (&) SetHash.new(<a>)).gist
# rakudo 2026.08: Set(b c) Bag(a b) Bag Bag(a b) Mix(a(2)) Mix(a(0.5)) Mix(a(-42)) Mix() Mix(a(-3)) Set(b c) Set(a b) Set(a) Set(1) Set(42) Set() Set(a) Set((Set)) Set() Set() SetHash Set BagHash Bag MixHash Mix MixHash Mix Bag(a) Bag(a) Bag Mix(a) Set() Set() Bag(a(2)) Set(c) Bag(a(7)) Mix() (Set(2 3) Set(1 2 3)) Set() Set(b) Set(Nil) Set((Any)) Set SetHash Bag MixHash Bag(a(2)) Bag(a) Set(a) Set(a) Bag(a) Mix(a(1.5)) Mix Mix(a(-2)) Mix(a(-1)) Mix(a(-1)) Mix(a(-1)) Mix(a(-1)) Mix(a(-1)) Set(a)
say ({a => 1, b => 0} (&) {a => 1, b => 1}).gist, " ", ({a => 1, b => 0}.Set (&) {a => 1, b => 1}.Set).gist, " ", ({a => 1, b => 0} (&) set(<a b>)).gist, " ", ({a => 1, b => 0} (&) <a b>).gist, " ", ({a => 1, b => 0} (&) {a => 1, b => 0}).gist, " ", ({b => 0} (&) {b => 0}).gist, " ", ({b => 0} (|) {b => 0}).gist, " ", ({b => 0} (-) {a => 1}).gist, " ", ({b => 0} (^) {c => 0}).gist, " ", ({b => 0} (+) {b => 0}).gist, " ", ({b => 0} (.) {b => 0}).gist, " ", ({b => 0} (<=) {}), " ", ({b => 0} (==) {}), " ", ({b => 0} (==) {b => 0}), " ", ({b => 0} (==) set()), " ", ("b" (elem) {b => 0}), " ", (:{b => 0} (&) :{b => 0}).gist, " ", ({a => 1, b => 0} (&) {b => 1}.Set).gist
# rakudo 2026.08: Set(a b) Set(a) Set(a) Set(a) Set(a b) Set(b) Set() Set() Set() Bag() Bag() True False True True False Set() Set()
```
rakupp 4.0.1-84: differs — `{a => 1, b => 0} ∩ {a => 1, b => 1}` and `{b => 0} ∩ {b => 0}` give the sane `Set(a)` and `Set()` (keep that), `[(&)]` with no arguments is `(Any)`, and `{b => 0} (==) {}` is True (SB-38).

### SB-33  `(-)` / `∖` difference                                              D:partial R:yes V:quirk
Set − anything removes the right side's keys; Bag − Bag subtracts weights
and drops a key at ≤ 0 (`bag(<a a a b>) − bag(<a b>)` is `Bag(a(2))`); a
Set on the right of a Bag subtracts 1 per key, a list on the right of a Bag
is coerced to a BAG (`bag(<a a a b>) − <a a>` is `Bag(a b)`); Mix − anything
subtracts algebraically and KEEPS negatives (`mix() − mix(<a b>)` is
`Mix(a(-1) b(-1))`, `set(<a b>) − ("b" => -1).Mix` `Mix(a b(2))`, `<a b c d> −
mix(<a b c e>)` `Mix(d e(-1))`); a Set on either side of a Bag/Mix makes the
result a Bag/Mix (`set(<a b>) − bag(<a b b>)` is `Bag()`). The quirk: a
plain Hash or a Pair on the right of a MIX is taken as a Set (its values
ignored: `("a" => 2.5).Mix − {a => 2}` is `Mix(a(1.5))`, `mix(<a>) − (a => 0.5)`
is `Mix()`), while on the right of a Bag it is a Bag (`bag(<a a a>) − {a => 2,
b => 0}` is `Bag(a)`). Mutability follows the left operand except that
`[(-)]` with ONE hash-form argument returns the immutable form (`[(-)]
SetHash.new(<a>)` is a `Set`); with three or more arguments the kind
upgrades and the first argument's mutability is kept (`[(-)] SetHash.new(<a
b>), <a>, <c>` is a SetHash, `[(-)] <a>.Set, <a>.Set, <a>.Set, (a => -2).Mix`
is `Mix(a)`, `[(-)] (:42a).Bag, (:7a).Bag, (:41a).Bag` `Bag()`). The empty
results are the sentinels; `Nil − 1` is `Set(Nil)`.
```
say (set(<a b c>) (-) set(<b>)).gist, " ", (set(<a b c>) ∖ <b c d>).gist, " ", (bag(<a a a b>) (-) bag(<a b>)).gist, " ", (bag(<a a a b>) (-) <a a>).gist, " ", (bag(<a a a b>) (-) set(<a>)).gist, " ", (set(<a b>) (-) bag(<a b b>)).gist, " ", (set(<a b>) (-) bag(<a b b>)).^name, " ", (set(<a b>) (-) bag(<b>)).gist, " ", (bag(<a b b>) (-) set(<a b>)).gist, " ", (mix() (-) mix(<a b>)).gist, " ", (mix(<a>) (-) bag(<a a a>)).gist, " ", (set(<a b>) (-) mix(<a b b>)).gist, " ", (set(<a b>) (-) ("b" => -1).Mix).gist, " ", (<a b c d> (-) mix(<a b c e>)).gist, " ", (("a" => 2.5).Mix (-) <a>).gist, " ", (("a" => 2.5).Mix (-) {a => 2}).gist, " ", (bag(<a a a>) (-) {a => 2, b => 0}).gist, " ", ({a => 42, b => 0} (-) bag(<a a b b c>)).gist, " ", ({a => 42, b => 0} (-) bag(<a a b b c>)).^name, " ", ({a => 42} (-) mix(<a b>)).gist, " ", (<a b c> (-) <c d e>).gist, " ", (<a b c> (-) <c d e>).^name, " ", ({a => 1, b => 0} (-) {a => 0}).gist, " ", (:{1 => "x"} (-) :{1 => ""}).gist, " ", (42 (-) 666).gist, " ", ("ab" (-) "a").gist, " ", ((set() (-) set()) =:= set()), " ", (SetHash.new(<a>) (-) set()).^name, " ", (set(<a>) (-) SetHash.new).^name, " ", (BagHash.new(<a>) (-) mix()).^name, " ", (mix(<a>) (-) BagHash.new).^name, " ", (SetHash.new(<a b>) (-) mix(<a>)).gist, " ", (SetHash.new(<a b>) (-) mix(<a>)).^name, " ", (bag(<a>) (-) MixHash.new(<b>)).^name, " ", ([(-)]).gist, " ", ([(-)] set(<a>)).gist, " ", ([(-)] SetHash.new(<a>)).^name, " ", ([(-)] BagHash.new(<a>)).^name, " ", ([(-)] MixHash.new(<a>)).^name, " ", ([(-)] <a b>).gist, " ", ([(-)] <a b c>, <c d e>, <e f>).gist, " ", ([(-)] <a b c>.Mix, <c d e>.Mix, <e f>.Mix).gist, " ", ([(-)] (:42a).Bag, (:7a).Bag, (:41a).Bag).gist, " ", ([(-)] <a>.Set, <a>.Set, <a>.Set, (a => -2).Mix).gist, " ", ([(-)] SetHash.new(<a b>), <a>, <c>).^name, " ", ([(-)] set(<a b>), bag(<a>), <b>).^name, " ", ([(-)] SetHash.new(<a b>), bag(<a>), <c>).^name, " ", ([(-)] set(<a b>), bag(<a>)).gist, " ", ((1..3, 1..3) Z(-) (2..4, 1..4)).gist, " ", (1 (-) ($ = :42foo,)).gist, " ", (Nil (-) 1).gist, " ", (set(<a>) (-) Nil).gist, " ", (Set (-) Set).gist, " ", (set() (-) Set).gist, " ", (bag(<a a>) (-) <a>).gist, " ", (bag(<a a>) (-) (a => 1)).gist, " ", (bag(<a a>) (-) (a => 5)).gist, " ", (bag(<a a>) (-) {a => 5}).gist, " ", (bag(<a a>) (-) (a => 5).Set).gist, " ", (mix(<a>) (-) (a => 0.5)).gist, " ", (mix(<a>) (-) {a => 0.5}).gist, " ", (mix(<a>) (-) (a => 0.5).Mix).gist, " ", (set(<a b>) (-) SetHash.new(<a>)).^name, " ", (BagHash.new(<a a>) (-) bag(<a>)).gist, " ", (MixHash.new(<a>) (-) set(<a b>)).gist, " ", ([(-)] mix(<a>), mix(<a>), mix(<b>)).gist, " ", ([(-)] bag(<a a>), bag(<a>), bag(<a>)).gist, " ", (([(-)] bag(<a a>), bag(<a>), bag(<a>)) =:= bag()), " ", (([(-)] set(<a>), <a>, <a>) =:= set()), " ", ([(-)] set(<a>), <b>).gist, " ", ((a => -1).Mix (-) (a => -1).Mix).gist, " ", ((a => -1).Mix (-) (a => -2).Mix).gist, " ", ((a => -1).Mix (-) set(<a>)).gist, " ", (set(<a>) (-) (a => -1).Mix).gist
# rakudo 2026.08: Set(a c) Set(a) Bag(a(2)) Bag(a b) Bag(a(2) b) Bag() Bag Bag(a) Bag(b) Mix(a(-1) b(-1)) Mix(a(-2)) Mix(b(-1)) Mix(a b(2)) Mix(d e(-1)) Mix(a(1.5)) Mix(a(1.5)) Bag(a) Bag(a(40)) Bag Mix(a(41) b(-1)) Set(a b) Set Set(a) Set(1) Set(42) Set(ab) True SetHash Set MixHash Mix MixHash(b) MixHash Mix Set() Set(a) Set Bag Mix Set(a) Set(a b) Mix(a b d(-1) e(-2) f(-1)) Bag() Mix(a) SetHash Bag BagHash Bag(b) (Set(1) Set()) Set(1) Set(Nil) Set(a) Set() Set() Bag(a) Bag(a) Bag() Bag() Bag(a) Mix() Mix() Mix(a(0.5)) Set BagHash(a) MixHash(b(-1)) Mix(b(-1)) Bag() True True Set(a) Mix() Mix(a) Mix(a(-2)) Mix(a(2))
```
rakupp 4.0.1-84: differs — `("a" => 2.5).Mix − {a => 2}` subtracts 2, `mix(<a>) − (a => 0.5)` and `− {a => 0.5}` subtract 0.5, `[(-)]` with no arguments is `(Any)`, `[(-)]` of one hash form keeps it mutable, `[(-)] <a>.Set ×3, (a => -2).Mix` is `Mix(a(2))`, and the empty results are not the sentinels.

### SB-34  `(^)` / `⊖` symmetric difference                                    D:partial R:yes V:quirk
Two operands: Set semantics keep the keys on exactly one side; Bag/Mix
semantics keep the ABSOLUTE difference of the weights and drop a key at 0
(`bag(<a b b c>) ⊖ bag(<b c d>)` is `Bag(a b d)`, `set(<a b>) ⊖ ("b" => -1).Mix`
`Mix(a b(2))`, `(a => -3.14).Mix ⊖ mix()` `Mix(a(3.14))`); kinds, mutability
and coercions follow SB-31 (a Hash against a Bag is a Bag: `bag(<a a b b c>)
⊖ {a => 42, b => 0}` is `Bag(a(40) b(2) c)`; two Hashes honour truthiness). The
quirk is the n-ary form: `[(^)]` with three or more arguments is NOT the
fold of the binary operator. With Set semantics a key survives only when it
occurs in exactly ONE argument (`[(^)] <a b>, <b c>, <c d>` is `Set(a d)`, but
`[(^)] <a>, <a>, <a>` is `Set()` where `(<a> ⊖ <a>) ⊖ <a>` is `Set(a)`); with
Bag/Mix semantics the weight is the highest minus the second-highest across
the arguments, an absent key counting as 0 for a Mix (`[(^)] bag(<a a a>),
bag(<a>), bag(<a>)` is `Bag(a(2))` where the fold gives `Bag(a)`; `[(^)] (a =>
1).Mix, (a => -1).Mix, (a => 2).Mix` is `Mix(a)` where the fold gives `Mix()`).
With no arguments `Set()`, with one QuantHash itself. The Roast tables for
the n-ary form assert these answers, so step two must implement them
verbatim.
```
say (set(<a b c>) (^) set(<b c d>)).gist, " ", (<a b> ⊖ <b c>).gist, " ", (bag(<a b b c>) (^) bag(<b c d>)).gist, " ", (bag(<a b b>) (^) bag(<a b>)).gist, " ", (set(<a b>) (^) bag(<a b b>)).gist, " ", (set(<a b>) (^) bag(<a b b>)).^name, " ", (set(<a b>) (^) ("b" => -1).Mix).gist, " ", (("a" => -3.14).Mix (^) mix()).gist, " ", (mix() (^) ("a" => -3.14).Mix).gist, " ", (("a" => 2.5).Mix (^) <a b>).gist, " ", (("a" => 2.5).Mix (^) ("a" => -1).Mix).gist, " ", (("a" => -1).Mix (^) ("a" => -3).Mix).gist, " ", (bag(<a a b b c>) (^) {a => 42, b => 0}).gist, " ", ({a => 42, b => 0} (^) bag(<a a b b c>)).gist, " ", ({a => 42, b => 0} (^) bag(<a a b b c>)).^name, " ", (<a b c d> (^) mix(<a b c e>)).gist, " ", ({a => 1, b => 0} (^) {b => 1, c => 0}).gist, " ", (:{1 => "x", 2 => ""} (^) :{2 => "y", 3 => 1}).gist, " ", (42 (^) 666).gist, " ", (42 (^) 42).gist, " ", ((set() (^) set()) =:= set()), " ", (SetHash.new(<a>) (^) set()).^name, " ", (set(<a>) (^) SetHash.new).^name, " ", (SetHash.new(<a>) (^) bag(<a>)).^name, " ", (bag(<a>) (^) SetHash.new(<a>)).^name, " ", (BagHash.new(<a>) (^) mix(<a>)).^name, " ", (mix(<a>) (^) BagHash.new(<a>)).^name, " ", (SetHash.new(<a>) (^) mix(<b>)).^name, " ", ([(^)]).gist, " ", ([(^)] set(<a>)).gist, " ", ([(^)] SetHash.new(<a>)).^name, " ", ([(^)] <a b>).gist, " ", ([(^)] <a b c>, <c d e>, <e f g>).gist, " ", ([(^)] <a b c>.Bag, <c d e>.Bag, <e f g>.Bag).gist, " ", ([(^)] (:42a).Bag, (:7a).Bag, (:43a).Bag).gist, " ", ([(^)] (:42a).Bag, bag(), (:43a).Bag).gist, " ", ([(^)] (a => -42).Mix, <a>.Mix, (:42a).Mix).gist, " ", ([(^)] (a => -42).Mix, <b>.Set, (:42a).Bag).gist, " ", ([(^)] <a b>, <b c>, <c d>).gist, " ", ([(^)] <a>, <a>, <a>).gist, " ", ((<a> (^) <a>) (^) <a>).gist, " ", ([(^)] SetHash.new(<a b>), <b c>, <c d>).^name, " ", ([(^)] <a b>, BagHash.new(<b c>), <c d>).^name, " ", ([(^)] <a b>, BagHash.new(<b c>), <c d>).gist, " ", ((1..3, 1..3) Z(^) (2..4, 1..4)).gist, " ", (1 (^) ($ = :42foo,)).gist, " ", (Set (^) Set).gist, " ", (Set (^) set()).gist, " ", ([(^)] bag(<a a a>), bag(<a>), bag(<a>)).gist, " ", ((bag(<a a a>) (^) bag(<a>)) (^) bag(<a>)).gist, " ", ([(^)] mix(<a>), mix(<a>), mix(<a>)).gist, " ", ([(^)] bag(<a>), bag(<a>), bag(<a>)).gist, " ", ([(^)] <a>, <a>, <a>, <a>).gist, " ", ([(^)] <a>, <a>, <a>, <a>, <a>).gist, " ", ([(^)] (a => 3).Bag, (a => 1).Bag, (a => 2).Bag).gist, " ", ([(^)] (a => 3).Bag, (a => 2).Bag, (a => 1).Bag).gist, " ", ([(^)] (a => 1).Mix, (a => -1).Mix, (a => 2).Mix).gist, " ", (((a => 1).Mix (^) (a => -1).Mix) (^) (a => 2).Mix).gist, " ", (set(<a>) (^) Nil).gist, " ", (Nil (^) Nil).gist, " ", (set(<a b>) (^) (a => 0)).gist, " ", (bag(<a a>) (^) (a => 5)).gist, " ", (bag(<a a>) (^) {a => 5}).gist, " ", (mix(<a>) (^) (a => -1)).gist, " ", (set(<a b>) (^) SetHash.new(<a>)).gist
# rakudo 2026.08: Set(a d) Set(a c) Bag(a b d) Bag(b) Bag(b) Bag Mix(a b(2)) Mix(a(3.14)) Mix(a(3.14)) Mix(a(1.5) b) Mix(a(3.5)) Mix(a(2)) Bag(a(40) b(2) c) Bag(a(40) b(2) c) Bag Mix(d e) Set(a b) Set(1 2 3) Set(42 666) Set() True SetHash Set BagHash Bag MixHash Mix MixHash Set() Set(a) SetHash Set(a b) Set(a b d f g) Bag(a b d f g) Bag(a) Bag(a) Mix(a(41)) Mix(a(42) b) Set(a d) Set() Set(a) SetHash Bag Bag(a d) (Set(1 4) Set(4)) Set(1 foo) Set() Set((Set)) Bag(a(2)) Bag(a) Mix() Bag() Set() Set() Bag(a) Bag(a) Mix(a) Mix() Set(Nil a) Set() Set(a b) Bag(a(3)) Bag(a(3)) Mix(a(2)) Set(b)
```
rakupp 4.0.1-84: differs — `mix() ⊖ (a => -3.14).Mix` is `Mix(a(-3.14))` (the absolute value is missed for a key only on the right), `set() ⊖ set()` is not the sentinel, and `[(^)] SetHash.new(<a b>), <b c>, <c d>` is a Set (the first argument's mutability is lost); the n-ary answers otherwise match.

### SB-35  `(+)` / `⊎` addition                                                D:partial R:yes V:spec
Never a Set: Set with Set is a Bag with the weights added (`set(<a b>) (+)
set(<b c>)` is `Bag(a b(2) c)`), any Mix makes a Mix, negatives add
algebraically and a zero sum removes the key (`(a => -42).Mix (+) (:42a).Mix`
is the sentinel `mix()`, `set(<a b>) (+) ("b" => -1).Mix` `Mix(a)`); a Pair or
Hash is a weight (`1 (+) ($ = :42foo,)` is `Bag(1 foo(42))`, `bag(<a>) (+) (a
=> -2)` `Bag(a)`: the negative Pair is skipped as in SB-03, but on a Mix it
subtracts); type objects and Nil are elements (`Set (+) Set` is `Bag((Set)(2))`,
`Nil (+) Nil` `Bag(Nil(2))`). Mutability follows the left operand (`SetHash
(+) set()` is a BagHash, `set() (+) SetHash` a Bag, `bag() (+) BagHash` a Bag,
`BagHash.new (+) bag(<a>)` a BagHash); with no arguments `[(+)]` is `bag()`;
with ONE argument the result is always immutable (`[(+)] SetHash.new(<a>)`,
`[(+)] BagHash.new(<a>)` are Bags, `[(+)] MixHash.new(<a>)` a Mix); with three
or more the first argument's mutability is kept (`[(+)] SetHash.new(<a>),
<a>, <a>` is a BagHash). `set() (+) set()` is the sentinel `bag()`.
```
say (bag(<a a b>) (+) bag(<a c>)).gist, " ", (set(<a b>) (+) set(<b c>)).gist, " ", (set(<a b>) (+) set(<b c>)).^name, " ", (set(<a b>) ⊎ <b c>).gist, " ", (<a b> (+) <b c>).gist, " ", (<a b> (+) <b c>).^name, " ", (bag(<a a>) (+) ("a" => 0.5).Mix).gist, " ", (("a" => -42).Mix (+) (:42a).Mix).gist, " ", ((("a" => -42).Mix (+) (:42a).Mix) =:= mix()), " ", (set(<a b>) (+) ("b" => -1).Mix).gist, " ", (bag(<a a b>) (+) {a => 42, b => 0}).gist, " ", ({a => 42, b => 0} (+) {a => 1, c => 2}).gist, " ", ({a => 42} (+) <a d e>).gist, " ", ((:2a, :40a, :0b, :c) (+) (:4c, :42d, "e")).gist, " ", (42 (+) 42).gist, " ", (42 (+) 666).gist, " ", ("ab" (+) "ab").gist, " ", (Set (+) Set).gist, " ", (Set (+) set()).gist, " ", (set() (+) set()).gist, " ", ((set() (+) set()) =:= bag()), " ", (SetHash.new (+) set()).^name, " ", (set() (+) SetHash.new).^name, " ", (Set.new (+) SetHash.new).^name, " ", (SetHash.new(<a>) (+) bag(<a>)).^name, " ", (bag(<a>) (+) SetHash.new(<a>)).^name, " ", (BagHash.new(<a>) (+) mix(<a>)).^name, " ", (mix(<a>) (+) BagHash.new(<a>)).^name, " ", (SetHash.new(<a>) (+) mix(<a>)).^name, " ", (BagHash.new(<a>) (+) <a>).^name, " ", (<a> (+) BagHash.new(<a>)).^name, " ", (MixHash.new(<a>) (+) <a>).^name, " ", (bag() (+) BagHash.new(<a>)).^name, " ", (BagHash.new (+) bag(<a>)).^name, " ", (bag() (+) MixHash.new(<a>)).^name, " ", ([(+)]).gist, " ", ([(+)] set(<a>)).gist, " ", ([(+)] SetHash.new(<a>)).^name, " ", ([(+)] bag(<a>)).^name, " ", ([(+)] BagHash.new(<a>)).^name, " ", ([(+)] mix(<a>)).^name, " ", ([(+)] MixHash.new(<a>)).^name, " ", ([(+)] <a b>).gist, " ", ([(+)] {a => 2}).gist, " ", ([(+)] <a b c>, <c d e>, <d e f>).gist, " ", ([(+)] (:2a).Bag, (:7a).Bag, (:3a).Bag).gist, " ", ([(+)] (a => -42).Mix, set(), (:42a).Mix).gist, " ", ([(+)] (a => -42).Mix, <b>.Set, (:42a).Bag).gist, " ", ([(+)] SetHash.new(<a>), <a>, <a>).^name, " ", ((1..3, 1..3) Z(+) (2..4, 1..4)).gist, " ", (1 (+) ($ = :42foo,)).gist, " ", (Nil (+) Nil).gist, " ", (infix:<(+)>(SetHash.new(<a>))).^name, " ", (infix:<(+)>(BagHash.new(<a>))).^name, " ", (bag(<a>) (+) (a => 2)).gist, " ", (bag(<a>) (+) (a => -2)).gist, " ", (bag(<a>) (+) {a => -2}).gist, " ", (mix(<a>) (+) {a => -2}).gist, " ", (mix(<a>) (+) (a => -1)).gist, " ", (mix(<a>) (+) (a => -1).Mix).gist, " ", ((mix(<a>) (+) (a => -1).Mix) =:= mix()), " ", (bag(<a>) (+) bag(<a>) (+) bag(<a>)).gist, " ", ([(+)] bag(<a>), bag(<a>), bag(<a>)).gist, " ", (BagHash.new (+) BagHash.new).^name, " ", ((bag() (+) bag(<a>)) =:= bag(<a>)), " ", (set(<a>) (+) Set).gist, " ", (bag(<a>) (+) Bag).gist
# rakudo 2026.08: Bag(a(3) b c) Bag(a b(2) c) Bag Bag(a b(2) c) Bag(a b(2) c) Bag Mix(a(2.5)) Mix() True Mix(a) Bag(a(44) b) Bag(a(43) c(2)) Bag(a(43) d e) Bag(a(42) c(5) d(42) e) Bag(42(2)) Bag(42 666) Bag(ab(2)) Bag((Set)(2)) Bag((Set)) Bag() True BagHash Bag Bag BagHash Bag MixHash Mix MixHash BagHash Bag MixHash Bag BagHash Mix Bag() Bag(a) Bag Bag Bag Mix Mix Bag(a b) Bag(a(2)) Bag(a b c(2) d(2) e(2) f) Bag(a(12)) Mix() Mix(b) BagHash (Bag(1 2(2) 3(2) 4) Bag(1(2) 2(2) 3(2) 4)) Bag(1 foo(42)) Bag(Nil(2)) Bag Bag Bag(a(3)) Bag(a) Bag(a) Mix(a(-1)) Mix() Mix() True Bag(a(3)) Bag(a(3)) BagHash False Bag((Set) a) Bag((Bag) a)
```
rakupp 4.0.1-84: differs — the empty results are not the sentinels, `[(+)]` with no arguments is `(Any)`, `[(+)]` of one argument keeps the argument's type (`Set(a)`, `SetHash`, `BagHash`, `MixHash`) and `[(+)] {a => 2}` returns the Hash.

### SB-36  `(.)` / `⊍` multiplication                                          D:partial R:yes V:spec
Keys present on both sides with the weights multiplied (`bag(<a a b>) ⊍
bag(<a a a c>)` is `Bag(a(6))`, `Mix(a(-2)) ⊍ Mix(a(-3))` `Mix(a(6))`, `bag(<a
a>) ⊍ ("a" => 0.5).Mix` `Mix(a)`); Set with Set is a Bag of the common keys
with weight 1; a Pair or Hash on the right of a Bag is a Bag (so `bag(<a a>)
⊍ (a => 0.5)` is empty: 0.5 truncates to 0) and of a Mix a Mix (`mix(<a>) ⊍
(a => 0.5)` is `Mix(a(0.5))`); `42 ⊍ 666`, `set(<a>) ⊍ Set` and `bag(<a>) ⊍ Nil`
are empty. Mutability follows the left operand; `[(.)]` with no arguments
is `bag()`; with ONE argument a Set becomes its Bag and a hash form keeps
its mutability (`[(.)] SetHash.new(<a>)` is a BagHash, `[(.)] MixHash.new(<a>)`
a MixHash — unlike `(+)`); big weights stay exact.
```
say (bag(<a a b>) (.) bag(<a a a c>)).gist, " ", (set(<a b>) (.) set(<b c>)).gist, " ", (set(<a b>) (.) set(<b c>)).^name, " ", (<a b> ⊍ <b c>).gist, " ", (<a a b> (.) <b c>).gist, " ", (bag(<a a>) (.) ("a" => 0.5).Mix).gist, " ", (("a" => -2).Mix (.) ("a" => -3).Mix).gist, " ", (("a" => pi, "b" => tau).Mix (.) <b c>.Mix).gist, " ", (set(<a b>) (.) ("b" => -1).Mix).gist, " ", (bag(<a a b>) (.) {a => 42, b => 0}).gist, " ", ({a => 2} (.) {a => 3, b => 1}).gist, " ", ((:2a, :40a, :0b, :c) (.) (:2a, :4c, :42d)).gist, " ", (42 (.) 42).gist, " ", (42 (.) 666).gist, " ", (Set (.) Set).gist, " ", (Set (.) set()).gist, " ", ((set() (.) set()) =:= bag()), " ", (SetHash.new (.) set()).^name, " ", (set() (.) SetHash.new).^name, " ", (SetHash.new(<a>) (.) bag(<a>)).^name, " ", (bag(<a>) (.) SetHash.new(<a>)).^name, " ", (BagHash.new(<a>) (.) mix(<a>)).^name, " ", (mix(<a>) (.) BagHash.new(<a>)).^name, " ", (SetHash.new(<a>) (.) mix(<a>)).^name, " ", (MixHash.new(<a>) (.) set(<a>)).^name, " ", (BagHash.new(<a>) (.) <a>).^name, " ", (<a> (.) BagHash.new(<a>)).^name, " ", ([(.)]).gist, " ", ([(.)] set(<a>)).^name, " ", ([(.)] SetHash.new(<a>)).^name, " ", ([(.)] bag(<a>)).^name, " ", ([(.)] BagHash.new(<a>)).^name, " ", ([(.)] mix(<a>)).^name, " ", ([(.)] MixHash.new(<a>)).^name, " ", ([(.)] <a b>).gist, " ", ([(.)] <a b c>, <b c d>, <c d e>).gist, " ", ([(.)] (:2a).Bag, (:7a).Bag, (:3a).Bag).gist, " ", ([(.)] (a => -42).Mix, <a>.Mix, (:2a).Mix).gist, " ", ([(.)] (a => -42).Mix, <b>.Set, (:42a).Bag).gist, " ", ([(.)] SetHash.new(<a>), <a>, <a>).^name, " ", ((1..3, 1..3) Z(.) (2..4, 1..4)).gist, " ", (1 (.) ($ = :42foo,)).gist, " ", (bag(<a>) (.) Nil).gist, " ", (bag(<a a>) (.) (a => 3)).gist, " ", (bag(<a a>) (.) (a => 0.5)).gist, " ", (bag(<a a>) (.) (a => 0.5).Mix).gist, " ", (bag(<a a>) (.) (a => -1).Mix).gist, " ", (bag(<a a>) (.) {a => 3}).gist, " ", (mix(<a>) (.) {a => 0.5}).gist, " ", (mix(<a>) (.) (a => 0.5)).gist, " ", ((a => 0.5).Mix (.) (a => 0.5).Mix).gist, " ", (set(<a>) (.) Set).gist, " ", (BagHash.new(<a a>) (.) bag(<a a>)).gist, " ", (infix:<(.)>(SetHash.new(<a>))).^name, " ", (infix:<(.)>(set(<a>))).^name, " ", ((a => 2).Bag (.) (a => 10**20).Bag).gist
# rakudo 2026.08: Bag(a(6)) Bag(b) Bag Bag(b) Bag(b) Mix(a) Mix(a(6)) Mix(b(6.283185307179586)) Mix(b(-1)) Bag(a(84)) Bag(a(6)) Bag(a(84) c(4)) Bag(42) Bag() Bag((Set)) Bag() True BagHash Bag BagHash Bag MixHash Mix MixHash MixHash BagHash Bag Bag() Bag BagHash Bag BagHash Mix MixHash Bag() Bag(c) Bag(a(42)) Mix(a(-84)) Mix() BagHash (Bag(2 3) Bag(1 2 3)) Bag() Bag() Bag(a(6)) Bag() Mix(a) Mix(a(-2)) Bag(a(6)) Mix(a(0.5)) Mix(a(0.5)) Mix(a(0.25)) Bag() BagHash(a(4)) BagHash Bag Bag(a(200000000000000000000))
```
rakupp 4.0.1-84: differs — `set() ⊍ set()` is not the sentinel, `[(.)]` with no arguments is `(Any)`, `[(.)] set(<a>)` stays a Set, `bag(<a a>) ⊍ (a => 0.5)` is `Bag(a)`, and a 10**20 weight becomes `2e+20`.

### SB-37  `(<=)` `⊆`, `(<)` `⊂`, `(>=)` `⊇`, `(>)` `⊃` and their negations           D:partial R:yes V:spec
With Set semantics `A (<=) B` is "every key of A is in B", `(<)` adds "and B
is larger"; with Bag semantics each weight on the left must be ≤ the weight
on the right (absent = 0), `(<)` additionally requires one strictly smaller;
with Mix semantics the same comparison runs in BOTH directions with absent
keys as 0, so a negative weight is below absence (`(a => -1).Mix (<=) mix()`
True, `mix() (<=) (a => -1).Mix` False, `(a => -1).Mix (<) mix()` True, `mix()
(<) (a => 1).Mix` True, `(a => -2).Mix (<) (a => -1).Mix` True). The kind is
the higher of the two operands (a Set against a Bag uses weights:
`bag(<a a>) (<=) set(<a>)` False, `set(<a>) (<=) bag(<a a>)` True; a Str, Int,
Nil or list is coerced as in SB-31, so `<a a> (<=) bag(<a>)` is False, a
Hash honours truthiness: `{a => 0} (<=) {a => 1}` True, `{a => 1} (<=) {a =>
0}` False). `(>=)`/`(>)` swap the operands; `⊈`, `⊄`, `⊉`, `⊅` and `!(<=)`
negate. Type objects are elements (`Set (<=) Set` True, `Set (<) Set` False);
the operators chain (`set(<a>) ⊂ set(<a b>) ⊂ set(<a b c>)` True).
```
say (set(<a>) (<=) set(<a b>)), " ", (set(<a b>) ⊆ set(<a b>)), " ", (set(<a b>) (<) set(<a b>)), " ", (set(<a>) ⊂ set(<a b>)), " ", (set(<a b>) (>=) set(<a>)), " ", (set(<a b>) ⊇ <a>), " ", (set(<a b>) (>) <a b>), " ", (set(<a b>) ⊃ <a>), " ", (set(<a>) ⊈ set(<b>)), " ", (set(<a>) ⊄ set(<a>)), " ", (set(<a b>) ⊉ set(<c>)), " ", (set(<a>) ⊅ set(<a>)), " ", (bag(<a>) (<=) bag(<a a>)), " ", (bag(<a a>) (<=) bag(<a>)), " ", (bag(<a>) (<) bag(<a a>)), " ", (bag(<a a>) (<) bag(<a a b>)), " ", (bag(<a>) (<=) set(<a>)), " ", (bag(<a a>) (<=) set(<a>)), " ", (set(<a>) (<=) bag(<a a>)), " ", (set(<a b>) (<) bag(<a b>)), " ", (bag(<a>) (<) mix(<a a>)), " ", (("a" => -1).Mix (<=) mix()), " ", (mix() (<=) ("a" => -1).Mix), " ", (("a" => -1).Mix (<) mix()), " ", (mix() (<) ("a" => -1).Mix), " ", (mix() (<) ("a" => 1).Mix), " ", (("a" => -2).Mix (<) ("a" => -1).Mix), " ", (("a" => -1).Mix (<) ("a" => -1).Mix), " ", (("a" => 0.5).Mix (<=) bag(<a>)), " ", (bag(<a>) (<=) ("a" => 0.5).Mix), " ", ({a => 0} (<=) {a => 1}), " ", ({a => 1} (<=) {a => 0}), " ", ({a => 0} (<) {}), " ", ({} (<) {a => 0}), " ", ({} (<=) {}), " ", ({a => 1} (<=) <a b>), " ", (<a b> (<=) {a => 1, b => "x"}), " ", (<a a> (<=) <a>), " ", (<a a> (<) <a>), " ", (<a a> (<) <a b>), " ", (42 (<=) 42), " ", (42 (<) 42), " ", (Nil (<=) Nil), " ", (Set (<=) Set), " ", (Set (<) Set), " ", (set() (<=) Set), " ", (Set (<=) set()), " ", (1 (<=) ($ = :42foo,)), " ", (1 (>=) ($ = :42foo,)), " ", (set(<a>) (<=) SetHash.new(<a>)), " ", (SetHash.new (<) set(<a>)), " ", ((:0a,) (<) ("a",)), " ", (("a",) (<) (:0a,)), " ", (mix() (<) mix()), " ", (("a" => -1).Mix (<) ("b" => 1).Mix), " ", (("a" => 1).Mix (<) ("a" => 1, "b" => -1).Mix), " ", (("a" => 1, "b" => -1).Mix (<) ("a" => 1).Mix), " ", (bag(<a>) (<=) ("a" => -1).Mix), " ", (("a" => -1).Mix (<=) bag(<a>)), " ", ({a => 0} (<=) set()), " ", (set() (<=) {a => 0}), " ", ({a => 0} (<) set()), " ", (set() (<) {a => 0}), " ", ({a => 0} (<) {a => 1}), " ", ({a => 0} (<) (:a,)), " ", ((:a,) (<) {a => 0, b => 1}), " ", (bag(<a a>) (<) bag(<a a>)), " ", (bag(<a>) (<) bag(<a b>)), " ", (bag() (<) bag()), " ", (bag() (<) bag(<a>)), " ", (set() (<) set()), " ", (set() (<) set(<a>)), " ", (set(<a>) (<=) <a a>), " ", (<a a> (<=) set(<a>)), " ", (<a a> (<=) bag(<a>)), " ", (bag(<a a>) (<=) <a a>), " ", (bag(<a a>) (<=) <a>), " ", (set(<a>) (<=) set(<a>) (<=) set(<a b>)), " ", (set(<a>) ⊂ set(<a b>) ⊂ set(<a>))
# rakudo 2026.08: True True False True True True False True True True True True True False True True True False True False True True False True False True True False True False True False False False True True True True False True True False True True False True False False False True True True False False True False True False True True True False False True True False False True False True False True True True False True False True False
say (set(<a>) !(<=) set(<b>)), " ", (set(<a>) !(<) set(<a b>)), " ", (set(<a>) !(==) set(<b>)), " ", (1 !(elem) (2,)), " ", ((2,) !(cont) 1)
# rakudo 2026.08: True False True True True
```
rakupp 4.0.1-84: differs — `mix() (<=) (a => -1).Mix` is True and `(a => -1).Mix (<) mix()` False (a negative weight is not below absence), `("a" => 1, "b" => -1).Mix (<) ("a" => 1).Mix` is False, the chained `set(<a>) (<=) set(<a>) (<=) set(<a b>)` is False, and `!(<=)` does not parse (the second probe died at compile time).

### SB-38  `(==)` `≡` `≢` set equality                                          D:partial R:yes V:quirk
Equality in the higher kind of the two operands: `set(<a>) (==) bag(<a>)`
True, `bag(<a a>) (==) set(<a>)` False, `set(<a>) (==) ("a" => 0.5).Mix`
False, `bag(<a>) (==) ("a" => 1.0).Mix` True, `mix() (==) (a => -1).Mix` False,
`(a => -1).Mix (==) (a => -1).MixHash` True; all the empties are equal
(`set() (==) bag()`, `(==) mix()`, `(==) {}`, `(==) ()`, `(==) {a => 0}` True);
lists are compared as sets (`<a a> (==) <a>`, `(1,2,3) (==) (1,3,2)` True), a
Str or Int is one element (`42 (==) "42"` False), `Nil (==) Nil` True but
`Nil (==) set()` False, a Pair is a weight (`(a => 0) (==) set()` True), a
Mix with whole weights equals a Bag (`("a" => 2.0).Mix (==) bag(<a a>)`
True), and the operator chains. The quirk: two plain Hashes or object hashes
compare their element COUNTS before truthiness, so `{a => 0} (==) {}` and
`{} (==) {a => 0}` are False although `{a => 0} (==) set()` is True, while
`{a => 1} (==) {a => 2}` is True (only truthiness is compared once the counts
agree). `≢` and `!(==)` negate.
```
say (set(<a b>) (==) set(<b a>)), " ", (set(<a>) ≡ bag(<a>)), " ", (set(<a>) ≢ bag(<a a>)), " ", (bag(<a a>) (==) set(<a>)), " ", (set(<a>) (==) ("a" => 0.5).Mix), " ", (bag(<a>) (==) ("a" => 1.0).Mix), " ", (mix() (==) ("a" => -1).Mix), " ", (("a" => -1).Mix (==) ("a" => -1).MixHash), " ", (set() (==) bag()), " ", (set() (==) mix()), " ", (set() (==) {}), " ", (set() (==) ()), " ", (set() (==) {a => 0}), " ", ({} (==) {a => 0}), " ", ({a => 0} (==) {}), " ", ({a => 0} (==) {a => 0}), " ", ({a => 0} (==) set()), " ", ({a => 1} (==) {a => 2}), " ", ({a => 1} (==) <a>), " ", (<a a> (==) <a>), " ", ((1,2,3) (==) (1,3,2)), " ", ((1,2,3) ≢ (1,2,4)), " ", (42 (==) 42), " ", (42 (==) "42"), " ", (42 (==) <42>), " ", (Nil (==) Nil), " ", (Nil (==) set()), " ", (Set (==) Set), " ", (Set (==) set()), " ", (set(<a>) (==) SetHash.new(<a>)), " ", (bag(<a>) (==) BagHash.new(<a>)), " ", (:{} (==) :{}), " ", (:{a => 0} (==) {}), " ", ({a => 0} (==) :{}), " ", (1 (==) ($ = :42foo,)), " ", (<a b> (==) {a => 1, b => 1}), " ", (<a b> (==) {a => 1, b => 0}), " ", (<a> (==) {a => 1, b => 0}), " ", (set(<a>) (==) {a => 1, b => 0}), " ", ({a => 1, b => 0} (==) <a>), " ", (<a a b> (==) {a => 1, b => 1}), " ", ((1..3) (==) (1..3)), " ", (bag(<a>) == set(<a>)), " ", (bag(<a a>) == set(<a>)), " ", (<a> (==) {a => 0, b => 1}), " ", ({a => 0, b => 1} (==) <b>), " ", (<a> (==) :{a => 1, b => 0}), " ", ({a => 0} (==) :{a => 0}), " ", ({a => 0, b => 1} (==) {b => 1, c => 0}), " ", ((a => 1) (==) (a => 1)), " ", ((a => 1) (==) <a>), " ", ((a => 0) (==) set()), " ", (<a a> (==) bag(<a>)), " ", (<a a> (==) bag(<a a>)), " ", (bag(<a a>) (==) <a a>), " ", (mix(<a>) (==) <a>), " ", (("a" => 2.0).Mix (==) bag(<a a>)), " ", (("a" => 2e0).Mix (==) bag(<a a>)), " ", (set(<a>) (==) set(<a>) (==) bag(<a>))
# rakudo 2026.08: True True True False False True False True True True True True True False False True True True True True True True True False False True False True False True True True False False False True False True True True True True True False False True True True True True True True False True True True True True True
```
rakupp 4.0.1-84: differs — `mix() (==) (a => -1).Mix` is True, `{} (==) {a => 0}`, `{a => 0} (==) {}`, `:{a => 0} (==) {}` and `{a => 0} (==) :{}` are True (the consistent answer; keep it), `42 (==) "42"` is True, and the chained form answers False.

### SB-39  Precedence, associativity, metaoperators                              D:partial R:partial V:spec
`(|)`, `(^)`, `(+)`, `(-)`, `∪`, `⊖`, `⊎`, `∖` sit at junctive-or precedence
and `(&)`, `(.)`, `∩`, `⊍` at junctive-and: tighter than every chaining
operator (`<a b> (|) <b c> ~~ Set` and `<a b> (&) <b c> == 1` need no
parentheses), looser than arithmetic and `..` (`1 + 2 (|) 4` is `Set(3 4)`,
`1 (|) 2 + 3` `Set(1 5)`, `1 .. 3 (|) 4` is a Range whose end is a Set,
`<a> (|) <b> .. <c>` a Range whose start is a Set); `(&)` binds tighter than
`|` (`<a> (&) <b> | <c>` is a Junction). Mixing two different operators of
the same level without parentheses is a compile-time
`X::Syntax::NonListAssociative` (`<a> (|) <b> (-) <b>`, `<a> (|) <b> | <c>`,
`<a> (-) <b> ^ <c>`, `<a> (|) <b> ∪ <c>`); the same operator chains
list-associatively. The Boolean operators are CHAINING: `1 (elem) (1,2) ==
True` is `(1 (elem) (1,2)) and ((1,2) == True)` (False), `1 (elem) (1,) (elem)
(True,)` False, `set(<a>) (<=) set(<a>) (==) set(<a>)` True; `1 (elem) 1..3 &&
"yes"` is "yes"; `&` binds tighter than `(elem)`. `X(|)`, `Z(elem)`, `[+]` over
set results, `xx`, and the assignment forms `(|)=`, `(&)=`, `(-)=`, `(^)=`,
`(+)=`, `(.)=` all work; on a SetHash variable `(|)=` keeps a SetHash, on an
`is Set` variable it is `X::Assignment::RO`.
```
say ((<a b> (|) <b c>) ~~ Set), " ", (<a b> (|) <b c> ~~ Set), " ", (<a b> (&) <b c> == 1), " ", (<a b> (&) <b c> eqv set(<b>)), " ", (1 (elem) 1..3 && "yes"), " ", (1 (elem) (1,2) == True), " ", (so 1 (elem) (1,2) (elem) (True, False)), " ", (2 (elem) 1..3 (elem) (True,)), " ", ((<a> (|) <b>) (-) <b>).gist, " ", (<a> ∪ <b> ∪ <c>).gist, " ", (1 (elem) (1,2) and 3), " ", (1 (elem) (1,2) (elem) (True,)), " ", (bag(<a>) (+) bag(<a>) (<=) bag(<a a a>)), " ", (set(<a>) ⊂ set(<a b>) ⊂ set(<a b c>)), " ", ((1,2) X(|) (2,3)).gist, " ", ((1,2) Z(elem) ((1,),(3,))).gist, " ", ((1,2) (|) (2,3) === set(1,2,3)), " ", (<a b> (|) <c> ~~ set(<a b c>)), " ", (1 (elem) (1,2) ?? "t" !! "f"), " ", (set(<a>) (|) set(<b>) (|) set(<c>) (==) set(<a b c>)), " ", (<a> (|) <b> == <c> (|) <d>), " ", (<a> (|) <b> === <a> (|) <b>), " ", (<a b> (&) <b> === set(<b>)), " ", (1 (elem) (1,) || 0), " ", (not 1 (elem) (2,)), " ", (?(<a> (|) <b>)), " ", (+(<a> (|) <b>)), " ", (so <a> (|) <b>), " ", ([+] <a> (|) <b>, <c> (|) <d>), " ", (<a> (|) <b> , <c>).elems, " ", (<a> (|) <b> xx 2).elems, " ", (<a> ∪ <b> ~~ Setty), " ", (bag(<a>) (+) bag(<a>) == 2), " ", (do { my $s = set(<a>); $s (|)= <b>; $s (&)= <b c>; $s (-)= <z>; $s (^)= <c>; $s.gist }), " ", (do { my $b = bag(<a>); $b (+)= <a>; $b (.)= (a => 3); $b.gist }), " ", (do { my $s = SetHash.new(<a>); $s (|)= <b>; $s.^name ~ $s.gist }), " ", (do { my %s is SetHash = <a>; %s (|)= <b>; %s.^name ~ %s.gist }), " ", (do { my %s is Set = <a>; (try { %s (|)= <b>; %s.gist }) // $!.^name }), " ", (try EVAL('<a> (|) <b> (-) <b>')) // $!.^name, " ", (try EVAL('<a> (|) <b> | <c>')) // $!.^name, " ", (try EVAL('<a> (-) <b> ^ <c>')) // $!.^name, " ", (try EVAL('<a> (|) <b> ∪ <c>')) // $!.^name, " ", (try EVAL('(<a> (&) <b> | <c>).^name')) // $!.^name, " ", (try EVAL('(1 (elem) (1,) & 2).^name')) // $!.^name, " ", (try EVAL('(<a> (|) <b> .. <c>).min.^name')) // $!.^name, " ", (try EVAL('(1 .. 3 (|) 4).max.^name')) // $!.^name, " ", (try EVAL('(1 + 2 (|) 4).gist')) // $!.^name, " ", (try EVAL('(1 (|) 2 + 3).gist')) // $!.^name, " ", (try EVAL('(1 (elem) 1 + 0).gist')) // $!.^name, " ", (try EVAL('("a" ~ "b" (elem) ("ab",)).gist')) // $!.^name, " ", (try EVAL('(<a> (|) <b> eqv set(<a b>)).gist')) // $!.^name, " ", (try EVAL('(1 (elem) (1,) (elem) (True,)).gist')) // $!.^name, " ", (try EVAL('(set(<a>) (<=) set(<a>) (==) set(<a>)).gist')) // $!.^name, " ", (try EVAL('(1 (elem) (1,) == True).gist')) // $!.^name
# rakudo 2026.08: True True True True yes False False False Set(a) Set(a b c) 3 False True True (Set(1 2) Set(1 3) Set(2) Set(2 3)) (True False) True True t True True True True True True True 2 True 4 2 2 True True Set(b c) Bag(a(6)) SetHashSetHash(a b) SetHashSetHash(a b) X::Assignment::RO X::Syntax::NonListAssociative X::Syntax::NonListAssociative X::Syntax::NonListAssociative X::Syntax::NonListAssociative Junction Junction Set Set Set(3 4) Set(1 5) True True True False True True
```
rakupp 4.0.1-84: differs — the Boolean operators do not chain (`1 (elem) (1,2) == True` and `1 (elem) (1,) (elem) (True,)` are True, the `⊂` chain is False), mixed operators at one level are accepted (`<a> (|) <b> (-) <b>` is `Set(a b)`, `<a> (|) <b> | <c>` a Junction), `1 .. 3 (|) 4` and `<a> (|) <b> .. <c>` are Int ranges, `1 (|) 2 + 3` is 5, and `(|)=` on an `is Set` variable is accepted.

### SB-40  `(<+)`, `(>+)`, `≼`, `≽` were removed in 6.d                          D:yes R:partial V:spec
The four operators still exist as Subs but throw `X::AdHoc` when called
("(<+) was removed in v6.d, please use (<=) operator instead"), for any
operands including type objects and a Failure, and `use v6.c` inside an
`EVAL` does not bring them back. Roast keeps their 6.c semantics only under
`6.c/`.
```
say (try bag(<a>) (<+) bag(<a a>)) // $!.^name ~ ":" ~ $!.message.substr(0, 25), " ", (try bag(<a a>) (>+) bag(<a>)) // $!.^name, " ", (try set(<a>) ≼ bag(<a a>)) // $!.^name, " ", (try bag(<a a>) ≽ set(<a>)) // $!.^name, " ", (try Set (<+) Set) // $!.^name, " ", (try set(1) (<+) Failure.new) // $!.^name, " ", (try EVAL('use v6.c; bag(<a>) (<+) bag(<a a>)')) // $!.^name, " ", &infix:<<(<+)>>.^name, " ", &infix:<≼>.^name
# rakudo 2026.08: X::AdHoc:(<+) was removed in v6.d, X::AdHoc X::AdHoc X::AdHoc X::AdHoc X::AdHoc X::AdHoc Sub Sub
```
rakupp 4.0.1-84: differs — `(<+)` does not parse at all (the probe died at compile time).

### SB-41  A Failure operand throws                                             D:no R:yes V:spec
A Failure on either side of any set operator (`(|)`, `(&)`, `(-)`, `(^)`,
`(+)`, `(.)`, `(<)`, `(==)`, `(elem)`, `(cont)`, `(<=)`) throws the Failure's
exception at once (`X::AdHoc` for `Failure.new`, `X::Str::Numeric` for the
Failure of `"x".Int`), even when the Failure was already handled; so do
`Failure.new.Set`, `set(Failure.new)` and `(Failure.new,).Set`. A handled
Failure CAN be an element: `$x.so; $x.Set` is a one-element set holding the
Failure. A lazy `.Set` operand throws `X::Cannot::Lazy` (SB-08).
```
say (try set(1) (|) Failure.new) // $!.^name, " ", (try Failure.new (&) set(1)) // $!.^name, " ", (try set(1) (-) Failure.new) // $!.^name, " ", (try set(1) (^) Failure.new) // $!.^name, " ", (try set(1) (+) Failure.new) // $!.^name, " ", (try set(1) (.) Failure.new) // $!.^name, " ", (try set(1) (<) Failure.new) // $!.^name, " ", (try set(1) (==) Failure.new) // $!.^name, " ", (try 1 (cont) Failure.new) // $!.^name, " ", (try Failure.new (<=) set(1)) // $!.^name, " ", (try Failure.new.Set) // $!.^name, " ", (try set(Failure.new).elems) // $!.^name, " ", (try (Failure.new,).Set.elems) // $!.^name, " ", (try (1..*).map({$_}).Set) // $!.^name, " ", (try set(1) (|) (1..*).map({$_}).Set) // $!.^name, " ", do { my $x = "x".Int; $x.so; (try set(1) (|) $x) // $!.^name }, " ", do { my $x = "x".Int; $x.so; (try 1 (elem) $x) // $!.^name }, " ", do { my $x = "x".Int; $x.so; $x.Set.elems ~ ":" ~ $x.Set.keys[0].^name }, " ", do { my $x = "x".Int; $x.so; $x.Bag.elems }
# rakudo 2026.08: X::AdHoc X::AdHoc X::AdHoc X::AdHoc X::AdHoc X::AdHoc X::AdHoc X::AdHoc X::AdHoc X::AdHoc X::AdHoc X::AdHoc X::AdHoc X::Cannot::Lazy X::Cannot::Lazy X::Str::Numeric X::Str::Numeric 1:Failure 1
```
rakupp 4.0.1-84: differs — the line died at the first handled-Failure field (`$x.so` on a Failure throws the exception outright).

### SB-42  Numeric, string and boolean context                                  D:no R:partial V:spec
`+$q`, `.Numeric`, `.Int`, `.Num`, `.Real` are the total (Set: the element
count; a Mix total may be negative or fractional, `.Int` truncates), and
the arithmetic and comparison operators use it (`set(<a b>) + 1` is 3,
`bag(<a a>) * 2` 4, `set(<a>) < 2`, `set() == 0`, `set() ~~ 0` True, `set(<a b>)
~~ 2` True, `2 ~~ set(<a b>)` False). `~$q` and `eq` use `.Str` (`"a(2)"` for
`bag(<a a>)`), `set(<a>) ~~ "a"` is True (Str.ACCEPTS on the Str). `?$q` is
True when a key exists, including a Mix of negative weights; a MixHash whose
keys were set to 0 is False. A QuantHash is not Cool: `.chars`, `.succ`,
`.abs`, `.sqrt` are `X::Method::NotFound`, `~~ Cool`, `~~ Numeric`, `~~ Stringy`
and `~~ Int` are False, `.Stringy` still answers a Str.
```
say +set(<a b>), " ", +bag(<a a b>), " ", +mix(<a>), " ", +("a" => 0.5, "b" => -2).Mix, " ", (set(<a b>) + 1), " ", (bag(<a a>) * 2), " ", set(<a b>).Int, " ", ("a" => 2.5).Mix.Int, " ", ("a" => 2.5).Mix.Num, " ", set(<a>).Num.^name, " ", set(<a>).Real.^name, " ", ("a" => 1/3).Mix.Real.^name, " ", (~set(<a>)), " ", (~bag(<a a>)), " ", (~mix()).raku, " ", ("x" ~ set(<a>)), " ", (set(<a>) ~~ "a"), " ", (set(<a>) eq "a"), " ", (bag(<a a>) eq "a(2)"), " ", ?set(), " ", ?bag(<a>), " ", ?("a" => -1).Mix, " ", ?("a" => 0.0).MixHash, " ", (set(<a>) == 1), " ", (set(<a>) < 2), " ", (set(<a b>) > bag(<a>)), " ", (try set(<a>).chars) // $!.^name, " ", (try set(<a>).succ) // $!.^name, " ", (try set(<a>).abs) // $!.^name, " ", (try set(<a>).sqrt) // $!.^name, " ", (set(<a>) ~~ Cool), " ", (set(<a>) ~~ Numeric), " ", (set(<a>) ~~ Stringy), " ", (try set(<a>).Stringy.^name) // $!.^name, " ", bag(<a>).Numeric.^name, " ", (set() + 0), " ", (try set(<a>) lt "b") // $!.^name, " ", (set(<a>) + set(<b>)), " ", (bag(<a a>) - 1), " ", (set(<a>) * bag(<a a a>)), " ", (try (set(<a>) x 2).chars) // $!.^name, " ", (set(<a>) ~ set(<b>)).chars, " ", (mix(<a>) / 2), " ", (bag(<a a>) ** 2), " ", (set() == 0), " ", (set() ~~ 0), " ", (0 ~~ set()), " ", (set(<a>).Int.^name), " ", (("a" => 1.5).Mix.Numeric.raku), " ", (("a" => 1e0).Mix.Numeric.raku), " ", (set(<a>).Numeric.raku), " ", (bag(<a>).Real.raku), " ", (set(<a>) ~~ Int), " ", (set(<a b>) ~~ 2), " ", (2 ~~ set(<a b>)), " ", (set(<a>).Bool.^name), " ", (set(<a>).so), " ", (set(<a>).not), " ", (set(<a>) ?? "t" !! "f"), " ", (set() ?? "t" !! "f"), " ", (set() || "empty"), " ", (mix(("a" => -1),) && "neg")
# rakudo 2026.08: 2 3 1 -1.5 3 4 2 2 2.5 Num Int Rat a a(2) "" xa True True True False True True False True True True X::Method::NotFound X::Method::NotFound X::Method::NotFound X::Method::NotFound False False False Str Int 0 True 2 1 3 2 2 0.5 4 True True False Int 1.5 1e0 1 1 False True False Bool True False t f empty neg
```
rakupp 4.0.1-84: differs — `.Num` of a Mix truncates (2), `.Real` is a Num, the Cool methods exist (`.chars` 1, `.succ` 2), `~~ Cool` True, `set(<a b>) ~~ 2` False, and `.Numeric` of a Set is a Num.

### SB-43  The type objects as invocants                                       D:no R:partial V:spec
`Set.elems` is 1, `.Bool`/`.defined` False, `.hash` and `.Hash` `{}`,
`.Capture` `\()`, `.list` `(Set,)`, `.keys`/`.pairs`/`.values`/`.kv`/
`Bag.antipairs` empty, `.gist` `(Set)`, `.raku` `Set`, `.new`/`.new-from-pairs`
the empty set, `.Set` a set holding the type object, `.of` Bool, `.clone`
the type object (`SetHash.clone` is `X::AdHoc`), `.WHICH` a ValueObjAt,
`.default` `False` for Set. The instance-only methods refuse: `.total`,
`.Numeric`, `+Set`, `.iterator`, `Bag.default`, `SetHash.set(1)`,
`BagHash.add(1)` with `X::Parameter::InvalidConcreteness`; `.roll`, `.grab`,
`.fmt`, `.minpairs`, `.pickpairs`, `Bag.kxxv` with `X::Multi::NoMatch`;
`.Map` with `X::AdHoc`; `.pick` on the type object is `Any.pick` and returns
the type object.
```
say Set.elems, " ", Set.Bool, " ", Set.hash.raku, " ", Set.gist, " ", Set.raku, " ", (try Set.total) // $!.^name, " ", (try Set.keys.raku) // $!.^name, " ", Set.list.raku, " ", Set.new-from-pairs.gist, " ", Set.new.gist, " ", Set.defined, " ", (try Set.pairs.raku) // $!.^name, " ", (try Set.values.raku) // $!.^name, " ", (try Set.kv.raku) // $!.^name, " ", (try Set.pick.raku) // $!.^name, " ", (try Set.roll.raku) // $!.^name, " ", (try Bag.total) // $!.^name, " ", (try Mix.total) // $!.^name, " ", (try Set.Set.gist) // $!.^name, " ", (try Set.Bag.gist) // $!.^name, " ", (try Set.Map.raku) // $!.^name, " ", (try Set.Hash.raku) // $!.^name, " ", (try Bag.hash.raku) // $!.^name, " ", (try Set.Capture.raku) // $!.^name, " ", (try Set.default.raku) // $!.^name, " ", (try Bag.default.raku) // $!.^name, " ", (try Set.WHICH.^name) // $!.^name, " ", (try Set.Numeric) // $!.^name, " ", (try +Set) // $!.^name, " ", (try SetHash.elems) // $!.^name, " ", (try SetHash.gist) // $!.^name, " ", (try SetHash.Set.gist) // $!.^name, " ", (try Bag.Setty.^name) // $!.^name, " ", (try Set.grab) // $!.^name, " ", (try SetHash.grab) // $!.^name, " ", (try Set.iterator.^name) // $!.^name, " ", (try Set.fmt) // $!.^name, " ", (try Mix.roll.raku) // $!.^name, " ", (try Bag.kxxv.raku) // $!.^name, " ", (try Set.^name), " ", (try Set.list.elems), " ", (try Set.of.^name), " ", (try Set.minpairs.raku) // $!.^name, " ", (try Bag.antipairs.raku) // $!.^name, " ", (try Set.pickpairs.raku) // $!.^name, " ", (try SetHash.set(1)) // $!.^name, " ", (try BagHash.add(1)) // $!.^name, " ", (try Set.clone.^name) // $!.^name, " ", (try SetHash.clone.^name) // $!.^name, " ", (try Set.Setty.^name)
# rakudo 2026.08: 1 False {} (Set) Set X::Parameter::InvalidConcreteness () (Set,) Set() Set() False () () () Set X::Multi::NoMatch X::Parameter::InvalidConcreteness X::Parameter::InvalidConcreteness Set((Set)) Bag((Set)) X::AdHoc {} {} \() Bool::False X::Parameter::InvalidConcreteness ValueObjAt X::Parameter::InvalidConcreteness X::Parameter::InvalidConcreteness 1 (SetHash) Set((SetHash)) Set X::Multi::NoMatch X::Multi::NoMatch X::Parameter::InvalidConcreteness X::Multi::NoMatch X::Multi::NoMatch X::Multi::NoMatch Set 1 Bool X::Multi::NoMatch () X::Multi::NoMatch X::Parameter::InvalidConcreteness X::Parameter::InvalidConcreteness Set X::AdHoc Set
```
rakupp 4.0.1-84: differs — the refusals are `X::Method::NotFound` or plain values (`Set.roll` Set, `+Set` 0, `Set.iterator` Iterator, `Set.minpairs` a Pair list, `Set.clone` a path), and `Set.Map` throws `X::Hash::Store::OddNumber`.

### SB-44  Iteration, sort, cmp, list methods                                  D:partial R:partial V:spec
A QuantHash is NOT Iterable: `for $bag { }` runs once with the bag itself
(iterate `.pairs`, `.keys` or `.kv`); assigning it to an array or hash
gives its Pairs. The list methods act on `.list`, the Seq of `key => weight`
Pairs: `.sort` (by Pair `cmp`: key, then weight), `.sort(&by)`, `.map`,
`.grep`, `.first`, `.head`, `.max`/`.min` (by Pair `cmp`, or with a key
function), `.reverse`, `.Seq`, `.List`, `.Array`, `.Slip`, `.cache`, `.end`
(elems − 1), `.join` (pairs stringify as `key\tweight`); `.sum` is
`X::Multi::NoMatch` (Pairs); `$b[0]` returns the bag itself and `$b[1]` is
`X::OutOfRange` (a non-Positional). `cmp` compares sorted pairs (SB-16),
`leg` the Strs.
```
say do { my $b = bag <b a a>; my @p; for $b { @p.push($_.^name) }; (@p.raku, $b.sort.raku, $b.sort(*.value).raku, $b.map(*.key).sort.raku, $b.grep(*.value > 1).raku, $b.first(*.value == 1).raku, $b.elems, $b.Seq.^name, $b.List.^name, $b.List.elems, $b.Array.^name, $b.reverse.elems, $b.head.^name, $b.sort.map(*.key).raku, $b.pairs.sort.^name, ((1,2).Set.sort.raku), (set(<a>).sort.^name), $b.antipairs.sort.raku, $b.kv.^name, set(<a>).cache.^name, (set(<a b>) cmp set(<a b c>)), (set(<b>) cmp set(<a b>)), (bag(<a>) cmp mix(<a>)), (set(<a>) cmp 1), ((set(<a b>) cmp set(<a b>)).^name), (set(<a b>) cmp (<a b>).Set), ((<a b>.Set cmp <a b>.Bag).raku), $b.keys.sort.raku, $b.values.sort.raku, $b.sort.reverse.raku, $b.sort({ $^a.value <=> $^b.value || $^a.key cmp $^b.key }).raku, $b.max.raku, $b.min.raku, $b.max(*.value).raku, $b.min(*.value).raku, (try $b.sum) // $!.^name, (try $b.join(",").chars) // $!.^name, set(<a b>).sort.join(",").chars, $b.end, $b[0].^name, (try $b[1].raku) // $!.^name, $b.elems.^name, $b.Slip.elems, $b.Array.elems, $b.List[0].^name, (set(<a>) cmp set(<a>)) === Same, ([<] set(<a>), set(<b>)), (set(<a>) before set(<b>)), (bag(<a a>) leg bag(<a>)), set(<a b>).Str.words.elems, $b.pairs.elems, do { my @a = $b; @a.elems ~ " " ~ @a[0].^name }, do { my %h = $b; %h.sort.raku }, do { my ($x, $y) = $b.sort; $x.raku ~ " " ~ $y.raku }).join(" ") }
# rakudo 2026.08: ["Bag"] (:a(2), :b(1)).Seq (:b(1), :a(2)).Seq ("a", "b").Seq (:a(2),).Seq :b(1) 2 Seq List 2 Array 2 Pair ("a", "b").Seq Seq (1 => Bool::True, 2 => Bool::True).Seq Seq (1 => "b", 2 => "a").Seq Seq List Less More Same More Order Same Order::Same ("a", "b").Seq (1, 2).Seq (:b(1), :a(2)).Seq (:b(1), :a(2)).Seq :b(1) :a(2) :a(2) :b(1) X::Multi::NoMatch 7 13 1 Bag X::OutOfRange Int 2 2 Pair True False True More 2 2 1 Bag (:a(2), :b(1)).Seq :a(2) :b(1)
```
rakupp 4.0.1-84: differs in three fields — `.sum` is 0, `$b[0]` and `$b[1]` are Any.

## Counts

| | items |
|---|---|
| total | 44 |
| not fully stated by docs (D:yes) nor asserted by Roast (R:yes) | 17 |
| Rakudo bugs (do not imitate) | 4 — SB-07 `.Bag` truncates the source Mix's weights, SB-10 an uninitialised `is Set` variable is unusable, SB-30 `(|)` `(&)` `(+)` `(.)` hang on a Junction operand, SB-32 `Hash (&) Hash` ignores false values |
| quirks (recorded, step two decides) | 10 — SB-03 a negative Pair weight is skipped in Bag construction, SB-04 the string `"Inf"` slips into a Mix, SB-08 `X::Multi::Ambiguous` for a lazy Boolean operand and `.set(1..*)` hangs, SB-20 `.set(a Set)` adds Pairs, SB-22 `Inf`/`NaN` accepted by MixHash assignment, SB-24 the Mix hyper truncates to Int, SB-27 `grabpairs` on an empty SetHash gives `(Nil) => True`, SB-33 a Hash right of a Mix difference is a Set, SB-34 n-ary `[(^)]` is not the binary fold, SB-38 `{a => 0} (==) {}` is False |
| rakupp 4.0.1-84 differs | 44 |
| rakupp 4.0.1-84 matches | 0 — four items differ in one or two fields only (SB-05 the flattening depth and `Mu.Set`, SB-13 `.kxxv`, SB-28 the NaN exception type, SB-44 `.sum` and `[0]`), and SB-07, SB-30, SB-32 and parts of SB-27 and SB-38 differ because rakupp already has the sane answer |

Recurring rakupp gaps, for step two: keys are conflated by string value
(`1` and `"1"`, two equal Lists, a mixin, `set(1) === set("1")` — every
identity item in A, B and C), there is no shared empty sentinel and
`=:=` identity is lost everywhere; the hash forms return the raw assigned
value instead of the coerced one and skip the `Int()`/`Real()` coercions,
refusals and removals (SB-20 to SB-24), and `.add`/`.set` on the wrong type
answer an IO path; parameterized `Set[T]` is a name only; the reduction
forms `[(op)]` with zero or one argument answer `(Any)` or the argument's own
type; the Boolean set operators neither chain nor autothread consistently
and `!(<=)`, `!(==)`, `∊`, `∍`, `(<+)` do not parse; exception types are
generic (`X::AdHoc` for NaN counts, `X::Numeric::CannotConvert` for every
refused weight, `X::Method::NotFound` for the type-object refusals) and the
exception objects lack `.what`, `.expected`, `.method`, `.typename`; a Mix's
negative weights are dropped by `.Set`, by union and by the subset tests; the
type objects `SetHash.Baggy`/`Bag.Mixy` map to the wrong mutability; `*..*`
is a finite range of native-Int limits.

## Method (how this sheet was produced)

As for [Supply.md](Supply.md) and [Range.md](Range.md): sources dumped from
the tag with `git show`, one probe line per item run through both engines
with `alarm 10` in a fresh sandbox per engine, the outputs joined with `|`,
the whole set re-run against Rakudo from the finished sheet and compared
byte for byte (51 probes, 51 match). Traps met, for later sheets: a
subscript on a SetHash/BagHash/MixHash returns a live Proxy, so a bare
`$b<a>` inside a list literal is read only when the list is joined and
shows the FINAL state — decontainerize (`.raku`, `.Int`) at the point of
reading; `try EXPR` does not set `$!` for a Failure that EXPR returns, so
Failure-returning calls need a helper that inspects the value (the `T`
helper here reports `F:` for a returned Failure and `T:` for a throw); a
`set(...)` argument list flattens one level, so identity tests on Arrays and
Ranges need `Set.new(...)`; `set(:a)` and `Set.new(:a)` pass a NAMED argument;
`<"">` is a two-character string, not `""`; a Str's `.raku` does not escape a
tab, which corrupts a tab-separated results file; `.Str` and `.raku` of a
multi-element collection are in storage order and must be `.sort`ed or
reduced to `.chars`/`.elems` before recording; `(EXPR) ~~ Bool` gave two
different answers for the same constant expression in two statements
(dropped from the probes); a Junction operand of `(|)` spins forever, so any
probe that might hang wraps the call in `start` and waits on `Promise.in`.
D flags from `doc/Type/{Set,SetHash,Setty,Bag,BagHash,Baggy,Mix,MixHash,Mixy,
QuantHash}.rakudoc`, `doc/Language/setbagmix.rakudoc` and
`doc/Language/operators.rakudoc`; R flags from the 22 Roast files named in
the second paragraph.
