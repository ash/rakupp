# Nil, and Any as a one-element list — semantics sheet

Provenance: Rakudo tag `2026.08`, files `src/core.c/Nil.rakumod` (82 lines),
`Any.rakumod` (692) and `Any-iterable-methods.rakumod` (1,715), read in full
on 2026-09-17. `Mu.rakumod` is out of scope here. Oracle: Homebrew Rakudo
v2026.08. Compared against Raku++ 4.0.1-6-gd811876c (build-arm64,
2026-09-17). Format and legend: [README.md](README.md).

Two rules organise everything below. **Nil** answers every unknown method
and every subscript with Nil, but it is also a `Cool` and an `Any`, so the
list methods it inherits are real and their results are lists containing
Nil. **Any** treats a non-Iterable value, and a type object, as a
one-element list, so the whole list API applies to `42` and to `Int`.
docs.raku.org states the principle ("Raku intentionally confuses items and
single-element lists"); the edge rules inside the methods are what this
sheet records. 16 of the 50 items are neither fully documented
nor fully asserted by Roast.

Where the test corpus stands: `S02-types/nil.t` asserts the Nil basics and
that the mutators throw; the `S32-list/*.t` files assert most list methods
on `42` (grep, map, first, reduce, produce, unique, repeated, squish, toggle,
tail, skip, sort, min, max, classify, are, pick, roll). Both engines pass
nil.t's structure; the divergences below are inside the rules.

## Part 1 — Nil

### NA-01  Unknown methods and subscripts on Nil yield Nil          D:yes R:yes V:spec
Any method Nil does not have, with any arguments, returns Nil; so do
positional and associative subscripts, and chains of both.
```
say Nil.foo.raku, " ", Nil.foo(1, "a", :x).raku, " ", Nil.foo.bar.baz.raku
# rakudo 2026.08: Nil Nil Nil
say (Nil)[100].raku, " ", (Nil){100}.raku, " ", (Nil)<a><b>[3].raku, " ", Nil.foo<x>[1].raku, " ", Nil.foo[0]<y>.raku
# rakudo 2026.08: Nil Nil Nil Nil Nil
```
rakupp 4.0.2: matches.

### NA-02  Nil is its own instance, a Cool, and false                D:yes R:yes V:spec
`Nil.new` (with any arguments) is Nil itself. Nil is not defined, is false,
and its type is Nil; its ancestry is Cool, Any, Mu.
```
say (Nil.new === Nil), " ", (Nil.new(1,2) === Nil), " ", Nil.WHAT.raku, " ", Nil.DEFINITE, " ", Nil.defined, " ", Nil.so, " ", Nil.Bool
# rakudo 2026.08: True True Nil False False False False
say (Nil ~~ Str), " ", (Nil ~~ Cool), " ", Nil.isa(Cool), " ", Nil.^mro.map(*.^name).join(","), " ", ?Nil, " ", !Nil
# rakudo 2026.08: False True True Nil,Cool,Any,Mu False True
```
rakupp 4.0.2: matches.

### NA-03  Nil iterates as one element                              D:yes R:yes V:spec
`for Nil` runs once; Nil takes one slot in a list; its iterator yields Nil
then ends; `.list` is `(Nil,)`, `.elems` 1, `.end` 0.
```
say do { my $n = 0; $n++ for Nil; $n }, " ", (1, Nil, 3).elems, " ", (for Nil { $_ }).raku, " ", Nil.iterator.pull-one.raku, " ", Nil.list.raku, " ", Nil.elems, " ", Nil.end
# rakudo 2026.08: 1 3 (Nil,) Nil (Nil,) 1 0
say do { my $i = Nil.iterator; $i.pull-one.raku ~ " " ~ $i.pull-one.raku }
# rakudo 2026.08: Nil IterationEnd
```
rakupp 4.0.2: matches.

### NA-04  The inherited list methods work on Nil and return lists    D:no R:no V:spec
Because Nil is an Any, `map`, `grep`, `sort`, `reverse`, `unique`, `keys`,
`values`, `pairs`, `head`, `tail`, `first`, `pick`, `roll`, `skip`,
`combinations`, `permutations`, `flatmap`, `squish`, `collate`, `pairup`,
`nodemap`, `deepmap`, `List`, `Array`, `Slip`, `Seq`, `hash`, `Hash`,
`join` all apply to the one-element list `(Nil,)`. Type-object candidates
give `Nil` for `reduce`/`produce` and `()` for `keys`/`values`/`pairs`/
`antipairs`/`invert`; `tree` and `are` return Nil itself. Where a Nil
lands in an Array element it becomes Any (`Nil.Array` is `[Any]`).
```
say Nil.map({ $_.raku }).raku, " ", Nil.grep(*.defined).raku, " ", Nil.grep(!*.defined).elems, " ", Nil.head.raku, " ", Nil.tail.raku, " ", Nil.keys.raku, " ", Nil.values.raku, " ", Nil.pairs.raku, " ", Nil.sort.raku, " ", Nil.reverse.raku, " ", Nil.unique.raku, " ", Nil.repeated.raku
# rakudo 2026.08: ("Nil",).Seq ().Seq 1 Nil Nil () () () (Nil,).Seq (Nil,).Seq (Nil,).Seq ().Seq
say Nil.reduce(&[+]).raku, " ", Nil.produce(&[+]).raku, " ", Nil.antipairs.raku, " ", Nil.invert.raku, " ", Nil.tree.raku, " ", Nil.combinations.raku, " ", Nil.permutations.raku, " ", Nil.flatmap({ $_ }).raku, " ", Nil.squish.raku, " ", Nil.collate.raku, " ", Nil.pairup.raku
# rakudo 2026.08: Nil Nil () () Nil ((), (Nil,)).Seq ((Nil,),).Seq (Nil,).Seq (Nil,).Seq (Nil,).Seq ().Seq
say Nil.pick.raku, " ", Nil.roll(2).raku, " ", Nil.head(2).raku, " ", Nil.tail(2).raku, " ", Nil.skip.raku, " ", Nil.first.raku, " ", Nil.first(*.defined).raku, " ", Nil.sort(&[cmp]).raku, " ", Nil.nodemap({ $_ }).raku, " ", Nil.deepmap({ $_ }).raku
# rakudo 2026.08: Nil (Nil, Nil).Seq (Nil,).Seq (Nil,).Seq ().Seq Nil Nil (Nil,).Seq (Nil,) (Nil,)
say Nil.Array.raku, " ", Nil.List.raku, " ", Nil.Slip.raku, " ", Nil.Seq.raku, " ", Nil.hash.raku, " ", Nil.Hash.raku
# rakudo 2026.08: [Any] (Nil,) slip(Nil,) (Nil,).Seq {} {}
```
rakupp 4.0.2: matches.

### NA-05  Methods with only defined-invocant candidates fail on Nil    D:no R:no V:spec
`batch`, `rotor`, `toggle`, `slice`, `splice`, `min`, `minmax`, `sum` have no
candidate for a type object, so on Nil they throw `X::Multi::NoMatch`.
`Nil.Map` is an odd-element Map: `X::Hash::Store::OddNumber`.
```
say (try Nil.Map.raku) // $!.^name, " ", (try Nil.batch(1).raku) // $!.^name, " ", (try Nil.rotor(1).raku) // $!.^name, " ", (try Nil.toggle(* > 1).raku) // $!.^name, " ", (try Nil.slice(0).raku) // $!.^name, " ", (try Nil.splice.raku) // $!.^name, " ", (try Nil.are.raku) // $!.^name
# rakudo 2026.08: X::Hash::Store::OddNumber X::Multi::NoMatch X::Multi::NoMatch X::Multi::NoMatch X::Multi::NoMatch X::Multi::NoMatch Nil
say (try Nil.minmax.raku) // $!.^name, " ", (try Nil.min.raku) // $!.^name, " ", (try Nil.sum.raku) // $!.^name
# rakudo 2026.08: X::Multi::NoMatch X::Multi::NoMatch X::Multi::NoMatch
```
rakupp 4.0.2: matches.

### NA-06  Mutating Nil                                              D:partial R:partial V:spec
`STORE` throws `X::Assignment::RO`. `push`, `append`, `unshift`, `prepend`,
`ASSIGN-POS`, `ASSIGN-KEY` throw `X::AdHoc` ("Use of Nil.push not allowed").
`BIND-POS`/`BIND-KEY` return a Failure wrapping `X::Bind` with target `Nil`.
Roast asserts that all of them throw, not which type.
```
say (try { Nil.STORE(1); "no" }) // $!.^name, " ", (try { Nil.STORE; "no" }) // $!.^name
# rakudo 2026.08: X::Assignment::RO X::Assignment::RO
say (try { Nil.push(1); "no" }) // $!.^name ~ ":" ~ $!.message, " | ", (try { Nil.ASSIGN-POS(0, 1); "no" }) // $!.^name, " ", (try { Nil.unshift(1); "no" }) // $!.^name, " ", (try { Nil.append(1); "no" }) // $!.^name, " ", (try { Nil.prepend(1); "no" }) // $!.^name, " ", (try { Nil.ASSIGN-KEY("a", 1); "no" }) // $!.^name
# rakudo 2026.08: X::AdHoc:Use of Nil.push not allowed | X::Assignment::RO X::AdHoc X::AdHoc X::AdHoc X::Assignment::RO
say do { my $f = Nil.BIND-POS(0, 1); $f.^name ~ ":" ~ $f.exception.^name ~ ":" ~ $f.exception.target }, " ", do { my $f = Nil.BIND-KEY("a", 1); $f.^name }
# rakudo 2026.08: Failure:X::Bind:Nil Failure
```
rakupp 4.0.2: matches.

### NA-07  String methods on Nil return an empty Str and warn        D:no R:no V:spec
`chars chomp chop codes comb contains ends-with flip indent index indices lc
lines tc tclc rindex starts-with trans substr subst substr-eq substr-rw
wordcase words uc` each return `''` — a Str, even for `chars` and
`contains` — and warn "Use of Nil.<method> coerced to empty string".
```
say do { my @w; CONTROL { when CX::Warn { @w.push(.message); .resume } }; my $r = Nil.chars; $r.raku ~ " " ~ Nil.lc.raku ~ " " ~ Nil.substr(1).raku ~ " " ~ Nil.contains("a").raku ~ " " ~ Nil.index("a").raku ~ " " ~ Nil.words.raku ~ " " ~ Nil.comb.raku ~ " " ~ Nil.flip.raku ~ " | " ~ @w.elems ~ " " ~ @w[0] }
# rakudo 2026.08: "" "" "" "" "" "" "" "" | 8 Use of Nil.chars coerced to empty string
```
rakupp 4.0.2: matches — including the per-method warning.

### NA-08  Numeric and string coercion of Nil                        D:yes R:partial V:spec
`.Int` and `.Numeric` give 0, arithmetic treats Nil as 0, each with the
warning "Use of Nil in numeric context". `.Str` gives `""` with "Use of Nil
in string context"; interpolation and `~` warn the same way. `.gist` and
`.raku` give `"Nil"` silently. `Nil == 0` and `Nil eq ""` are True and warn.
`.ords` is an empty Seq and `.chrs` is `"\0"`, both warning (Roast asserts these two).
```
say do { my @w; CONTROL { when CX::Warn { @w.push(.message); .resume } }; Nil.Int.raku ~ " " ~ Nil.Numeric.raku ~ " " ~ (Nil + 1).raku ~ " " ~ (Nil * 2).raku ~ " " ~ (+Nil).raku ~ " | " ~ @w.unique.join("/") }
# rakudo 2026.08: 0 0 1 0 0 | Use of Nil in numeric context
say do { my @w; CONTROL { when CX::Warn { @w.push(.message); .resume } }; Nil.Str.raku ~ " " ~ (~Nil).raku ~ " " ~ "<{Nil}>" ~ " " ~ (Nil ~ "x").raku ~ " " ~ Nil.gist.raku ~ " " ~ Nil.raku.raku ~ " | " ~ @w.unique.join("/") ~ " | " ~ @w.elems }
# rakudo 2026.08: "" "" <> "x" "Nil" "Nil" | Use of Nil in string context | 4
say do { my @w; CONTROL { when CX::Warn { @w.push(.message); .resume } }; Nil.ords.raku ~ " " ~ Nil.chrs.raku ~ " | " ~ @w.join("/") }
# rakudo 2026.08: ().Seq "\0" | Use of Nil in string context/Use of Nil in numeric context
```
rakupp 4.0.2: differs only in the NUMERIC-context warning. The values match (`.Int` and `.Numeric` are the Int 0, `.chrs` is `"\0"`) and the string-context warning is issued wherever Nil reaches string context; arithmetic on Nil is still silent.

### NA-09  QuantHash coercion keeps Nil as an element                D:no R:no V:spec
```
say Nil.Set.raku, " ", Nil.Set.elems, " ", Nil.Bag.raku, " ", Nil.Mix.elems, " ", Nil.SetHash.elems, " ", Nil.Set.keys.raku
# rakudo 2026.08: Set.new(Nil) 1 (Nil=>1).Bag 1 1 (Nil,).Seq
```
rakupp 4.0.2: matches.

### NA-10  Nil.ACCEPTS returns a Bool; a Failure smartmatches Nil    D:no R:partial V:quirk
Rakudo's `Nil.ACCEPTS` answers True/False by type. Roast (`nil.t`) says it
should return Nil and marks that test todo, so the spec is unsettled here.
`Failure` is a subclass of Nil, so `Failure.new ~~ Nil` is True.
```
say Nil.sink.raku, " ", (Nil // 5), " ", (Nil ~~ Nil), " ", (Failure.new("x") ~~ Nil), " ", (Any ~~ Nil), " ", Nil.ACCEPTS(Any).raku, " ", Nil.ACCEPTS(Nil).raku, " ", (Nil ~~ Any)
# rakudo 2026.08: Nil 5 True True False Bool::False Bool::True True
```
rakupp 4.0.2: `Failure.new ~~ Nil` is True. `Nil.ACCEPTS(…)` stays Nil ON PURPOSE: `S02-types/nil.t` asserts `Nil.ACCEPTS(Any) === Nil` and passes here, and this sheet records the spec as unsettled — Roast is the gate.

### NA-11  Assigning Nil restores the container's default            D:yes R:partial V:spec
Untyped scalar → Any; typed → the type object; `is default(x)` → x; `Int:D`
→ `X::TypeCheck::Assignment`; native → `X::AdHoc`. An Array element takes
the default too (`[Nil]` is `[Any]`, `@a = Nil` gives `[Any]`). `%h = Nil`
is an odd-element hash store: `X::Hash::Store::OddNumber`; `Empty` empties.
Binding (`:=`) keeps Nil.
```
say do { my $x = 5; $x = Nil; $x.raku }, " ", do { my Int $i = 5; $i = Nil; $i.raku }, " ", do { my $d is default(7) = 1; $d = Nil; $d.raku }, " ", do { my Str $s = "a"; $s = Nil; $s.raku }, " ", (try { my Int:D $j = 5; $j = Nil; "no" }) // $!.^name
# rakudo 2026.08: Any Int 7 Str X::TypeCheck::Assignment
say (try { my int $n = 5; $n = Nil; $n.raku }) // $!.^name ~ ":" ~ $!.message, " | ", (try { my %h = a => 1; %h = Nil; %h.raku }) // $!.^name, " | ", do { my Int $i is default(3) = 5; $i = Nil; $i.raku }
# rakudo 2026.08: X::AdHoc:Cannot unbox a type object (Nil) to int. | X::Hash::Store::OddNumber | 3
say do { my @a = Nil; @a.raku }, " ", do { my @a = 1, 2; @a = Nil; @a.raku }, " ", do { my @a = 1, 2; @a = Empty; @a.raku }, " ", [Nil].raku, " ", (Nil,).raku, " ", do { my $x := Nil; $x.raku }, " ", do { my @a; @a.push(Nil); @a.raku }, " ", do { my %h; %h<a> = Nil; %h.raku }
# rakudo 2026.08: [Any] [Any] [] [Any] (Nil,) Nil [Any] {:a(Any)}
```
rakupp 4.0.2: differs — `[Nil]` is `[Any]` and every container default (untyped, typed, `is default`, an Array element, a Hash value) resets correctly; `Int:D` still accepts Nil, a native still takes Any, and `%h = Nil` is still `{}` rather than an odd-element store.

### NA-12  Nil as an argument                                        D:partial R:partial V:spec
An untyped parameter receives Nil itself, and a parameter with a default
still receives Nil (the default applies only to a missing argument). A
typed parameter — `Int $x`, `Int $x?`, `Int:D $x` — rejects Nil:
`X::TypeCheck::Binding::Parameter` at run time, and when the literal `Nil`
is visible at the call site the compiler rejects the call ("will never
work"). Roast marks the "optional gets its type object" and "default is
used" readings as todo, so Rakudo's behaviour is the one in force.
```
say do { sub f2(Int $x?) { $x.raku }; sub g { Nil }; (try f2(g())) // $!.^name }, " ", do { sub f5(Str $x) { $x.raku }; sub g { Nil }; (try f5(g())) // $!.^name }, " ", do { sub f6(Int:D $x) { $x.raku }; sub g { Nil }; (try f6(g())) // $!.^name }, " ", do { sub f7($x) { $x.raku }; sub g { Nil }; f7(g()) }, " ", do { sub f8($x = 5) { $x.raku }; sub g { Nil }; f8(g()) }
# rakudo 2026.08: X::TypeCheck::Binding::Parameter X::TypeCheck::Binding::Parameter X::TypeCheck::Binding::Parameter Nil Nil
say do { sub f4($x = Nil) { $x.raku }; f4() ~ " " ~ f4(Nil) }
# rakudo 2026.08: Nil Nil
```
rakupp 4.0.2: matches.

### NA-13  Where Nil comes from                                      D:yes R:yes V:spec
Empty routine or block, bare `return`, `return Nil`, `if 1 { }`, `EVAL ""`,
an out-of-range array read. Return-type constraints, including `:D`, do
not apply to Nil. A false `if` with no else yields `Empty`, not Nil.
```
say do { sub a(--> Int:D) { return Nil }; a().raku }, " ", do { sub b(--> Str) { Nil }; b().raku }, " ", do { sub c { }; c().raku }, " ", do { sub d { return }; d().raku }, " ", (if 1 { }).raku, " ", ({ ; }()).raku, " ", (EVAL "").raku, " ", (my @e; @e[5]).raku, " ", (if 0 { 1 }).raku
# rakudo 2026.08: Nil Nil Nil Nil Nil Nil Nil Any Empty
```
Note `@e[5]` is Any (the element default), not Nil.
rakupp 4.0.2: matches — the `(my @e; @e[5])` field reads `([], Any)` on both engines; the `Any` above is from a differently-parenthesised probe.

### NA-14  Nil as a classification key                               D:no R:no V:spec
`Nil.classify({ $_ })` yields an object hash with the key Nil and the value
`[Any]` (the stored Nil became the element default).
```
say Nil.classify({ $_ }).values.raku, " ", Nil.classify({ $_ }).keys[0].raku, " ", Nil.categorize({ ($_,) }).keys[0].raku
# rakudo 2026.08: ($[Any],).Seq Nil Nil
```
rakupp 4.0.2: differs only in the `.raku` of the bucket — `([Any],)` where Rakudo itemizes it as `($[Any],)`. The key is Nil and the stored Nil became Any, as it must.

### NA-15  Existence, deletion and junctions on Nil                  D:no R:no V:spec
`EXISTS-POS`/`EXISTS-KEY` and the `:exists` adverb answer False (a Bool, from
Any); `:delete` and `DELETE-POS` give Nil; `AT-POS` with several indices is
Nil; `.all`/`.any` are junctions of Nil.
```
say Nil.EXISTS-POS(0).raku, " ", ((Nil)[0]:exists).raku, " ", ((Nil)<a>:exists).raku, " ", ((Nil)[0]:delete).raku, " ", Nil.DELETE-POS(0).raku, " ", Nil.AT-POS(0, 1, 2).raku, " ", Nil.all.raku, " ", Nil.any.raku, " ", Nil.EXISTS-KEY("a").raku
# rakudo 2026.08: Bool::False Bool::False Bool::False Nil Nil Nil all(Nil) any(Nil) Bool::False
```
rakupp 4.0.2: matches.

## Part 2 — Any as a one-element list

### NA-16  .list of a non-Iterable is (self,)                        D:yes R:yes V:spec
For values and type objects alike; `List`, `Array`, `Slip`, `Seq`, `flat`,
`eager`, `cache` derive from it; `.serial` is the invocant.
```
say 42.list.raku, " ", Any.list.raku, " ", Str.list.raku, " ", "abc".list.raku, " ", 42.List.raku, " ", 42.Array.raku, " ", 42.Slip.raku, " ", 42.Seq.raku, " ", 42.flat.raku, " ", 42.eager.raku, " ", 42.cache.raku, " ", 42.serial.raku, " ", (1,2).serial.raku
# rakudo 2026.08: (42,) (Any,) (Str,) ("abc",) (42,) [42] slip(42,) (42,).Seq (42,).Seq (42,) (42,) 42 (1, 2)
```
rakupp 4.0.2: matches.

### NA-17  elems 1, end 0, one-shot iterator                         D:yes R:partial V:spec
```
say 42.elems, " ", Any.elems, " ", Int.elems, " ", "abc".elems, " ", (a => 1).elems, " ", 42.end, " ", Any.end, " ", 42.iterator.pull-one.raku, " ", Any.iterator.pull-one.raku
# rakudo 2026.08: 1 1 1 1 1 0 0 42 Any
```
rakupp 4.0.2: matches.

### NA-18  keys, values, kv, pairs, antipairs, invert                D:partial R:partial V:spec
A type object gives `()` for all six. A value gives them over `(value,)`:
keys `(0,)`, values `(42,)`, kv `(0, 42)`, pairs `(0 => 42,)`, antipairs
`(42 => 0,)`; `invert` needs Pairs and fails with `X::TypeCheck` at
reification. `Bool` and every `Enumeration` override the six with their
enum table: keys are the names, values the numbers.
```
say 42.keys.raku, " ", 42.values.raku, " ", 42.kv.raku, " ", 42.pairs.raku, " ", 42.antipairs.raku, " ", Any.keys.raku, " ", Any.values.raku, " ", Any.kv.raku, " ", Any.pairs.raku, " ", Any.antipairs.raku, " ", Any.invert.raku, " ", (try 42.invert.eager) // $!.^name, " ", (a => 1).invert.raku
# rakudo 2026.08: (0,).Seq (42,) (0, 42).Seq (0 => 42,).Seq (42 => 0,).Seq () () () () () () X::TypeCheck (1 => "a",).Seq
say Bool.keys.raku, " ", True.keys.raku, " ", True.values.raku, " ", Bool.pairs.raku, " ", True.kv.raku, " ", True.antipairs.raku, " ", True.elems, " ", True.list.raku, " ", True.invert.raku
# rakudo 2026.08: ("False", "True").Seq ("True", "False").Seq (1, 0).Seq (:False(0), :True(1)).Seq ("True", 1, "False", 0).Seq (0 => "False", 1 => "True").Seq 1 (Bool::True,) (1 => "True", 0 => "False").Seq
```
The enum order from an instance (`True.keys`) differs from the type's (`Bool.keys`); both are hash order.
rakupp 4.0.2: differs — the six answer the right shapes for a value and for a type object (`.values` is a List, the rest Seqs), but `42.invert` is `X::Method::NotFound` where Rakudo fails the type check at reification, and the ENUM table is not implemented: `Bool.keys` is `()`, not the two names.

### NA-19  hash, Hash, Map                                           D:no R:no V:spec
A type object: `.hash` and `.Hash` are `{}`, `.Map` is an odd-element store
(`X::Hash::Store::OddNumber`). A value is stored as a hash initializer, so a
Pair works and anything else is odd: `42.hash`, `"abc".hash`, `42.Map` all
throw `X::Hash::Store::OddNumber`.
```
say (try "abc".hash) // $!.^name, " ", (try 42.hash) // $!.^name, " ", (a => 1).hash.raku, " ", (try 42.Map) // $!.^name, " ", (a => 1).Map.raku, " ", Any.Hash.raku, " ", (try Any.Map) // $!.^name, " ", Any.hash.raku
# rakudo 2026.08: X::Hash::Store::OddNumber X::Hash::Store::OddNumber {:a(1)} X::Hash::Store::OddNumber Map.new((:a(1))) {} X::Hash::Store::OddNumber {}
```
rakupp 4.0.2: matches.

### NA-20  map: arity, :item, named forms, non-callables             D:partial R:partial V:spec
A block of arity N consumes N elements per call; when the count does not
divide, the last call dies (`X::AdHoc`, "Too few positionals") unless the
extra parameters are optional. `:item` maps the invocant as one item.
`map(flat => &f)`, `map(node => &f)`, `map(deep => &f)`, `map(duck => &f)` —
named, with no positional — delegate to flatmap, nodemap, deepmap, duckmap;
a positional callable plus an unknown named runs the plain map. A
non-callable argument throws `X::Cannot::Map` with `.what` (invocant type),
`.using` ("a Hash", "a List", or the literal) and `.suggestion`.
```
say 42.map({ $_ * 2 }).raku, " ", Any.map({ $_ }).raku, " ", Any.map({ Slip }).raku, " ", "ab".map(*.uc).raku
# rakudo 2026.08: (84,).Seq (Any,).Seq (Slip,).Seq ("AB",).Seq
say (try (1,2,3).map(-> $a, $b { $a + $b }).eager) // $!.^name ~ ":" ~ $!.message, " | ", (1,2,3).map(-> $a, $b? { $a }).raku, " ", (1,2,3,4).map(-> $a, $b { $a + $b }).raku, " ", (try 42.map(-> $a, $b { $a }).eager) // $!.^name
# rakudo 2026.08: X::AdHoc:Too few positionals passed; expected 2 arguments but got 1 | (1, 3).Seq (3, 7).Seq X::AdHoc
say (1,2).map(:item, *.elems).raku, " ", 42.map(:item, *.elems).raku, " ", (1,(2,3)).map(flat => { $_ }).raku, " ", (1,(2,3)).map(node => *.elems).raku, " ", (1,(2,3)).map(deep => * * 10).raku, " ", (1,"a").map(duck => -> Int $x { $x * 10 }).raku
# rakudo 2026.08: (2,).Seq (1,).Seq (1, 2, 3).Seq (1, 2) (10, $(20, 30)) (10, "a")
say (try 42.map(1)) // $!.^name ~ ":" ~ $!.what ~ ":" ~ $!.using, " | ", (try (1,2).map((3,4))) // $!.^name ~ ":" ~ $!.using, " | ", (try (1,2).map({a => 1})) // $!.^name ~ ":" ~ $!.using
# rakudo 2026.08: X::Cannot::Map:Int:'1' | X::Cannot::Map:a List | X::Cannot::Map:a Hash
```
rakupp 4.0.2: matches.

### NA-21  grep: how the test is judged, and the adverbs             D:yes R:partial V:spec
A Regex matches against `.Str`; a Callable's result is judged — a Regex
result is smartmatched, a Junction collapsed, anything else by truth; any
other object uses `ACCEPTS`. A Bool test throws `X::Match::Bool`. Adverbs
`:k`, `:kv`, `:p`, `:v` select the output shape; a negated one (`:!k`)
means plain values; two of them at once throw `X::Adverb` with `.nogo`;
an unknown adverb throws `X::Adverb` with `.unexpected`. `next` and `last`
inside the test block work as in a loop.
```
say 42.grep({ 1 }).raku, " ", 42.grep(Str).raku, " ", 42.grep(/4/).raku, " ", 42.grep(42).raku, " ", 42.grep(*.is-prime).raku, " ", (1,2,3).grep({ /2/ }).raku, " ", (1,2,3).grep({ $_ == 1|3 }).raku, " ", <a b>.grep(/b/).raku, " ", (try 42.grep(True)) // $!.^name
# rakudo 2026.08: (42,).Seq ().Seq (42,).Seq (42,).Seq ().Seq (2,).Seq (1, 3).Seq ("b",).Seq X::Match::Bool
say 42.grep({ 1 }, :k).raku, " ", 42.grep({ 1 }, :kv).raku, " ", 42.grep({ 1 }, :p).raku, " ", 42.grep({ 1 }, :v).raku, " ", (1,2,3).grep(* > 1, :!k).raku, " ", (try (1,2).grep(* > 1, :k, :v)) // $!.^name ~ ":" ~ $!.nogo.raku, " ", (try (1,2).grep(* > 1, :zap)) // $!.^name ~ ":" ~ $!.unexpected.raku
# rakudo 2026.08: (0,).Seq (0, 42).Seq (0 => 42,).Seq (42,).Seq (2, 3).Seq X::Adverb:("k", "v").Seq X::Adverb:("zap",).Seq
say (1..5).grep({ next if $_ == 2; last if $_ == 4; True }).raku, " ", Any.grep(*.defined).raku, " ", Any.grep(Any).raku, " ", Int.grep(Int).raku
# rakudo 2026.08: (1, 3).Seq ().Seq (Any,).Seq (Int,).Seq
```
`:!v` is reported as an unexpected adverb 'v' (the intended message names a negated :v); harmless.
rakupp 4.0.2: matches.

### NA-22  first: same tests, :end, adverbs, Nil when absent         D:yes R:yes V:spec
No test means the first element (`:end` the last), even a falsy one. A
Bool test returns a Failure (`X::Match::Bool`), two adverbs a Failure
(`X::Adverb`). Not found is Nil. `:end` on a lazy list throws `X::Cannot::Lazy`.
```
say 42.first.raku, " ", 42.first(* > 1).raku, " ", 42.first(* > 100).raku, " ", 42.first(* > 1, :k).raku, " ", 42.first(* > 1, :p).raku, " ", 42.first(* > 1, :kv).raku, " ", 42.first(* > 1, :v).raku, " ", (1,2,3).first(* > 1, :end).raku, " ", (1,2,3).first(* > 1, :end, :k).raku, " ", (1,2,3).first(:end).raku, " ", ().first.raku
# rakudo 2026.08: 42 42 Nil 0 0 => 42 (0, 42) 42 3 2 3 Nil
say do { my $f = 42.first(True); $f.^name ~ ":" ~ $f.exception.^name }, " ", (1,2,3).first(/2/).raku, " ", (1,2,3).first(/2/, :end, :k).raku, " ", (1,2,3).first(Int, :end).raku, " ", (1..*).first(* > 3).raku, " ", (try (1..*).first(* > 3, :end)) // $!.^name, " ", do { my $f = (1,2).first(* > 1, :k, :v); $f.^name ~ ":" ~ $f.exception.^name }, " ", (1,2).first(* > 1, :!k).raku, " ", ().first(:end).raku, " ", (1,2,3).first(:k).raku
# rakudo 2026.08: Failure:X::Match::Bool 2 1 3 4 X::Cannot::Lazy Failure:X::Adverb 2 Nil 0
say (0,1,2).first(:k).raku, " ", (0, "", 2).first.raku, " ", (0, "", 2).first(:v).raku, " ", Any.first.raku, " ", Any.first(*.defined).raku
# rakudo 2026.08: 0 0 0 Any Nil
```
rakupp 4.0.2: differs only in `:end` on a LAZY list, which answers instead of throwing `X::Cannot::Lazy`. The Bool matcher and the two-adverb misuse are Failures, and `:kv` is a List.

### NA-23  sum                                                       D:yes R:partial V:spec
Defined invocant: the sum of its list, starting from 0. Type object:
`X::Multi::NoMatch`. Lazy: a Failure `X::Cannot::Lazy`. A non-numeric
element: `X::Str::Numeric`. Sub form: `sum()` is 0.
```
say 42.sum, " ", ().sum, " ", (1,2).sum, " ", sum(), " ", sum(42), " ", sum(1,2,3)
# rakudo 2026.08: 42 0 3 0 42 6
say (1..*).map({ $_ }).sum.^name, " ", (1..*).map({ $_ }).sum.exception.^name, " ", (try ("a", 1).sum) // $!.^name, " ", (try Int.sum) // $!.^name, " ", (try Any.sum) // $!.^name
# rakudo 2026.08: Failure X::Cannot::Lazy X::Str::Numeric X::Multi::NoMatch X::Multi::NoMatch
```
rakupp 4.0.2: matches.

### NA-24  sort and collate                                          D:yes R:partial V:spec/quirk
A type object sorts as `(self,)`. A one-argument `&by` is a key extractor,
two arguments a comparator. A lazy list with `&by` throws
`X::Cannot::Lazy`; **without** `&by`, a lazy list whose iterator is already
monotonically increasing (a Range) is returned as-is and stays lazy.
`collate` is `sort(&[coll])` and returns a Seq. The sub form `sort(&by, 3, 1, 2)`
is ambiguous between `(&by, +values)` and `(+values)` and throws
`X::Multi::Ambiguous` (quirk); pass the values as one list. `sort()` dies
"Must specify something to sort".
```
say 42.sort.raku, " ", Any.sort.raku, " ", Int.sort.raku, " ", (3,1,2).sort(-*).raku, " ", (3,1,2).sort(-> $a, $b { $b <=> $a }).raku, " ", <b A a>.collate.raku, " ", (try (1..*).sort) // $!.^name, " ", (try (1..*).sort(&[<=>])) // $!.^name
# rakudo 2026.08: (42,).Seq (Any,).Seq (Int,).Seq (3, 2, 1).Seq (3, 2, 1).Seq ("a", "A", "b").Seq (...) X::Cannot::Lazy
say sort(3, 1, 2).raku, " ", (try sort(-*, 3, 1, 2)) // $!.^name, " ", sort(-*, (3, 1, 2)).raku, " ", (try sort({ $^b <=> $^a }, 3, 1, 2)) // $!.^name, " ", sort({ $^b <=> $^a }, (3, 1, 2)).raku, " ", sort(&[<=>], (3, 1, 2)).raku, " ", (try sort()) // $!.message, " ", sort(42).raku
# rakudo 2026.08: (1, 2, 3).Seq X::Multi::Ambiguous (3, 2, 1).Seq X::Multi::Ambiguous (3, 2, 1).Seq (1, 2, 3).Seq Must specify something to sort (42,).Seq
```
rakupp 4.0.2: differs — `sort()` says "Must specify something to sort" and `collate` is a Seq; `(1..*).sort` still throws `X::Cannot::Lazy` instead of staying lazy, and the ambiguous sub call still sorts rather than raising `X::Multi::Ambiguous` (the quirk this sheet records).

### NA-25  reduce and produce                                        D:partial R:yes V:spec
A type object gives Nil. A one-element list **calls the reducer with that
single element**: an operator returns it, a two-parameter block without a
default dies (`X::AdHoc`, "Too few positionals"); Roast's own reducer is
`-> $a, $b = 0`. An empty list gives the operator's identity (`[+]` 0,
`[**]` 1, `[~]` "") and dies for a plain block. `produce` on empty is `()`.
```
say 42.reduce(&[+]).raku, " ", Any.reduce(&[+]).raku, " ", Int.reduce(&[+]).raku, " ", 42.produce(&[+]).raku, " ", Any.produce(&[+]).raku, " ", (1,2,3).reduce(&[-]).raku, " ", (1,2,3).produce(&[-]).raku
# rakudo 2026.08: 42 Nil Nil (42,).Seq Nil -4 (1, -1, -4).Seq
say (try (1,).reduce({ $^a + $^b })) // $!.^name, " ", (1,).reduce(&[+]).raku, " ", (try (5,).reduce(-> $a, $b { $a + $b })) // $!.^name, " ", ([+] (1,)), " ", (1,2).reduce({ $^a + $^b }), " ", (try ().reduce({ $^a + $^b })) // $!.^name
# rakudo 2026.08: X::AdHoc 1 X::AdHoc 1 3 X::AdHoc
say ().reduce(&[+]).raku, " ", ().reduce(&[**]).raku, " ", ().reduce(&[~]).raku, " ", ().produce(&[+]).raku
# rakudo 2026.08: 0 1 "" ().Seq
```
rakupp 4.0.2: matches.

### NA-26  unique, repeated, squish on scalars                       D:yes R:yes V:spec
```
say 42.unique.raku, " ", 42.repeated.raku, " ", 42.squish.raku, " ", Any.unique.raku, " ", Any.squish.raku, " ", (1,"1",1.0).unique.raku, " ", (1,2,4,5).squish(:with(-> $prev, $cur { $cur == $prev + 1 })).raku, " ", <a A b>.unique(:as(&lc)).raku, " ", (1,2,3,4).unique(:with(-> $a, $b { $a %% 2 == $b %% 2 })).raku
# rakudo 2026.08: (42,).Seq ().Seq (42,).Seq (Any,).Seq (Any,).Seq (1, "1", 1.0).Seq (1, 4).Seq ("a", "b").Seq (1, 2).Seq
```
`squish`'s `:with` receives (previous kept, current). rakupp 4.0.1: differs — `squish(:with)` is ignored: `(1, 2, 4, 5)`.

### NA-27  pairup                                                    D:yes R:no V:spec
A type object gives `().Seq`. Consecutive elements pair up; an element
that is already a Pair is kept; a non-itemized Map contributes its pairs;
an odd count throws `X::Pairup::OddNumber` when the Seq is reified.
```
say (1,2,3,4).pairup.raku, " ", Any.pairup.raku, " ", (a => 1, 2, 3).pairup.raku, " ", ({a => 1}, 2, 3).pairup.raku, " ", ($(a => 1, b => 2), 3, 4).pairup.raku, " ", ().pairup.raku, " ", ((a => 1), (b => 2)).pairup.raku
# rakudo 2026.08: (1 => 2, 3 => 4).Seq ().Seq (:a(1), 2 => 3).Seq (:a(1), 2 => 3).Seq (${:a(1), :b(2)} => 3, 4 => Nil).Seq ().Seq (:a(1), :b(2)).Seq
say (try { my @a = 42.pairup; "no-throw" }) // "caught:" ~ $!.^name, " ", (try { (1,2,3).pairup.List; "no-throw" }) // "caught:" ~ $!.^name
# rakudo 2026.08: caught:X::Pairup::OddNumber no-throw
```
Note the itemized Map `$(…)` is a key, and the trailing 4 pairs with Nil in this reified-by-`.raku` view; `.List` on the odd list does not reify and so does not throw yet.
rakupp 4.0.2: differs only in WHEN the odd count is noticed — `.pairup` is eager here, so `X::Pairup::OddNumber` is raised as the Seq is built rather than as it reifies. The type, the Map spread and the shapes match.

### NA-28  toggle                                                    D:yes R:yes V:spec
```
say 42.toggle.raku, " ", 42.toggle(:off).raku, " ", 42.toggle(!*).raku, " ", (1..8).toggle(* < 3, * > 5).raku, " ", (1..8).toggle(* < 3, * > 5, :off).raku, " ", (1..8).toggle(* < 3).raku, " ", (try Any.toggle.raku) // $!.^name
# rakudo 2026.08: (42,).Seq ().Seq ().Seq (1, 2, 6, 7, 8).Seq (1,).Seq (1, 2).Seq X::Multi::NoMatch
```
rakupp 4.0.2: matches.

### NA-29  head, tail, skip                                          D:yes R:yes V:spec
On a type object `head`/`tail` return the type object and `head(n)` a
one-element Seq. On a scalar `head`/`tail` return the scalar itself, not a
list. On an empty list both are Nil.
```
say 42.head.raku, " ", 42.tail.raku, " ", Any.head.raku, " ", Any.tail.raku, " ", Any.head(2).raku, " ", Any.tail(2).raku, " ", 42.head(0).raku, " ", 42.head(*-1).raku, " ", 42.tail(*+10).raku, " ", 42.tail(*-1).raku, " ", ().head.raku, " ", ().tail.raku, " ", (1,2,3).head(*-1).raku, " ", (1,2,3).tail(*-1).raku
# rakudo 2026.08: 42 42 Any Any (Any,).Seq (Any,).Seq ().Seq ().Seq (42,).Seq ().Seq Nil Nil (1, 2).Seq (2, 3).Seq
say 42.skip.raku, " ", 42.skip(*).raku, " ", Any.skip.raku, " ", Any.skip(*-1).raku, " ", Any.skip(*-0).raku, " ", (1,2,3).skip(*-2).raku, " ", (1,2,3).skip(5).raku, " ", (1,2,3).skip("1").raku, " ", (1,2,3).skip(-1).raku, " ", (1,2,3).skip(0).raku
# rakudo 2026.08: ().Seq ().Seq ().Seq (Any,).Seq ().Seq (2, 3).Seq ().Seq (2, 3).Seq (1, 2, 3).Seq (1, 2, 3).Seq
```
rakupp 4.0.2: matches.

### NA-30  batch and rotor need a defined invocant                   D:yes R:yes V:spec
```
say 42.batch(2).raku, " ", 42.rotor(1).raku, " ", (try Any.batch(2).raku) // $!.^name, " ", (try Any.rotor(1).raku) // $!.^name, " ", (1..5).batch(:2elems).raku, " ", (1..5).batch(2).raku, " ", (1..5).rotor(2).raku, " ", (1..5).rotor(2, :partial).raku, " ", (1..5).rotor((2 => -1, 1)).raku
# rakudo 2026.08: ((42,),).Seq ((42,),).Seq X::Multi::NoMatch X::Multi::NoMatch ((1, 2), (3, 4), (5,)).Seq ((1, 2), (3, 4), (5,)).Seq ((1, 2), (3, 4)).Seq ((1, 2), (3, 4), (5,)).Seq ((1, 2), (2,), (3, 4), (4,)).Seq
```
rakupp 4.0.2: matches — `:2elems` is the batch SIZE now, not a `size => gap` pair whose key numified to zero.

### NA-31  are: the strictest common type                            D:yes R:yes V:spec
A type object returns itself; an empty list Nil; otherwise the narrowest
type or role every element matches, walking the first element's MRO
including roles (`(1, 2.5)` → Real, `(1, "a")` → Cool, `(1, 2, 3,
Date.today)` → Any). `.are(Type)` is True or a Failure "Expected 'Int' but
got 'Str' in element 1"; on a type object the Failure names the invocant.
A lazy list throws `X::Cannot::Lazy`.
```
say 42.are.raku, " ", Any.are.raku, " ", (1, 2.5).are.raku, " ", (1, "a").are.raku, " ", ().are.raku, " ", (1, 2).are(Int), " ", do { my $f = (1, "a").are(Int); $f.^name ~ ":" ~ $f.exception.message }, " ", (1, 2).are(Mu), " ", do { my $f = Any.are(Int); $f.^name ~ ":" ~ $f.exception.message }, " ", 42.are(Int), " ", (try (1..*).are) // $!.^name, " ", (Int, Str).are.raku, " ", (1, Int).are.raku, " ", (1, 2, 3, Date.today).are.raku, " ", (Int, Cool).are.raku
# rakudo 2026.08: Int Any Real Cool Nil True Failure:Expected 'Int' but got 'Str' in element 1 True Failure:Expected 'Int' but got 'Any' True X::Cannot::Lazy Cool Int Any Cool
```
rakupp 4.0.2: differs only in the Failure's wording for a type-object invocant, which names "in element 0" where Rakudo names no element. The common type is now found by walking the first element's linearisation, so a user class's own chain counts.

### NA-32  nodemap, deepmap, duckmap                                 D:partial R:partial V:spec/quirk
`nodemap` applies one level and returns a List (a Hash for an Associative).
`deepmap` descends into Iterables, **itemizes** each inner result, flattens
a Slip result, drops an Empty result, and returns the invocant's own type
when that type can store (Array → Array, List → List, Seq → List). On an
Associative it maps the values and **unwraps a one-element Positional
value** (`{a => [5]}` becomes `{a => 6}`) — a quirk. `duckmap` applies the
callable where its signature accepts the element, passes the element
through otherwise, and recurses into Iterables. All three die for a
callable with more than one parameter and warn that FIRST/NEXT/LAST
phasers are ignored.
```
say 42.nodemap(* + 1).raku, " ", 42.deepmap(* + 1).raku, " ", 42.duckmap(* + 1).raku, " ", [1, [2, 3]].deepmap(* + 1).raku, " ", (1, (2, 3)).nodemap(*.elems).raku, " ", (1, (2, 3)).deepmap(* * 10).raku, " ", [1, [2, 3]].duckmap(-> Int $x { $x * 10 }).raku, " ", {a => [1, 2]}.deepmap(* + 1).raku, " ", {a => [5]}.deepmap(* + 1).raku, " ", {a => 1, b => 2}.nodemap(* + 1).raku
# rakudo 2026.08: (43,) (43,) (43,) [2, [3, 4]] (1, 2) (10, $(20, 30)) [10, [20, 30]] {:a($[2, 3])} {:a(6)} {:a(2), :b(3)}
say (try (1,2).nodemap(-> $a, $b { })) // $!.message, " | ", (1, (2, 3)).deepmap({ $_ == 2 ?? Empty !! $_ }).raku, " ", (1, (2, 3)).deepmap({ ($_, $_).Slip }).raku, " ", do { my @w; CONTROL { when CX::Warn { @w.push(.message); .resume } }; (1,2).nodemap({ FIRST { }; $_ }).raku ~ " " ~ @w.raku }
# rakudo 2026.08: .nodemap only supports Callables with a single parameter, got 2 | (1, $(3,)) (1, 1, $(2, 2, 3, 3)) (1, 2) [".nodemap ignores FIRST phaser(s)"]
say (1, "a", (2, "b")).duckmap(-> Int $x { $x * 10 }).raku, " ", 42.duckmap(-> Str $s { "S" }).raku, " ", (1,2).Seq.deepmap(* + 1).^name, " ", [1,2].deepmap(* + 1).^name, " ", (1,2).deepmap(* + 1).^name, " ", (1,2).nodemap(* + 1).^name, " ", {a => 1}.nodemap(* + 1).^name, " ", {a => 1}.duckmap(-> Int $x { $x + 1 }).raku
# rakudo 2026.08: (10, "a", $(20, "b")) (42,) List Array List List Hash {:a(2)}
```
rakupp 4.0.2: differs — inner results are itemized, a Slip flattens, an Empty is dropped and a multi-parameter mapper is refused; the Associative one-element unwrap and the FIRST/NEXT/LAST phaser warning are not there.

### NA-33  classify and categorize                                   D:yes R:yes V:spec
The result is an **object hash** (`Hash[Mu,Mu,Any]`): keys keep their type.
No argument dies (`X::AdHoc`, "Must specify something to classify with, a
Callable, Hash or List"). A `*` test classifies by identity. `:as`
transforms the stored values, not the keys. `:into(%h)` stores into the
given hash (a plain Hash stringifies keys) and appends to existing arrays.
A Hash or List test maps by lookup. `categorize`'s test returns a list of keys.
```
say 42.classify({ $_ }).raku, " ", 42.classify({ $_ }).keys[0].^name, " ", (1,2,3).classify(* % 2).raku, " ", (1,2,3).classify(* % 2, :as(* * 10)).raku, " ", (1,2).classify(%(1 => "odd", 2 => "even")).raku, " ", (0,1).classify(<zero one>).raku, " ", (try (1,2).classify) // $!.^name, " ", do { my %h; (1,2,3).classify(* % 2, :into(%h)); %h.raku }, " ", (1,2,3).classify(*).raku
# rakudo 2026.08: (my Mu %{Mu} = 42 => $[42]) Int (my Mu %{Mu} = 0 => $[2], 1 => $[1, 3]) (my Mu %{Mu} = 0 => $[20], 1 => $[10, 30]) (my Mu %{Mu} = :even($[2]), :odd($[1])) (my Mu %{Mu} = :one($[1]), :zero($[0])) X::AdHoc {"0" => $[2], "1" => $[1, 3]} (my Mu %{Mu} = 1 => $[1], 2 => $[2], 3 => $[3])
say (1,2,3).classify(* % 2).^name, " ", do { my %h = z => [0]; (1,2).classify({ "z" }, :into(%h)); %h.raku }, " ", (1,2).classify(:as(* * 2), * % 2).raku
# rakudo 2026.08: Hash[Mu,Mu,Any] {:z($[0, 1, 2])} (my Mu %{Mu} = 0 => $[4], 1 => $[2])
say (1,2,3).categorize({ $_ %% 2 ?? <even all> !! <odd all> }).raku, " ", 42.categorize({ ($_,) }).raku, " ", (1,2).categorize(%(1 => <a b>, 2 => <b>)).raku, " ", (try 42.categorize) // $!.^name
# rakudo 2026.08: (my Mu %{Mu} = :all($[1, 2, 3]), :even($[2]), :odd($[1, 3])) (my Mu %{Mu} = 42 => $[42]) (my Mu %{Mu} = :a($[1]), :b($[1, 2])) X::AdHoc
```
rakupp 4.0.2: differs only in `.^name`, which is `Hash[Mu,Mu]` where Rakudo says `Hash[Mu,Mu,Any]`. The result is an object hash, `:as` transforms only the stored value, `:into` appends, and the no-argument form dies.

### NA-34  push and friends autovivify an undefined container        D:yes R:partial V:spec
On an undefined container, `push`/`append` create `SELF.new` if the type
object is Positional, else an Array; `unshift`/`prepend` always create an
Array. The new object is assigned into the container, so a typed `Int $i`
fails with `X::TypeCheck::Assignment`, and a `List $l` fails with
`X::Immutable` because List's own method wins. On a defined non-Positional
value there is no candidate: `X::Multi::NoMatch`.
```
say do { my $x; $x.push(1, 2); $x.raku }, " ", do { my $x; $x.append((1, 2)); $x.raku }, " ", do { my $x; $x.unshift(1); $x.raku }, " ", do { my $x; $x.prepend((1, 2)); $x.raku }, " ", do { my Array $a; $a.push(1); $a.raku }
# rakudo 2026.08: $[1, 2] $[1, 2] $[1] $[1, 2] $[1]
say (try { my Int $i; $i.push(1); $i.raku }) // $!.^name, " ", (try { my List $l; $l.push(1); $l.raku }) // $!.^name, " ", (try 42.push(1)) // $!.^name, " ", (try { my List $l; $l.unshift(1); $l.raku }) // $!.^name, " ", (try { my Positional $p; $p.push(1); $p.raku }) // $!.^name, " ", (try "a".push(1)) // $!.^name
# rakudo 2026.08: X::TypeCheck::Assignment X::Immutable X::Multi::NoMatch X::Immutable X::Assignment::RO X::Multi::NoMatch
```
rakupp 4.0.2: differs — typed containers still accept the Array (`[1]`), `42.push(1)` is still `X::Method::NotFound`, and the vivified Array is not marked as sitting in a Scalar, so `.raku` reads `[1, 2]` for `$[1, 2]`.

### NA-35  Positional subscripting of a scalar                       D:partial R:? V:spec
Index 0 — also `0.9`, `"0"`, `*-1` — returns the value itself; any other
index returns a Failure `X::OutOfRange` (what "Index", range `0..0`), for
type objects too (`(Any)[0]` is Any, `(Any)[1]` a Failure). NaN or Inf as
index throws `X::Numeric::CannotConvert`; a type object as index throws
`X::AdHoc` ("Unable to call postcircumfix [ (Int) ] with a type object").
`42[0][0]` is 42, `42[0, 0]` is `(42, 42)`, `42[()]` is `()`, `42[*]` is
`(42,)`. `:exists` is True for 0 only; `:delete` returns a Failure "Can not
remove elements from a Int" (Nil for a type object).
```
say 42[0], " ", 42[0][0], " ", do { my $f = 42[1]; $f.^name ~ ":" ~ $f.exception.^name ~ ":" ~ $f.exception.what ~ ":" ~ $f.exception.range }, " ", 42[*-1], " ", 42[1.5].^name, " ", 42[0.9], " ", (try 42[NaN]) // $!.^name ~ ":" ~ $!.message, " | ", (try 42[Inf]) // $!.^name, " ", 42["0"], " | ", 42[0, 0].raku, " ", 42[()].raku, " ", 42[*].raku
# rakudo 2026.08: 42 42 Failure:X::OutOfRange:Index:0..0 42 Failure 42 X::Numeric::CannotConvert:Cannot convert NaN to Int | X::Numeric::CannotConvert 42 | (42, 42) () (42,)
say (42[0]:exists), " ", (42[1]:exists), " ", (42[0,1]:exists).raku, " ", ((Any)[0]:exists), " ", (Any)[0].raku, " ", (Any)[1].^name, " ", do { my $f = 42[0]:delete; $f.^name ~ ":" ~ $f.exception.message }, " ", ((Any)[0]:delete).raku, " ", ((Any)[1]:exists)
# rakudo 2026.08: True False (Bool::True, Bool::False) False Any Failure Failure:Can not remove elements from a Int Nil False
say (try (Any)[Int]) // $!.^name ~ ":" ~ $!.message.lines[0], " | ", (try 42[Int]) // $!.^name
# rakudo 2026.08: X::AdHoc:Unable to call postcircumfix [ (Int) ] with a type object | X::AdHoc
```
rakupp 4.0.2: differs — an out-of-range index is the Failure with its `what`/`got`/`range`, `:exists` answers by index, and `42[()]`, `42[*]` and `42[0, 0]` are the slices they should be; a NaN/Inf index is `X::OutOfRange` rather than `X::Numeric::CannotConvert`, and a type object as index reads element 0 instead of dying.

### NA-36  Assigning through a subscript                             D:yes R:yes V:spec
On an undefined container the chain autovivifies Arrays and Hashes
(`$x[2] = 5`, `$x<a><b> = 3`, `$x[0]<k> = 4`); a read does not vivify. On a
defined immutable value: index 0 throws `X::Assignment::RO`, another index
throws `X::OutOfRange`, a key throws `X::AdHoc`.
```
say do { my $x; $x[2] = 5; $x.raku }, " ", do { my $x; $x{"a"} = 1; $x.raku }, " ", do { my $x; $x[0][1] = 2; $x.raku }, " ", do { my $x; $x<a><b> = 3; $x.raku }, " ", do { my $x; $x[0]<k> = 4; $x.raku }, " ", do { my $x; my $v = $x[3]; $x.raku }, " ", do { my $x; my $v = $x<a>; $x.raku }
# rakudo 2026.08: $[Any, Any, 5] ${:a(1)} $[[Any, 2],] ${:a(${:b(3)})} $[{:k(4)},] Any Any
say (try { my $y = 42; $y[0] = 1; $y.raku }) // $!.^name, " ", (try { my $y = 42; $y[1] = 1; $y.raku }) // $!.^name, " ", (try { my $y = 42; $y<a> = 1; $y.raku }) // $!.^name, " ", do { my $x; $x[1][0] = 7; $x.raku }, " ", do { my $x; $x<a>[1] = 8; $x.raku }, " ", do { my @a; @a[1]<k> = 9; @a.raku }
# rakudo 2026.08: X::Assignment::RO X::OutOfRange X::AdHoc $[Any, [7]] ${:a($[Any, 8])} [Any, {:k(9)}]
```
rakupp 4.0.2: differs only in the `.raku` of what vivification builds — `[Any, Any, 5]` where Rakudo itemizes `$[Any, Any, 5]`. Assigning through a subscript on a defined value no longer REPLACES it: the three deaths match.

### NA-37  Associative subscripting of a non-Associative             D:yes R:? V:spec
`42<a>` is a Failure "Type Int does not support associative indexing.";
`:exists` is False; `:delete` a Failure "Can not remove values from a Int";
binding a key into a value throws `X::Bind`; binding into an undefined
container vivifies a Hash. A zen slice `%h{}` is the hash; with an unknown
adverb it is a Failure `X::Adverb`.
```
say do { my $f = 42<a>; $f.^name ~ ":" ~ $f.exception.message }, " ", (42<a>:exists), " ", do { my $f = 42<a>:delete; $f.^name ~ ":" ~ $f.exception.message }, " ", do { my $x; $x<a> := 1; $x.raku }, " ", (try { 42<a> := 1 }) // $!.^name, " ", do { my %h = a => 1; %h{}.raku }, " ", do { my %h = a => 1; my $f = %h{}:foo; $f.^name ~ ":" ~ $f.exception.^name ~ ":" ~ $f.exception.unexpected.raku }, " ", do { my $x; my $v = $x<a>; ($x.raku, $v.raku).join("/") }
# rakudo 2026.08: Failure:Type Int does not support associative indexing. False Failure:Can not remove values from a Int ${:a(1)} X::Bind {:a(1)} Failure:X::Adverb:("foo",).Seq Any/Any
```
rakupp 4.0.2: differs — `42<a>` and `42<a>:delete` are Failures and `:exists` is False; binding a key into a value does not raise `X::Bind`, and a zen slice with an unknown adverb does not fail.

### NA-38  Junctions from any value                                  D:yes R:yes V:spec
```
say 42.any.raku, " ", (1,2).all.raku, " ", Any.any.raku, " ", 42.one.raku, " ", 42.none.raku, " ", (so 42.any == 42), " ", (so Any.any.defined)
# rakudo 2026.08: any(42) all(1, 2) any(Any) one(42) none(42) True False
```
rakupp 4.0.2: matches.

### NA-39  QuantHash and Supply coercions                            D:yes R:yes V:spec
```
say 42.Set.raku, " ", Any.Set.raku, " ", Any.Set.elems, " ", 42.Mix.raku, " ", 42.SetHash.raku, " ", 42.MixHash.raku, " ", 42.Supply.list.raku, " ", Any.Supply.list.raku, " ", (1,1,2).Bag.elems
# rakudo 2026.08: Set.new(42) Set.new(Any) 1 (42=>1).Mix SetHash.new(42) (42=>1).MixHash (42,) (Any,) 2
```
rakupp 4.0.2: matches (the `.raku` order of a multi-element Bag differs, both are hash order).

### NA-40  match on Any stringifies and sets the caller's $/         D:no R:? V:spec
A type object warns "Use of uninitialized value of type Any in string context".
```
say do { 42.match(/4/).raku ~ " " ~ $/.raku }, " ", do { "abc".match(/b/); 42.match(/9/).raku ~ " " ~ $/.raku }, " ", (1..3).match(/2/).raku, " ", do { my @w; CONTROL { when CX::Warn { @w.push(.message); .resume } }; Any.match(/x/).raku ~ " " ~ @w.elems }
# rakudo 2026.08: Match.new(:orig("42"), :from(0), :pos(1)) Match.new(:orig("42"), :from(0), :pos(1)) Nil Nil Match.new(:orig("1 2 3"), :from(2), :pos(3)) Nil 1
```
rakupp 4.0.2: differs only in the missing warning for a TYPE OBJECT invocant; Nil in string context does warn.

### NA-41  join                                                      D:yes R:yes V:spec
A scalar joins to its Str; a type object to `""` with the uninitialized
warning; a Nil element to `""` with "Use of Nil in string context"; a nested
list is stringified with spaces.
```
say 42.join.raku, " ", 42.join("-").raku, " ", (1,2).join("-").raku, " ", (1, (2, 3)).join(",").raku, " ", join("-", 1, 2), " ", join("-").raku
# rakudo 2026.08: "42" "42" "1-2" "1,2 3" 1-2 ""
say do { my @w; CONTROL { when CX::Warn { @w.push(.message); .resume } }; Any.join.raku ~ " " ~ @w.elems ~ " " ~ (1, Nil, 3).join(",").raku ~ " " ~ @w.elems }
# rakudo 2026.08: "" 1 "1,,3" 2
```
rakupp 4.0.2: matches.

### NA-42  reverse, combinations, permutations                       D:yes R:yes V:spec
```
say 42.reverse.raku, " ", 42.combinations.raku, " ", 42.combinations(1).raku, " ", 42.permutations.raku, " ", Any.reverse.raku, " ", Any.combinations.raku, " ", (1,2).combinations(2).raku, " ", (1,2,3).combinations(2..3).raku, " ", (1,2).permutations.raku
# rakudo 2026.08: (42,).Seq ((), (42,)).Seq ((42,),).Seq ((42,),).Seq (Any,).Seq ((), (Any,)).Seq ((1, 2),).Seq ((1, 2), (1, 3), (2, 3), (1, 2, 3)).Seq ((1, 2), (2, 1)).Seq
```
rakupp 4.0.2: matches.

### NA-43  pick and roll                                             D:yes R:yes V:spec
`pick(n)` never yields more than exists; `pick(*)` is a permutation;
`pick(**)` re-picks endlessly; a type object picks itself; an empty list
picks Nil and picks-n `()`.
```
say 42.pick.raku, " ", 42.pick(2).raku, " ", 42.roll(3).raku, " ", 42.pick(*).raku, " ", 42.pick(**).head(3).raku, " ", ().pick.raku, " ", ().roll.raku, " ", ().pick(2).raku, " ", ().roll(2).raku, " ", pick(2, 1..10).elems, " ", roll(3, 7).raku
# rakudo 2026.08: 42 (42,).Seq (42, 42, 42).Seq (42,).Seq (42, 42, 42).Seq Nil Nil () ().Seq 2 (7, 7, 7).Seq
say Any.pick.raku, " ", Any.roll.raku, " ", Int.pick(2).raku, " ", (try Any.pick(**).head(2).raku) // $!.^name
# rakudo 2026.08: Any Any (Int,).Seq (Any, Any).Seq
```
rakupp 4.0.2: differs only in `pick(**)`, which is finite here — the HyperWhatever re-pick is not built. `Any.pick`, `Int.pick(2)` and the empty list's shapes match.

### NA-44  tree                                                      D:yes R:no V:spec
A type object or a non-Iterable returns itself. An Iterable becomes nested
**itemized Seqs**. `tree(n)` descends n levels (`tree(0)` leaves the list
alone, `tree(1)` itemizes only the outer). `tree(*)` is `tree`. With
callables, the first applies to the whole and each later one to the next
level down; a list of callables works the same.
```
say Any.tree.raku, " ", 42.tree.raku, " ", (1, (2, 3)).tree.raku, " ", [1, [2, 3]].tree.raku, " ", (1, (2, 3)).tree(1).raku, " ", (1, (2, 3)).tree(0).raku, " ", (1, (2, 3)).tree(*).raku, " ", (1, (2, 3)).tree(*.elems).raku, " ", (1, (2, 3)).tree(*.join("-"), *.elems).raku, " ", (1, (2, (3, 4))).tree(*.join("|"), *.join("-"), *.elems).raku, " ", (1, (2, 3)).tree([*.elems, *.join("-")]).raku
# rakudo 2026.08: Any 42 $((1, $((2, 3).Seq)).Seq) $((1, $((2, 3).Seq)).Seq) $((1, (2, 3)).Seq) (1, (2, 3)) $((1, $((2, 3).Seq)).Seq) 2 "1-2" "1|2-2" 2
```
rakupp 4.0.2: differs — a non-Iterable and a type object answer themselves, and the callable forms compute the right values; the nested itemized-Seq form an Iterable should take is not built.

### NA-45  flatmap, slice, splice, nl-out                            D:partial R:no V:spec
`flatmap` is map then flat. `slice(@indices)` on a defined invocant takes
those positions and requires strictly increasing indices, dying at
reification otherwise ("Provided index 0, which is lower than 1"); on a
type object `X::Multi::NoMatch`. `splice` has no candidate on Any:
`X::Multi::NoMatch`. `nl-out` is `"\n"`.
```
say 42.flatmap({ ($_, $_) }).raku, " ", (1, (2, 3)).flatmap({ $_ }).raku, " ", 42.slice(0).raku, " ", (1,2,3).slice(0, 2).raku, " ", (1,2,3).slice(1..2).raku, " ", (try (1,2,3).slice(0, 0).eager) // $!.^name, " ", (try (1,2,3).slice(2, 0).eager) // $!.^name, " ", (try Any.slice(0)) // $!.^name, " ", (try 42.splice) // $!.^name, " ", 42.nl-out.raku
# rakudo 2026.08: (42, 42).Seq (1, 2, 3).Seq (42,).Seq (1, 3).Seq (2, 3).Seq X::AdHoc X::AdHoc X::Multi::NoMatch X::Multi::NoMatch "\n"
```
rakupp 4.0.2: differs — `flatmap` and `slice` match, and `Any.slice` is `X::Multi::NoMatch`; `.slice` does not require strictly increasing indices, and `42.splice` is `X::Method::NotFound` rather than `X::Multi::NoMatch`.

### NA-46  ACCEPTS on plain objects is identity                      D:yes R:yes V:spec
A type-object argument never matches a value (`Int ~~ 42` is False); a
defined argument matches by `===`; value types override (`42 ~~ 42.0`).
```
say 42.ACCEPTS(Int), " ", 42.ACCEPTS(42), " ", (Int ~~ 42), " ", (42 ~~ 42.0), " ", ("a" ~~ "a"), " ", Any.ACCEPTS(42), " ", (42 ~~ Any), " ", do { class AC { }; my $a = AC.new; ($a ~~ $a) ~ " " ~ ($a ~~ AC.new) ~ " " ~ $a.ACCEPTS(AC) ~ " " ~ (AC ~~ $a) }
# rakudo 2026.08: False True False True True True True True False False False
```
rakupp 4.0.2: matches.

### NA-47  === and its one-argument form                             D:yes R:yes V:spec
With one argument `===` is True. Type objects compare by identity; values
by type plus `WHICH`, so value types (Int, Str, Range, Pair) compare by
value and containers (Array, List, Hash, Slip) by identity.
```
say infix:<===>(5), " ", (Nil === Nil), " ", ((1,2) eqv (1,2)), " ", (Any === Mu), " ", do { my $a = [1]; ($a === $a) ~ " " ~ ($a === [1]) }, " ", ({a => 1} === {a => 1}), " ", ((1,2) === (1,2)), " ", ((1,2).Slip === (1,2).Slip), " ", (1..2 === 1..2), " ", ((a => 1) === (a => 1)), " ", (<a b> === <a b>)
# rakudo 2026.08: True True True False True False False False False True True False
say (1 === 1), " ", (1 === 1.0), " ", ("a" === "a"), " ", (Int === Int), " ", (Int === Str), " ", (1 ⩶ 1)
# rakudo 2026.08: True False True True False True
```
rakupp 4.0.2: matches.

### NA-48  ++ and -- on an undefined container                       D:yes R:yes V:spec
`$x++` yields 0 and leaves 1; `++$x` yields 1; `$x--` yields 0 and leaves
-1; `--$x` yields -1. A typed `Int` container works; a `Str` container
fails the assignment of 1 (`X::TypeCheck::Assignment`).
```
say do { my $x; my $r = $x++; $r ~ ":" ~ $x }, " ", do { my $x; my $r = ++$x; $r ~ ":" ~ $x }, " ", do { my $x; my $r = $x--; $r ~ ":" ~ $x }, " ", do { my $x; my $r = --$x; $r ~ ":" ~ $x }, " ", do { my Int $i; $i++; $i }, " ", do { my Str $s; (try { $s++; $s }) // $!.^name }, " ", do { my $x = Nil; $x++; $x }
# rakudo 2026.08: 0:1 1:1 0:-1 -1:-1 1 X::TypeCheck::Assignment 1
```
rakupp 4.0.2: differs — the Str container still accepts 1.

### NA-49  The sub forms, and min/max with undefined operands        D:yes R:partial V:spec
The list subs take `+values`: one Iterable argument is the list, several
arguments are the elements. `grep(True, …)` throws `X::Match::Bool`,
`first(True, …)` returns a Failure. For `min`/`max`, an undefined operand
loses to a defined one; with only undefined operands, or no operands, the
result is `Inf` for min and `-Inf` for max, and `().minmax` is `Inf..-Inf`.
`min(Range)` is `Range.min`. The `minmax` infix of two lists spans both.
```
say elems(42), " ", keys(42).raku, " ", values(42).raku, " ", pairs(42).raku, " ", kv(42).raku, " ", end(42), " ", elems((1,2)), " ", elems([1,2]), " ", keys((1,2)).raku, " ", map({ $_ * 2 }, 1, 2).raku, " ", map(* * 2, (1, 2)).raku, " ", grep(* > 1, 1, 2, 3).raku, " ", (try grep(True, 1, 2)) // $!.^name, " ", first(* > 1, 1, 2, 3), " ", do { my $f = first(True, 1); $f.^name }
# rakudo 2026.08: 1 (0,).Seq (42,) (0 => 42,).Seq (0, 42).Seq 0 2 2 (0, 1).Seq (2, 4).Seq (2, 4).Seq (2, 3).Seq X::Match::Bool 2 Failure
say (1 min 2), " ", (Int min 5), " ", (5 min Int), " ", (Int max 5), " ", (Int min Str).raku, " ", min(Int, Str).raku, " ", max(Int, Str).raku, " ", min().raku, " ", max().raku, " ", ([min] (3, 1, 2)), " ", ((3, 1, 2) minmax (0, 9)).raku, " ", min(3, 1, 2), " ", max(<b a c>), " ", min(<bb a ccc>, :by(*.chars)), " ", minmax(3, 1, 2).raku, " ", (3, Any, 1).min, " ", (Any, Int).min.raku, " ", ().min.raku, " ", ().max.raku, " ", ().minmax.raku, " ", 42.minmax.raku, " ", ("a", 2).min.raku
# rakudo 2026.08: 1 5 5 5 Inf Inf -Inf Inf -Inf 1 0..9 1 c a 1..3 1 Inf Inf -Inf Inf..-Inf 42..42 2
say reduce(&[+], 1, 2, 3), " ", produce(&[+], 1, 2, 3).raku, " ", unique(1, 1, 2).raku, " ", squish(1, 1, 2, 1).raku, " ", repeated(1, 1, 2).raku, " ", head(2, 1..5).raku, " ", tail(2, 1..5).raku, " ", skip(2, 1..5).raku, " ", classify(* % 2, 1, 2, 3).raku, " ", nodemap(* + 1, (1, 2)).raku, " ", deepmap(* + 1, [1, [2]]).raku, " ", reduce(&[+], (1, 2, 3)), " ", unique((1, 1, 2)).raku, " ", head(2, (1..5)).raku
# rakudo 2026.08: 6 (1, 3, 6).Seq (1, 2).Seq (1, 2, 1).Seq (1,).Seq (1, 2).Seq (4, 5).Seq (3, 4, 5).Seq (my Mu %{Mu} = 0 => $[2], 1 => $[1, 3]) (2, 3) [2, [3]] 6 (1, 2).Seq (1, 2).Seq
```
rakupp 4.0.2: differs — the sub forms answer the method's own shapes now (`keys(42)` is `(0,).Seq`, `values(42)` a List); `Int min 5` is still `(Int)`, `Int min Str` still `Str`, and `().minmax` still `0..-1`.

### NA-50  item                                                      D:yes R:? V:spec
`item` with several arguments itemizes them as one list; with one argument it itemizes that value.
```
say item(1, 2).raku, " ", item(42).raku, " ", item((1, 2)).raku
# rakudo 2026.08: $(1, 2) 42 $(1, 2)
```
rakupp 4.0.2: matches.

## Counts

| | items |
|---|---|
| total | 50 |
| not fully stated by docs (D:yes) nor asserted by Roast (R:yes) | 16 |
| Rakudo bugs (do not imitate) | 0 — none found in this area |
| quirks (recorded, step two decides) | 3 — NA-10 ACCEPTS, NA-24 sort ambiguity, NA-32 hash unwrap |
| rakupp 4.0.1 differs (before implementation) | 43 |
| rakupp 4.0.1 matches (before implementation) | 6 |
| **rakupp 4.0.2 matches** | **28** |
| rakupp 4.0.2 differs | 22 — and NA-10 on purpose |

Implemented 2026-09-18 from this sheet alone, with the Homebrew Rakudo
`v2026.08` as oracle and Roast as the gate. Of the 91 probes above, 18
matched Rakudo before and **56** match now; of the 50 items, 6 matched fully
and **28** do. Roast: the whole suite goes **725 fully-passing files → 729**
with no file regressing, and the files this sheet names
(`S32-list/*.t`, `S02-types/nil.t`, `S02-types/autovivification.t`,
`S03-operators/autoincrement.t`, `S03-operators/identity.t`,
`S03-smartmatch/any-any.t`) go **26 → 28**; `S02-types/nil.t` fails only its
four `#?rakudo todo` assertions, and `S32-list/are.t` and `roll.t` now pass
in full, as does `integration/advent2011-day20.t`.

Both rakupp defects this sheet surfaced are fixed: `(1..5).batch(:2elems)`
is `((1, 2), (3, 4), (5,))` (NA-30), and assigning through a subscript on a
defined Int raises instead of replacing it (NA-36).

What the 22 remaining items still want, in the order they would pay off:
the ENUM table behind `Bool.keys` and `42.invert` (NA-18); `.tree`'s nested
itemized Seqs (NA-44); laziness where Rakudo keeps it — `(1..*).sort`,
`.first(:end)`, `.pairup`'s reification (NA-24, NA-22, NA-27); the
NUMERIC-context warning for Nil (NA-08); the `$(…)` marker on a value a
Scalar container holds, which is what the last `.raku` differences in NA-14,
NA-34 and NA-36 come down to; `Int:D`/native assignment and `%h = Nil`
(NA-11); `.push` on a typed container (NA-34); and `min`/`max` with an
undefined operand (NA-49).

## Method (how this sheet was produced)

As for [Supply.md](Supply.md): `git show 2026.08:src/core.c/<file>`, read in
full; one-line probes run through `perl -e 'alarm 10; exec @ARGV'` on the
Homebrew 2026.08 binary and on `build-arm64/rakupp`; outputs recorded
verbatim. Four probe rounds were needed because a lazy Seq throws at
reification, outside the `try` that produced it; every such probe now
reifies inside the `try`. D flags from `doc/Type/Nil.rakudoc`,
`doc/Type/Any.rakudoc` and `doc/Language/subscripts.rakudoc`; R flags from
`S02-types/nil.t`, `S02-types/autovivification.t`, `S32-list/*.t`,
`S03-operators/autoincrement.t`, `S03-operators/identity.t`,
`S03-smartmatch/any-any.t`.
