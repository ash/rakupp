# List, Array, Slip, Seq — semantics sheet

Provenance: Rakudo tag `2026.08`, files `src/core.c/List.rakumod` (1,637
lines), `Array.rakumod` (1,653), `Seq.rakumod` (224), `Slip.rakumod` (131),
read in full on 2026-09-18. The iterator classes behind them
(`Rakudo/Iterator.rakumod`) were not read: they are mechanism. Oracle:
Homebrew Rakudo v2026.08. Extracted against Raku++ 4.0.1-23-g80b16d18
(build-arm64, 2026-09-18); **implemented 2026-09-19** against
4.0.1-50-g4cbe020, and the per-item lines below say where that left each
one. Format and legend: [README.md](README.md).

Where this sits against the declared spec: Rakudo passes 20 of 20
`S32-array` files and 50 of 50 `S32-list` files here; Raku++ passed 6 and
23 when the sheet was written, 10 and 26 at the commit the implementing
session branched from, and 10 and 27 after it. The file count is the
coarse measure: what this sitting actually moved is `S09-typed-arrays`,
from 605 of 4,261 declared assertions to 3,693 — one fault, the
`@arr.map(* *= 2)` of LA-08, was killing five of its ten files partway
through. The rules below are the ones those files, and real programs, keep
tripping over: the
single-argument rule at every entry point, what a hole is, which errors are
Failures and which are thrown, what a Seq allows after it has been
iterated, and where `flat` stops. 4 of the 36 items are neither fully
documented nor fully asserted by Roast.

The implementing session ran every probe here through BOTH engines rather
than reading the recorded output alone, and the 2026.08 binary disagreed
with the sheet three times: LA-16's lazy `:delete` probe and LA-22's lazy
`unshift` probe each lost a `.Seq` in transcription, and LA-24's last case
records `X::Cannot::Lazy` for a `splice(0, 1)` on a lazy array that the
binary accepts (`[1]`). The binary won all three.
`t/regression/list-array-semantics-sheet.raku` is the gate that came out of
this sitting; it passes under Rakudo as well, so it is a parity test, not a
snapshot of us.

## A. Construction, printing, identity

### LA-01  raku and gist forms                                     D:yes R:yes V:spec
A one-element list prints with a trailing comma; an Array whose single
element is Iterable does too; an itemized list or array carries a `$`.
```
say ().raku, " ", (1,).raku, " ", (1, 2).raku, " ", $(1, 2).raku, " ", $().raku, " ", [].raku, " ", [1].raku, " ", [[1, 2],].raku, " ", [(1, 2),].raku, " ", $[1, 2].raku, " ", [1, 2].gist, " ", (1, 2).gist, " ", ((1, 2), [3]).gist, " ", (1, (2, 3)).raku
# rakudo 2026.08: () (1,) (1, 2) $(1, 2) $( ) [] [1] [[1, 2],] [(1, 2),] $[1, 2] [1 2] (1 2) ((1 2) [3]) (1, (2, 3))
```
rakupp 4.0.1-50: matches.

### LA-02  gist caps at 100 elements; lazy lists print as (...)    D:no R:no V:spec
`.gist` shows 100 elements then `...`. `.raku` of a lazy list shows up to
100 reified elements and ends `...).lazy`; a Range stays `1..Inf`. A lazy
Seq or List gists as `(...)`, a lazy Array as `[...]` whatever has been
reified, and stringifies as `...`.
```
say (1..200).list.gist.chars, " ", (1..200).list.gist.substr(*-8), " ", (1..200).list.raku.chars, " ", (1..*).list.raku.substr(*-12), " ", (1..*).raku, " ", (1..*).list.raku.chars
# rakudo 2026.08: 297 100 ...) 892 100...).lazy 1..Inf 400
say (1..*).map({ $_ }).gist, " ", (1..*).list.gist, " ", do { my @a = 1..*; @a[1]; @a.gist }, " ", (1..*).map({ $_ }).Str.raku
# rakudo 2026.08: (...) (...) [...] "..."
```
rakupp 4.0.1-50: matches.

### LA-03  Bool, Int, Str, end of a List                           D:yes R:yes V:spec
```
say ().Bool, " ", (1,).Bool, " ", ().elems, " ", ().end, " ", (1, 2).end, " ", +(1, 2), " ", (1, 2).Int, " ", (1, 2).Str.raku, " ", ("a", (1, 2)).Str.raku, " ", ().Str.raku, " ", (1, 2).Numeric, " ", [].Bool, " ", [1].so
# rakudo 2026.08: False True 0 -1 1 2 2 "1 2" "a 1 2" "" 2 False True
```
rakupp 4.0.1-50: matches.

### LA-04  The single-argument rule at construction                D:yes R:yes V:spec
One Iterable argument is the list; several arguments are each an element,
lists included. Anything in a Scalar container (`$x`, `$(…)`) is one
element. A Set contributes its pairs, a Hash its pairs, a Str is one
element. `cache(1, 2)` returns an Array.
```
say Array.new(1, 2).raku, " ", Array.new((1, 2)).raku, " ", Array.new([1, 2]).raku, " ", Array.new((1, 2), (3, 4)).raku, " ", Array.new($(1, 2)).raku, " ", Array.new.raku, " ", Array.new(1..*).is-lazy
# rakudo 2026.08: [1, 2] [1, 2] [1, 2] [(1, 2), (3, 4)] [(1, 2),] [] True
say do { my @a = 1, (2, 3); @a.raku }, " ", do { my @a = (1, 2), 3; @a.raku }, " ", do { my @a = [1, 2], 3; @a.raku }, " ", do { my $x = (1, 2); my @a = $x; @a.raku }, " ", do { my @a = $(1, 2); @a.raku }, " ", do { my @a = (1, 2); @a.raku }, " ", do { my @a = set(1); @a.raku }, " ", do { my @a = {a => 1}; @a.raku }, " ", do { my @a = (1, 2).Seq; @a.raku }, " ", do { my @a = "abc"; @a.raku }, " ", do { my @a = Empty; @a.raku }, " ", do { my $s = (1, 2).Seq; my @a = $s; @a.raku }
# rakudo 2026.08: [1, (2, 3)] [(1, 2), 3] [[1, 2], 3] [(1, 2),] [(1, 2),] [1, 2] [1 => Bool::True] [:a(1)] [1, 2] ["abc"] [] [(1, 2).Seq,]
say list(1, 2).raku, " ", list((1, 2)).raku, " ", list([1, 2]).raku, " ", list(1).raku, " ", list().raku, " ", cache(1, 2).raku, " ", cache((1..*)).^name
# rakudo 2026.08: (1, 2) (1, 2) (1, 2) (1,) () [1, 2] Array
```
Note the last case of the second probe: a Seq held in a scalar is one
element. rakupp 4.0.1: differs — `Array.new((1, 2), (3, 4))` flattens to `[1, 2, 3, 4]`, `Array.new($(1, 2))` to `[1, 2]`; `@a = set(1)` and `@a = {a => 1}` print as Lists (`(1 => Bool::True,)`); `cache(1, 2)` is a List.

### LA-05  Comma and Slip flattening                               D:yes R:yes V:spec
A Slip dissolves into the surrounding list; `Empty` contributes nothing;
prefix `|` slips even an itemized list; a lone slip in parentheses stays a Slip.
```
say (1, |(2, 3), 4).raku, " ", (1, (2, 3).Slip).raku, " ", (1, slip(2, 3)).raku, " ", (|(1, 2), |(3, 4)).raku, " ", (1, Empty, 2).raku, " ", (Empty,).raku, " ", (Empty, Empty).raku, " ", (1, |[2, 3]).raku, " ", (1, |$(2, 3)).raku, " ", (|(1, 2)).raku, " ", (1, |()).raku, " ", slip(1, 2).raku, " ", slip().raku, " ", slip((1, 2)).raku, " ", |(1, 2)
# rakudo 2026.08: (1, 2, 3, 4) (1, 2, 3) (1, 2, 3) (1, 2, 3, 4) (1, 2) () () (1, 2, 3) (1, 2, 3) slip(1, 2) (1,) slip(1, 2) Empty slip(1, 2) 12
```
rakupp 4.0.1-50: matches.

### LA-06  Slip and Empty                                          D:partial R:? V:quirk
`Empty` is a Slip that is **not defined** (its definedness is its Bool);
`slip()` is `Empty` itself; list methods on `Empty` short-circuit to `Empty`,
Nil or `""`.
```
say Empty.raku, " ", Empty.defined, " ", slip(1).defined, " ", Empty.Bool, " ", Empty.elems, " ", (Empty === Empty), " ", (slip() === Empty), " ", Empty.List.raku, " ", slip(1, 2).List.raku, " ", $(slip(1, 2)).raku, " ", Empty.map({ $_ }).raku, " ", Empty.head.raku, " ", Empty.join("-").raku, " ", Empty.are.raku, " ", slip(1, 2).Slip.raku, " ", slip(1, 2).WHAT.raku, " ", slip(1, 2) ~~ List
# rakudo 2026.08: Empty False True False 0 True True () (1, 2) $(slip(1, 2)) Empty Nil "" Nil slip(1, 2) Slip True
```
rakupp 4.0.1-50: matches.

### LA-07  A List is immutable, its containers are not             D:yes R:yes V:spec
Assigning into a List element fails with `X::Assignment::RO` unless that
element is a container (then the assignment goes through). Binding into a
List is `X::Bind`; push, pop, shift, unshift, append, prepend are
`X::Immutable` (`.method` names the call); `splice` has no List candidate.
```
say (try { my $l = (1, 2); $l[0] = 5; $l.raku }) // $!.^name, " ", do { my $x = 1; my $l = ($x, 2); $l[0] = 5; $l.raku ~ " " ~ $x }, " ", (try { (1, 2)[0] := 5; "no" }) // $!.^name, " ", (try (1, 2).push(3)) // $!.^name ~ ":" ~ $!.method, " ", (try (1, 2).pop) // $!.^name, " ", (try (1, 2).shift) // $!.^name, " ", (try (1, 2).unshift(0)) // $!.^name, " ", (try (1, 2).splice(0, 1)) // $!.^name
# rakudo 2026.08: X::Assignment::RO $(5, 2) 5 X::Bind X::Immutable:push X::Immutable X::Immutable X::Immutable X::Multi::NoMatch
```
rakupp 4.0.1-50: differs — the first assignment throws `X::Assignment::RO` past the `try`, and `(1, 2)[0] := 5` is `X::Assignment::RO` where Rakudo says `X::Bind`. The rest matches. Both need list elements to be containers (see LA-08/LA-09).

### LA-08  Iterating an Array yields its containers                D:yes R:yes V:spec
`for @a { $_++ }` and `.map({ $_ = 5 })` modify the array; `.values` yields
the same containers. A List of values is read-only (`for (1, 2) { $_++ }`
is `X::Multi::NoMatch`), so is `@a.List` (a copy of values), so is a
pointy-block parameter unless declared `<->`.
```
say do { my @a = 1, 2; for @a { $_++ }; @a.raku }, " ", do { my @a = 1, 2; @a.map({ $_ = 5 }); @a.raku }, " ", (try { for (1, 2) { $_++ }; "no" }) // $!.^name, " ", do { my @a = 1, 2; for @a.List { (try $_++) // "ro" }; @a.raku }, " ", do { my @a = 1, 2; for @a <-> $x { $x++ }; @a.raku }, " ", do { my @a = 1, 2; for @a -> $x { (try $x++) // "ro" }; @a.raku }, " ", do { my @a = 1, 2; for @a.values { (try { $_ = 5; "w" }) // "ro" }; @a.raku }
# rakudo 2026.08: [2, 3] [5, 5] X::Multi::NoMatch [1, 2] [2, 3] [1, 2] [5, 5]
```
rakupp 4.0.1-88: matches — `++` on a readonly is `X::Multi::NoMatch`, the multi-dispatch failure Rakudo reports (not the `X::Assignment::RO` that a plain `=` raises).

### LA-09  Binding to and from array elements                      D:yes R:yes V:spec
`@a[0] := @a[1]` aliases two slots. `my $x := @a[0]` aliases the container.
`my ($x, $y) := @a` binds the **values**, read-only. A Seq cannot be bound
to an `@` variable (`X::TypeCheck::Binding`: not Positional), nor can a plain
value; a List can.
```
say do { my @a = 1, 2; @a[0] := @a[1]; @a[1] = 7; @a[0] }, " ", do { my @a = 1, 2; my $x := @a[0]; $x = 9; @a[0] }, " ", do { my @a = 1, 2; my ($x, $y) := @a; ((try { $x = 9; "w" }) // $!.^name) ~ " " ~ @a[0] }, " ", (try { my @a = 1, 2; my @b := @a.map({ $_ }); "bound" }) // $!.^name, " ", do { my @a := (1, 2); @a.^name }, " ", (try { my @a := 42; "bound" }) // $!.^name
# rakudo 2026.08: 7 9 X::AdHoc 1 X::TypeCheck::Binding List X::TypeCheck::Binding
```
rakupp 4.0.1-50: differs — unchanged. `@a[0] := @a[1]` does not alias, the list binding is writable, and a Seq and `42` bind. This is the container/binding layer, not a local fix.

### LA-10  Array, List and Slip conversions copy                   D:partial R:? V:spec
`.Array` gives fresh containers; `.List` of an Array is a snapshot of
values, so later writes to either side are invisible to the other; `.List`
of a lazy array throws `X::Cannot::Lazy`; `.eager` returns the list itself
and on an infinite list never returns.
```
say (1, 2).eager.raku, " ", (1, 2).Slip.raku, " ", (1, 2).Array.raku, " ", (1, 2).List.raku, " ", do { my $l = (1, 2); $l.Array[0] = 9; $l.raku }, " ", do { my @a = 1, 2; my @b = @a.List; @b[0] = 9; @a.raku }, " ", do { my @a = 1, 2; my $l := @a.List; @a[0] = 9; $l.raku }, " ", (try { my @c = 1..*; @c.List; "no" }) // $!.^name, " ", (1..*).list.head(3).eager.raku
# rakudo 2026.08: (1, 2) slip(1, 2) [1, 2] (1, 2) $(1, 2) [1, 2] (1, 2) X::Cannot::Lazy (1, 2, 3)
```
rakupp 4.0.1-50: differs — `$l.Array[0] = 9` still dies "Target is not assignable" (`.Array` of a List yields no containers); `.eager` and the lazy `.List` refusal match.

### LA-11  Capture of a list                                       D:yes R:? V:spec
Pairs become named arguments; a Pair's own Capture is its key and value; a
lazy list gives a Failure `X::Cannot::Lazy`.
```
say (1, 2).Capture.raku, " ", (1, a => 2).Capture.raku, " ", (a => 1).Capture.raku, " ", (try (1..*).list.Capture) // $!.^name, " ", (1..*).list.Capture.^name, " ", ().Capture.raku
# rakudo 2026.08: \(1, 2) \(1, :a(2)) \(:key("a"), :value(1)) X::Cannot::Lazy Failure \()
```
rakupp 4.0.1-50: matches.

### LA-12  fmt                                                     D:yes R:? V:spec
A nested list is formatted recursively and joined with the same separator,
so it flattens; a format with two directives dies (`X::AdHoc`) because each
element is formatted alone; `%%` is a literal percent.
```
say ("a", "b").fmt.raku, " ", (1, 2).fmt("%02d").raku, " ", (1, 2).fmt("%d", "-").raku, " ", (1, (2, 3)).fmt("<%s>", ",").raku, " ", (1, 2).fmt("%s").raku, " ", ().fmt("%d").raku, " ", (try (1, 2).fmt("%d and %d")) // $!.^name, " ", [1, 2].fmt("%02d", ":").raku, " ", (1, 2).fmt("%d%%").raku
# rakudo 2026.08: "a b" "01 02" "1-2" "<1>,<2>,<3>" "1 2" "" X::AdHoc "01:02" "1\% 2\%"
```
rakupp 4.0.1-50: matches.

### LA-13  sum                                                     D:yes R:yes V:quirk
A nested list numifies to its element count (`(1, (2, 3)).sum` is 3); a
Rat sum prints as `5.0`; a lazy list gives a Failure `X::Cannot::Lazy`.
```
say (1, 2, 3).sum, " ", ().sum, " ", (1, 2.5, 3/2).sum.raku, " ", (1, (2, 3)).sum, " ", (1..*).list.sum.^name
# rakudo 2026.08: 6 0 5.0 3 Failure
```
rakupp 4.0.1-50: matches.

### LA-14  join                                                    D:yes R:yes V:spec
Nested lists stringify with spaces; a type object stringifies to `""` with
the uninitialized warning; a hole stringifies to the default's Str (`""`
for Any, `"0"` for `is default(0)`); a Junction element autothreads the
whole join; the separator is coerced to Str; a lazy list joins what is
reified and appends `...`.
```
say do { my @a = 1..*; @a[2]; @a.join(",") }, " ", (1..*).list.join(",").raku, " ", do { my @w; CONTROL { when CX::Warn { @w.push(1); .resume } }; ((1, Int, 3).join(",").raku, do { my @a; @a[2] = 3; @a.join(",").raku }, do { my Int @a is default(0); @a[2] = 3; @a.join(",").raku }, (1, any(2, 3)).join("-").raku, (1, (2, (3, 4))).join(",").raku, ([1, 2], 3).join("-").raku, (1, 2).join(3).raku).join(" ") ~ " w=" ~ @w.elems }
# rakudo 2026.08: 1,2,3,... "..." "1,,3" ",,3" "0,0,3" any("1-2", "1-3") "1,2 3 4" "1 2-3" "132" w=2
```
rakupp 4.0.1-50: differs only in the junction case — `(1, any(2, 3)).join("-")` collapses instead of autothreading, and the warning count is 3 rather than 2. The lazy prefix, the holes and the typed default all match.

## B. Indexing

### LA-15  Indices: negative, beyond the end, fractional, type objects   D:yes R:yes V:spec
A literal negative index is a **compile-time** error. A negative index at
run time gives a Failure `X::OutOfRange` (range `0..^Inf`); `:exists` on it
is False; assigning or binding to it throws `X::OutOfRange`. Beyond the end a
List gives Nil and an Array gives Any without growing (elems unchanged,
`:exists` False); assigning beyond the end grows the array with holes. A
fractional index truncates; a Str index numifies; a type object as index
throws `X::AdHoc`.
```
say do { my $i = -1; my $f = (1, 2)[$i]; $f.^name ~ ":" ~ $f.exception.^name ~ ":" ~ $f.exception.range }, " ", do { my $i = -1; ((1, 2)[$i]:exists) }, " ", (1, 2)[2].raku, " ", (1, 2)[1.7], " ", (1, 2)[*-1], " ", (1, 2)[*-5].^name, " ", (1, 2)[0, 1].raku, " ", (1, 2)[1..*].raku, " ", (1, 2)[*].raku, " ", (1, 2)[^1].raku, " ", (1, 2, 3)[1..*-1].raku, " ", (1, 2)["1"], " ", (try (1, 2)[Int]) // $!.^name
# rakudo 2026.08: Failure:X::OutOfRange:0..^Inf False Nil 2 2 Failure (1, 2) (2,) (1, 2) (1,) (2, 3) 2 X::AdHoc
say do { my @a = 1, 2; @a[5].raku ~ " " ~ @a.elems ~ " " ~ (@a[5]:exists) }, " ", do { my @a = 1, 2; @a[5] = 1; @a.elems ~ " " ~ @a.raku }, " ", do { my @a = 1, 2; my $i = -1; ((try { @a[$i] = 5; "no" }) // $!.^name) }, " ", do { my @a = 1, 2; my $i = -1; ((try { @a[$i] := 5; "no" }) // $!.^name) }, " ", do { my @a = 1, 2; my $i = -1; (@a[$i]:delete).^name }, " ", do { my @a = 1, 2; @a[2.9].raku ~ " " ~ @a.elems }, " ", do { my @a = 1, 2; (try @a[Int]) // $!.^name }
# rakudo 2026.08: Any 2 False 6 [1, 2, Any, Any, Any, 1] X::OutOfRange X::OutOfRange Failure Any 2 X::AdHoc
```
Both engines reject `(1, 2)[-1]` at compile time. rakupp 4.0.1: differs — `@a[Int]` yields 1, a negative `:delete` yields Any, and its `X::OutOfRange` has no `.range`.

### LA-16  Holes and :delete                                       D:yes R:yes V:spec
Deleting an element returns its value and leaves a hole: elems unchanged,
`:exists` False, the slot reads as the default. Deleting the **last**
element shrinks the array, and the shrink continues past earlier holes.
Deleting beyond the end returns the default and changes nothing. A typed
array with `is default(0)` reads and prints the hole as 0. On a lazy array
the delete reifies up to that index. Assigning into a hole fills it.
```
say do { my @a = 1, 2, 3; my $v = @a[1]:delete; $v ~ " " ~ @a.elems ~ " " ~ (@a[1]:exists) ~ " " ~ @a[1].raku ~ " " ~ @a.raku }, " ", do { my @a = 1, 2, 3; @a[2]:delete; @a.elems ~ " " ~ @a.raku }, " ", do { my @a = 1, 2, 3; @a[1]:delete; @a[2]:delete; @a.elems ~ " " ~ @a.raku }, " ", do { my @a = 1, 2, 3; (@a[5]:delete).raku ~ " " ~ @a.elems }, " ", do { my Int @b is default(0) = 1, 2; @b[0]:delete; @b[0] ~ " " ~ @b.raku }, " ", do { my @a = 1, 2, 3; (@a[0, 2]:delete).raku ~ " " ~ @a.raku }, " ", do { my @a = 1, 2; @a[1]:delete; @a[1] = 5; @a.raku }
# rakudo 2026.08: 2 3 False Any [1, Any, 3] 2 [1, 2] 1 [1] Any 3 0 Array[Int].new(0, 2) (1, 3) [Any, 2] [1, 5]
say do { my @c = 1..*; my $d = @c[1]:delete; $d ~ " " ~ @c.head(4).raku ~ " " ~ (@c[1]:exists) }
# rakudo 2026.08: 2 (1, Any, 3, 4) False
```
rakupp 4.0.1-50: differs — a delete on a LAZY array still shifts the rest (`(1, 3, 4, 5)`, `:exists` True). A typed hole now prints as its default.

### LA-17  Subscript adverbs on arrays                              D:yes R:yes V:spec
`:kv`, `:p`, `:k`, `:v` drop missing positions; `:!exists` inverts;
`:exists:kv` and `:exists:p` pair each index with its Bool;
`:delete:exists` reports the state before deletion; an unknown adverb is
`X::Multi::NoMatch` (a dispatch failure, unlike hashes).
```
say do { my @a = <a b c>; (@a[0, 1]:kv).raku ~ " " ~ (@a[0, 1]:p).raku ~ " " ~ (@a[0, 5]:k).raku ~ " " ~ (@a[0, 5]:v).raku ~ " " ~ (@a[0, 5]:!exists).raku ~ " " ~ (@a[0, 5]:exists:kv).raku ~ " " ~ (@a[0, 5]:exists:p).raku ~ " " ~ (@a[0, 5]:kv).raku }, " ", do { my @a = <a b c>; (@a[1]:kv).raku ~ " " ~ (@a[1]:p).raku ~ " " ~ (@a[5]:kv).raku ~ " " ~ (@a[5]:p).raku ~ " " ~ (@a[5]:k).raku ~ " " ~ (@a[5]:v).raku ~ " " ~ (@a[1]:delete:exists) ~ " " ~ (@a[1]:exists) }, " ", do { my @a = <a b c>; (try @a[0]:foo) // $!.^name }, " ", do { my @a = <a b c>; (@a[*]:k).raku ~ " " ~ (@a[^5]:v).raku ~ " " ~ (@a[*-1]:p).raku }
# rakudo 2026.08: (0, "a", 1, "b") (0 => "a", 1 => "b") (0,) ("a",) (Bool::False, Bool::True) (0, Bool::True) (0 => Bool::True,) (0, "a") (1, "b") 1 => "b" () () () () True False X::Multi::NoMatch (0, 1, 2) ("a", "b", "c") 2 => "c"
```
rakupp 4.0.1-50: matches.

### LA-18  Slice assignment                                        D:yes R:yes V:spec
A slice assignment is a list assignment: one value per selected slot, the
remaining slots get Nil (the default), extra values are dropped, and a
nested list is one value. `@a[*]` selects the current indices; `@a[]`
selects the whole array and replaces it. The expression returns the
assigned slice.
```
say do { my @a = 1, 2, 3; @a[0, 1] = 5, 6; @a.raku }, " ", do { my @a = 1, 2, 3; @a[1..2] = <a b>; @a.raku }, " ", do { my @a = 1, 2, 3; @a[*] = 0; @a.raku }, " ", do { my @a = 1, 2, 3; @a[0, 1] = 5; @a.raku }, " ", do { my @a = 1, 2, 3; @a[0..1] = 7, 8, 9; @a.raku }, " ", do { my @a = 1, 2; @a[3, 4] = 8, 9; @a.raku }, " ", do { my @a = 1, 2, 3; @a[*-1, 0] = 0, 9; @a.raku }
# rakudo 2026.08: [5, 6, 3] [1, "a", "b"] [0, Any, Any] [5, Any, 3] [7, 8, 3] [1, 2, Any, 8, 9] [9, 2, 0]
say do { my @a = 1, 2, 3; @a[*] = 7, 8, 9, 10; @a.raku }, " ", do { my @a = 1, 2, 3; @a[] = 5, 6; @a.raku }, " ", do { my @a = 1, 2, 3; @a[1..*] = 8, 9; @a.raku }, " ", do { my @a = 1, 2, 3; @a[^2] = 8, 9; @a.raku }, " ", do { my @a = 1, 2, 3; @a[0, 1] = (5, 6), 7; @a.raku }, " ", do { my @a = 1, 2, 3; my @b = @a[0, 1] = 8, 9; @b.raku }, " ", do { my @a = 1, 2, 3; @a[1, 0] = @a[0, 1]; @a.raku }
# rakudo 2026.08: [7, 8, 9] [5, 6] [1, 8, 9] [8, 9, 3] [(5, 6), 7, 3] [8, 9] [2, 1, 3]
```
rakupp 4.0.1-50: matches.

### LA-19  Multi-dimensional subscripts on nested arrays            D:yes R:yes V:spec
`@a[1;0]` is `@a[1][0]`; a semicolon slice is `(3, 4)`; reading beyond the
outer end gives Any without growing; assigning grows; `:exists` and
`:delete` work per dimension; indexing a scalar element again returns the
element itself.
```
say do { my @a = [1, 2], [3, 4]; @a[1;0] ~ " " ~ @a[1][0] ~ " " ~ @a[1;0,1].raku }, " ", do { my @a = [1, 2], [3, 4]; @a[0;0] = 9; @a.raku }, " ", do { my @a = [1, 2]; @a[2;0].raku ~ " " ~ @a.elems }, " ", do { my @a = [1, 2]; @a[2;0] = 1; @a.raku }, " ", do { my @a = [1, 2], [3, 4]; (@a[0;1]:exists) ~ " " ~ (@a[0;5]:exists) ~ " " ~ (@a[5;0]:exists) }, " ", do { my @a = [1, 2], [3, 4]; (@a[1;1]:delete) ~ " " ~ @a.raku }, " ", do { my @a = 1, 2; my $f = @a[0;0]; $f.^name }
# rakudo 2026.08: 3 3 (3, 4) [[9, 2], [3, 4]] Any 2 [1, 2, [1]] True False False 4 [[1, 2], [3]] Int
```
rakupp 4.0.1-50: matches.

### LA-20  Shaped arrays                                           D:yes R:yes V:spec
`my @a[2;2]` has shape `(2, 2)`, elems 2, and prints as
`Array.new(:shape(2, 2), …)`. An index outside the shape throws; `push`
and `pop` throw `X::IllegalOnFixedDimensionArray`; a partial view
(`@a[1][1]`) is `X::NYI`; `.keys` enumerates index tuples;
`Array.new(:shape(2))` is fixed at 2; a Range as shape dies; a type as
shape warns and is ignored.
```
say do { my @a[2;2]; @a.shape.raku ~ " " ~ @a.elems ~ " " ~ @a.raku }, " ", do { my @a[2;2]; @a[1;1] = 5; @a.raku ~ " " ~ @a[1;1] }, " ", do { my @a[2;2]; (try @a[1][1]) // $!.^name }, " ", do { my @a[2;2]; ((try { @a[2;0] = 1; "no" }) // $!.^name) }, " ", [1, 2].shape.raku, " ", do { my @a[2]; ((try { @a.push(3); "no" }) // $!.^name) }, " ", do { my @a[2]; @a[0] = 1; @a.raku ~ " " ~ @a.elems ~ " " ~ ((try @a.pop) // $!.^name) }, " ", do { my @a[2;2] = (1, 2), (3, 4); @a.raku ~ " " ~ @a[1;0] }, " ", do { my @a[2]; ((try { @a[2] = 1; "no" }) // $!.^name) }, " ", do { my Int @a[2]; ((try { @a[0] = "x"; "no" }) // $!.^name) }, " ", do { my @a[2]; @a.gist ~ " " ~ @a[1].raku ~ " " ~ (@a[1]:exists) }, " ", do { my @a[2;2]; @a.keys.raku }
# rakudo 2026.08: (2, 2) 2 Array.new(:shape(2, 2), [Any, Any], [Any, Any]) Array.new(:shape(2, 2), [Any, Any], [Any, 5]) 5 X::NYI X::AdHoc (*,) X::IllegalOnFixedDimensionArray Array.new(:shape(2,), [1, Any]) 2 X::IllegalOnFixedDimensionArray Array.new(:shape(2, 2), [1, 2], [3, 4]) 3 X::AdHoc X::TypeCheck::Assignment [(Any) (Any)] Any False ((0, 0), (0, 1), (1, 0), (1, 1)).Seq
say Array.new(:shape(2)).raku, " ", Array.new(:shape(2)).elems, " ", (try Array.new(:shape(1..2))) // $!.^name, " ", do { my @w; CONTROL { when CX::Warn { @w.push(.message); .resume } }; Array.new(:shape(Int)).raku ~ " " ~ @w.elems }
# rakudo 2026.08: Array.new(:shape(2,), [Any, Any]) 2 X::AdHoc [] 1
```
rakupp 4.0.1-50: differs only in `@a[1][1]`, which answers Nil where Rakudo raises `X::NYI` — a partial view of a shaped array is read by a subscript fast path that never sees the shape. Everything else matches: the out-of-shape write, both `.raku` forms, the Range shape and the type-shape warning.

### LA-21  Typed arrays                                            D:yes R:yes V:spec
The type shows in `.raku` (`Array[Int].new(1, 2)`, empty `Array[Int].new()`)
but not in `.gist`. `.of` is the type, `.default` the type object (or the
`is default` value); a hole in an `is default(0)` array prints as 0;
assigning Nil stores the type object; a wrong type is
`X::TypeCheck::Assignment`; `Array[42]` is a compile-time error; `.name` is
the variable's name.
```
say Array[Int].new(1, 2).raku, " ", do { my Int @a; @a.raku }, " ", do { my Int @a = 1; @a.gist ~ " " ~ @a.raku }, " ", do { my @a is default(5); @a.raku ~ " " ~ @a.default }, " ", do { my Str @s = "a"; @s.raku }, " ", Array[Int].new.gist, " ", (try Array[Int].new("a")) // $!.^name, " ", Array[Int].^name, " ", Array[Int].of.raku, " ", do { my Int @a; @a.^name }
# rakudo 2026.08: Array[Int].new(1, 2) Array[Int].new() [1] Array[Int].new(1) [] 5 Array[Str].new("a") [] X::TypeCheck::Assignment Array[Int] Int Array[Int]
say do { my @a is default(7); @a[2] = 1; @a[0].raku ~ " " ~ @a[1].raku ~ " " ~ @a.default ~ " " ~ @a.raku }, " ", do { my @a; @a.of.raku ~ " " ~ @a.default.raku }, " ", do { my Int @a; @a.of.raku ~ " " ~ @a.default.raku }, " ", do { my Int @a; (try { @a.push("x"); "no" }) // $!.^name }, " ", do { my Int @a = 1, 2; (try { @a[0] = "x"; "no" }) // $!.^name }, " ", do { my @foo; @foo.name }, " ", do { my Int @a is default(0); @a[1] = 5; @a.raku }, " ", do { my Int @a = 1, 2; @a[0] = Nil; @a.raku }, " ", do { my @a = 1, 2; @a.dynamic }, " ", do { my @*d = 1; @*d.dynamic }
# rakudo 2026.08: 7 7 7 [7, 7, 1] Mu Any Int Int X::TypeCheck::Assignment X::TypeCheck::Assignment @foo Array[Int].new(0, 5) Array[Int].new(Int, 2) False True
```
rakupp 4.0.1-50: matches.

## C. Mutation

### LA-22  push and append: the single-argument rule again          D:yes R:yes V:spec
`push` adds each argument as one element, so `push((1, 2))` adds a list
and `push([1, 2])` an array; only a Slip spreads. `append` spreads one
Iterable argument (a Hash spreads into its pairs) but not an itemized one,
and several arguments are elements. Both return the array. A lazy argument
to `append`, or a `push`/`append` onto a lazy array, throws
`X::Cannot::Lazy`; `unshift` onto a lazy array works.
```
say do { my @a; my $r := @a.push(1, 2); ($r =:= @a) ~ " " ~ @a.raku }, " ", do { my @a; @a.push((1, 2)); @a.raku }, " ", do { my @a; @a.push(|(1, 2)); @a.raku }, " ", do { my @a; @a.push([1, 2]); @a.raku }, " ", do { my @a; @a.push((1, 2), (3, 4)); @a.raku }, " ", do { my @a; @a.push(1..*); @a.elems ~ " " ~ @a[0].^name }, " ", do { my @a; @a.push(Empty); @a.raku }, " ", do { my @a; @a.push((1, 2).Seq); @a.raku }
# rakudo 2026.08: True [1, 2] [(1, 2),] [1, 2] [[1, 2],] [(1, 2), (3, 4)] 1 Range [] [(1, 2).Seq,]
say do { my @a; @a.append((1, 2)); @a.raku }, " ", do { my @a; @a.append($(1, 2)); @a.raku }, " ", do { my @a; @a.append([1, 2]); @a.raku }, " ", do { my @a; @a.append(1, 2); @a.raku }, " ", do { my @a; @a.append((1, 2), (3, 4)); @a.raku }, " ", do { my @a; @a.append((1, (2, 3))); @a.raku }, " ", do { my @a; ((try { @a.append(1..*); "no" }) // $!.^name) }, " ", do { my @a = 1..*; ((try { @a.push(1); "no" }) // $!.^name) }, " ", do { my @a; @a.append("abc"); @a.raku }, " ", do { my @a; @a.append({a => 1}); @a.raku }, " ", do { my @a; @a.append(%(a => 1, b => 2)); @a.elems }, " ", do { my @a = 1..*; ((try { @a.unshift(0); "no" }) // $!.^name) ~ " " ~ @a.head(2).raku }
# rakudo 2026.08: [1, 2] [(1, 2),] [1, 2] [1, 2] [(1, 2), (3, 4)] [1, (2, 3)] X::Cannot::Lazy X::Cannot::Lazy ["abc"] [:a(1)] 2 no (0, 1)
say do { my @a = 3; @a.unshift(1, 2); @a.raku }, " ", do { my @a = 3; @a.unshift((1, 2)); @a.raku }, " ", do { my @a = 3; @a.unshift(|(1, 2)); @a.raku }, " ", do { my @a = 3; @a.prepend((1, 2)); @a.raku }, " ", do { my @a = 3; @a.prepend($(1, 2)); @a.raku }, " ", do { my @a = 3; @a.prepend(1, 2); @a.raku }
# rakudo 2026.08: [1, 2, 3] [(1, 2), 3] [1, 2, 3] [1, 2, 3] [(1, 2), 3] [1, 2, 3]
```
rakupp 4.0.1-50: matches.

### LA-23  pop and shift                                           D:yes R:yes V:spec
On an empty array both return a Failure `X::Cannot::Empty` (`.action`
names the method). `shift` on a lazy array reifies one element and works;
`pop` on a lazy array is a Failure `X::Cannot::Lazy`. A hole pops or shifts
as the default. Trailing deletions shrink the array first.
```
say do { my $f = [].pop; $f.^name ~ ":" ~ $f.exception.^name ~ ":" ~ $f.exception.action }, " ", do { my $f = [].shift; $f.^name ~ ":" ~ $f.exception.^name }, " ", do { my @a = 1..*; @a.shift ~ " " ~ @a.head(2).raku }, " ", do { my @a = 1..*; my $f = @a.pop; $f.^name ~ ":" ~ $f.exception.^name }, " ", do { my @a is default(9) = 1, 2; @a[0]:delete; @a.shift ~ " " ~ @a.raku }, " ", do { my @a = 1, 2, 3; @a.pop ~ " " ~ @a.shift ~ " " ~ @a.raku }, " ", do { my @a = 1, 2; @a[3] = 4; my $p1 = @a.pop; my $p2 = @a.pop; "$p1|{$p2.raku}|{@a.raku}" }, " ", do { my Int @a is default(0) = 1, 2; @a[4] = 4; @a[4]:delete; @a[3]:delete; @a.raku ~ " " ~ @a.pop.raku }
# rakudo 2026.08: Failure:X::Cannot::Empty:pop Failure:X::Cannot::Empty 1 (2, 3).Seq Failure:X::Cannot::Lazy 9 [2] 3 1 [2] 4|Any|[1, 2] Array[Int].new(1, 2) 2
```
rakupp 4.0.1-50: matches.

### LA-24  splice                                                  D:yes R:yes V:spec
Returns the removed elements as an Array of the same type. The offset may
be `*` (the end) or a callable of the length; the size may be `*` or a
callable of the remaining length; a size beyond the end clamps. A
replacement array is spread, and so is an itemized one in 6.d. Errors are
**thrown**: offset past the end is `X::OutOfRange` (what "Offset argument to
splice", range `0..elems`), a negative size is `X::OutOfRange` ("Size
argument to splice"), a negative offset likewise, a wrongly typed
replacement is `X::TypeCheck::Splice`, a lazy replacement or a lazy array is
`X::Cannot::Lazy`.
```
say do { my @a = 1..5; my @r = @a.splice(1, 2); @r.raku ~ " " ~ @a.raku }, " ", do { my @a = 1..5; @a.splice(1, 2, <a b c>).raku ~ " " ~ @a.raku }, " ", do { my @a = 1..5; @a.splice(1).raku ~ " " ~ @a.raku }, " ", do { my @a = 1..5; @a.splice.raku ~ " " ~ @a.raku }, " ", do { my @a = 1..3; @a.splice(*, 0, 9).raku ~ " " ~ @a.raku }, " ", do { my @a = 1..3; @a.splice(*-1).raku ~ " " ~ @a.raku }, " ", do { my @a = 1..3; @a.splice(1, *).raku ~ " " ~ @a.raku }, " ", do { my @a = 1..3; @a.splice(0, 2, [7, 8]).raku ~ " " ~ @a.raku }, " ", do { my @a = 1..3; @a.splice(0, 0, $[7, 8]); @a.raku }, " ", do { my @a = 1..3; @a.splice(1, 99).raku ~ " " ~ @a.raku }, " ", do { my @a = 1..3; @a.splice(3, 0, 4).raku ~ " " ~ @a.raku }
# rakudo 2026.08: [2, 3] [1, 4, 5] [2, 3] [1, "a", "b", "c", 4, 5] [2, 3, 4, 5] [1] [1, 2, 3, 4, 5] [] [] [1, 2, 3, 9] [3] [1, 2] [2, 3] [1] [1, 2] [7, 8, 3] [7, 8, 1, 2, 3] [2, 3] [1] [] [1, 2, 3, 4]
say do { my @a = 1..3; (try @a.splice(9)) // $!.^name ~ ":" ~ $!.what ~ ":" ~ $!.range }, " ", do { my @a = 1..3; (try @a.splice(1, -1)) // $!.^name ~ ":" ~ $!.what ~ ":" ~ $!.range }, " ", do { my @a = 1..3; my $i = -1; (try @a.splice($i)) // $!.^name }, " ", do { my Int @b = 1, 2; (try @b.splice(0, 1, "x")) // $!.^name }, " ", do { my @a = 1..3; (try @a.splice(0, 1, 1..*)) // $!.^name }, " ", [].splice(0, 1).raku, " ", (try [].splice(1)) // $!.^name, " ", do { my Int @b = 1, 2, 3; @b.splice(1, 1).of.raku ~ " " ~ @b.splice(0, 1).raku }, " ", do { my @a = 1..*; (try @a.splice(0, 1)) // $!.^name }, " ", do { my @a = 1..3; @a.splice(3).raku ~ " " ~ @a.raku }, " ", do { my @a = 1..3; @a.splice(1, 0).raku ~ " " ~ @a.raku }
# rakudo 2026.08: X::OutOfRange:Offset argument to splice:0..3 X::OutOfRange:Size argument to splice:0..^2 X::OutOfRange X::TypeCheck::Splice X::Cannot::Lazy [] X::OutOfRange Int Array[Int].new(1) X::Cannot::Lazy [] [1, 2, 3] [] [1, 2, 3]
```
rakupp 4.0.1-50: matches. (The sheet's last case is wrong: the 2026.08 binary ACCEPTS `splice(0, 1)` on a lazy array and answers `[1]`, where the line above records `X::Cannot::Lazy`.)

### LA-25  grab                                                    D:yes R:? V:spec
Removes and returns random elements: one, `grab(n)` up to n, `grab(*)` all;
an empty array gives Nil or an empty Seq; a lazy array throws `X::Cannot::Lazy`.
```
say do { my @a = 1, 2, 3; @a.grab.^name ~ " " ~ @a.elems }, " ", do { my @a = 1, 2, 3; @a.grab(*).elems ~ " " ~ @a.elems }, " ", do { my @a = 1, 2, 3; @a.grab(2).elems ~ " " ~ @a.elems }, " ", do { my @a = 1, 2, 3; @a.grab(9).elems }, " ", [].grab.raku, " ", [].grab(2).raku, " ", do { my @c = 1..*; (try @c.grab) // $!.^name }, " ", do { my @a = 1, 2, 3; @a.grab({ $_ - 1 }).elems }
# rakudo 2026.08: Int 2 3 0 2 1 3 Nil ().Seq X::Cannot::Lazy 2
```
rakupp 4.0.1-50: matches.

### LA-26  clone is shallow and shares a lazy source               D:yes R:? V:spec
Element containers are new; nested arrays are shared; a lazy array's clone
shares the reifier, so both see the same generated values and each
reifies once; the type is kept.
```
say do { my @a = 1, 2; my @b = @a.clone; @b[0] = 9; @a[0] }, " ", do { my @a = [1, 2],; my @b = @a.clone; @b[0][0] = 9; @a[0][0] }, " ", do { my @a = 1..*; my @b = @a.clone; @b[2] = 0; @a[2] ~ " " ~ @b.head(3).raku ~ " " ~ @b.is-lazy }, " ", do { my Int @a = 1; my @b := @a.clone; @b.of.raku }, " ", do { my @a = 1, 2; my @b := @a.clone; @b.push(3); @a.elems }
# rakudo 2026.08: 1 9 3 (1, 2, 0).Seq True Int 2
```
rakupp 4.0.1-50: differs — unchanged. Writing into a lazy clone still loses the reified values before it (`(Any, Any, 0)`).

## D. Laziness, Seq, iteration

### LA-27  What a lazy list allows                                 D:yes R:yes V:spec
Indexing, `Bool`, `head`, `is-lazy` work. `elems`, `reverse`, `sum`,
`pick`, `roll`, `Capture` return a Failure `X::Cannot::Lazy`; `sort`,
`tail`, `roll($n)`, `.List` of a lazy array throw it; `join` gives the
reified prefix plus `...`; `eager` never returns.
```
say (try (1..*).list.elems) // $!.^name, " ", (1..*).list.elems.^name, " ", (1..*).list.is-lazy, " ", (1, 2).is-lazy, " ", (1..*).map({ $_ }).is-lazy, " ", (1..*).list.Bool, " ", (1..*).list[3], " ", (1..*).list.head(2).raku, " ", (try (1..*).list.reverse.eager) // $!.^name, " ", (1..*).list.reverse.^name, " ", (1..*).list.sum.^name, " ", (try (1..*).list.sort.eager) // $!.^name
# rakudo 2026.08: X::Cannot::Lazy Failure True False True True 4 (1, 2).Seq X::Cannot::Lazy Failure Failure X::Cannot::Lazy
say (try (1..*).list.tail(2).eager) // $!.^name, " ", (try (1..*).list.tail) // $!.^name, " ", (1..*).list.head(*-1).^name, " ", (1..*).list.pick.^name, " ", (try (1..*).list.roll(2)) // $!.^name, " ", (1..*).list.roll.^name, " ", do { my @a = 1..*; @a.is-lazy ~ " " ~ @a[3] ~ " " ~ ((try @a.elems) // $!.^name) }
# rakudo 2026.08: X::Cannot::Lazy X::Cannot::Lazy Seq Failure X::Cannot::Lazy Failure True 4 X::Cannot::Lazy
```
rakupp 4.0.1-50: matches. Which refusals FAIL and which THROW was measured against the 2026.08 binary for a lazy List and a lazy Array separately; the two agree except for `pop`, which a List refuses as `X::Immutable`.

### LA-28  A Seq can be iterated once, unless cached               D:yes R:yes V:spec
The second `.iterator`, and `.elems` or `.is-lazy` after the iterator was
taken, throw `X::Seq::Consumed`. `.cache` keeps the values; `.elems` and
`.Bool` cache implicitly; assigning the Seq into an array caches it too.
`.eager` returns a List but leaves the Seq consumed; `.list` hands the
iterator to a lazy List and consumes the Seq; `.sink` consumes. A consumed
Seq prints as `Seq.new()`. `Seq.new` without an iterator is consumed from
the start.
```
say do { my $s = (1, 2).Seq; $s.iterator; ((try $s.iterator) // $!.^name) }, " ", do { my $s = (1, 2).Seq; my @a = $s; ((try $s.elems) // $!.^name) }, " ", do { my $s = (1, 2).Seq; $s.cache; $s.elems ~ " " ~ $s.list.raku }, " ", do { my $s = (1, 2).Seq; my $e = $s.eager; $e.raku ~ " " ~ ((try $s.elems) // $!.^name) }, " ", do { my $s = (1, 2).Seq; $s.list; ((try $s.elems) // $!.^name) }, " ", do { my $s = (1, 2).Seq; $s.iterator; $s.raku }, " ", do { my $s = (1, 2).Seq; $s.iterator; ((try $s.is-lazy) // $!.^name) }, " ", do { my $s = (1, 2).Seq; for $s { }; ((try $s.elems) // $!.^name) }, " ", do { my $s = (1, 2).Seq; my @a = $s; ((try { my @b = $s; @b.raku }) // $!.^name) }, " ", do { my $s = (1, 2).Seq; $s.sink; ((try $s.elems) // $!.^name) }, " ", (try Seq.new.elems) // $!.^name, " ", do { my $s = (1..3).map({ $_ }); ($s.elems, $s.elems).raku }, " ", do { my $s = (1..3).map({ $_ }); $s.Bool ~ " " ~ $s.elems }, " ", (1, 2).Seq.Str.raku, " ", (1, 2).Seq.gist, " ", (1..*).map({ $_ }).rotate.^name, " ", do { my $s = (1, 2).Seq; my $l = $s.list; $l.elems ~ " " ~ ((try $s.elems) // $!.^name) }
# rakudo 2026.08: X::Seq::Consumed 2 2 (1, 2) $(1, 2) X::Seq::Consumed X::Seq::Consumed $(Seq.new()) X::Seq::Consumed 2 [(1, 2).Seq,] X::Seq::Consumed X::Seq::Consumed (3, 3) True 3 "1 2" (1 2) Failure 2 X::Seq::Consumed
```
rakupp 4.0.1-50: differs — unchanged, and deliberately left: a Seq is never consumed here (`$s.sink; $s.elems` is 2) and `Seq.new.iterator` prints garbage. Consumption needs a shared per-Seq flag that survives copying, and every internal re-read of a Seq would have to be audited against it; that is its own sitting.

### LA-29  Seq methods                                             D:yes R:yes V:spec
```
say (1, 2, 3).Seq.elems, " ", (1, 2, 3).Seq.end, " ", (1, 2, 3).Seq.Bool, " ", ().Seq.Bool, " ", +(1, 2).Seq, " ", (1..*).map({ $_ }).elems.^name, " ", (1..*).map({ $_ }).elems.exception.^name, " ", (1..*).map({ $_ }).Bool, " ", (try (1..*).map({ $_ }).end.^name), " ", $((1, 2).Seq).raku, " ", (1..*).map({ $_ }).join(",").raku, " ", (1, 2).Seq.join("-").raku, " ", (1..*).map({ $_ }).reverse.^name, " ", (try (1..*).map({ $_ }).reverse.eager) // $!.^name
# rakudo 2026.08: 3 2 True False 2 Failure X::Cannot::Lazy True Nil $((1, 2).Seq) "..." "1-2" Failure X::Cannot::Lazy
say (1, 2, 3).Seq.reverse.raku, " ", (1, 2, 3).Seq.rotate.raku, " ", (1, 2, 3).Seq.rotate(-1).raku, " ", (1, 2, 3).Seq.rotate(0).^name, " ", (1..*).map({ $_ }).rotate.^name, " ", (1..*).map({ $_ }).rotate.exception.^name, " ", (3, 1, 2).Seq.sort.raku, " ", (1, 2, 3).Seq.slice(0, 2).raku, " ", (try (1, 2, 3).Seq.slice(2, 0).eager) // $!.^name, " ", (1..*).map({ $_ }).head(*).^name, " ", (1..*).map({ $_ }).head(Inf).^name, " ", (1, 2, 3).Seq.head(0).raku, " ", (1, 2, 3).Seq.head(-1).raku, " ", (1, 2, 3).Seq.head(2).raku, " ", (1..*).map({ $_ }).skip(2).head(1).raku
# rakudo 2026.08: (3, 2, 1).Seq (2, 3, 1).Seq (3, 1, 2).Seq Seq Failure X::Cannot::Lazy (1, 2, 3).Seq (1, 3).Seq X::AdHoc Seq Seq ().Seq ().Seq (1, 2).Seq (3,).Seq
say (1, 2).Seq.Capture.raku, " ", (1, a => 2).Seq.Capture.raku, " ", (1, 2).Seq.Int, " ", (1, 2).Seq.Numeric, " ", (1, 2).Seq.Seq.^name, " ", (1, 2).Seq.list.^name, " ", (1, 2).Seq.Array.raku, " ", (1, 2).Seq.Slip.raku, " ", Seq.new.raku, " ", (try Seq.new.iterator) // $!.^name
# rakudo 2026.08: \(1, 2) \(1, :a(2)) 2 2 Seq List [1, 2] slip(1, 2) Seq.new() X::Seq::Consumed
```
A lazy `.rotate` is a Failure, `.slice` needs increasing indices and dies at reification, `.head(0)` and `.head(-1)` are empty, `.head(*)` is the Seq itself.
rakupp 4.0.1-50: differs — `Seq.new.raku` is `()` rather than `Seq.new()` (it needs the consumption flag of LA-28 to tell an unstarted Seq from an empty one), and `.slice(2, 0)` does not refuse decreasing indices. `.rotate`, the lazy `.elems`/`.rotate` Failures and the lazy `.Bool` all match now.

### LA-30  Seq and List smartmatch                                 D:yes R:yes V:spec
A Seq matches elementwise with equal length; a lazy side never matches. A
List pattern matches elementwise with `ACCEPTS`, `*` taking one element
and `**` any run; a non-Iterable topic against a list of non-Match
elements is False; `gather` returns a Seq.
```
say ((1, 2).Seq ~~ (1, 2)), " ", ((1, 2).Seq ~~ (1, 2, 3)), " ", ((1, 2, 3).Seq ~~ (1, 2)), " ", ((1..*).Seq ~~ (1, 2)), " ", ((1, 2).Seq ~~ (1..*)), " ", ((1, 2) ~~ (1, 2).Seq), " ", ((Int, Int).Seq ~~ (1, 2)), " ", ((1, 2).Seq ~~ (Int, Int)), " ", (gather { take 1; take 2 }).raku, " ", (gather { take 1; take 2 }).^name, " ", (gather { }).raku, " ", (gather { take 1 } ).is-lazy
# rakudo 2026.08: True False False False True True False True (1, 2).Seq Seq ().Seq False
say ((1, 2, 3) ~~ (1, 2, 3)), " ", ((1, 2, 3) ~~ (1, 2)), " ", ((1, 2, 3) ~~ (1, **)), " ", ((1, 2, 3) ~~ (**, 3)), " ", ((1, 2, 3) ~~ (1, **, 3)), " ", ((1, 2, 3) ~~ (Int, Int, Int)), " ", ((1, 2, 3) ~~ (Int, Str, Int)), " ", ((1, 2) ~~ (1, 2, 3)), " ", (() ~~ ()), " ", ((1, 2) ~~ List), " ", ((1, 2) ~~ (1, *)), " ", ((1, 2, 3) ~~ (1, *, 3)), " ", (42 ~~ (1, 42)), " ", (42 ~~ ()), " ", (() ~~ 42).raku
# rakudo 2026.08: True False True True True True False False True True True True False False Bool::False
```
rakupp 4.0.1-50: matches.

## E. List assignment

### LA-31  Destructuring assignment                                D:partial R:? V:quirk
One value per scalar target, extra values dropped, missing ones Nil (the
default, so a typed target resets to its type object). The right side is
decontainerized first, so `($a, $b) = ($b, $a)` swaps. `*` as a target
skips a value, but only with pre-declared variables: `my ($a, *, $c)` is a
compile error. A non-scalar target slurps the rest. A **parenthesised group
inside `my (…)` does not destructure**: its variables stay Mu and the
corresponding value is dropped. An inner list on the right is one value,
itemized.
```
say do { my ($a, $b) = 1, 2, 3; ($a, $b).raku }, " ", do { my ($a, $b) = 1; ($a, $b).raku }, " ", do { my $a = 1; my $b = 2; ($a, $b) = ($b, $a); ($a, $b).raku }, " ", do { my $a; my $c; ($a, *, $c) = 1, 2, 3; ($a, $c).raku }, " ", do { my ($a, @rest) = 1, 2, 3; ($a, @rest).raku }, " ", do { my ($a, ($b, $c), $d) = 1, (2, 3), 4; ($a, $b, $c, $d).raku }, " ", do { my ($a, ($b, $c)) = 1, 2, 3; ($a, $b, $c).raku }, " ", do { my (@a, $b) = 1, 2, 3; (@a, $b).raku }, " ", do { my Int $i = 5; ($i, my $z) = (); ($i, $z).raku }, " ", do { my $x = 1; my $y = 2; ($x, $y) = 5; ($x, $y).raku }, " ", do { my ($a, $b, $c) = (1, 2), 3; ($a, $b, $c).raku }, " ", do { my ($a, $b) = [1, 2], 3; ($a, $b).raku }
# rakudo 2026.08: (1, 2) (1, Any) (2, 1) (1, 3) (1, [2, 3]) (1, Mu, Mu, 4) (1, Mu, Mu) ([1, 2, 3], Any) (Int, Any) (5, Any) ($(1, 2), 3, Any) ($[1, 2], 3)
```
The `(1, Mu, Mu, 4)` result is the quirk; Roast was not checked for it.
rakupp 4.0.1-50: differs — the nested group still destructures, and a typed target still resets to Any rather than its type object. `(1, 2), 3` now keeps the inner list as one itemized value, which is the part real programs meet.

## F. Operators

### LA-32  xx                                                      D:yes R:? V:spec
The left side is re-evaluated per repetition, so `[1] xx 2` gives two
distinct arrays and `++$n xx 3` counts; a bare block is a value, not
called. A Slip result spreads, a Seq result is cached. The count
truncates, `*` and `Inf` are endless, a Bool is 0 or 1, a negative count is
empty, and `xx` with no arguments is `X::NoZeroArgMeaning`.
```
say (1 xx 3).raku, " ", (1 xx 0).raku, " ", (1 xx *).head(2).raku, " ", (1 xx Inf).^name, " ", (1 xx 2.7).raku, " ", (1 xx True).raku, " ", (1 xx False).raku
# rakudo 2026.08: (1, 1, 1).Seq ().Seq (1, 1).Seq Seq (1, 1).Seq (1,).Seq ().Seq
say do { my $n = 0; (++$n xx 3).raku }, " ", ([1] xx 2).map(*.WHICH).unique.elems, " ", ((1, 2) xx 2).raku, " ", (1 xx -1).raku, " ", ((1, 2).Slip xx 2).raku, " ", ((1..2).Seq xx 2).raku, " ", ({ 42 } xx 2).elems, " ", ({ 42 } xx 2)[0].^name, " ", (try infix:<xx>()) // $!.^name
# rakudo 2026.08: (1, 2, 3).Seq 2 ((1, 2), (1, 2)).Seq ().Seq (1, 2, 1, 2).Seq ((1, 2), (1, 2)).Seq 2 Block X::NoZeroArgMeaning
```
rakupp 4.0.1-50: differs only in `([1] xx 2).map(*.WHICH).unique.elems`, which is 1 — two Arrays with equal elements share an identity here (see LA-36). `++$n xx 3`, `1 xx Inf` and the Seq caching all match.

### LA-33  X, Z, roundrobin                                        D:yes R:yes V:spec
`Z` stops at the shortest list; `X` is the cross product, lazy when its
first operand is; `:with` folds each tuple; `roundrobin` interleaves and
`:slip` flattens; empty operands give empty results.
```
say (1, 2 X 3, 4).raku, " ", (1, 2 X+ 10, 20).raku, " ", (1, 2 Z 3, 4).raku, " ", ((1, 2, 3) Z (4, 5)).raku, " ", (1, 2 Z+ 10, 20).raku, " ", zip((1, 2), (3, 4)).raku, " ", cross((1, 2), (3,)).raku, " ", roundrobin((1, 2, 3), (4, 5)).raku, " ", roundrobin((1, 2), (3, 4), :slip).raku, " ", (() X (1, 2)).raku, " ", ((1, 2) Z ()).raku
# rakudo 2026.08: ((1, 3), (1, 4), (2, 3), (2, 4)).Seq (11, 21, 12, 22).Seq ((1, 3), (2, 4)).Seq ((1, 4), (2, 5)).Seq (11, 22).Seq ((1, 3), (2, 4)).Seq ((1, 3), (2, 3)).Seq ((1, 4), (2, 5), (3,)).Seq (1, 3, 2, 4).Seq ().Seq ().Seq
say zip((1, 2), (3, 4), :with(&[~])).raku, " ", infix:<X>((1, 2), (3, 4), :with(&[+])).raku, " ", (1, 2 X~ 3, 4).raku, " ", ((1, 2) X (3, 4) X (5,)).raku, " ", ((1, 2) Z (3, 4) Z (5, 6)).raku, " ", ((1..*) Z (3, 4)).raku, " ", ((1..*) X (3, 4)).head(2).raku, " ", roundrobin((1, 2), (3,), (4, 5, 6)).raku, " ", roundrobin().raku, " ", ([Z] ((1, 2), (3, 4))).raku, " ", ([X] ((1, 2), (3,))).raku, " ", ((1, 2) Z (3, 4)).^name, " ", ((1, 2) X (3, 4)).is-lazy, " ", ((1..*) X (3, 4)).is-lazy, " ", ((1, 2) Z (1..*)).elems
# rakudo 2026.08: ("13", "24").Seq (4, 5, 5, 6).Seq ("13", "14", "23", "24").Seq ((1, 3, 5), (1, 4, 5), (2, 3, 5), (2, 4, 5)).Seq ((1, 3, 5), (2, 4, 6)).Seq ((1, 3), (2, 4)).Seq ((1, 3), (1, 4)).Seq ((1, 3, 4), (2, 5), (6,)).Seq ().Seq ((1, 3), (2, 4)).Seq ((1, 3), (2, 3)).Seq Seq False True 2
```
rakupp 4.0.1-50: differs only in `((1..*) X (3, 4)).is-lazy`, which is False — a cross over an endless left operand is built eagerly from its prefix. `zip(:with)` and `infix:<X>(:with)` match.

### LA-34  Where flat stops                                        D:yes R:yes V:spec
`flat` descends into Iterables that are **not** in a Scalar container. Every
Array element is in one, and so is `$(…)`, so `[1, [2, 3]].flat` keeps the
inner array while `(1, [2, 3]).flat` (an Array sitting directly in a List)
spreads it. `.flat` on an Array therefore flattens nothing unless
`:hammer` is given.
```
say flat(1, (2, (3,))).raku, " ", flat([1, [2]]).raku, " ", flat((1, [2, 3])).raku, " ", (1, (2, (3, 4))).flat.raku, " ", [1, (2, 3)].flat.raku, " ", [1, [2, 3]].flat.raku, " ", [1, [2, 3]].flat(:hammer).raku, " ", (1, $(2, 3)).flat.raku, " ", ((1, 2), (3, 4)).flat.raku, " ", (1, [2, 3]).flat.raku
# rakudo 2026.08: (1, 2, 3).Seq (1, $[2]).Seq (1, 2, 3).Seq (1, 2, 3, 4).Seq (1, $(2, 3)).Seq (1, $[2, 3]).Seq (1, 2, 3).Seq (1, $(2, 3)).Seq (1, 2, 3, 4).Seq (1, 2, 3).Seq
```
rakupp 4.0.1-50: matches, `$` markers included.

### LA-35  sort :k, reverse and rotate subs, pick, roll, combinations   D:yes R:yes V:spec
`sort(:k)` returns the sorted indices as a List; `rotate` coerces its
count; `reverse((1, 2), 3)` keeps the inner list as one item; `reverse()`
is `X::NoZeroArgMeaning`; `pick(n)` is capped at the size and `().pick(3)`
is an empty List while `().roll(2)` is an empty Seq; a combinations Range
is clamped to `0..elems`; `combinations(3, 2)` works on `^3`.
```
say (3, 1, 2).sort(:k).raku, " ", (3, 1, 2).sort(-*, :k).raku, " ", (1, 2, 3).rotate("2").raku, " ", ().reverse.raku, " ", reverse((1, 2), 3).raku, " ", rotate((1, 2, 3), 1).raku, " ", (try reverse()) // $!.^name
# rakudo 2026.08: (1, 2, 0) (0, 2, 1) (3, 1, 2).Seq ().Seq (3, $(1, 2)).Seq (2, 3, 1).Seq X::NoZeroArgMeaning
say (1, 2, 3).pick(5).elems, " ", (1, 2, 3).pick(0).raku, " ", ().pick.raku, " ", ().pick(3).raku, " ", (1, 2, 3).pick({ $_ - 1 }).elems, " ", (1, 2).roll(*).head(4).elems, " ", ().roll.raku, " ", ().roll(2).raku
# rakudo 2026.08: 3 ().Seq Nil () 2 4 Nil ().Seq
say (1, 2, 3).combinations(0..1).raku, " ", (1, 2, 3).combinations(2..5).raku, " ", (1, 2, 3).combinations(-1..1).raku, " ", ().permutations.raku, " ", ().combinations.raku, " ", combinations(3, 2).raku, " ", combinations(3).raku, " ", permutations(2).raku, " ", combinations((1, 2), 1).raku, " ", (1, 2, 3).combinations(4).raku
# rakudo 2026.08: ((), (1,), (2,), (3,)).Seq ((1, 2), (1, 3), (2, 3), (1, 2, 3)).Seq ((), (1,), (2,), (3,)).Seq ((),).Seq ((),).Seq ((0, 1), (0, 2), (1, 2)).Seq ((), (0,), (1,), (2,), (0, 1), (0, 2), (1, 2), (0, 1, 2)).Seq ((0, 1), (1, 0)).Seq ((1,), (2,)).Seq ().Seq
```
rakupp 4.0.1-50: matches.

## G. Identity

### LA-36  Array identity and equivalence                          D:yes R:yes V:spec
An Array's `WHICH` is an object identity, so `[1] === [1]` is False;
`eqv` is structural but type-strict (`[1, 2] eqv (1, 2)` is False);
smartmatch between arrays and lists is elementwise.
```
say [1, 2].WHICH.^name, " ", ([1] === [1]), " ", ([1, 2] eqv [1, 2]), " ", ([1, 2] eqv (1, 2)), " ", ((1, 2) eqv (1, 2)), " ", ([1, [2]] eqv [1, [2]]), " ", ([1, 2] ~~ [1, 2]), " ", ((1, 2) ~~ [1, 2])
# rakudo 2026.08: ObjAt False True False True True True True
```
rakupp 4.0.1-50: matches — `.WHICH` now answers an ObjAt (a ValueObjAt for the immutable value types). The identity it carries is still structural for a List or Array, so `((1,2),(1,2)).unique` keeps one element where Rakudo keeps two; that is the same gap LA-32's `[1] xx 2` probe names.

## Counts

| | items |
|---|---|
| total | 36 |
| not fully stated by docs (D:yes) nor asserted by Roast (R:yes) | 4 |
| Rakudo bugs (do not imitate) | 0 |
| quirks (recorded, step two decides) | 3 — LA-06 Empty undefined, LA-13 nested sum, LA-31 group in `my (…)` |
| rakupp 4.0.1 differed (extraction, 2026-09-18) | 29 |
| rakupp 4.0.1-50 differs (implementation, 2026-09-19) | 13 |
| rakupp 4.0.1-50 matches | 21 |

Of the 13 that still differ, 9 differ only in a named corner — each item's
own line says which. The three quirks were decided: LA-06's `Empty` is
implemented as Rakudo has it (a singleton that is undefined), LA-13's
nested `sum` already matched, and LA-31's group-in-`my (…)` was left alone,
because producing `(1, Mu, Mu, 4)` for `my ($a, ($b, $c), $d)` is a
Rakudo behaviour no program should be written against and no Roast file
asks for.

What is left, and why it is left:

- **Elements are not containers** (LA-07, LA-08 `.values`, LA-09, LA-10
  `.Array[0] = 9`). A `ValueList` holds values, so two slots cannot be
  aliased and a List of values cannot be read-only-per-element. `.map` and
  `.first` write through today via the driver's element slot, which is what
  the typed-array files needed; the general case is the container/binding
  layer that `docs/dev/findings/RAKU-PP-BIG-AREAS` ranks second and wants
  an lvalue refactor first.
- **A Seq is never consumed** (LA-28, and LA-29's `Seq.new.raku`). Needs a
  per-Seq flag shared across copies, plus an audit of every internal
  re-read of a Seq. Its own sitting.
- **List identity is structural** (LA-32's `[1] xx 2`, and the note on
  LA-36). Switching `whichOf` to an address for Array/List is a one-line
  change with a wide blast radius — the quanthash key comment in
  `Builtins.cpp` records a previous attempt that cost ~30 Roast assertions.
- Three single corners: a `:delete` on a LAZY array shifts the rest
  (LA-16), a lazy clone loses its reified prefix (LA-26), and a cross over
  an endless operand is not lazy (LA-33). Each is small and local.

## Method (how this sheet was produced)

As for [Supply.md](Supply.md). Seven probe rounds were needed: a literal
negative index and `Array[42]` are compile-time errors and killed whole
lines, `.eager` on an infinite list hangs (the alarm killed it), and
several lazy operations throw at reification outside the `try`. D flags
from `doc/Type/{List,Array,Seq,Slip}.rakudoc` and `doc/Language/list.rakudoc`;
R flags from `S32-array/*.t`, `S32-list/*.t`, `S02-types/{array,array_ref,
array-shapes,lazy-lists,flattening,list,lists,multi_dimensional_array}.t`,
`S09-typed-arrays/*.t`.
