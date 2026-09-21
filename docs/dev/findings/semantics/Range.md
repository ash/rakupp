# Range — semantics sheet

Provenance: Rakudo tag `2026.08`, file `src/core.c/Range.rakumod` (935
lines) read in full on 2026-09-21, plus the `infix:<..>` family and
`prefix:<^>` it defines, the `Range` candidates of `+ - * / cmp eqv` and
the exception classes it throws (`Exception.rakumod`). The `...` sequence
machinery that multi-character string ranges delegate to was NOT read: it
is its own sheet. Oracle: Homebrew Rakudo v2026.08 on macOS. Compared
against Raku++ 4.0.1-84-ga4291988 (build-arm64, 2026-09-21). Format and
legend: [README.md](README.md).

Where this sits against the declared spec: Rakudo passes all five Range
files here (`S02-types/range.t`, `S02-types/range-iterator.t`,
`S03-operators/range.t`, `range-basic.t`, `range-int.t`) and
`S03-smartmatch/range-range.t`; Raku++ passes `range-int.t` only. The rules below are what those files and real programs
lean on: how the four operators build a range, which endpoints are
refused, what counts as an element, how a range compares with a number, a
string or another range, and what the many list-like methods return. 12
of the 33 items are neither fully documented nor fully asserted by Roast.
Two Rakudo behaviours are recorded as bugs and seven as quirks, one of
them (the multi-character string range) a quirk that hurts; step two
should not imitate the bugs.

Every probe ran with `alarm 10` and standard input closed. Where a probe
line also exercises neighbouring items, the statement says which fields
matter.

## A. Construction and printing

### RG-01  The four operators, prefix `^`, and how a range prints          D:yes R:yes V:spec
`..`, `^..`, `..^`, `^..^` keep both endpoints as given; `^N` is `0..^N`
after coercing N to Numeric, so `^"5"` and `^@a` are integer ranges;
`*` at either end becomes `-Inf` or `Inf` and marks the range infinite.
`.raku` prints `^N` only for an Int range from 0 with an excluded end,
otherwise the endpoints' own `.raku` joined by the operator
(`0..^5.5`, `0e0..^5`, `"a".."c"`); `.Str` and `.gist` differ: `.Str`
of a finite range is its elements joined by spaces (empty for an empty
range), `.Str` of an infinite range writes `*` for the infinite end and
keeps both carets (`1^..^*`); `.gist` is `.raku`.
```
say (1..5).raku, " ", (1^..5).raku, " ", (1..^5).raku, " ", (1^..^5).raku, " ", (^5).raku, " ", (^5.5).raku, " ", (^"5").raku, " ", (^5.5e0).raku, " ", do { my @a = 3,5,3; (^@a).raku }, " ", (^-1).raku, " ", (^0).elems, " ", (1..*).raku, " ", (*..1).raku, " ", (*..*).raku, " ", (1^..*).raku, " ", (*^..^*).raku, " ", ("a".."c").raku, " ", (1.5..2).raku, " ", (0..^5).raku, " ", (0^..^5).raku, " ", (-1..^5).raku, " ", (0..5).raku, " ", (0..^5.0).raku, " ", (0e0..^5).raku
# rakudo 2026.08: 1..5 1^..5 1..^5 1^..^5 ^5 0..^5.5 ^5 0..^5.5e0 ^3 ^-1 0 1..Inf -Inf..1 -Inf..Inf 1^..Inf -Inf^..^Inf "a".."c" 1.5..2 ^5 0^..^5 -1..^5 0..5 0..^5.0 0e0..^5
say (1..5).gist, "|", (1..5).Str, "|", (1..*).Str, "|", (1..*).gist, "|", (*..*).Str, "|", (*..1).Str, "|", (1^..^*).Str, "|", ("a".."c").Str, "|", (1..0).Str, "|", (1.5..3).Str, "|", ~(3..2), "|", (1..5).fmt("%02d"), "|", (1..3).fmt("<%s>", ","), "|", (1..*).raku, "|", (1..5).Str.^name, "|", ("a".."c").gist
# rakudo 2026.08: 1..5|1 2 3 4 5|1..*|1..Inf|*..*|*..1|1^..^*|a b c||1.5 2.5||01 02 03 04 05|<1>,<2>,<3>|1..Inf|Str|"a".."c"
say (1..*).Str, " ", (1^..^*).Str, " ", (*..*).Str, " ", (1..Inf).gist, " ", (1..*).raku, " ", (1..∞).raku, " ", (-∞..∞).gist, " ", (1..*).gist.^name, " ", (1.5..*).Str, " ", ("a"..*).Str, " ", ("a"..*).gist, " ", (*..*).raku, " ", (*^..1).Str, " ", (*..^1).Str, " ", (1..^*).Str, " ", (1..^*).gist, " ", (^Inf).Str, " ", (^Inf).gist, " ", (^Inf).raku, " ", (1e0..*).Str, " ", (1e0..*).gist, " ", (*..1e0).Str
# rakudo 2026.08: 1..* 1^..^* *..* 1..Inf 1..Inf 1..Inf -Inf..Inf Str 1.5..* a..* "a"..Inf -Inf..Inf *^..1 *..^1 1..^* 1..^Inf 0..^* 0..^Inf 0..^Inf 1..* 1e0..Inf *..1
```
rakupp 4.0.1: differs — `.raku` loses non-Int endpoints (`^5.5`, `0..^5.0`
and `0e0..^5` all print `^5`; `*^..^*` prints `-Inf..Inf`; `(1.5..*)` prints
`1..Inf`, `("a"..*)` prints `0..Inf`, `(*.."z")` prints `-Inf..0`), and
`.Str` drops the second caret of `1^..^*`.

### RG-02  Endpoints that are refused, and the WhateverCode case            D:partial R:yes V:spec
A Range, a Seq or a Complex at either end throws `X::Range::InvalidArg`
at construction, `.got` holding the offending endpoint (for a Seq, the
type object `Seq`). `Range.new` refuses the same. A `*` with an
operation attached (`1..*+1`) is not a Range at all but a WhateverCode
that returns one when called; `^*` likewise. `1..2..3` is a compile-time
`X::Syntax::NonAssociative`.
```
say (try 10 .. ^20) // $!.^name ~ ":" ~ $!.got.raku, " ", (try ^10 .. 20) // $!.^name ~ ":" ~ $!.got.raku, " ", (try * .. 42i) // $!.^name ~ ":" ~ $!.got.raku, " ", (try 42.map({$_}) .. *) // $!.^name ~ ":" ~ $!.got.raku, " ", (try Range.new(1..2, 3)) // $!.^name, " ", (try 1 .. (1, 2).Seq) // $!.^name ~ ":" ~ $!.got.raku, " ", (try 1i .. 5) // $!.^name, " ", (try 1 .. 5i) // $!.^name, " ", (try (1..2) .. 3) // $!.^name
# rakudo 2026.08: X::Range::InvalidArg:^20 X::Range::InvalidArg:^10 X::Range::InvalidArg:<0+42i> X::Range::InvalidArg:Seq X::Range::InvalidArg X::Range::InvalidArg:Seq X::Range::InvalidArg X::Range::InvalidArg X::Range::InvalidArg
say (1..5).^name, " ", (1..*+1).^name, " ", (1..*+1)(4).raku, " ", (^*).^name, " ", (try EVAL('1..2..3')) // $!.^name, " ", (try EVAL('(1..5 xx 2).elems')) // $!.^name, " ", EVAL('(1..5 Z 1..5).elems'), " ", (1..5, 7).elems, " ", (1 .. 5 == 5).raku, " ", (1 .. 5 || 6).raku, " ", (2 ** 2 .. 5).raku, " ", (1 .. 5 + 1).raku, " ", (1..5).WHAT.gist, " ", (1e0..2e0).raku, " ", (1.5e0..*).raku, " ", (1.5..*).raku, " ", ("a"..*).raku, " ", (*.."z").raku
# rakudo 2026.08: Range WhateverCode.new 1..5 WhateverCode.new X::Syntax::NonAssociative X::Range::InvalidArg 5 2 Bool::True 1..5 4..5 1..6 (Range) 1e0..2e0 1.5e0..Inf 1.5..Inf "a"..Inf -Inf.."z"
```
The docs describe the WhateverCode case; the refusals have no docs page.
rakupp 4.0.1: differs — every refused endpoint is accepted silently
(`10 .. ^20` is `10..0`, `* .. 42i` is `-Inf..0`, a Seq endpoint becomes
its count), `1..2..3` parses, and `^*` is a Range `0..3`.

### RG-03  Endpoint coercion: only a Real left end coerces the right      D:partial R:yes V:spec
When the left endpoint is Real, the right one is coerced to Real: a
numeric string becomes an Int, an Array or List its element count, an
unparsable string throws `X::Str::Numeric` at construction. A List or
Match on the left is numified. A Str on the left coerces nothing, so
`"1"..9` is a range of strings and `"5"..9` keeps its `Str` minimum.
`1..Any` and `1..Nil` construct (with a warning for Nil) and are empty;
`1..True` is one element long.
```
say (1 .. "10").max.^name, " ", +(1 .. "10"), " ", do { my @a = 1..10; (1 .. @a).max }, " ", do { my @one = 1; (@one .. 3).min.^name ~ (@one .. 3).min }, " ", ("1"..9).list.raku, " ", ("1"..9).min.^name, " ", do { "1 3" ~~ /(\d) . (\d)/; ($0..$1).min.^name ~ " " ~ ($0..$1).raku }, " ", (1..Any).raku, " ", ("a"..5).raku, " ", ("a"..5).elems, " ", (try (1.."b").elems) // $!.^name, " ", ("100.B".."102.B").list.raku, " ", (1 .. [<a b c d e>]).Str, " ", do { my @three = 1,1,1; (1 ..^ @three).Str }, " ", (1..5.0).max.^name, " ", (1.."5").max.^name, " ", (1 .. "5").raku, " ", ("5" .. 9).raku, " ", (1 .. "5e0").max.^name, " ", (1.5 .. "5").max.^name, " ", (try (1 .. "abc").raku) // $!.^name, " ", ("abc" .. 5).elems, " ", (1 .. Nil).raku, " ", (try (Nil .. 5).raku) // $!.^name, " ", (1 .. True).raku, " ", (1 .. True).elems, " ", (1 .. Date.today).is-int
# rakudo 2026.08: Int 10 10 Int1 ("1", "2", "3", "4", "5", "6", "7", "8", "9") Str Int 1..3 1..Any "a"..5 0 X::Str::Numeric ("100.B", "101.B", "102.B") 1 2 3 4 5 1 2 Rat Int 1..5 "5"..9 Num Int X::Str::Numeric 0 1..0 Nil..5 1..Bool::True 1 False
```
(`1 .. Nil` also prints "Use of Nil in numeric context".) rakupp 4.0.1:
differs — `"1"..9` yields Ints, `1..Any` is `1..0`, `"a"..5` is `0..5`
with 6 elements, `1 .. "abc"` is `1..0` instead of throwing, `1..True`
is `1..1`, and `.is-int` is missing.

### RG-04  is-int, infinite, is-lazy, excludes-min/max, bounds, min, max   D:partial R:yes V:spec
`is-int` is True only when both endpoints are Int objects (`1..5.0`,
`1..1e0` and `1..Inf` are not, `1..2**70` is). `infinite` is True when
either endpoint is `Inf`, `-Inf` or NaN as a Num, but `Inf..1` is not
infinite and neither is `1..-Inf`. `is-lazy` follows `infinite`. `min`,
`max` and `bounds` return the raw endpoints, exclusions ignored, so
`(^3).max` is 3 and `(5..1).min` is 5. `Range.new` takes the same
arguments as the operators plus `:excludes-min`/`:excludes-max`.
```
say (1..5).is-int, " ", (1..5.5).is-int, " ", ("a".."e").is-int, " ", (1..Inf).is-int, " ", (1..2**70).is-int, " ", (1..Any).is-int, " ", (1..^5.0).is-int, " ", (1..1e0).is-int, " ", (1..1.0).is-int, " ", (^Inf).is-int, " ", (^0).is-int, " ", Int.Range.is-int, " ", UInt.Range.is-int, " ", (^5).is-int, " ", (^5.5).is-int, " ", (1..^5).is-int, " ", (1 .. "5").is-int
# rakudo 2026.08: True False False False True False False False False False True False False True False True True
say (1..*).min, " ", (1..*).max, " ", (*..1).min, " ", (1..*).infinite, " ", (1..Inf).infinite, " ", (1..5).infinite, " ", (-Inf..5).infinite, " ", (1..NaN).infinite, " ", (NaN..1).infinite, " ", (Inf..1).infinite, " ", (1..5).excludes-min, " ", (1^..^5).excludes-min, " ", (1^..^5).excludes-max, " ", (^5).excludes-max, " ", (1..5).excludes-max, " ", ("a".."b").infinite, " ", ("a"..*).infinite, " ", ("a"..*).max, " ", (*.."z").min, " ", (1.5..*).infinite, " ", (1..Inf) eqv (1..*), " ", (1..*) === (1..Inf), " ", (1..Inf).is-lazy, " ", (Inf..1).elems, " ", (Inf..1).is-lazy, " ", (1..-Inf).is-lazy, " ", (1..-Inf).elems, " ", (1..*).min.^name, " ", (1..*).max.^name, " ", (^*).raku, " ", (1..*).infinite.^name, " ", (1..5).excludes-min.^name, " ", (Date.today..*).infinite, " ", (1..1e0).infinite, " ", (-Inf..-Inf).infinite, " ", (Inf..Inf).infinite, " ", (1..*).is-int, " ", (^Inf).is-int, " ", (1..*).elems.^name
# rakudo 2026.08: 1 Inf -Inf True True False True True True False False True True True False False True Inf -Inf True True True True 0 False False 0 Int Num WhateverCode.new Bool Bool True False True True False False Failure
say (1..Inf).is-lazy, " ", (-Inf..0).is-lazy, " ", (1..5).is-lazy, " ", (Inf..0).is-lazy, " ", (NaN..NaN).is-lazy, " ", (1..NaN).is-lazy, " ", (1..5).elems.^name, " ", ("a".."e").elems.^name, " ", (1..Inf).elems.^name, " ", (*..1).is-lazy, " ", (1..*).elems.^name, " ", (try (1..*).elems.raku) // $!.^name, " ", (try ("a"..*).elems.raku) // $!.^name, " ", (try (1.5..*).elems.raku) // $!.^name, " ", (Inf..Inf).elems.^name, " ", (Inf..NaN).elems.^name, " ", (NaN..1).elems.^name, " ", (try (Inf..Inf).elems.raku) // $!.^name, " ", (Inf..0).elems, " ", (5..-Inf).elems, " ", (1..-Inf).elems, " ", (-Inf..-Inf).elems.^name, " ", (NaN..NaN).elems.^name, " ", (1..*).is-lazy, " ", (1..2**70).is-lazy, " ", ("a"..*).is-lazy, " ", ("a".."z").is-lazy
# rakudo 2026.08: True True False False True True Int Int Failure True Failure X::Cannot::Lazy X::Cannot::Lazy X::Cannot::Lazy Failure Failure Failure X::Cannot::Lazy 0 0 0 Failure Failure True False True False
say Range.new(1, 5).raku, " ", Range.new(1, 5, :excludes-min).raku, " ", Range.new("a", "z", :excludes-max).raku, " ", Range.new(1, 5, :!excludes-min).raku, " ", Range.new(*, 5).raku, " ", Range.new(1, *).infinite, " ", Range.new(1, Inf).raku, " ", Range.new(5, 1).elems, " ", Range.new(1, 1, :excludes-min).elems, " ", (Range.new(1, 3, :excludes-min, :excludes-max)).list.raku, " ", (try Range.new(1..2, 3)) // $!.^name, " ", Range.new(1, 2).is-int, " ", Range.new(1.0, 2).is-int, " ", Range.new(1, "5").max.^name, " ", Range.new("1", 5).min.^name
# rakudo 2026.08: 1..5 1^..5 "a"..^"z" 1..5 -Inf..5 True 1..Inf 0 0 (2,) X::Range::InvalidArg True False Int Str
```
The NaN rules and `Inf..1` are undocumented. rakupp 4.0.1: differs —
`is-int`, `infinite` and `Range.new` do not exist; `is-lazy` is False
for `*..1`, `NaN..NaN` and `1..NaN`; `.elems` of an endless range is the
Num `Inf` rather than a Failure.

### RG-05  Truth, WHICH, `===`, `eqv`                                        D:partial R:yes V:quirk
Every instantiated Range is True, even an empty one (`1..0`, `1^..1`,
`"b".."a"`); Roast marks the False answer as 6.e. `WHICH` is
`Range|` followed by `.raku`, so ranges are value types: `===` and `eqv`
hold for equal endpoints and exclusions of the same types (`1..2` is not
`eqv` `1.0..2` or `1..^2`), a mixin changes the type and breaks both.
```
say (1..2).WHICH, " ", (1..2) === (1..2), " ", (1..2) eqv (1..2), " ", (1..2) eqv (1.0..2), " ", (1..2) eqv (1..^2), " ", (1..2) eqv (1^..2), " ", (1..2) == (1..2), " ", (1..2) eqv ((1..2) but role {}), " ", ?(1..0), " ", ?(1^..1), " ", ?(1..5), " ", (1..0).so, " ", ?("b".."a"), " ", ?Range, " ", (1..5).defined, " ", ("a".."b").WHICH, " ", (1..2) === ((1..2) but role {}), " ", (1..2).WHICH.^name, " ", (1..2) eqv (1..2.0), " ", ((1..3), (1..3)).unique.elems, " ", ((1..3), (1..2)).unique.elems, " ", (1..2) === (1..2.0), " ", (1..*) === (1..*), " ", ((1..2) but role {}).WHICH
# rakudo 2026.08: Range|1..2 True True False False False True False True True True True True False True Range|"a".."b" False ValueObjAt False 1 2 False True Range+{<anon|3>}|1..2
```
rakupp 4.0.1: differs — `eqv` with a mixin is True, and a mixed-in
range's WHICH is an address, not a value.

### RG-06  Precedence around `..`                                            D:partial R:yes V:spec
`..` sits below every arithmetic, `xx`, `~`, hyper and `gcd`/`div`/`mod`
operator and above `~~`, `==`, `eqv`, `//`, `andthen` and the junctive
`and`; so `1..5 xx 2` is `1..(5 xx 2)` (an InvalidArg), `1..3 ~ "x"`
is `1.."3x"` (`X::Str::Numeric`), `1..2 + 2` is `1..4`, while
`1..3 eqv 1..3` and `1..3 ~~ 1..3` compare two ranges. `cmp`, `leg`
and `but` share its level and are non-associative with it. A prefix
`|` or `~` applied to the first endpoint compiles with a "Potential
difficulties" worry telling the writer to parenthesize the range;
under `use fatal` that worry is a fatal `X::Worry::Precedence::Range`. A one-element slip numifies to 1, so `my @a = |4..5` is 1 to 5 and `~4..5` is `"4"..5`.
```
say (try EVAL('(1..3 ~ "x").raku')) // $!.^name, " ", (try EVAL('(1 .. 3 ~ "x").raku')) // $!.^name, " ", EVAL('(1 .. 2 ** 2).raku'), " ", EVAL('(1 .. -2).raku'), " ", EVAL('(-2 .. -1).raku'), " ", EVAL('(1 ..^ 5 - 1).raku'), " ", EVAL('(1 .. 5).elems'), " ", EVAL('(1..5).elems'), " ", EVAL('(1 ..5).elems'), " ", (try EVAL('(1.. 5).elems')) // $!.^name, " ", EVAL('(1..5 X 1..2).elems'), " ", EVAL('(1..3 Z+ 1..3).raku'), " ", EVAL('([+] 1..5)'), " ", EVAL('(1..3 >>+>> 1).raku'), " ", EVAL('(1 ~~ 1..3).raku'), " ", (try EVAL('(1..3 eqv 1..3).raku')) // $!.^name, " ", (try EVAL('(1..3 cmp 1..3).raku')) // $!.^name, " ", (try EVAL('(1..3 ~~ 1..3).raku')) // $!.^name, " ", EVAL('(1 .. 5 == 5).raku'), " ", EVAL('(1 .. 5 || 6).raku'), " ", EVAL('(1 .. 5 ?? 1 !! 2).raku'), " ", EVAL('(1..5 Z 1..5).elems'), " ", (try EVAL('(1..5 xx 2).elems')) // $!.^name, " ", EVAL('(1..5, 7).elems'), " ", EVAL('(1 .. 5 + 1).raku'), " ", EVAL('(1 .. 5 * 2).raku'), " ", EVAL('(2 ** 2 .. 5).raku'), " ", EVAL('(-1..5).raku'), " ", EVAL('(1..-5).elems'), " ", (try EVAL('(1..3 ~~ Range)')) // $!.^name, " ", EVAL('(1..3 andthen 5).raku'), " ", EVAL('(1..3 and 5).raku'), " ", (try EVAL('(1..3 min 2).raku')) // $!.^name, " ", (try EVAL('(1..3 max 2).raku')) // $!.^name, " ", (try EVAL('(1..3 but 5).raku')) // $!.^name, " ", EVAL('(1..3 // 5).raku'), " ", EVAL('(1..3 X~ "a").raku'), " ", (try EVAL('(1..3 leg 1..3).raku')) // $!.^name, " ", EVAL('(2 ** 3 .. 9).raku'), " ", EVAL('(1 + 1 .. 3 * 2).raku'), " ", EVAL('(1 .. 2 == 2).raku'), " ", EVAL('(1 .. 3 gcd 6).raku'), " ", EVAL('(1 .. 6 div 2).raku'), " ", EVAL('(1 .. 6 mod 4).raku'), " ", EVAL('(1 .. 2 +< 1).raku'), " ", EVAL('(1 .. 3 ~~ 3).raku')
# rakudo 2026.08: X::Str::Numeric X::Str::Numeric 1..4 1..-2 -2..-1 1..^4 5 5 5 5 10 (2, 4, 6).Seq 15 1..4 Bool::True Bool::True X::Syntax::NonAssociative Bool::True Bool::True 1..5 1 5 X::Range::InvalidArg 2 1..6 1..10 4..5 -1..5 0 True 5 5 1..3 2 X::Syntax::NonAssociative 1..3 ("1a", "2a", "3a").Seq X::Syntax::NonAssociative 8..9 2..6 Bool::True 1..3 1..3 1..2 1..4 Bool::True
say EVAL(q[my @a = |4..5; @a.raku]), " ", EVAL(q[(~4..5).raku]), " ", EVAL(q[my @a = |(4..5); @a.raku]), " ", EVAL(q[((~4)..5).raku]), " ", (try EVAL(q[use fatal; my @a = |4..5; @a.raku])) // $!.^name, " ", (try EVAL(q[(1..5 xx 2).elems])) // $!.^name ~ ":" ~ $!.got.^name
# rakudo 2026.08: [1, 2, 3, 4, 5] "4"..5 [4, 5] "4"..5 X::Worry::Precedence::Range+{X::Comp} X::Range::InvalidArg:Seq
```
(`|4..5` and `~4..5` each print a "Potential difficulties: To apply a
Slip flattener to a range, parenthesize the whole range" / "To
stringify a range, parenthesize the whole range" worry.) rakupp 4.0.1:
differs — `1..3 ~ "x"` is `1..3`, `cmp`/`leg`/`but` beside `..` parse
(`Order::Same`, `1..3`), `1..5 xx 2` is 2, `~4..5` is the numeric `4..5`,
and no worry is issued, fatal or not.

## B. Counting

### RG-07  elems                                                          D:yes R:yes V:spec
An Int range counts arithmetically; any other finite range counts its
list; an infinite range fails with `X::Cannot::Lazy` whose `.action` is
`.elems`; a range whose start is after its end has 0, including
`Inf..0` and `5..-Inf`. `^5.5` has 6 elements (0 to 5) because the
excluded end only bites when a step lands on it. `+Range` (the type
object) is 0 with a warning.
```
say (1..5).elems, " ", (1^..^10).elems, " ", (1..0).elems, " ", (1.1..5).elems, " ", (1.5..3).elems, " ", ("a".."e").elems, " ", ("a"^..^"e").elems, " ", (Inf..0).elems, " ", (5..-Inf).elems, " ", +Range, " ", (^5).elems, " ", (^5.5).elems, " ", (1..1).elems, " ", (1^..1).elems, " ", ((1..*).elems).^name, " ", do { my $e = (1..*).elems; $e.so; $e.exception.^name ~ ":" ~ $e.exception.action }, " ", do { my $e = (*..*).elems; $e.so; $e.exception.action }, " ", do { my $e = (-Inf..-Inf).elems; $e.so; $e.^name }, " ", (1..Inf).is-lazy, " ", (-Inf..0).is-lazy, " ", (1..5).is-lazy, " ", (Inf..0).is-lazy, " ", (NaN..NaN).is-lazy, " ", (1..NaN).is-lazy, " ", (1..5).elems.^name, " ", ("a".."e").elems.^name
# rakudo 2026.08: 5 8 0 4 2 5 3 0 0 0 5 6 1 0 Failure X::Cannot::Lazy:.elems .elems Failure True True False False True True Int Int
```
rakupp 4.0.1: differs — `(^5.5).elems` is 5 (its list stops at 4), and an
infinite range answers the Num `Inf` instead of failing (so the line above
dies at `.exception`).

### RG-08  Numeric context                                                D:no R:yes V:quirk
`+$range` is `elems` for an Int range; for two Numeric endpoints it is
0 when start is after end, `Inf` when either end is infinite (so
`+(-∞..-∞)` and `+(∞..∞)` are `Inf` while their `.elems` fail), NaN
when either end is NaN, and otherwise `floor(max - min - excludes-min)
+ 1`, minus one more when the span is a whole number and the end is
excluded; a non-numeric range counts its list. `.Int` follows.
```
say +(6..6), " ", +(6^..6), " ", +(6..^6), " ", +(6..^6.1), " ", +(1.2..4), " ", +(1..^3.3), " ", +(2.3..3.1), " ", +(10..9), " ", +(-∞..-∞), " ", +(∞..∞), " ", +(5..-∞), " ", +(1..*), " ", +(*..5), " ", +("a".."e"), " ", +("aa".."ab"), " ", (1.2..4).Numeric.^name, " ", +(1^..^10), " ", (1..5).Int, " ", (1..5).Int.^name, " ", +((5..2).reverse), " ", +(1.5^..^3.5), " ", +(1.5..^3.5), " ", +(1.5..3.5), " ", (1.5..3.5).list.raku, " ", +(1..NaN), " ", +(NaN..1), " ", +(1.5..*)
# rakudo 2026.08: 1 0 0 1 3 3 1 0 Inf Inf 0 Inf Inf 5 2 Int 8 5 Int 0 1 2 3 (1.5, 2.5, 3.5) NaN NaN Inf
```
The `Inf` for `-∞..-∞` is asserted by Roast (rakudo/rakudo#3637) even
though `.elems` of the same range fails; that asymmetry is the quirk.
rakupp 4.0.1: differs — infinite ranges numify to 1, 0 or 10000, NaN
ranges to 0 or 2.

## C. Iteration

### RG-09  Numeric iteration steps by `succ` from the start                D:yes R:yes V:spec
Elements are `min`, `min.succ`, … while not past `max`; an excluded
start begins at `min.succ`, an excluded end stops before a value equal
to `max`. So `1.1..4` is 1.1, 2.1, 3.1; `1..4.9` is 1, 2, 3, 4; `^5.5`
reaches 5; `1..3.0` and `1..3e0` yield Ints (the start's type wins),
`1.0..3` yields Rats and `1e0..3e0` Nums; `1^..^2` and `1.1^..^1.1` are
empty; `(1/3)..2` steps by whole numbers from 1/3.
```
say (1.1..4).list.raku, " ", (1.9..4).list.raku, " ", (1.1^..4).list.raku, " ", (1..4.9).list.raku, " ", (1..^4.1).list.raku, " ", (1.9^..^4.9).list.raku, " ", (1.1^..^4.9).list.raku, " ", (1e0..3e0).list.raku, " ", (1..3e0).list.raku, " ", (1..3.0).list.raku, " ", (1.0..3).list.raku, " ", (1.1..1.1).list.raku, " ", (1.1^..^1.1).list.raku, " ", (1..1).list.raku, " ", (^5.5).list.raku, " ", (0.5..^2).list.raku, " ", (1..3).list.raku, " ", (1..^3).list.raku, " ", (0.1e0..0.4e0).list.raku, " ", (1/3..2).list.raku
# rakudo 2026.08: (1.1, 2.1, 3.1) (1.9, 2.9, 3.9) (2.1, 3.1) (1, 2, 3, 4) (1, 2, 3, 4) (2.9, 3.9) (2.1, 3.1, 4.1) (1e0, 2e0, 3e0) (1, 2, 3) (1, 2, 3) (1.0, 2.0, 3.0) (1.1,) () (1,) (0, 1, 2, 3, 4, 5) (0.5, 1.5) (1, 2, 3) (1, 2) (0.1e0,) (<1/3>, <4/3>)
say (1..1).list.raku, " ", (1^..1).list.raku, " ", (1..^1).list.raku, " ", (1^..^2).list.raku, " ", (1^..^3).list.raku, " ", (2^..2).elems, " ", (-17^..^-15).list.raku, " ", (-17..-19).list.raku, " ", (0..-1).list.raku, " ", (^0).list.raku, " ", (^1).list.raku, " ", (^0.1).list.raku, " ", ("a"^..^"b").list.raku, " ", ("a"^..^"a").list.raku, " ", ("b".."a").list.raku, " ", ('!'^..^'&').list.raku, " ", ('%'^..^'&').list.raku, " ", (" ".." ").list.raku, " ", ("\0".."\x[3]").elems, " ", (1.1^..^1.1).elems, " ", (1..0).list.raku, " ", (1..1).raku, " ", ("a".."a").raku, " ", (^0).raku, " ", ("a".."a").Str, " ", (1..1).Str, " ", (5..1).min, " ", (5..1).max, " ", (5..1).elems, " ", (5..1).Str.raku, " ", (5..1).bounds.raku, " ", (5..1).raku, " ", (1.5..1).elems, " ", ("b".."a").elems, " ", (5..1).list.raku, " ", ("b".."a").raku, " ", (1..1.5).list.raku, " ", (1..0.5).list.raku, " ", (1^..1.5).list.raku
# rakudo 2026.08: (1,) () () () (2,) 0 (-16,) () () () (0,) (0,) () () () ("\"", "#", "\$", "\%") () (" ",) 4 0 () 1..1 "a".."a" ^0 a 1 5 1 0 "" (5, 1) 5..1 0 0 () "b".."a" (1,) () ()
```
rakupp 4.0.1: differs — a Num or Rat endpoint on the right changes the
element type (`1..4.9` yields `1e0..4e0`, `1..1.5` yields `1e0`),
`1e0..3e0` and `1.0..3` yield Ints, `^5.5` stops at 4, `^0.1` is empty.

### RG-10  Single-character string ranges walk codepoints                  D:partial R:yes V:spec
When both endpoints are one character long (an Int on the right counts
by its digit), the range is the codepoint interval, so `'Y'..'d'`
passes through `[ \ ] ^ _` and a backtick, `"1"..9` yields the strings
"1" to "9", `"A".."z"` has 58 elements and `"\0".."\x[3]"` four. A start
after its end is empty.
```
say ("a".."e").list.raku, " ", ("a"^..^"e").list.raku, " ", ("A".."a").elems, " ", ('Y'..'d').Str, " ", ("Y".."AB").list.raku, " ", ("aa".."ad").list.raku, " ", ("a".."ad").elems, " ", ("a".."ad").list.tail(3).raku, " ", ("aa".."b").head(4).raku, " ", ("aa".."b").is-lazy, " ", ("az".."bb").list.raku, " ", ("Az".."Bb").list.raku, " ", ("zz".."aaa").head(2).raku, " ", ("1".."9").list.raku, " ", ("1".."10").list.raku, " ", ("9".."10").list.raku, " ", ("01".."03").list.raku, " ", ("8".."12").head(3).raku, " ", ("x".."x").list.raku, " ", ("A".."z").elems, " ", ("é".."ë").list.raku, " ", ("a".."c").pick(*).sort.raku, " ", ("a".."e")[1..2].raku
# rakudo 2026.08: ("a", "b", "c", "d", "e") ("b", "c", "d") 33 Y Z [ \ ] ^ _ ` a b c d () ("aa", "ab", "ac", "ad") 1 ("a",).Seq ("aa",).Seq False ("az", "ay", "ax", "aw", "av", "au", "at", "as", "ar", "aq", "ap", "ao", "an", "am", "al", "ak", "aj", "ai", "ah", "ag", "af", "ae", "ad", "ac", "ab", "bz", "by", "bx", "bw", "bv", "bu", "bt", "bs", "br", "bq", "bp", "bo", "bn", "bm", "bl", "bk", "bj", "bi", "bh", "bg", "bf", "be", "bd", "bc", "bb") ("Az", "Ay", "Ax", "Aw", "Av", "Au", "At", "As", "Ar", "Aq", "Ap", "Ao", "An", "Am", "Al", "Ak", "Aj", "Ai", "Ah", "Ag", "Af", "Ae", "Ad", "Ac", "Ab", "Bz", "By", "Bx", "Bw", "Bv", "Bu", "Bt", "Bs", "Br", "Bq", "Bp", "Bo", "Bn", "Bm", "Bl", "Bk", "Bj", "Bi", "Bh", "Bg", "Bf", "Be", "Bd", "Bc", "Bb") ().Seq ("1", "2", "3", "4", "5", "6", "7", "8", "9") ("1",) () ("01", "02", "03") ().Seq ("x",) 58 ("é", "ê", "ë") ("a", "b", "c").Seq ("b", "c")
```
The single-character fields match on both engines (the multi-character
fields of this line belong to RG-11). rakupp 4.0.1: matches on the
single-character fields.

### RG-11  Multi-character string ranges are per-position products       D:no R:partial V:quirk
When both endpoints are strings longer than one character, Rakudo does
not iterate by `succ`: it hands the endpoints to the `...` sequence
machinery, which for equal-length endpoints forms the cross product of
per-position character ranges, slowest position first, and each
position may run DOWN (`"az".."bb"` is az, ay, … ab, bz, … bb;
`"08".."11"` is 08, 07, … 01, 18, … 11). Endpoints of different lengths
give degenerate answers (`"a".."ad"` is one element, `"1".."10"` one,
`"a".."bb"` two, `"x".."ab"` none, yet `"a".."zz"` is 702). `"aa".."zz"`
is 676 either way, which is why the divergence hides. Reversal follows
the same product backwards.
```
say ("aa".."bb").list.raku, " ", ("ab".."ba").list.raku, " ", ("aa".."az").list.raku, " ", ("aa".."ba").list.raku, " ", ("ay".."bb").list.raku, " ", ("a".."bb").elems, " ", ("a".."bb").list.tail(3).raku, " ", ("aa".."ab").list.raku, " ", ("x".."ab").elems, " ", ("10".."20").elems, " ", ("10".."20").list.head(3).raku, " ", ("08".."11").list.raku, " ", ("a1".."b2").list.raku, " ", ("1a".."2b").list.raku, " ", ("aa".."cc").elems, " ", ("aaa".."aab").list.raku, " ", ("ab".."aa").list.raku, " ", ("aa".."bb").reverse.raku, " ", ("a".."bb").reverse.head(3).raku, " ", ("aa".."zz").elems, " ", ("a".."zz").elems, " ", ("ba".."ab").list.raku, " ", ("Aa".."Bb").list.raku, " ", ("a".."e").elems, " ", ("é".."ë").elems, " ", ("aa".."bb").elems, " ", ("a9".."b1").list.raku, " ", ("9".."11").list.raku, " ", ("1".."11").elems, " ", ("A".."z").elems
# rakudo 2026.08: ("aa", "ab", "ba", "bb") ("ab", "aa", "bb", "ba") ("aa", "ab", "ac", "ad", "ae", "af", "ag", "ah", "ai", "aj", "ak", "al", "am", "an", "ao", "ap", "aq", "ar", "as", "at", "au", "av", "aw", "ax", "ay", "az") ("aa", "ba") ("ay", "ax", "aw", "av", "au", "at", "as", "ar", "aq", "ap", "ao", "an", "am", "al", "ak", "aj", "ai", "ah", "ag", "af", "ae", "ad", "ac", "ab", "by", "bx", "bw", "bv", "bu", "bt", "bs", "br", "bq", "bp", "bo", "bn", "bm", "bl", "bk", "bj", "bi", "bh", "bg", "bf", "be", "bd", "bc", "bb") 2 ("a", "b").Seq ("aa", "ab") 0 2 ("10", "20").Seq ("08", "07", "06", "05", "04", "03", "02", "01", "18", "17", "16", "15", "14", "13", "12", "11") ("a1", "a2", "b1", "b2") ("1a", "1b", "2a", "2b") 9 ("aaa", "aab") () ("bb", "ba", "ab", "aa").Seq ("bb", "ba", "az").Seq 676 702 () ("Aa", "Ab", "Ba", "Bb") 5 3 4 ("a9", "a8", "a7", "a6", "a5", "a4", "a3", "a2", "a1", "b9", "b8", "b7", "b6", "b5", "b4", "b3", "b2", "b1") () 1 58
```
Rakudo's own source calls this "the magic sequence, identical to ..."
and points at rakudo/rakudo#2238; Roast asserts only the codepoint cases
and `"Y".."AB"` being empty. rakupp 4.0.1: differs, by decision — it
iterates by `succ` (`"aa".."bb"` is 28 strings, `"a1".."b2"` twelve),
the choice made on 2026-07-27 when the Roast payoff of the product rule
measured +4 against ~5,600 assertions that depend on the
one-character fast path staying exact.

### RG-12  Endless and degenerate ranges                                  D:partial R:yes V:quirk
`1..*` and `"a"..*` iterate forever by `succ`; `*..1` and `-Inf..0`
yield `-Inf` forever (`-Inf.succ` is `-Inf`), `NaN..NaN` yields NaN
forever, `Inf..Inf`, `Inf..NaN` and `Inf..*` yield nothing, `1.5..*`
steps 1.5, 2.5, …; `.list`, `.flat`, `.Seq` and an Array assigned from
an infinite range are all lazy. Roast pins the `-Inf` and NaN streams
(rakudo/rakudo#4297 "no floating point drifts").
```
say (1..*).head(3).raku, " ", ("a"..*).head(3).raku, " ", (*..1).head(2).raku, " ", (-Inf..0).head(2).raku, " ", (NaN..NaN).head(2).raku, " ", (Inf..Inf)[^2].raku, " ", (Inf..NaN)[^2].raku, " ", (1..Inf)[10], " ", (1..*)[10], " ", (1.5..*).head(3).raku, " ", (1..*).is-lazy, " ", (*..1).is-lazy, " ", (1..*).list.is-lazy, " ", do { my @a = 1..*; @a[3] ~ " " ~ @a.is-lazy }, " ", (1..*).flat.is-lazy, " ", (1..*).Seq.is-lazy, " ", (1..*).map(* * 2).head(3).raku, " ", (1..*).grep(* %% 3).head(2).raku, " ", ("aa"..*).head(3).raku, " ", (1e0..*).head(2).raku, " ", (1..*).first(* > 5), " ", (Inf..*).head(2).raku, " ", (1..Inf).elems.^name
# rakudo 2026.08: (1, 2, 3).Seq ("a", "b", "c").Seq (-Inf, -Inf).Seq (-Inf, -Inf).Seq (NaN, NaN).Seq (Nil, Nil) (Nil, Nil) 11 11 (1.5, 2.5, 3.5).Seq True True True 4 True True True (2, 4, 6).Seq (3, 6).Seq ("aa", "ab", "ac").Seq (1e0, 2e0).Seq 6 ().Seq Failure
```
rakupp 4.0.1: differs — `"a"..*` and `"aa"..*` yield 0, 1, 2; `*..1` and
`-Inf..0` yield huge negative Ints; `NaN..NaN` yields one 0; `Inf..Inf`
and `Inf..*` yield the native Int limits; `1.5..*` and `1e0..*` yield
Ints; `(*..1).is-lazy` is False.

### RG-13  reverse walks down from max by `pred`                            D:partial R:partial V:bug
`reverse` returns a Seq. For an Int range it is the elements backwards
and honours both exclusions; for a one-character string range likewise;
an infinite range fails with `X::Cannot::Lazy` (action `.reverse`), and
`-Inf..3` counts down from 3 forever. For any other range Rakudo starts
at `max` (or `max.pred` when excluded) and steps by `pred` while not
before `min`, so the result is NOT the reversed list: `(1.1..4).reverse`
is 4, 3, 2 where the list is 1.1, 2.1, 3.1, and `(1..4.5).reverse` is
4.5, 3.5, 2.5, 1.5 where the list is 1, 2, 3, 4. Do not imitate that.
```
say (1..5).reverse.raku, " ", (1^..5).reverse.raku, " ", ("a".."d").reverse.raku, " ", ("a"^..^"d").reverse.raku, " ", (5..2).reverse.raku, " ", (1.1..4).reverse.raku, " ", (1.1..4).list.reverse.raku, " ", (1..4.5).reverse.raku, " ", (1.5^..^4.5).reverse.raku, " ", (^3).reverse.raku, " ", (2..2).reverse.raku, " ", (2^..2).reverse.raku, " ", ("aa".."ac").reverse.raku, " ", (1..4).reverse.^name, " ", (1..2**70).reverse.head(2).raku, " ", (1e0..3e0).reverse.raku, " ", (1..-Inf).reverse.raku, " ", (1..3).reverse.reverse.raku, " ", ("a".."e").reverse.head(2).raku, " ", (1.5..4).reverse.raku, " ", (1..3.5).reverse.raku, " ", (1..3.5).list.raku, " ", (0.5..^3).reverse.raku, " ", (0.5..^3).list.raku
# rakudo 2026.08: (5, 4, 3, 2, 1).Seq (5, 4, 3, 2).Seq ("d", "c", "b", "a").Seq ("c", "b").Seq ().Seq (4, 3, 2).Seq (3.1, 2.1, 1.1).Seq (4.5, 3.5, 2.5, 1.5).Seq (3.5, 2.5).Seq (2, 1, 0).Seq (2,).Seq ().Seq ("ac", "ab", "aa").Seq Seq (1180591620717411303424, 1180591620717411303423).Seq (3e0, 2e0, 1e0).Seq ().Seq (1, 2, 3).Seq ("e", "d").Seq (4, 3, 2).Seq (3.5, 2.5, 1.5).Seq (1, 2, 3) (2, 1).Seq (0.5, 1.5, 2.5)
say (try (1..*).reverse.raku) // $!.^name, " ", (-Inf..3).reverse.head(3).raku, " ", (NaN..NaN).reverse.head(2).raku, " ", (try (*..*).reverse.^name) // $!.^name, " ", (try (1..*).reverse.head(2).raku) // $!.^name, " ", (try (1..Inf).reverse.raku) // $!.^name, " ", (try ("a"..*).reverse.raku) // $!.^name
# rakudo 2026.08: X::Cannot::Lazy (3, 2, 1).Seq (NaN,).Seq X::Cannot::Lazy X::Cannot::Lazy X::Cannot::Lazy X::Cannot::Lazy
say do { my $r = (1..*).reverse; $r.so; $r.^name ~ ":" ~ $r.exception.^name ~ ":" ~ $r.exception.action }
# rakudo 2026.08: Failure:X::Cannot::Lazy:.reverse
```
The docs say the elements are reversed and still show `(1..∞).reverse`
producing Infs; both are wrong for 2026.08. rakupp 4.0.1: differs —
reversing `1..-Inf` throws "Cannot reverse an infinite range" (Rakudo:
empty), an infinite range throws instead of failing, and
`(-Inf..3).reverse` counts down from the native Int minimum.

### RG-14  first with :end                                                 D:no R:no V:bug
`first(&test, :end)` searches from the end; with `:k` the index is
counted from the FRONT (`(1..10).first(* %% 3, :end, :k)` is 8) and `:p`
pairs that index with the value, but `:kv` returns the index in the
reversed sequence (`(1, 9)`), which contradicts `:k`.
```
say (1..10).first(* %% 3), " ", (1..10).first(* %% 3, :end), " ", (1..10).first(* %% 3, :end, :k), " ", (1..10).first(* %% 3, :end, :p).raku, " ", (1..10).first(* %% 3, :k), " ", (1..10).first(* %% 3, :kv).raku, " ", (1..10).first(* > 100, :end).raku, " ", ("a".."e").first("c", :end, :k), " ", (1..10).first(* %% 3, :end, :v), " ", (1..10).first(* %% 3, :end, :kv).raku, " ", (1..*).first(* %% 7), " ", (1..10).first(* %% 3, :end, :k).^name, " ", (1..10).first(* %% 3, :end, :p).^name, " ", ("a".."e").first("c", :end, :p).raku, " ", (1.5..4.5).first(* > 2, :end), " ", (1.5..4.5).first(* > 2, :end, :k), " ", (1..10).first(* %% 3, :end, :kv).^name
# rakudo 2026.08: 3 9 8 8 => 9 2 (2, 3) Nil 2 9 (1, 9) 7 Int Pair 2 => "c" 4.5 3 List
```
rakupp 4.0.1: matches on every field but the `:end, :kv` one, where it
answers `(8, 9)`, the consistent value; keep that.

### RG-15  Elements are read-only values, and a range does not flatten     D:no R:partial V:spec
Iterating a range hands out values, not containers: `$_++` in a `for`
is `X::Multi::NoMatch`, `$_ = 5` an `X::AdHoc` assignment refusal, a
`-> $x is rw` block `X::Parameter::RW`; `is copy` works. The loop
variable of `for $i..3` never touches `$i`. `(1..3).list[0] = 9` is
`X::Assignment::RO`, and a range bound to an `@` variable stays a Range
(`push` is `X::Immutable`) while one assigned is copied into an Array.
In a list, a range is one item: `for 1..3, 7..8` runs twice and
`$s += $_` adds 3 and 2.
```
say do { my $i = 1; for $i..3 -> $j { }; $i }, " ", do { my @r; for 1..3 { @r.push($_ * 2) }; @r.raku }, " ", (try { for 1..3 { $_++ }; "modified" }) // $!.^name, " ", (try { for 1..3 -> $x is rw { $x++ }; "ok" }) // $!.^name, " ", do { my @a = 1..3; for @a { $_++ }; @a.raku }, " ", (1e0..1e0).map(*.^name).raku, " ", do { my $c = 0; ++$c for (2**130) .. (2**130 + 2); $c }, " ", (1..3).map({ $_ * 2 }).raku, " ", (1..3).grep(* > 1).raku, " ", (1..3).reduce(&[+]), " ", ([+] 1..100), " ", (1..3).join("|"), " ", do { my $c = 0; for 1..3 -> $x is copy { $x++; $c += $x }; $c }, " ", (try { for 1..3 { $_ = 5 }; "assigned" }) // $!.^name, " ", (try { for "a".."c" { $_ = "x" }; "assigned" }) // $!.^name, " ", (try { for 1.5..3 { $_ = 5 }; "assigned" }) // $!.^name, " ", do { my @a = (1..3).list; @a[0] = 9; @a.raku }, " ", (try { (1..3).list[0] = 9; (1..3).list.raku }) // $!.^name, " ", (try { my @l := (1..3).list; @l[0] = 9; @l.raku }) // $!.^name, " ", (1..3).map(-> $x is rw { $x }).^name, " ", (try { (1..3).map(-> $x is rw { $x++ }).eager; "ok" }) // $!.^name, " ", do { my $s = 0; for 1..3 { $s += $_ }; $s }, " ", do { my $s = 0; for 1..* { last if $_ > 5; $s += $_ }; $s }, " ", do { my @a = 1..*; @a[5] }, " ", do { my @a = 1..3; @a.push(4); @a.elems }, " ", do { my @a := 1..3; (try { @a.push(4); "pushed" }) // $!.^name }, " ", do { my @a := 1..3; @a.^name }, " ", do { my $s = 0; for (1..3).reverse { $s += $_ }; $s }, " ", do { my $s = 0; for 1..3, 7..8 { $s += $_ }; $s }, " ", do { my $n = 0; for 1..3 X 1..2 { $n++ }; $n }, " ", do { my $s = ""; for 1..3 -> $a, $b = 0 { $s ~= "$a$b," }; $s }
# rakudo 2026.08: 1 [2, 4, 6] X::Multi::NoMatch X::Parameter::RW [2, 3, 4] ("Num",).Seq 3 (2, 4, 6).Seq (2, 3).Seq 6 5050 1|2|3 9 X::AdHoc X::AdHoc X::AdHoc [9, 2, 3] X::Assignment::RO X::Assignment::RO Seq X::Parameter::RW 6 15 6 4 X::Immutable Range 6 5 6 12,30,
```
rakupp 4.0.1: differs — `$_++` is `X::Assignment::RO`, an `is rw`
pointy block is accepted, `1e0..1e0` yields an Int, a loop over a
big-Int range runs zero times, `@a := 1..3` makes an Array and `push`
succeeds, `for 1..3, 7..8` adds nothing, and the two-parameter block
loses the default (`12,3,`).

## D. Indexing

### RG-16  AT-POS and EXISTS-POS                                             D:yes R:yes V:spec
An Int range answers `[$i]` arithmetically and `[$i]` past the end is
Nil; a non-Int range indexes its list; `[*-1]` works for finite ranges
and fails with `X::Cannot::Lazy` for infinite ones; an index that
resolves negative is a Failure `X::OutOfRange` (`what` "Effective
index", `range` "0..^Inf"); a fractional or string index is truncated
or numified; `EXISTS-POS` is False for a negative or fractional
position and for a Str position.
```
say (1..5)[1], " ", (1..5)[10].raku, " ", (1..*)[10], " ", (1..8)[*-1], " ", (1..8)[1,3].raku, " ", (1..5)[^2].raku, " ", (2..1)[^2].raku, " ", (2.1..1.1)[^2].raku, " ", (1.5..4)[1], " ", ("a".."e")[2], " ", (1..5).EXISTS-POS(4), " ", (1..5).EXISTS-POS(5), " ", (1..5).EXISTS-POS(-1), " ", (1..5).AT-POS(0), " ", (try { (1..5)[0] = 9 }) // $!.^name, " ", (1..5)[*].raku, " ", (1..5)[].raku, " ", (1..*)[^3].raku, " ", (1^..5)[0], " ", (^5)[4], " ", (^5)[5].raku, " ", (1..5)[2.7], " ", (1..5)["2"], " ", (1..5)[1..2].raku, " ", (1..5)[*-2..*].raku, " ", (^5)[*-9].^name, " ", (try (^5)[*-9] + 1) // $!.^name, " ", (try (^5)[*-9] + 1) // $!.what ~ ":" ~ $!.got ~ ":" ~ $!.range, " ", (1.5..4)[*-1], " ", ("a".."e")[*-1], " ", (try (1..*)[*-1].^name) // $!.^name, " ", (1..5)[5].raku, " ", (1..5)[4], " ", (1..5).EXISTS-POS(2.5), " ", ("a".."e").EXISTS-POS(4), " ", (1.5..4).EXISTS-POS(2), " ", (1.5..4).elems, " ", (1..5)[*-1], " ", (1..5)[*-5], " ", (1..*)[0], " ", (1..*)[1000000], " ", (1..5)[1, 10].raku, " ", (1.5..4)[10].raku, " ", ("a".."e")[10].raku, " ", (1..5).EXISTS-POS(0), " ", (1..5).EXISTS-POS("1")
# rakudo 2026.08: 2 Nil 11 8 (2, 4) (1, 2) (Nil, Nil) (Nil, Nil) 2.5 c True False False 1 X::Assignment::RO (1, 2, 3, 4, 5) 1..5 (1, 2, 3) 2 4 Nil 3 3 (2, 3) (4, 5) Failure X::OutOfRange Effective index:-4:0..^Inf 3.5 e X::Cannot::Lazy Nil 5 False True True 3 5 1 1 1000001 (2, Nil) Nil Nil True False
```
rakupp 4.0.1: differs — `EXISTS-POS` is True for -1, 2.5 and "1"; the
Failure's `what` is "Index"; `("a".."e")[10]` is the `Str` type object.

### RG-17  A range as a subscript                                           D:yes R:yes V:spec
`@a[1..*]`, `@a[1..^*]` and `@a[3..*]` are clipped to the source's end
(`ended-by`); positions past the end are Nil for a List and Any for an
Array; `[2..1]` is empty; a Rat range indexes by its elements
(`[1.5..3]` is positions 1.5 and 2.5, so b and c); `[-1..1]` and
`[*-7..1]` throw `X::OutOfRange`; `[*..*]` and `[*..2]` throw
`X::Numeric::CannotConvert` (`-Inf` is not an index); a hash slice with
a range is a slice of stringified keys; `substr(1..*)` and `comb[1..*]`
follow.
```
say <a b c d e>[1..3].raku, " ", <a b c d e>[1..*].raku, " ", <a b c d e>[3..^*].raku, " ", <a b c d e>[^2].raku, " ", <a b c>[1..5].raku, " ", [1,2,3][1..5].raku, " ", <a b c d e>[*-2..*].raku, " ", <a b c d e>[2..1].raku, " ", <a b c d e>[1.5..3].raku, " ", do { my @a = <a b c d e>; @a[1..2] = <x y>; @a.raku }, " ", do { my @a = 1..3; @a[1..*] = 9, 9; @a.raku }, " ", <a b c d e>[1..*][0], " ", <a b c d e>[0..^0].raku, " ", "abcdef".substr(1..3), " ", "abcdef".comb[1..*].raku, " ", <a b c d e>[1^..^4].raku, " ", (try <a b c d e>[*..*].elems) // $!.^name, " ", <a b c d e>[3..*].raku, " ", <a b c d e>[5..*].raku, " ", (try <a b c d e>[-1..1].raku) // $!.^name, " ", (try <a b c d e>[*-7..1].raku) // $!.^name, " ", do { my %h = a => 1; %h{1..2}.raku }, " ", <a b c d e>[(1..3).reverse].raku, " ", <a b c d e>[1..^*].raku, " ", <a b c d e>[^*].raku, " ", <a b c d e>[0..*-2].raku, " ", (try <a b c d e>[*..2].raku) // $!.^name, " ", "abcdef".substr(2..*), " ", "abcdef".comb[^3].raku, " ", <a b c d e>[6..*].raku, " ", <a b c d e>[1^..*].raku, " ", <a b c d e>[3..10].elems, " ", do { my @a = <a b c>; @a[1..5].raku }, " ", do { my @a = <a b c>; @a[1..*].raku }, " ", "abcdef".substr(1..*)
# rakudo 2026.08: ("b", "c", "d") ("b", "c", "d", "e") ("d", "e") ("a", "b") ("b", "c", Nil, Nil, Nil) (2, 3, Any, Any, Any) ("d", "e") () ("b", "c") ["a", "x", "y", "d", "e"] [1, 9, 9] b () bcd ("b", "c", "d", "e", "f") ("c", "d") X::Numeric::CannotConvert ("d", "e") () X::OutOfRange X::OutOfRange (Any, Any) ("d", "c", "b") ("b", "c", "d", "e") ("a", "b", "c", "d", "e") ("a", "b", "c", "d") X::Numeric::CannotConvert cdef ("a", "b", "c") () ("c", "d", "e") 8 ("b", "c", Any, Any, Any) ("b", "c") bcdef
```
rakupp 4.0.1: differs in three fields — `[1.5..3]` gives b, c, d;
`[*..*]` has 1 element and `[*..2]` is empty instead of throwing.

### RG-18  A Range is immutable                                             D:no R:yes V:spec
`push`, `pop`, `shift`, `unshift`, `append`, `prepend` throw
`X::Immutable` with `.typename` "Range" and `.method` the name; `splice`
has no candidate (`X::Multi::NoMatch`); `[0] = 9` and `ASSIGN-POS` are
`X::Assignment::RO`, `BIND-POS` and `$r[0] := 9` are `X::Bind`; the
attribute accessors are read-only (`X::Assignment::RO`). `.list.push`
fails the same way for the List; `.Array.push` works on the copy.
```
say (try { (1..5).push(6) }) // $!.^name ~ ":" ~ $!.typename ~ ":" ~ $!.method, " ", (try { (1..5).pop }) // $!.^name ~ ":" ~ $!.method, " ", (try { (1..5).shift }) // $!.method, " ", (try { (1..5).unshift(0) }) // $!.method, " ", (try { (1..5).append(6) }) // $!.method, " ", (try { (1..5).prepend(0) }) // $!.method, " ", (try { my $r = 1..5; $r.min = 2 }) // $!.^name, " ", (try { (1..5).excludes-min = True }) // $!.^name, " ", (try { my $r = 1..5; $r.list.push(6); $r.elems }) // $!.^name ~ ":" ~ $!.typename, " ", do { my @a = 1..3; @a.push(4); @a.raku }, " ", (try { (1..5).splice(0, 1) }) // $!.^name, " ", (try { (1..5).BIND-POS(0, 9) }) // $!.^name, " ", (try { (1..5).ASSIGN-POS(0, 9) }) // $!.^name
# rakudo 2026.08: X::Immutable:Range:push X::Immutable:pop shift unshift append prepend X::Assignment::RO X::Assignment::RO X::Immutable:List [1, 2, 3, 4] X::Multi::NoMatch X::Bind X::Assignment::RO
say (try { (1..5).push(6) }) // $!.^name, " ", (try { (1..5).pop }) // $!.^name, " ", (try { (1..5).shift }) // $!.^name, " ", (try { (1..5).unshift(0) }) // $!.^name, " ", (try { (1..5).append(6) }) // $!.^name, " ", (try { (1..5).prepend(0) }) // $!.^name, " ", (try { my $r = 1..5; $r.min = 2 }) // $!.^name, " ", (try { (1..5).excludes-min = True }) // $!.^name, " ", (try { my $r = 1..5; $r.list.push(6); $r.elems }) // $!.^name, " ", (try { (1..5).splice(0, 1) }) // $!.^name, " ", (try { (1..5).BIND-POS(0, 9) }) // $!.^name, " ", (try { (1..5).ASSIGN-POS(0, 9) }) // $!.^name, " ", (try { (1..5)[0] = 9 }) // $!.^name, " ", (try { my $r = 1..5; $r[0] := 9 }) // $!.^name, " ", (try { my $r = 1..5; $r.Array.push(6).elems }) // $!.^name
# rakudo 2026.08: X::Immutable X::Immutable X::Immutable X::Immutable X::Immutable X::Immutable X::Assignment::RO X::Assignment::RO X::Immutable X::Multi::NoMatch X::Bind X::Assignment::RO X::Assignment::RO X::Bind 6
```
rakupp 4.0.1: differs — the six mutators, `splice`, `BIND-POS` and
`ASSIGN-POS` are `X::Method::NotFound`, and `$r[0] := 9` binds (9).

## E. Matching

### RG-19  A number against a range                                        D:partial R:yes V:spec
An Int against an Int range compares with exclusions; against a range
with two Numeric endpoints the topic is numified, a Str topic through
`.Numeric` so `"42"`, `" 3 "` and `"0x10"` match numerically and a
string that does not parse is simply False (no exception); `Inf`,
`-Inf` and big Ints match the open ends; NaN never matches; `True` is 1.
When only one endpoint is Numeric (`*.."5"`), comparison is the generic
`before`/`after`, so 42 ~~ *.."5" is True (string order) but
42 ~~ *..5 is False. The docs' claims that `'raku'` matches `1..*` and
`-∞..∞` are false in 2026.08.
```
say 3 ~~ 1..5, " ", 2.5 ~~ 1..5, " ", 5.0e0 ~~ 1..5, " ", 5 ~~ 1..^5, " ", 1 ~~ 1^..5, " ", 5.001 ~~ 1..5, " ", "42" ~~ 20..50, " ", "13" ~~ 20..50, " ", "abc" ~~ 1..10, " ", "5" ~~ *..10, " ", "raku" ~~ 1..*, " ", "raku" ~~ -Inf..Inf, " ", "raku" ~~ -Inf^..^Inf, " ", 1..10 ~~ "5", " ", 42 ~~ *.."5", " ", 42 ~~ *..5, " ", 42 ~~ *.."3", " ", 1.5 ~~ 1^..^2, " ", 4.5 ~~ 0..^5, " ", 5 ~~ ^5, " ", -0.1 ~~ ^5, " ", 0 ~~ ^5, " ", 4.9999999999999999999999 ~~ 0..^5, " ", 3 ~~ 1.5..3.5, " ", 3 ~~ 1.5^..^3, " ", Inf ~~ 1..*, " ", -Inf ~~ *..1, " ", NaN ~~ 1..5, " ", NaN ~~ *..*, " ", 3 ~~ Range, " ", 2**70 ~~ 1..*, " ", 1.5 ~~ 1..2, " ", "1.5" ~~ 1..2, " ", " 3 " ~~ 1..5, " ", "0x10" ~~ 10..20, " ", True ~~ 0..1, " ", 3 ~~ 3..3, " ", 3 ~~ 3^..3
# rakudo 2026.08: True True True False False False True False False True False False False False True False False True True False False True True True False True True False False False True True True True True True True False
```
rakupp 4.0.1: differs in five fields — `"raku"` matches `-Inf..Inf` and
`-Inf^..^Inf`, `42 ~~ *.."5"` is False, and `Inf ~~ 1..*`,
`-Inf ~~ *..1`, `2**70 ~~ 1..*` are False.

### RG-20  A string against a string range                                D:yes R:yes V:spec
With a Str endpoint the comparison is string order (`before`/`after`),
so `'ax'` is inside `'aa'..'zz'` and `'a'..'zz'`, an Int topic is
stringified (`42 ~~ "3".."9"` is True, `42 ~~ "5".."9"` False), `""`
is before `"a"`, and an open end (`'c'..*`, `*..'c'`) keeps string order.
```
say 'x' ~~ 'a'..'z', " ", 'x' ~~ 'a'..'c', " ", 'ax' ~~ 'aa'..'zz', " ", 'ax' ~~ 'a'..'zz', " ", 0 ~~ 'a'..'g', " ", 'd' ~~ 'c'..*, " ", 'b' ~~ 'c'..*, " ", 'b' ~~ *..'c', " ", 'd' ~~ *..'c', " ", ' ' ~~ ' '..'A', " ", 42 ~~ "3".."9", " ", "42" ~~ "3".."9", " ", 42 ~~ "5".."9", " ", 42 ~~ "1".."3", " ", "abc" ~~ "a".."b", " ", "b" ~~ "a"^..^"c", " ", "a" ~~ "a"^..^"c", " ", "c" ~~ "a"^..^"c", " ", "aa" ~~ "a".."b", " ", "B" ~~ "a".."z", " ", "é" ~~ "a".."z", " ", "b" ~~ "a".."c", " ", 1.5 ~~ "1".."2", " ", "" ~~ "a".."z", " ", "z" ~~ "a".."y"
# rakudo 2026.08: True False True True False True False True False True True True False False True True False False True False False True True False False
```
rakupp 4.0.1: differs — `'ax'` is outside both `'aa'..'zz'` and
`'a'..'zz'`, and the open-ended `'c'..*` / `*..'c'` answer the wrong way.

### RG-21  A range against a range                                          D:yes R:yes V:spec
A numeric range is inside another when its min is greater, or equal
without adding an excluded point, and its max less or equal likewise;
a string range uses `gt`/`lt` the same way; mixed string and number
compare as strings on a string range and never match on a numeric one;
an infinite outer range accepts everything; an empty range with
in-range endpoints (`5..1 ~~ 1..5`) still matches; the types are not
compared (`1..2 ~~ 1e0..2e0`), but a mixin on the outer range is a
different type for `~~` too.
```
say 2..3 ~~ 1..12, " ", 1..10 ~~ -∞..∞, " ", 1..10 ~~ -∞^..^∞, " ", 1..2 ~~ *..10, " ", 2..5 ~~ 1..*, " ", 'a'..'j' ~~ 'b'..'c', " ", 'b'..'c' ~~ 'a'..'j', " ", 1^..5 ~~ 1..5, " ", 1..5 ~~ 1^..5, " ", 1..5 ~~ 1..5, " ", 1..^5 ~~ 1..^5, " ", 1..5 ~~ 1..^5, " ", 1..^5 ~~ 1..5, " ", 0..5 ~~ 1..5, " ", 1.5..2.5 ~~ 1..3, " ", 1..3 ~~ 1.5..2.5, " ", "a".."c" ~~ 1..5, " ", 1..5 ~~ "a".."c", " ", (1..5).ACCEPTS(2..3), " ", ^5 ~~ 0..4, " ", 0..4 ~~ ^5, " ", (1..*) ~~ (1..*), " ", (1..2) ~~ ((1..2) but role {}), " ", (5..1) ~~ (1..5), " ", (1..5) ~~ (5..1), " ", (2..2) ~~ (1..3), " ", ("b".."b") ~~ ("a".."c"), " ", (1..3) ~~ (1..*), " ", (1..*) ~~ (1..3), " ", (1.5..2) ~~ (1..2), " ", (1..2) ~~ (1.5..2), " ", (1..2) ~~ (1e0..2e0), " ", ("1".."2") ~~ (1..2), " ", (1..2) ~~ ("1".."2"), " ", (1..2) ~~ ("a".."z"), " ", ("1".."2") ~~ ("a".."z"), " ", (*..*) ~~ (1..2), " ", (1..2) ~~ (*..*)
# rakudo 2026.08: True True True True True False True True False True True False True False True False False False True False True True True True False True True True False True False True True True False False False True
```
rakupp 4.0.1: differs in three fields — the mixin case is False, and a
string range against a numeric range (`"1".."2" ~~ 1..2`) or the reverse
is False.

### RG-22  Other topics, junctions, in-range                               D:partial R:partial V:spec
A Junction threads; a Complex matches when its imaginary part rounds
away; any Any topic that supports `cmp` with the endpoints works, so a
Date is inside a Date range and iterates by day, a Version inside a
Version range; the `Any` type object is False; a Mu that cannot be
compared throws `X::Range::Incomparable` (`.topic`, `.endpoint`,
`.what-endpoint`). `in-range` returns True or throws `X::OutOfRange`
whose `.what` is the optional name (default "Value"), `.got` the
topic's `.raku` and `.range` the range's gist.
```
say i ~~ 1..10, " ", <0+0i> ~~ -1..10, " ", <42+0i> ~~ 10..50, " ", <42+0.0000000000000001i> ~~ 40..50, " ", <42+0i> ~~ 10e0..50e0, " ", i ~~ "a".."z", " ", so (3,0).one ~~ 1..9, " ", so (3|20) ~~ 1..9, " ", so (3&20) ~~ 1..9, " ", (1..3).ACCEPTS(2), " ", (1..3).ACCEPTS(4), " ", 2 ~~ (1..3), " ", (1..5).grep(2..3).raku, " ", (1..10).grep(* ~~ 3..4).raku, " ", <1 2 3 10>.grep(2..5).raku, " ", "10" ~~ 2..5, " ", 10 ~~ "2".."5", " ", (1..3).ACCEPTS(2).^name, " ", (so 3 ~~ (1..5)|(10..15)), " ", <a 2 b>.grep(1..5).raku, " ", so ("3"|"x") ~~ 1..5, " ", so ("3"&"x") ~~ 1..5, " ", (1..3).ACCEPTS(Any), " ", (try (1..3).ACCEPTS(Mu)) // $!.^name, " ", 2 ~~ (1..3).list, " ", (1..3) ~~ (1..3).list, " ", (1..3).list ~~ (1..3)
# rakudo 2026.08: False True True True True False True True False True False True (2, 3).Seq (3, 4).Seq (IntStr.new(2, "2"), IntStr.new(3, "3")).Seq False False Bool True (IntStr.new(2, "2"),).Seq True False False X::Range::Incomparable False True True
say Date.new("2020-06-15") ~~ Date.new("2020-01-01")..Date.new("2020-12-31"), " ", Date.new("2021-06-15") ~~ Date.new("2020-01-01")..Date.new("2020-12-31"), " ", (Date.new("2020-01-01")..Date.new("2020-01-03")).list.raku, " ", (Date.new("2020-01-01")..Date.new("2020-01-03")).elems, " ", (try { my $r = 1..5; class C { }; C.new ~~ $r }) // $!.^name, " ", (try { class M is Mu { }; M.new ~~ 1..5 }) // $!.^name, " ", (v1.0..v2.0).ACCEPTS(v1.5), " ", v3 ~~ v1..v2, " ", (1..5).in-range(3), " ", (try (1..5).in-range(7)) // $!.^name, " ", (try (1..5).in-range(7, "Level")) // $!.message, " ", (try ('א'..'ת').in-range('p', "Letter")) // $!.message, " ", (try (1..5).in-range("x")) // $!.message, " ", (Date.new("2020-01-01")..Date.new("2020-01-03")).reverse.raku, " ", (Date.new("2020-01-01")^..^Date.new("2020-01-04")).list.raku, " ", (Date.new("2020-01-01")..Date.new("2020-01-03")).^name, " ", (try (1..5).in-range(7)) // $!.what ~ ":" ~ $!.got ~ ":" ~ $!.range, " ", (1..5).in-range(3).^name, " ", (try (1..5).in-range(2.5)) // $!.^name, " ", (try ("a".."c").in-range("d")) // $!.message, " ", (Date.new("2020-01-01")..Date.new("2020-01-03")).is-int, " ", (Date.new("2020-01-01")..Date.new("2020-01-03")).min.^name
# rakudo 2026.08: True False (Date.new(2020,1,1), Date.new(2020,1,2), Date.new(2020,1,3)) 3 False X::TypeCheck::Binding True False True X::OutOfRange Level out of range. Is: 7, should be in 1..5 Letter out of range. Is: "p", should be in "א".."ת" Value out of range. Is: "x", should be in 1..5 (Date.new(2020,1,3), Date.new(2020,1,2), Date.new(2020,1,1)).Seq (Date.new(2020,1,2), Date.new(2020,1,3)) Range Value:7:1..5 Bool True Value out of range. Is: "d", should be in "a".."c" False Date
```
(The first line also warns "Use of uninitialized value of type Any in
string context" for the `Any` topic.) rakupp 4.0.1: differs — the
Complex with a tiny imaginary part is rejected, `ACCEPTS(Mu)` is False
rather than `X::Range::Incomparable`, and a Date range is a List, so
`is-int` and the rest of the second line die.

## F. Arithmetic, comparison, aggregates

### RG-23  `+ - * /` with a Real shift or scale both endpoints             D:yes R:yes V:quirk
`$r + $v`, `$v + $r`, `$r - $v`, `$r * $v`, `$v * $r`, `$r / $v` build
a new range of the same type (a subclass or mixin survives) with the
exclusions kept and the endpoints' types following arithmetic
(`(1..10)/2` is `0.5..5.0`, `+ 1e0` gives Nums, `+ 2**65` big Ints,
`(1..*) * 0` is `0..NaN`). Anything else falls to the generic operator
on `.Numeric`: `1 - (1..10)` is -9, `2 / (1..10)` is 0.2,
`(1..10) + "1"` is 11, `(1..10) + 1i` a Complex. A string range plus a
Real yields a range whose endpoints are Failures (`"a" + 1`).
```
say ((1..10) + 1).raku, " ", ((1..10) - 1).raku, " ", ((1..10) * 2).raku, " ", ((1..10) / 2).raku, " ", (1 + (1..10)).raku, " ", (2 * (1..^10)).raku, " ", ((1..^10) - 1).raku, " ", ((1^..^10) / 2).raku, " ", (^4 + 2).list.raku, " ", ((^4) / 2).list.raku, " ", ((^4) * 2).list.raku, " ", (1..2 + 2).raku, " ", ((1..10) + 0.5).raku, " ", ((1..10) + 1e0).raku, " ", ((1..3) + 2**65).raku, " ", ((1..10) + 1).is-int, " ", ((1..10) / 2).is-int, " ", (try ((1..10) + 1i).raku) // $!.^name, " ", (try (1 - (1..10)).raku) // $!.^name, " ", (try (2 / (1..10)).raku) // $!.^name, " ", (try ((1..10) + "1").raku) // $!.^name, " ", (((2..^5) but role Meows {}) + 5).^name, " ", ((1..*) + 1).raku, " ", ((*..*) * 2).raku, " ", ((1..*) * 0).raku, " ", ((1..10) × 2).raku, " ", ((1..10) ÷ 2).raku, " ", ((1..10) − 1).raku, " ", (try (("a".."c") + 1).raku) // $!.^name, " ", ((1..10) * -1).raku, " ", ((1..10) * -1).elems, " ", ((1..10) + 1).^name, " ", ((1..10) * 2.5).raku
# rakudo 2026.08: 2..11 0..9 2..20 0.5..5.0 2..11 2..^20 ^9 0.5^..^5.0 (2, 3, 4, 5) (0.0, 1.0) (0, 1, 2, 3, 4, 5, 6, 7) 1..4 1.5..10.5 2e0..11e0 36893488147419103233..36893488147419103235 True False <10+1i> -9 0.2 11 Range+{Meows} 2..Inf -Inf..Inf 0..NaN 2..20 0.5..5.0 0..9 Failure.new(exception => X::Str::Numeric.new(source => "a", pos => 0, reason => "base-10 number must begin with valid digits or '.'"), backtrace => Backtrace.new)..Failure.new(exception => X::Str::Numeric.new(source => "c", pos => 0, reason => "base-10 number must begin with valid digits or '.'"), backtrace => Backtrace.new) -1..-10 0 Range 2.5..25.0
```
The Failure endpoints are the quirk. rakupp 4.0.1: differs — Rat and
Num endpoints print without their type (`0.5..5`, `(0, 1)`), `+ 0.5`,
`+ 1e0`, `+ 2**65`, `+ 1i`, `1 - r`, `2 / r`, `+ "1"`, `(1..*) + 1`,
`(*..*) * 2` and `(1..*) * 0` are all wrong, and `("a".."c") + 1` adds
to the codepoints (`98..100`).

### RG-24  cmp and sorting                                                 D:yes R:no V:spec
Two ranges compare by min, then by excludes-min, then max, then the
reverse of excludes-max; a Real on one side is treated as the range
`$n..$n`; a Positional is compared with the range's list; `<=>` and
`leg` coerce (Numeric and Str). So `(1..2) cmp 1` is More,
`(1^..2) cmp (1..2)` is More, `(0..^1)` sorts before `(0..1)`, and
`(1..2) cmp [1,2]` is Same.
```
say (1..2) cmp (1..2), " ", (1..2) cmp (1..3), " ", (1..4) cmp (1..3), " ", (1..2) cmp 3, " ", (1..2) cmp 1, " ", 3 cmp (1..2), " ", (1..2) cmp [1,2], " ", [1,2] cmp (1..2), " ", (1..2) cmp [1,3], " ", (1^..2) cmp (1..2), " ", (1..^2) cmp (1..2), " ", (1..2) cmp (1..^2), " ", (1..2) cmp 1.5, " ", ("a".."c") cmp ("a".."d"), " ", ((1..2) <=> (1..3)), " ", (1..2) eqv (2..1), " ", (1..2) cmp (1..2.0), " ", (1..3).sort.raku, " ", ((1..3), (1..2), (0..5)).sort.raku, " ", ((1..3) leg (1..3)), " ", (1..3) before (2..3), " ", (try ((1..2) cmp "1 2")) // $!.^name, " ", ((0..1), (0..^1), (0^..1)).sort.raku, " ", ((1..3) cmp (1..3).list), " ", ((1..2) cmp (1,2,3)), " ", ((1..2) cmp 2), " ", (1..2) cmp (1..*), " ", (1..*) cmp (1..2), " ", ((1..2) cmp (1..2)).^name, " ", ((1..3), (1..2)).min.raku, " ", ("a".."c") cmp ("b".."c"), " ", (1..2) cmp (0..2), " ", (1..3) cmp 3, " ", (3..3) cmp 3, " ", 1 cmp (1..2), " ", (1..2) cmp (1..2).Str, " ", ((1..2) leg (1..3)), " ", (try ((1..2) cmp "a")) // $!.^name, " ", ((1..2) cmp (1..2)) === Same
# rakudo 2026.08: Same Less More Less More More Same Same Less More Less More Less Less Less False Same (1, 2, 3).Seq (0..5, 1..2, 1..3).Seq Same True Same (^1, 0..1, 0^..1).Seq Same Less Less Less More Order 1..2 Less More Less Same Less Same Less Less True
```
rakupp 4.0.1: matches.

### RG-25  sum                                                            D:partial R:no V:spec
When integer bounds exist (RG-26), `sum` is the closed form, so
`(1..10**20).sum` and `(0..5.5).sum` (0 to 5) are instant; an open end
gives `Inf` or `-Inf`, both open NaN; a non-integral start sums the
list; a string range fails with `X::Str::Numeric`; an empty range sums
to 0.
```
say (1..10).sum, " ", (1..10**20).sum, " ", (1^..^10).sum, " ", (1..*).sum, " ", (*..5).sum, " ", (*..*).sum, " ", (1.5..3).sum, " ", (0..5.5).sum, " ", (1.5..4).sum, " ", (^5).sum, " ", (5..1).sum, " ", (1..1).sum, " ", (-Inf..-Inf).sum, " ", (try ("a".."c").sum) // $!.^name, " ", (1..10).minmax.raku, " ", (3.5..4.5).minmax.raku, " ", ("a".."z").minmax.raku, " ", (-Inf..Inf).minmax.raku, " ", (^10).minmax.raku, " ", (1..5).bounds.raku, " ", (1^..^5).bounds.raku, " ", (1..*).bounds.raku, " ", (1..*).minmax.raku, " ", (1..0).minmax.raku, " ", (1..3).sum.^name, " ", (1.5..3).sum.^name, " ", (1..10).minmax(:by(-*)).raku, " ", ((1..3) minmax (5..6)).raku, " ", (1e0..3e0).sum, " ", (1e0..3e0).sum.^name, " ", (1..*).sum.^name, " ", (^Inf).sum, " ", (1..0).sum, " ", (1.5..1).sum, " ", (1..2**70).sum.chars, " ", (0..^5).sum, " ", (0.5..5).sum, " ", (0.5..5).list.raku, " ", (1..5e0).sum, " ", (1..5e0).sum.^name, " ", (1..5.0).sum.^name, " ", (-5..5).sum, " ", (1..3).minmax.^name, " ", (1..3).minmax.raku, " ", (1..3.5).minmax.raku, " ", (1..*).bounds.^name
# rakudo 2026.08: 55 5000000000000000000050000000000000000000 44 Inf -Inf NaN 4 15 7.5 10 0 1 -Inf X::Str::Numeric (1, 10) (3.5, 4.5) ("a", "z") (-Inf, Inf) (0, 9) (1, 5) (1, 5) (1, Inf) (1, Inf) (1, 0) Int Rat 10..1 1..6 6 Int Num Inf 0 0 42 10 12.5 (0.5, 1.5, 2.5, 3.5, 4.5) 15 Int Int 0 List (1, 3) (1, 3.5) List
```
(The `minmax` fields belong to RG-26; note that `minmax(:by)` hands the
job to the List and comes back as a Range, `10..1`.) rakupp 4.0.1:
matches on every `sum` field; the `minmax` fields differ (RG-26).

### RG-26  int-bounds and minmax                                            D:yes R:yes V:spec
`int-bounds` returns the first and last integer the range iterates
when the start is a whole number (`(0..5.5)` is (0, 5), `(1..^5.0)` is
(1, 4), a big Int end is kept, an empty range gives a reversed pair)
and a Failure `X::AdHoc` ("Cannot determine integer bounds") for a
fractional start, a string range, NaN or an infinite end; the two-
argument form stores into its arguments and returns a Bool. `minmax`
is `int-bounds` for an Int range, `(min, max)` otherwise, and a Failure
`X::AdHoc` ("Cannot return minmax on Range with excluded ends") for a
non-Int range with an excluded end, `^Inf` included.
```
say (2..5).int-bounds.raku, " ", (2..^5).int-bounds.raku, " ", (2^..5).int-bounds.raku, " ", (^10).int-bounds.raku, " ", (0..5.5).int-bounds.raku, " ", (0..^5.5).int-bounds.raku, " ", (1..^5.0).int-bounds.raku, " ", (0..5e0).int-bounds.raku, " ", ((1..5).int-bounds(my $lo, my $hi)) ~ ":$lo:$hi", " ", ((1.5..5).int-bounds(my $l2, my $h2)) ~ ":" ~ $l2.raku, " ", (1..2**70).int-bounds.raku, " ", (1^..^2).int-bounds.raku, " ", (5..1).int-bounds.raku, " ", ((1.0..5).int-bounds).raku, " ", ((1..5.0).int-bounds).raku, " ", ((1e0..5).int-bounds).raku, " ", ((1..5e0).int-bounds).raku, " ", ((1.0..5).int-bounds(my $l3, my $h3)) ~ ":$l3:$h3", " ", (1.5..3).int-bounds.^name, " ", (try (1.5..3).int-bounds.raku) // $!.^name ~ ":" ~ $!.message, " ", (try (NaN..1).int-bounds.raku) // $!.^name, " ", (try (1..Inf).int-bounds.raku) // $!.^name, " ", (try (-Inf..1).int-bounds.raku) // $!.^name, " ", (try ("a".."c").int-bounds.raku) // $!.^name, " ", (try (1..NaN).int-bounds.raku) // $!.^name, " ", ((1..Inf).int-bounds(my $l4, my $h4)) ~ ":" ~ $l4.raku, " ", (0..5.5).int-bounds.^name, " ", (1..*).int-bounds.^name, " ", (2..1).int-bounds.raku, " ", (1..0.5).int-bounds.raku, " ", (0..0.5).int-bounds.raku, " ", (0^..^0.5).int-bounds.raku
# rakudo 2026.08: (2, 5) (2, 4) (3, 5) (0, 9) (0, 5) (0, 5) (1, 4) (0, 5) True:1:5 False:Any (1, 1180591620717411303424) (2, 1) (5, 1) (1, 5) (1, 5) (1, 5) (1, 5) True:1:5 Failure X::AdHoc:Cannot determine integer bounds X::AdHoc X::AdHoc X::AdHoc X::AdHoc X::AdHoc False:Any List Failure (2, 1) (1, 0) (0, 0) (1, 0)
say (try (^Inf).minmax.raku) // $!.^name ~ ":" ~ $!.message, " ", (try (1.5..^3).minmax.raku) // $!.^name ~ ":" ~ $!.message, " ", (^Inf).minmax.^name, " ", (try (1^..^Inf).minmax.raku) // $!.^name, " ", (try (1.5^..3).minmax.raku) // $!.^name, " ", (try ("a"^.."c").minmax.raku) // $!.^name, " ", (1^..^5).minmax.raku, " ", (1^..5).minmax.raku, " ", (1..^5).minmax.raku
# rakudo 2026.08: X::AdHoc:Cannot return minmax on Range with excluded ends X::AdHoc:Cannot return minmax on Range with excluded ends Failure X::AdHoc X::AdHoc X::AdHoc (2, 4) (2, 5) (1, 4)
```
rakupp 4.0.1: differs — `int-bounds` is missing, and `minmax` never
fails: it returns native-Int limits for `^Inf`, `(1, 2)` for `1.5..^3`,
`(3, 4)` for `3.5..4.5`, codepoints for a string range.

### RG-27  min and max with :k, :kv, :p                                    D:no R:yes V:quirk
`min` is the raw start; `min(:k)` is 0, `min(:kv)` `(0, min)`, `min(:p)`
`0 => min`. `max` is the raw end; `max(:k)` is `.end`, the index of the
LAST ELEMENT, even when the end is excluded, so `(2^..^6).max(:kv)` is
`(2, 6)` although neither 2 nor 6 is an element/index pair; for an
infinite range the key is `Inf`; for an empty range it is -1. `:by`
hands over to the List.
```
say (2..6).min, " ", (2..6).max, " ", (2..6).min(:k), " ", (2..6).min(:kv).raku, " ", (2..6).min(:p).raku, " ", (2..6).max(:k), " ", (2..6).max(:kv).raku, " ", (2..6).max(:p).raku, " ", (2..6).min(:!k), " ", (2..Inf).max(:k), " ", (2..Inf).max(:p).raku, " ", (2^..^6).min, " ", (2^..^6).max, " ", (2^..^6).max(:k), " ", (2^..^6).min(:k), " ", min(2..6, :k), " ", max(2..6, :p).raku, " ", (2..6).min(:by(-*)), " ", (2..6).max(:by(-*)), " ", ("a".."c").max(:k), " ", (1..0).max(:k), " ", (1..0).min(:kv).raku, " ", (1.5..3).max(:k), " ", (1..5).min.^name, " ", (1..*).max.^name, " ", ("a".."c").min, " ", (2..6).max(:!kv), " ", (5..1).min, " ", (5..1).max, " ", (^3).max, " ", (^3).max(:k), " ", (^3).max(:kv).raku, " ", (^0).max(:k), " ", (^0).max(:p).raku
# rakudo 2026.08: 2 6 0 (0, 2) 0 => 2 4 (4, 6) 4 => 6 2 Inf Inf => Inf 2 6 2 0 0 4 => 6 6 2 2 -1 (0, 1) 1 Int Num a 6 5 1 3 2 (2, 3) -1 -1 => 0
```
rakupp 4.0.1: differs — the adverbs are ignored (`min(:k)` is 2,
`max(:kv)` is 6), `:by` is ignored, `("a".."c").max(:k)` is "c".

### RG-28  pick and roll                                                  D:yes R:yes V:spec
`pick` and `roll` without an argument return one element (an Int for
an Int range without building the list, a Str for a string range) or
Nil from an empty range; with a count they return a Seq (empty for an
empty range or a count of 0 or less, a Str count is coerced, a string
count that does not parse throws `X::Str::Numeric`); `pick(*)` and
`roll(*)` are Seqs; a vast Int range picks without enumerating; an
infinite range answers Nil.
```
say (1..100).pick.^name, " ", (1..100).pick ~~ 1..100, " ", (1..100).pick(1).^name, " ", (1..100).pick(*).elems, " ", (1..100).pick(*).sort.head(3).raku, " ", (1..100).pick(200).elems, " ", (1..100).pick("3").elems, " ", ('b'..'y').pick.^name, " ", ('b'..'y').pick(*).elems, " ", (1..100).roll.^name, " ", (1..100).roll(5).elems, " ", (1..100).roll(*).head(5).elems, " ", (1..0).pick.raku, " ", (1..0).pick(3).raku, " ", (1..0).roll.raku, " ", (1..0).roll(2).raku, " ", (1..0).roll(*).raku, " ", (1..5).pick(-1).raku, " ", (1..5).roll(0).raku, " ", ((1 +< 125) .. (1 +< 126 - 1)).pick(3).elems, " ", ((1 +< 125) .. (1 +< 126 - 1)).roll(*).head(2).elems, " ", (1.5..3.5).pick.^name, " ", (1.5..3.5).pick(*).sort.raku, " ", (1..*).pick.raku, " ", (1..*).roll.raku, " ", (1..5).pick(2).elems, " ", (1..5).pick(2).unique.elems, " ", (1..5).roll(*).^name, " ", (1..5).pick(*).^name, " ", (1..5).pick(1).elems, " ", (^100).pick(*).elems, " ", (1..2).pick(*).sort.raku, " ", (1..5).pick(2.9).elems, " ", (1..5).pick(1e0).elems, " ", (try (1..5).pick("x")) // $!.^name
# rakudo 2026.08: Int True Seq 100 (1, 2, 3).Seq 100 3 Str 24 Int 5 5 Nil ().Seq Nil ().Seq ().Seq ().Seq ().Seq 3 2 Rat (1.5, 2.5, 3.5).Seq Nil Nil 2 2 Seq Seq 1 100 (1, 2).Seq 2 1 X::Str::Numeric
```
rakupp 4.0.1: differs — `(1..0).pick(3)` is a List, `roll(*)` on a vast
range yields nothing, `pick` and `roll` on `1..*` return huge Ints, and
`pick("x")` is an empty List.

### RG-29  rand                                                           D:yes R:yes V:spec
`rand` needs two Real endpoints (a string range gives a Failure
`X::AdHoc`, "Can only get a random value on Real values") and fails
with `X::Range::Rand::InvalidEndpoints` (`.min`, `.max`) when the start
is not below the end, either end is Inf or NaN, or the endpoints are
within 1e-15 of each other; it returns a Num, and an excluded endpoint
is never returned.
```
say (1..10).rand.^name, " ", 1 <= (1..10).rand < 10, " ", 0 <= (^10).rand < 10, " ", (0.1^..0.3).rand.^name, " ", (try ("a".."z").rand.raku) // $!.^name, " ", (try (1..1).rand.raku) // $!.^name, " ", (try (5..1).rand.raku) // $!.^name, " ", (try (1..Inf).rand.raku) // $!.^name, " ", (try (NaN..1).rand.raku) // $!.^name, " ", (try (1..1+1e-16).rand.raku) // $!.^name, " ", (1.5..2.5).rand.^name, " ", (1e0..2e0).rand.^name, " ", do { my $s = 0; for ^2000 { $s = 1 if (1..^(1+10e-15)).rand == 1+10e-15 }; $s }, " ", (1..2).rand ~~ 1..2, " ", (try ("1".."9").rand.raku) // $!.^name, " ", (try (1..2).rand(3)) // $!.^name, " ", (1..2).rand < 2, " ", (^1).rand < 1, " ", do { my @r = (1^..^2).rand xx 100; @r.grep(1 < * < 2).elems }, " ", (try (1..1).rand.raku) // $!.message.lines[0], " ", (try (5..1).rand.raku) // $!.message.lines[0], " ", (try (1..1+1e-16).rand.raku) // $!.message.lines[0], " ", (try ("a".."z").rand.raku) // $!.message, " ", ("a".."z").rand.^name, " ", (1..1).rand.^name, " ", (1..*).rand.^name, " ", (try (1..*).rand.raku) // $!.^name, " ", (try (*..1).rand.raku) // $!.^name, " ", (try (1..NaN).rand.raku) // $!.^name, " ", (1..2**70).rand.^name, " ", (1..2**70).rand < 2**70
# rakudo 2026.08: Num True True Num X::AdHoc X::Range::Rand::InvalidEndpoints X::Range::Rand::InvalidEndpoints X::Range::Rand::InvalidEndpoints X::Range::Rand::InvalidEndpoints X::Range::Rand::InvalidEndpoints Num Num 0 True X::AdHoc X::AdHoc True True 100 Impossible to generate random numbers for a range where endpoints are equal Impossible to get a random number from range containing no values. Impossible to generate random numbers for a range where endpoints are equal Can only get a random value on Real values, did you mean .pick? Failure Failure Failure X::Range::Rand::InvalidEndpoints X::Range::Rand::InvalidEndpoints X::Range::Rand::InvalidEndpoints Num True
say do { my $f = (1..1).rand; $f.so; $f.exception.^name ~ ":" ~ $f.exception.min ~ ":" ~ $f.exception.max }, " ", do { my $f = ("a".."z").rand; $f.so; $f.^name ~ ":" ~ $f.exception.^name }
# rakudo 2026.08: X::Range::Rand::InvalidEndpoints:1:1 Failure:X::AdHoc
```
rakupp 4.0.1: differs — an infinite range throws ("Impossible to get a
random number from an infinite range") instead of failing, and the
exception carries no `.min`/`.max`.

## G. Conversions and the rest of the surface

### RG-30  list, flat, Array, Capture, and friends                          D:partial R:partial V:spec
`.list` is a List, `.flat` and `.Seq` Seqs (lazy for an infinite range,
as is `.Array`), `.flat` on the type object `(Range,)`; `keys`, `kv`,
`pairs`, `antipairs` index from 0; `.Map` and `.Hash` pair up the
elements and throw `X::Hash::Store::OddNumber` for an odd count;
`.Capture` is `\(:min, :max, :excludes-min, :excludes-max, :infinite,
:is-int)`, six nameds and no positionals, so a sub-signature must
accept the extras; `.fmt` formats the list.
```
say (1..3).list.^name, " ", (1..3).flat.^name, " ", Range.flat.raku, " ", (1..3).Seq.^name, " ", (1..3).Array.raku, " ", (1..3).Slip.raku, " ", (try (1..3).Map.raku) // $!.^name, " ", (1..4).Map.raku, " ", (1..3).Str.^name, " ", (1..3).keys.raku, " ", (1..3).kv.raku, " ", (1..3).pairs.raku, " ", (1..3).antipairs.raku, " ", ("a".."c").values.raku, " ", (1..3).head, " ", (1..3).tail, " ", (1..3).end, " ", (1..*).head(2).raku, " ", (1..3).List.^name, " ", (1..3).eager.raku, " ", (1..3).cache.raku, " ", (1..3).Bag.raku, " ", (1..3).Set.elems, " ", (1..3).join("|"), " ", (1..3 Z <a b c>).raku, " ", ((1..2) xx 2).raku, " ", (try (1..3).Hash.raku) // $!.^name, " ", (1..4).Hash.raku, " ", (1..3).Numeric.^name, " ", (1..3).elems.^name, " ", (1..3).Array.^name, " ", (1..3).List.raku, " ", (1..3).Seq.raku, " ", (1..3).flat.raku, " ", (^3).list.raku, " ", ("a".."c").list.raku, " ", (1..3).list.is-lazy, " ", (1..3).fmt("%d", "-"), " ", (1..3).fmt, " ", (try (1..*).fmt("%d").^name) // $!.^name, " ", (1..*).Array.^name, " ", (1..*).list.^name, " ", (1..*).Seq.head(2).raku, " ", do { my @a = 1..3; @a[0] = 9; @a.raku }, " ", (1..3).Array.is-lazy, " ", (1..*).Array.is-lazy, " ", (1..*).list.is-lazy, " ", (1..3).Set.raku, " ", (1..3).Mix.total
# rakudo 2026.08: List Seq (Range,) Seq [1, 2, 3] slip(1, 2, 3) X::Hash::Store::OddNumber Map.new(("1" => 2,"3" => 4)) Str (0, 1, 2).Seq (0, 1, 1, 2, 2, 3).Seq (0 => 1, 1 => 2, 2 => 3).Seq (1 => 0, 2 => 1, 3 => 2).Seq ("a", "b", "c") 1 3 2 (1, 2).Seq List (1, 2, 3) (1, 2, 3) (1=>1,2=>1,3=>1).Bag 3 1|2|3 ((1, "a"), (2, "b"), (3, "c")).Seq (1..2, 1..2).Seq X::Hash::Store::OddNumber {"1" => 2, "3" => 4} Int Int Array (1, 2, 3) (1, 2, 3).Seq (1, 2, 3).Seq (0, 1, 2) ("a", "b", "c") False 1-2-3 1 2 3 Str Array List (1, 2).Seq [9, 2, 3] False True True Set.new(2,1,3) 3
say (1..2).Capture.raku, " ", do { sub f((:$min, :$max, :$excludes-max, *%)) { "$min-$max-$excludes-max" }; f(1..^3) }, " ", (1..2).Capture.^name, " ", (1..2).Capture.keys.sort.raku, " ", (1..2).Capture.elems, " ", do { my (:$min, :$max, *%) := (3..7).Capture; "$min,$max" }
# rakudo 2026.08: \(:!excludes-max, :!excludes-min, :!infinite, :is-int, :max(2), :min(1)) 1-3-True Capture ("excludes-max", "excludes-min", "infinite", "is-int", "max", "min").Seq 0 3,7
```
rakupp 4.0.1: differs — `.Map` is missing (the first line dies there),
`.Capture` refuses ("Cannot unpack or Capture"), and `*%` in a
sub-signature is a parse error.

### RG-31  Cool methods see the string, `contains` and `index` warn         D:no R:no V:quirk
A Range is a Cool, so string methods act on `.Str` (`(1..3).uc` is
"1 2 3", `.chars` 5, `.flip` "3 2 1", `.words` the elements) and
numeric methods on `.Numeric` (`.abs`, `.sqrt`, `== 3`, `< 4`); `succ`
and `pred` do not exist; `.Int` of an infinite range fails
`X::Numeric::CannotConvert`. `contains` and `index` answer on the
string but first `warn` that they look at the `.Str` representation.
```
say (1..3).Numeric, " ", ((1..3) + 0).raku, " ", (1..3).Int, " ", +("a".."c"), " ", (try ("a".."c").Int) // $!.^name, " ", (1..3).Real, " ", (1..3) == 3, " ", (1..3) < 4, " ", (1..3).Bool, " ", (1..0).Bool, " ", ?Range, " ", (1..3).so, " ", (try (1..3).succ.raku) // $!.^name, " ", (try (1..3).pred.raku) // $!.^name, " ", (1..3).abs, " ", (1..3).sqrt, " ", (1..3).chars, " ", (1..3).uc, " ", (1..3).words.raku, " ", (1..3).comb.raku, " ", (1..3).split(" ").raku, " ", (1..3).flip, " ", (1..3).Str.chars, " ", (1..3).Stringy.^name, " ", (1..3) eq "1 2 3", " ", (1..3) ~ "!", " ", (1..3).ord, " ", (1..3).trim, " ", ("a".."c").Numeric, " ", ("a".."c") == 3, " ", (1..*).Int.^name, " ", (try (1..*).Int) // $!.^name
# rakudo 2026.08: 3 1..3 3 3 3 3 True True True True False True X::Method::NotFound X::Method::NotFound 3 1.7320508075688772 5 1 2 3 ("1", "2", "3").Seq ("1", " ", "2", " ", "3").Seq ("1", "2", "3").Seq 3 2 1 5 Str True 1 2 3! 49 1 2 3 3 True Failure X::Numeric::CannotConvert
say do { my @w; CONTROL { when CX::Warn { @w.push(.message.substr(0, 40)); .resume } }; ((1..3).contains(2), (10..12).contains("0 1"), (1..3).index(3), (1..3).contains("4")).join(",") ~ " " ~ @w.elems ~ " " ~ @w[0] }
# rakudo 2026.08: True,True,4,False 4 Applying '.contains' to a Range will loo
```
rakupp 4.0.1: differs — `succ`, `pred`, `abs` and `sqrt` answer 1, -1,
0 and 0, `(1..*).Int` is `Inf`, and no warning is issued.

### RG-32  The Range of a numeric type                                     D:partial R:yes V:spec
`Int.Range` is `-Inf^..^Inf`, `UInt.Range` `0..^Inf`, `Num.Range`,
`Rat.Range`, `FatRat.Range` and `Rational.Range` `-Inf..Inf`, `Bool.Range`
the Int one; each native int type answers its exact bounds
(`int8` -128..127, `uint64` 0..18446744073709551615, `byte` 0..255,
`atomicint` and `int` the 64-bit range, `num` and `num32` `-Inf..Inf`);
`Str`, `Complex`, `Real` and `Numeric` have no `.Range`; on an instance
it is `X::Parameter::InvalidConcreteness`.
```
say Int.Range.raku, " ", UInt.Range.raku, " ", Num.Range.raku, " ", Rat.Range.raku, " ", FatRat.Range.raku, " ", (try Str.Range.raku) // $!.^name, " ", (try 42.Range.raku) // $!.^name, " ", (try Complex.Range.raku) // $!.^name, " ", Int.Range.infinite, " ", -1 ~~ UInt.Range, " ", 0 ~~ UInt.Range, " ", Inf ~~ UInt.Range, " ", 2**70 ~~ UInt.Range, " ", 1.5 ~~ Int.Range, " ", Inf ~~ Int.Range, " ", Inf ~~ Num.Range, " ", NaN ~~ Num.Range, " ", (try Real.Range.raku) // $!.^name, " ", (try Numeric.Range.raku) // $!.^name, " ", (try Bool.Range.raku) // $!.^name, " ", (try Rational.Range.raku) // $!.^name
# rakudo 2026.08: -Inf^..^Inf 0..^Inf -Inf..Inf -Inf..Inf -Inf..Inf X::Method::NotFound X::Parameter::InvalidConcreteness X::Method::NotFound True False True False True True False True False X::Method::NotFound X::Method::NotFound -Inf^..^Inf -Inf..Inf
say int8.Range.raku, " ", uint8.Range.raku, " ", int16.Range.raku, " ", uint64.Range.raku, " ", int.Range.raku, " ", uint.Range.raku, " ", byte.Range.raku, " ", int32.Range.int-bounds.raku, " ", 5 ~~ int8.Range, " ", 200 ~~ int8.Range, " ", atomicint.Range.raku, " ", num.Range.raku, " ", int64.Range.raku, " ", int8.Range.elems, " ", 2**64 ~~ uint64.Range, " ", 2**64-1 ~~ uint64.Range, " ", num32.Range.raku, " ", uint32.Range.raku, " ", int8.Range.is-int, " ", int8.Range.min.^name
# rakudo 2026.08: -128..127 0..255 -32768..32767 0..18446744073709551615 -9223372036854775808..9223372036854775807 0..18446744073709551615 0..255 (-2147483648, 2147483647) True False -9223372036854775808..9223372036854775807 -Inf..Inf -9223372036854775808..9223372036854775807 256 False True -Inf..Inf 0..4294967295 True Int
```
rakupp 4.0.1: differs — the native types have no `.Range`, `Bool.Range`
and `Rational.Range` are missing, `2**70 ~~ UInt.Range` and
`Inf ~~ Num.Range` are False, and `Inf ~~ Int.Range` is False where
Rakudo says True.

### RG-33  The type object                                                  D:no R:no V:spec
`Range` itself numifies to 0 with an "uninitialized value" warning,
stringifies to "", has one element like any type object, lists as
`(Range,)`, is False and undefined and not lazy; the instance methods
(`min`, `is-int`, `infinite`, `reverse`, `sum`, `pick`, `rand`,
`bounds`) refuse it with `X::AdHoc`, `X::Parameter::InvalidConcreteness`
or `X::Multi::NoMatch`; `5 ~~ Range` is False and `Range ~~ Range` True.
Its MRO is Range, Cool, Any, Mu; its roles Positional and Iterable.
```
say (quietly +Range), " ", Range.elems, " ", (quietly Range.Str).raku, " ", Range.gist, " ", Range.raku, " ", Range.list.raku, " ", Range.flat.raku, " ", ?Range, " ", Range.defined, " ", Range.is-lazy, " ", (try Range.min) // $!.^name, " ", (try Range.elems) // $!.^name, " ", (try Range.is-int) // $!.^name, " ", (try Range.infinite) // $!.^name, " ", (try Range.iterator.^name) // $!.^name, " ", (try Range.reverse.raku) // $!.^name, " ", (try Range.sum) // $!.^name, " ", (try Range.pick.raku) // $!.^name, " ", (try Range.rand.raku) // $!.^name, " ", (try Range.bounds.raku) // $!.^name, " ", (try (Range ~~ Range).raku) // $!.^name, " ", (try (5 ~~ Range).raku) // $!.^name, " ", (try Range.ACCEPTS(5).raku) // $!.^name, " ", Range.^name, " ", Range.^mro.map(*.^name).raku, " ", Range.^roles.map(*.^name).raku, " ", (1..2).^roles.map(*.^name).raku, " ", (1..2) ~~ Positional, " ", (1..2) ~~ Iterable, " ", (1..2) ~~ Cool, " ", (1..2) ~~ List, " ", (1..2) ~~ Numeric, " ", (1..2) ~~ Seq
# rakudo 2026.08: 0 1 "" (Range) Range (Range,) (Range,) False False False X::AdHoc 1 X::AdHoc X::AdHoc X::AdHoc X::Parameter::InvalidConcreteness X::Multi::NoMatch X::Multi::NoMatch X::AdHoc X::AdHoc Bool::True Bool::False Bool::False Range ("Range", "Cool", "Any", "Mu").Seq ("Positional", "Iterable").Seq ("Positional", "Iterable").Seq True True True False False False
```
rakupp 4.0.1: differs — `Range.is-lazy` is `X::Method::NotFound` and the
line dies there.

## Counts

| | items |
|---|---|
| total | 33 |
| not fully stated by docs (D:yes) nor asserted by Roast (R:yes) | 12 |
| Rakudo bugs (do not imitate) | 2 — RG-13 `reverse` of a non-integral range, RG-14 `first(:end, :kv)` |
| quirks (recorded, step two decides) | 7 — RG-05 empty ranges are True, RG-08 `+(-∞..-∞)`, RG-11 string cross product, RG-12 `-Inf` and NaN streams, RG-23 Failure endpoints, RG-27 `max(:k)` of an excluded end, RG-31 `contains` warning |
| rakupp 4.0.1 differs | 29 |
| rakupp 4.0.1 matches | 4 — RG-10 (single-character fields), RG-14 (all but the buggy corner), RG-24, RG-25 |

Recurring rakupp gaps, for step two: the object has no `is-int`,
`infinite`, `int-bounds`, `Range.new`, `Capture` or `Map`, and the six
mutators are `X::Method::NotFound` rather than `X::Immutable`; `.raku`
throws away the endpoint types; invalid endpoints (a Range, a Seq, a
Complex, an unparsable string) are accepted; non-Int endpoints on the
right change the element type; every "infinite" case is handled with
native-Int limits or `Inf` where Rakudo streams `-Inf`/NaN, fails with
`X::Cannot::Lazy` or yields nothing; `pick`/`roll` on an infinite range
return huge Ints; smartmatch mishandles `Inf`, big Ints, string
endpoints with an open end and string-vs-numeric range pairs; `+ - * /`
with a non-Int Real or with a Range on the right are wrong; the
`min`/`max` adverbs are ignored. The multi-character string product
(RG-11) is a deliberate divergence, not a gap.

## Method (how this sheet was produced)

As for [Supply.md](Supply.md). Four probe rounds. Traps met: `..` is
non-associative with `but`, `cmp` and `leg`, so `(1..2 but role {})`
must be `((1..2) but role {})`; a reduce operator inside a `say` list
(`[+] (), " ", …`) takes the rest of the list as its operands and must
be parenthesized; `1..5 xx 2` is `1..(5 xx 2)`; a zero-denominator Rat
must be shown with `.raku`, not printed; `try EXPR` sets `$!` for a
Failure returned by `+"abc"` but not for one returned by `==` or `<`
(those stay a Failure, so `// $!.^name` reads "Any"); rakupp's
exceptions lack the `.action`, `.got`, `.min` attributes, so every
attribute check went on a line of its own. D flags from
`doc/Type/Range.rakudoc` and `doc/Language/operators.rakudoc`; R flags
from `S02-types/range.t`, `range-iterator.t`, `S03-operators/range.t`,
`range-basic.t`, `range-int.t`, `S03-smartmatch/range-range.t`,
`S09-subscript/slice.t`, `S02-types/whatever.t`, `S02-types/capture.t`,
`S32-list/first-end*.t` and `S02-types/int-uint.t`.
