# Hash, Map, Pair — semantics sheet

Provenance: Rakudo tag `2026.08`, files `src/core.c/Hash.rakumod` (534
lines), `Map.rakumod` (694), `Hash/Object.rakumod` (392), `Pair.rakumod`
(234), read in full on 2026-09-18. Oracle: Homebrew Rakudo v2026.08.
Compared against Raku++ 4.0.1-23-g80b16d18 (build-arm64, 2026-09-18).
Format and legend: [README.md](README.md).

Where this sits against the declared spec: Rakudo passes 17 of 17
`S32-hash` files here, Raku++ 6. The rules below are what those files and
real programs depend on: how a hash is filled, what a key is, what a
missing key returns, which adverb combinations are legal, how `push`
stacks values, what a Map refuses, and when a Pair's value is a container.
2 of the 20 items are neither fully documented nor fully
asserted by Roast.

## A. Construction

### HM-01  Hash assignment                                         D:yes R:yes V:spec
Pairs, alternating key/value lists, Maps and itemized Pairs all store; an
odd number of plain items throws `X::Hash::Store::OddNumber` with `.found`
(the count) and `.last` (the leftover); a later duplicate key wins; `()`
empties; the values are copied but a nested Array is shared.
```
say do { my %h = a => 1, b => 2; %h.raku }, " ", do { my %h = "a", 1, "b", 2; %h.raku }, " ", (try { my %h = 1, 2, 3; %h.raku }) // $!.^name ~ ":" ~ $!.found ~ ":" ~ $!.last, " ", do { my %h = <a b> Z=> 1..*; %h.raku }, " ", do { my %h = (a => 1), (b => 2); %h.raku }, " ", do { my %h = %(a => 1), b => 2; %h.raku }, " ", do { my %h = $(a => 1); %h.raku }, " ", do { my %h = ((a => 1), 2 => 3); %h.raku }, " ", do { my %h = (a => 1, a => 2); %h.raku }, " ", do { my %h = (a => 1, b => 2); %h = (); %h.raku }, " ", do { my %h = a => 1; %h = %h, b => 2; %h.raku }, " ", do { my %h = a => [1, 2]; my %g = %h; %g<a>.push(3); %h<a>.elems }
# rakudo 2026.08: {:a(1), :b(2)} {:a(1), :b(2)} X::Hash::Store::OddNumber:3:3 {:a(1), :b(2)} {:a(1), :b(2)} {:a(1), :b(2)} {:a(1)} {"2" => 3, :a(1)} {:a(2)} {} {:a(1), :b(2)} 3
say (try %(1, 2, 3)) // $!.^name, " ", (try { my %h = { $_ } }) // $!.message.lines[0], " ", (try { my %h = 1 }) // $!.^name, " ", do { my %h = Empty; %h.raku }, " ", do { my %h = (a => 1).Seq; %h.raku }, " ", do { my %h = [a => 1, b => 2]; %h.raku }, " ", (try { my %h = ([a => 1],); %h.raku }) // $!.^name ~ ":" ~ $!.last.raku, " ", do { my %h = a => 1, "b", 2; %h.raku }
# rakudo 2026.08: X::Hash::Store::OddNumber Cannot use a Callable as the only argument to store in a Hash.  If the X::Hash::Store::OddNumber {} {:a(1)} {:a(1), :b(2)} X::Hash::Store::OddNumber:[:a(1)] {:a(1), :b(2)}
```
A block that uses `$_` is a Callable, and storing one alone is its own error.
rakupp 4.0.1: differs — an odd count is stored silently (`{"1" => 2}`, `{}`), and so is a Callable.

### HM-02  Curly braces: Hash or Block                             D:yes R:yes V:spec
`{a => 1}` and `{}` are Hashes; `{ ; }`, `{ $_ }` and `{ a => $_ }` are
Blocks. `%()` and `hash()` always build a Hash; `hash("a", 1)` takes a
list, `hash(a => 1)` takes nameds.
```
say {a => 1}.^name, " ", {}.^name, " ", { ; }.^name, " ", {a => 1, b => 2}.raku, " ", %(a => 1).raku, " ", %().raku, " ", hash(a => 1).raku, " ", hash("a", 1).raku, " ", hash().raku, " ", ({ $_ }).^name, " ", ({ a => 1 }).^name, " ", ({ a => $_ }).^name
# rakudo 2026.08: Hash Hash Block {:a(1), :b(2)} {:a(1)} {} {:a(1)} {:a(1)} {} Block Hash Block
```
Assigning a `{…}` composer to a hash (`%h = {a => 1}`) compiles with a
"Useless use of hash composer" warning. rakupp 4.0.1: matches.

## B. Keys

### HM-03  Keys stringify                                          D:yes R:yes V:spec
A plain Hash's key type is `Str(Any)`: `%h{1}` and `%h<1>` are the same
key, `1.5` becomes `"1.5"`, an undefined key becomes `""` with the
uninitialized warning. A list in the subscript is a slice, so `%h<a b> = 1`
leaves the second key Any. `.of` is Mu and `.default` Any unless typed.
```
say do { my @w; CONTROL { when CX::Warn { @w.push(1); .resume } }; my %h; %h{Any} = 1; %h.keys.raku ~ " " ~ @w.elems }, " ", do { my %h; %h{1} = "x"; %h.keys[0].^name ~ " " ~ %h.raku ~ " " ~ %h{1} ~ " " ~ %h<1> }, " ", do { my %h; %h{1.5} = 1; %h{"1.5"} }, " ", do { my %h; %h{(1, 2)} = 3, 4; %h.raku }, " ", do { my %h; %h<a b> = 1, 2; %h.raku }, " ", do { my %h; %h<a b> = 1; %h.raku }, " ", do { my %h = a => 1; %h.keyof.raku ~ " " ~ %h.of.raku ~ " " ~ %h.default.raku }, " ", do { my Int %h; %h.default.raku ~ " " ~ %h.of.raku }
# rakudo 2026.08: ("",).Seq 1 Str {"1" => "x"} x x 1 {"1" => 3, "2" => 4} {:a(1), :b(2)} {:a(1), :b(Any)} Str(Any) Mu Any Int Int
```
rakupp 4.0.1: differs — no warning for the undefined key; `my Int %h; %h.default` is Any.

### HM-04  Object hashes                                           D:yes R:yes V:spec
`my %h{Int}` keeps keys as objects, typed: a Str or Rat key is
`X::TypeCheck::Binding::Parameter`, and `%h<1>` (a Str) does not find the
Int key 1. `my %h{Any}` and `:{ }` (`Hash[Mu,Mu,Any]`) keep `1` and `"1"`
distinct. To use an aggregate as one key it must be itemized, otherwise
the subscript is a slice.
```
say do { my %h{Int}; ((try { %h<x> = 1; "no" }) // $!.^name) ~ " " ~ ((try { %h{1.5} = 1; "no" }) // $!.^name) ~ " " ~ %h.raku }, " ", do { my %h{Any}; %h{1} = "a"; %h{"1"} = "b"; %h.elems ~ " " ~ %h.keys.map(*.^name).sort.raku }, " ", do { my %h{Int} = 1 => "a", 2 => "b"; %h.keys.sort.raku ~ " " ~ (%h{1}:exists) ~ " " ~ (%h<1>:exists) }, " ", do { my %h{Int}; %h{1} = "a"; %h.raku }, " ", do { my %h{Any}; %h{[1, 2]} = "a"; %h{[1, 2]}.raku ~ " " ~ %h.elems }, " ", :{ 1 => "a" }.raku, " ", :{ 1 => "a" }.^name, " ", :{ }.keyof.raku, " ", do { my %h{Int}; %h.keyof.raku ~ " " ~ %h.^name }
# rakudo 2026.08: X::TypeCheck::Binding::Parameter X::TypeCheck::Binding::Parameter (my Any %{Int}) 2 ("Int", "Str").Seq (1, 2).Seq True False (my Any %{Int} = 1 => "a") ("a", Any) 2 (my Mu %{Mu} = 1 => "a") Hash[Mu,Mu,Any] Mu Int Hash[Any,Int]
```
rakupp 4.0.1: differs — no key type check, `1` and `"1"` collapse, `%h<1>` finds the Int key, `:{ }` is named `Hash[Mu,Mu]`.

## C. Access and autovivification

### HM-05  Missing keys, chains, autovivification                  D:yes R:yes V:spec
A missing key reads as Any (or the `is default` value) without being
created; reading a chain creates nothing; assigning through a chain, or
`push`, `++`, `+=`, `~=` on a missing key, creates the intermediate
containers. An empty typed hash prints with its declaration.
```
say do { my %h = a => 1; %h<b>.raku ~ " " ~ %h.elems ~ " " ~ (%h<b>:exists) }, " ", do { my %h = a => 1; my $v = %h<b><c>; %h.raku }, " ", do { my %h; %h<a><b> = 1; %h.raku }, " ", do { my %h; %h<a>[1] = 1; %h.raku }, " ", do { my %h; %h<a>.push(1); %h.raku }, " ", do { my %h; %h<a>++; %h<b> += 2; %h<c> ~= "x"; %h.raku }, " ", do { my %h is default(0); %h<a>.raku ~ " " ~ %h.default ~ " " ~ (%h<a>:exists) }, " ", do { my Int %h is default(9); %h<a> ~ " " ~ %h.raku }
# rakudo 2026.08: Any 1 False {:a(1)} {:a(${:b(1)})} {:a($[Any, 1])} {:a($[1])} {:a(1), :b(2), :c("x")} 0 0 False 9 (my Int %)
```
rakupp 4.0.1: differs — an empty typed hash prints as `{}`.

### HM-06  Typed values, Nil, binding                              D:yes R:yes V:spec
A wrong value type is `X::TypeCheck::Assignment`; assigning Nil stores
the type object (`Int`) or Any; a key bound to a value cannot be assigned.
```
say do { my Int %h; (try { %h<a> = "x"; "no" }) // $!.^name }, " ", do { my Int %h = a => 1; %h<a> = Nil; %h.raku }, " ", do { my %h = a => 1; %h<a> = Nil; %h.raku }, " ", do { my %h; %h<a> := 5; ((try { %h<a> = 6; "no" }) // $!.^name) }
# rakudo 2026.08: X::TypeCheck::Assignment (my Int % = :a(Int)) {:a(Any)} X::AdHoc
```
rakupp 4.0.1: differs — the typed hash prints without its declaration; the bound key gives `X::Assignment::RO`.

### HM-07  :exists and :delete, and their combinations              D:yes R:yes V:spec
`:delete` returns the value (the default for a missing key); `:exists`
answers per key; `:exists:kv` and `:exists:p` list only existing keys with
True, `:!exists:kv` lists them with False; `:exists:k` is a Failure
`X::Adverb` (nogo `exists`, `k`); `:delete:exists` reports the state before
deleting; `:delete:kv`, `:delete:p`, `:delete:v` return what was deleted; an
unknown adverb throws `X::Adverb`; the zen slice takes adverbs too.
```
say do { my %h = a => 1; %h<a>:delete ~ " " ~ %h.elems }, " ", do { my %h = a => 1; (%h<b>:delete).raku }, " ", do { my %h is default(0) = a => 1; (%h<b>:delete).raku }, " ", do { my %h = a => 1, b => 2, c => 3; (%h<a c>:delete).raku ~ " " ~ %h.raku }, " ", do { my %h = a => 1, b => 2; (%h<a>:delete:exists) ~ " " ~ (%h<a>:exists) }, " ", do { my %h = a => 1; (%h<a b>:exists).raku ~ " " ~ (%h<a>:exists) ~ " " ~ (%h<a b>:!exists).raku }
# rakudo 2026.08: 1 0 Any 0 (1, 3) {:b(2)} True False (Bool::True, Bool::False) True (Bool::False, Bool::True)
say do { my %h = a => 1; (%h<a b>:exists:kv).raku ~ " " ~ (%h<a b>:exists:p).raku ~ " " ~ (%h<a b>:!exists:kv).raku }, " ", do { my %h = a => 1; my $f = %h<a b>:exists:k; $f.^name ~ ":" ~ $f.exception.^name ~ ":" ~ $f.exception.nogo.raku }, " ", do { my %h = a => 1; (%h<a>:exists:delete) ~ " " ~ %h.elems }, " ", do { my %h = a => 1, b => 2; (%h<a b>:delete:kv).raku ~ " " ~ %h.elems }, " ", do { my %h = a => 1, b => 2; (%h<a z>:delete:p).raku }, " ", do { my %h = a => 1; (%h<a>:delete:kv).raku ~ " " ~ (%h<a>:delete:v).raku }, " ", do { my %h = a => 1; (try %h<a>:foo) // $!.^name }, " ", do { my %h = a => 1; (%h{*}:k).raku ~ " " ~ (%h{*}:kv).raku ~ " " ~ (%h{*}:delete).raku ~ " " ~ %h.elems }
# rakudo 2026.08: ("a", Bool::True) (:a,) ("a", Bool::False) Failure:X::Adverb:("exists", "k").Seq True 0 ("a", 1, "b", 2) 0 (:a(1),) ("a", 1) () X::Adverb ("a",) ("a", 1) (1,) 0
```
rakupp 4.0.1: differs — `:exists:k` returns `(Bool::True, Bool::False)` instead of failing.

### HM-08  Slices                                                  D:yes R:yes V:spec
A missing key in a slice is Any; `:v`, `:kv`, `:p`, `:k` drop missing keys;
`%h{*}` is all values; `%h{}` and `%h<>` are the hash itself.
```
say do { my %h = a => 1, b => 2; %h<a b>.raku ~ " " ~ %h<a c>.raku ~ " " ~ (%h<a c>:v).raku ~ " " ~ (%h<a c>:kv).raku ~ " " ~ (%h<a c>:p).raku ~ " " ~ (%h<a c>:k).raku ~ " " ~ %h{*}.sort.raku ~ " " ~ %h{}.raku ~ " " ~ %h<>.raku }, " ", do { my %h = a => 1; (%h<a>:kv).raku ~ " " ~ (%h<a>:p).raku ~ " " ~ (%h<z>:kv).raku ~ " " ~ (%h<z>:p).raku ~ " " ~ (%h<z>:k).raku ~ " " ~ (%h<z>:v).raku }, " ", do { my %h = a => 1, b => 2; %h{%h.keys.sort}.raku ~ " " ~ %h<a b c>.elems }
# rakudo 2026.08: (1, 2) (1, Any) (1,) ("a", 1) (:a(1),) ("a",) (1, 2).Seq {:a(1), :b(2)} {:a(1), :b(2)} ("a", 1) :a(1) () () () () (1, 2) 3
```
rakupp 4.0.1: matches.

## D. push and append

### HM-09  push stacks, append flattens, a bare pair is a named argument   D:yes R:yes V:spec
`%h.push(a => 1)` passes `a => 1` as a **named argument** and pushes
nothing; write `%h.push((a => 1))`. A repeated key turns the slot into an
Array and later values are pushed onto it; `append` flattens a new
Iterable value into that array while `push` adds it as one element; an
unpaired trailing item warns "Trailing item in Hash.push"; a lazy source
fails with `X::Cannot::Lazy`; both return the hash.
```
say do { my %h; %h.push(a => 1); %h.push(a => 2); %h.raku }, " ", do { my %h; %h.push((a => 1)); %h.push((a => 2)); %h.push((a => 3)); %h.raku }, " ", do { my %h; %h.push("a", 1, "b", 2); %h.raku }, " ", do { my %h; %h.push((a => [1, 2])); %h.push((a => 3)); %h.raku }, " ", do { my %h; %h.append((a => [1, 2])); %h.append((a => [3, 4])); %h.raku }, " ", do { my %h; %h.push((a => [1, 2])); %h.push((a => [3, 4])); %h.raku }, " ", do { my %h = a => 1; %h.append((a => (2, 3))); %h.raku }, " ", do { my %h = a => 1; %h.push((a => (2, 3))); %h.raku }, " ", do { my %h; my $r := %h.push((a => 1)); ($r =:= %h) }, " ", do { my %h; %h.push((a => 1, b => 2)); %h.raku }, " ", do { my @w; CONTROL { when CX::Warn { @w.push(.message); .resume } }; my %h; %h.push("a", 1, "b"); %h.raku ~ " " ~ @w.raku }
# rakudo 2026.08: {} {:a($[1, 2, 3])} {:a(1), :b(2)} {:a($[1, 2, 3])} {:a($[1, 2, 3, 4])} {:a($[1, 2, [3, 4]])} {:a($[1, 2, 3])} {:a($[1, (2, 3)])} True {:a(1), :b(2)} {:a(1)} ["Trailing item in Hash.push"]
```
rakupp 4.0.1: matches on all of these; a lazy source is accepted where Rakudo fails.

## E. Listing, printing, comparing

### HM-10  keys, values, kv, pairs, antipairs, invert, sort          D:yes R:yes V:spec
`invert` yields one pair per element of a list value; `sort` sorts the
pairs by key; `.list` and iteration produce Pairs; `.head` is a Pair.
```
say do { my %h = b => 2, a => 1; %h.keys.sort.raku ~ " " ~ %h.values.sort.raku ~ " " ~ %h.kv.sort.raku ~ " " ~ %h.pairs.sort.raku ~ " " ~ %h.antipairs.sort.raku ~ " " ~ %h.invert.sort.raku ~ " " ~ %h.elems ~ " " ~ %h.end }, " ", do { my %h = a => (1, 2), b => 3; %h.invert.sort.raku }, " ", do { my %h = a => [1, 2]; %h.invert.sort.raku }, " ", do { my %h = b => 2, a => 1; %h.sort.raku ~ " " ~ %h.sort(*.value).raku ~ " " ~ %h.list.elems ~ " " ~ %h.List.^name ~ " " ~ %h.map(*.key).sort.raku ~ " " ~ %h.head.^name ~ " " ~ (for %h -> $p { $p.^name }).unique.raku }
# rakudo 2026.08: ("a", "b").Seq (1, 2).Seq (1, 2, "a", "b").Seq (:a(1), :b(2)).Seq (1 => "a", 2 => "b").Seq (1 => "a", 2 => "b").Seq 2 1 (1 => "a", 2 => "a", 3 => "b").Seq (1 => "a", 2 => "a").Seq (:a(1), :b(2)).Seq (:a(1), :b(2)).Seq 2 List ("a", "b").Seq Pair ("Pair",).Seq
```
rakupp 4.0.1: matches.

### HM-11  gist, Str, raku                                         D:partial R:yes V:spec
`gist` is sorted by key and capped at 100 pairs then `, ...`; `Str` is
sorted pairs joined by newlines, each `key\tvalue`; `raku` is sorted and
uses the colon-pair form for keys that are identifier-like, which
includes `a-b`, `a'b` and `é`, and quotes the rest (`"a b"`, `"1"`,
`"-a"`); an itemized hash carries `$`; a Bool value inside a hash prints in
the long form (`:a(Bool::True)`).
```
say do { my %h = b => 2, a => 1; %h.gist ~ " " ~ %h.Str.raku ~ " " ~ %h.raku ~ " " ~ %h.Bool ~ " " ~ %().Bool ~ " " ~ +%h ~ " " ~ %h.Int }, " ", do { my %h = (1..150).map({ $_ => 1 }); %h.gist.chars ~ " " ~ %h.gist.substr(*-6) }, " ", do { my %h = a => [1, 2], b => {c => 3}; %h.raku ~ " " ~ %h.gist }, " ", ${a => 1}.raku, " ", do { my %h = "a b" => 1, "1" => 2, "a-b" => 3, "-a" => 4, "a'b" => 5, "é" => 6; %h.raku }, " ", do { my %h = a => True, b => False, c => "x"; %h.raku }
# rakudo 2026.08: {a => 1, b => 2} "a\t1\nb\t2" {:a(1), :b(2)} True False 2 2 951 , ...} {:a($[1, 2]), :b(${:c(3)})} {a => [1 2], b => {c => 3}} ${:a(1)} {"-a" => 4, "1" => 2, "a b" => 1, :a'b(5), :a-b(3), :é(6)} {:a(Bool::True), :b(Bool::False), :c("x")}
```
rakupp 4.0.1: differs — the gist is not capped (1392 chars), and `a'b` and `é` are quoted.

### HM-12  Smartmatch against a hash, eqv, ===                     D:yes R:partial V:spec
A Str topic asks whether the key exists; a list topic whether any element
is a key; a Regex whether any key matches; a Hash topic is `eqv`; a Map
topic is False (different type); an undefined topic stringifies with the
uninitialized warning. `eqv` needs the same type, keys and values; `===`
is identity.
```
say do { my @w; CONTROL { when CX::Warn { @w.push(1); .resume } }; my %h = b => 2, a => 1; ("a" ~~ %h) ~ " " ~ ("z" ~~ %h) ~ " " ~ (<a z> ~~ %h) ~ " " ~ (<x z> ~~ %h) ~ " " ~ (/^a/ ~~ %h) ~ " " ~ (/^z/ ~~ %h) ~ " " ~ (%(a => 1, b => 2) ~~ %h) ~ " " ~ (%(a => 1) ~~ %h) ~ " " ~ (1 ~~ %h) ~ " " ~ (Any ~~ %h) ~ " " ~ (Map.new((a => 1, b => 2)) ~~ %h) ~ " w=" ~ @w.elems }, " ", do { my %h = a => 1; (%h eqv %(a => 1)) ~ " " ~ (%h eqv %(a => 1, b => 2)) ~ " " ~ (%h eqv Map.new((a => 1))) ~ " " ~ (%() eqv %()) ~ " " ~ (%h === %h) ~ " " ~ (%(a => 1) === %(a => 1)) ~ " " ~ (%h eqv %(a => 1.0)) ~ " " ~ (%h eqv :{ a => 1 }) }
# rakudo 2026.08: True False True False True False True False False False False w=1 True False False True True False False False
```
rakupp 4.0.1: differs — a Hash topic with equal contents is False, and no warning for the undefined topic.

## F. Map

### HM-13  Map construction and printing                           D:yes R:yes V:spec
`Map.new` takes a list of pairs or alternating items (odd is
`X::Hash::Store::OddNumber`); with only named arguments it uses them as
pairs; the empty Map prints as `Map.new`; `.raku` has no space after the
comma between pairs; `.Hash` gives a Hash, `.Map` of a Hash a Map.
```
say Map.new((a => 1, b => 2)).raku, " ", Map.new(a => 1).raku, " ", Map.new("a", 1).raku, " ", (try Map.new(1)) // $!.^name, " ", Map.new.raku, " ", Map.new((a => 1)).gist, " ", Map.new((a => 1)).Str.raku, " ", Map.new((a => 1)).elems, " ", Map.new(%(a => 1)).raku, " ", Map.new((a => 1)).Hash.raku, " ", Map.new((a => 1)).Map.^name, " ", %(a => 1).Map.raku, " ", $(Map.new((a => 1))).raku
# rakudo 2026.08: Map.new((:a(1),:b(2))) Map.new((:a(1))) Map.new((:a(1))) X::Hash::Store::OddNumber Map.new Map.new((a => 1)) "a\t1" 1 Map.new((:a(1))) {:a(1)} Map Map.new((:a(1))) $(Map.new((:a(1))))
```
rakupp 4.0.1: differs — the empty Map prints as `Map.new(())`, the itemized one without `$`.

### HM-14  Map immutability, and what stays live                   D:partial R:partial V:quirk
Assigning to an existing key dies ("Cannot change key 'a' in an immutable
Map"), to a new key too ("Cannot add key"), and `:delete` and binding die;
**but a value that is a container stays assignable** through the Map.
`%h.Map` snapshots the values; `Map.new(%h)` keeps the Hash's containers,
so a later change to `%h<a>` shows through it, while new keys do not. A
missing key reads as Nil, not Any; a Map has no `.default`; `.clone`
returns the same object.
```
say do { my $m = Map.new((a => 1)); (try { $m<a> = 2; "no" }) // $!.^name ~ ":" ~ $!.message }, " | ", do { my $m = Map.new((a => 1)); (try { $m<b> = 2; "no" }) // $!.message }, " | ", do { my $x = 1; my $m = Map.new((a => $x)); $m<a> = 5; $m<a> ~ " " ~ $x }, " ", do { my $m = Map.new((a => 1)); (try { $m<a>:delete; "no" }) // $!.^name }, " ", do { my %h = a => 1; my $m = %h.Map; %h<a> = 2; $m<a> }, " ", do { my %h = a => 1; my $m = Map.new(%h); %h<a> = 2; $m<a> }, " ", do { my %h = a => 1; my $m = Map.new(%h); %h<b> = 2; $m.elems }, " ", do { my $m = Map.new((a => 1)); ($m<z>).raku ~ " " ~ ($m<z>:exists) ~ " " ~ $m<a z>.raku }, " ", do { my %h := Map.new((a => 1)); (try { %h = (b => 1); "no" }) // $!.^name }, " ", do { my $m = Map.new((a => 1)); (try { $m<a> := 2; "no" }) // $!.^name }, " ", (try Map.new((a => 1)).default) // $!.^name
# rakudo 2026.08: X::AdHoc:Cannot change key 'a' in an immutable Map | Cannot add key 'b' to an immutable Map | 5 5 X::AdHoc 1 2 1 Nil False (1, Nil) X::Assignment::RO X::Bind X::Method::NotFound
say do { my $m = Map.new((b => 2, a => 1)); $m.sort.raku ~ " " ~ $m.pairs.sort.raku ~ " " ~ $m.antipairs.sort.raku ~ " " ~ $m.kv.sort.raku ~ " " ~ $m.invert.sort.raku ~ " " ~ $m.list.elems ~ " " ~ $m.head.^name ~ " " ~ $m.Bool ~ " " ~ +$m ~ " " ~ $m.clone.WHICH eq $m.WHICH }, " ", do { my $m = Map.new((a => 1, b => 2)); ("a" ~~ $m) ~ " " ~ ($m ~~ Map.new((a => 1, b => 2))) ~ " " ~ ($m eqv Map.new((a => 1, b => 2))) ~ " " ~ ($m eqv %(a => 1, b => 2)) ~ " " ~ ($m<a b>.raku) ~ " " ~ ($m<a c>.raku) }, " ", do { my @w; CONTROL { when CX::Warn { @w.push(1); .resume } }; Map.new((a => 1)).contains("a") ~ " " ~ @w.elems }
# rakudo 2026.08: (:a(1), :b(2)).Seq (:a(1), :b(2)).Seq (1 => "a", 2 => "b").Seq (1, 2, "a", "b").Seq (1 => "a", 2 => "b").Seq 2 Pair True 2 True True True True False (1, 2) (1, Nil) True 1
```
The container-stays-live behaviour of `Map.new(%h)` is the quirk. `.contains` and `.index` on a Map work on its Str form and warn.
rakupp 4.0.1: differs — a Map is fully mutable, `Map.new(%h)` snapshots, a missing key is Any, `.clone` is a new object, `.default` is Nil, a Map does not smartmatch an equal Map, and `.contains` does not warn.

### HM-15  Coercions and formatting of a hash                       D:yes R:partial V:spec
Set, Bag and Mix use the values as weights: Set keeps truthy values, Bag
positive ones, Mix any non-zero including negatives. `.Capture` turns the
hash into named arguments; `.Supply` emits pairs; `.roll`/`.pick` return a
Pair; `.fmt` formats the pairs, or only the keys when the format has one
directive.
```
say do { my %h = a => 2, b => 1; %h.Bag.raku ~ " " ~ %h.Mix.raku }, " ", do { my %h = a => 0, b => 1; %h.Set.raku ~ " " ~ %h.Bag.raku }, " ", do { my %h = a => -1, b => 2; %h.Mix.raku ~ " " ~ %h.Bag.raku }, " ", do { my %h = a => 1; %h.Capture.raku ~ " " ~ %h.Supply.list.raku ~ " " ~ %h.roll.^name ~ " " ~ %h.pick.^name ~ " " ~ %h.roll(2).elems ~ " " ~ %().roll.raku ~ " " ~ %().pick.raku }, " ", do { my %h = a => 1, b => 2; %h.fmt.raku ~ " " ~ %h.fmt("%s=%s", ",").raku ~ " " ~ %h.fmt("%s").raku }
# rakudo 2026.08: ("a"=>2,"b"=>1).Bag ("a"=>2,"b"=>1).Mix Set.new("b") ("b"=>1).Bag ("b"=>2,"a"=>-1).Mix ("b"=>2).Bag \(:a(1)) (:a(1),) Pair Pair 2 Nil Nil "a\t1\nb\t2" "a=1,b=2" "a\nb"
```
rakupp 4.0.1: differs — `.Bag` keeps a negative weight; `.fmt` gives `"a b"`.

## G. Pair

### HM-16  Pair raku, gist, Str                                    D:partial R:yes V:spec
`.raku` uses the colon form when the key is a Str that looks like an
identifier (letters, digits, `-` or `'` between word characters, Unicode
letters), otherwise `"key" => value`; a True or False value with such a
key prints as `:a` / `:!a`; a Pair key is parenthesised; a type-object or
Nil key prints by name (`:Int(1)`, `:Nil(1)`); `:arglist` forces the arrow
form. `.gist` is `a => 1`, `.Str` is `a\t1`.
```
say (a => 1).raku, " ", (a => 1).gist, " ", (a => 1).Str.raku, " ", ("a b" => 1).raku, " ", ("1" => 1).raku, " ", (1 => "a").raku, " ", ("a-b" => 1).raku, " ", ("-a" => 1).raku, " ", ("a-" => 1).raku, " ", ("a'b" => 1).raku, " ", ("" => 1).raku, " ", (a => True).raku, " ", (a => False).raku, " ", ("a b" => True).raku, " ", ((a => 1) => 2).raku, " ", ((a => 1) => 2).gist, " ", (Int => 1).raku, " ", (Any => 1).raku, " ", (Nil => 1).raku, " ", (NaN => 1).raku, " ", (1.5 => 1).raku, " ", ("é" => 1).raku, " ", ("a" => 1).raku(:arglist)
# rakudo 2026.08: :a(1) a => 1 "a\t1" "a b" => 1 "1" => 1 1 => "a" :a-b(1) "-a" => 1 "a-" => 1 :a'b(1) "" => 1 :a :!a "a b" => Bool::True (:a(1)) => 2 (a => 1) => 2 :Int(1) :Any(1) :Nil(1) :NaN(1) 1.5 => 1 :é(1) "a" => 1
```
Note `Int => 1` is the pair with the **string** key `"Int"`: a bareword before `=>` is quoted.
rakupp 4.0.1: differs — `:a-(1)`, `"a'b" => 1`, `"é" => 1`, `:a(Bool::True)`, and `:arglist` is ignored.

### HM-17  Pair as a one-element Associative                        D:yes R:yes V:spec
```
say (a => 1).key, " ", (a => 1).value, " ", (a => 1).kv.raku, " ", (a => 1).keys.raku, " ", (a => 1).values.raku, " ", (a => 1).pairs.raku, " ", (a => 1).antipair.raku, " ", (a => 1).antipairs.raku, " ", (a => 1).invert.raku, " ", (a => (1, 2)).invert.raku, " ", (a => 1).elems, " ", (a => 1).list.raku, " ", (a => 1)<a>, " ", (a => 1)<b>.raku, " ", ((a => 1)<a>:exists), " ", (a => 1).Capture.raku, " ", (a => 1).fmt.raku, " ", (a => 1).fmt("%s:%s").raku, " ", Pair.new("a", 1).raku, " ", Pair.new(key => "a", value => 1).raku, " ", pair("a", 1).raku, " ", (a => 1).Str.raku, " ", (a => 1).Bool, " ", (a => 0).Bool, " ", (a => 1).iterator.pull-one.raku, " ", ("a" ⇒ 1).raku
# rakudo 2026.08: a 1 ("a", 1).Seq ("a",).Seq (1,).Seq (:a(1),).Seq 1 => "a" (1 => "a",).Seq (1 => "a",).Seq (1 => "a", 2 => "a").Seq 1 (:a(1),) 1 Nil True \(:key("a"), :value(1)) "a\t1" "a:1" :a(1) :a(1) :a(1) "a\t1" True True :a(1) :a(1)
```
A Pair is always true, even with a false value; a missing key reads as Nil.
rakupp 4.0.1: differs — there is no `pair` sub and `⇒` does not parse.

### HM-18  A Pair's value is whatever was passed                     D:partial R:partial V:spec
`a => $x` keeps `$x`'s container: assigning through `.value` changes `$x`
and vice versa. A literal value is read-only (`X::Assignment::RO`), and so
is the key. The Pairs that `%h.pairs` yields alias the hash's containers.
`.clone` of a literal Pair stays read-only.
```
say do { my $x = 1; my $p = a => $x; ((try { $p.value = 5; "assigned" }) // $!.^name) ~ " " ~ $p.value ~ " " ~ $x }, " ", do { my $x = 1; my $p = a => $x; $x = 7; $p.value }, " ", do { my $p = a => 1; ((try { $p.value = 5; $p.value }) // $!.^name) }, " ", do { my $p = a => 1; ((try { $p.key = "b"; "no" }) // $!.^name) }, " ", do { my $x = 1; my $p = Pair.new("a", $x); ((try { $p.value = 5; "assigned" }) // $!.^name) ~ " " ~ $x }, " ", do { my $p = (a => 1); my $q = $p.clone; ((try { $q.value = 2 }) // "ro") ~ " " ~ $p.value }, " ", do { my %h = a => 1; my $p = %h.pairs[0]; $p.value = 9; %h<a> }, " ", do { my @a = (a => 1); my $p = @a[0]; ((try { $p.value = 2; "ok" }) // $!.^name) }, " ", do { my $p = :a(1); ((try { $p.value = 2; "ok" }) // $!.^name) }, " ", do { my $v = 1; my $p = :a($v); ((try { $p.value = 2; "ok" }) // $!.^name) ~ " " ~ $v }
# rakudo 2026.08: assigned 5 5 7 X::Assignment::RO X::Assignment::RO assigned 5 ro 1 9 X::Assignment::RO X::Assignment::RO ok 2
```
rakupp 4.0.1: differs — every Pair gets a fresh container: a literal value is assignable, and a variable is copied, not aliased.

### HM-19  Pair identity, eqv, cmp                                  D:yes R:yes V:spec
`===` is by value only when the value is a value type and not a container
(`WHICH` is then a `ValueObjAt`); a Pair holding a variable or an Array
compares by object identity. `eqv` is structural, `cmp` compares keys then
values, `.unique` follows `===`.
```
say ((a => 1) === (a => 1)), " ", ((a => 1) eqv (a => 1)), " ", ((a => 1) eqv (a => 1.0)), " ", ((a => 1) eqv (a => 2)), " ", do { my $x = 1; ((a => $x) === (a => $x)) ~ " " ~ ((a => $x) eqv (a => 1)) }, " ", ((a => 1) cmp (a => 2)), " ", ((a => 1) cmp (b => 0)), " ", ((a => 1) cmp (a => 1)), " ", ((a => [1]) === (a => [1])), " ", (a => 1).WHICH.^name, " ", (a => [1]).WHICH.^name, " ", ((a => 1), (a => 1)).unique.elems, " ", ((a => [1]), (a => [1])).unique.elems
# rakudo 2026.08: True True False False False True Less Less Same False ValueObjAt ObjAt 1 2
```
rakupp 4.0.1: differs — a container-valued Pair compares equal by `===`, `WHICH` is a Str, `.unique` merges the array-valued pairs.

### HM-20  Pair smartmatch                                          D:yes R:yes V:spec
With the Pair on the right: against another Pair, key and value must both
`ACCEPTS`; against any other object, the key names a **method** called on
the topic and the Bools of the result and the value are compared, so
`"abc" ~~ (chars => 3)` is True because both are truthy; an unknown method
throws `X::Method::NotFound`. With the Pair on the left of a Hash, the
Pair's Str is looked up as a key.
```
say ((a => 1) ~~ %(a => 1)), " ", ((a => 1) ~~ %(a => 2)), " ", ((a => 1) ~~ %(b => 1)), " ", ((a => 1) ~~ (a => 1)), " ", ((a => 1) ~~ (a => 2)), " ", ((a => Int) ~~ (a => 1)), " ", ((a => 1) ~~ (a => Int)), " ", ((a => 1) ~~ (Str => 1)), " ", (42 ~~ (is-prime => False)), " ", (try 42 ~~ (:even)) // $!.^name, " ", (try 42 ~~ (:frobnicate)) // $!.^name, " ", ("abc" ~~ (chars => 3)), " ", ("abc" ~~ (:chars)), " ", ((:a) ~~ Pair), " ", ((1, 2) ~~ (elems => 2)), " ", ((a => 1) ~~ Associative), " ", ("" ~~ (:!chars)), " ", (42 ~~ (:is-prime)), " ", (7 ~~ (:is-prime))
# rakudo 2026.08: False False False True False False True False True X::Method::NotFound X::Method::NotFound True True True True True True False True
```
rakupp 4.0.1: differs — `(a => 1) ~~ (a => Int)` is False, an unknown method gives False instead of throwing, `(1, 2) ~~ (elems => 2)` is False.

## Counts

| | items |
|---|---|
| total | 20 |
| not fully stated by docs (D:yes) nor asserted by Roast (R:yes) | 2 |
| Rakudo bugs (do not imitate) | 0 |
| quirks (recorded, step two decides) | 1 — HM-14 `Map.new(%h)` keeps live containers |
| rakupp 4.0.1 differs | 16 |
| rakupp 4.0.1 matches | 3 |

Recurring rakupp gaps, for step two: silent acceptance of odd-count and
Callable-only hash stores; no key-type enforcement on object hashes; a
mutable Map with Any for a missing key; Pair values always copied into a
fresh container; `.raku` key quoting; the uncapped gist.

## Method (how this sheet was produced)

As for [Supply.md](Supply.md). Two probe rounds. Two traps worth naming:
`%h.push(a => 1)` silently passes a named argument, and `Int => 1` is the
pair with the string key `"Int"`. D flags from `doc/Type/{Hash,Map,Pair}.rakudoc`
and `doc/Language/{hashmap,subscripts}.rakudoc`; R flags from `S32-hash/*.t`,
`S02-types/{hash,hash_ref,pair,nested_pairs,autovivification}.t`,
`S09-hashes/objecthash.t`.
