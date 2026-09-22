# Int, Num, Rat, FatRat — semantics sheet

Provenance: Rakudo tag `2026.08`, files `src/core.c/Int.rakumod` (595
lines), `Num.rakumod` (602), `Rat.rakumod` (477), `Rational.rakumod`
(387), `Real.rakumod` (164), `Numeric.rakumod` (394) and the numeric
half of `Cool.rakumod` (330 of 585), read in full on 2026-09-21; the
`compare-as-*` coercion helpers of `Rakudo/Internals.rakumod`, the
`Polymod` entry of `Rakudo/Iterator.rakumod`, the numeric `cmp`/`<=>`
candidates of `Order.rakumod` and the exception classes named below
(`Exception.rakumod`) were read for the rules they carry. Out of scope:
Complex, the numeric literal grammar and `Str.Numeric` (in
[Str.md](Str.md), ST-06 to ST-08), `sprintf` beyond what `.fmt` shows,
and the trigonometric values themselves (S32-trig). Oracle: Homebrew
Rakudo v2026.08 on macOS. First compared against Raku++
4.0.1-84-ga4291988 (build-arm64, 2026-09-21) and, after the implementation
pass of 2026-09-22, against 4.0.1-118 (build-arm64); every `rakupp` line
below is the LATTER. Format and legend: [README.md](README.md).

Where this sits against the declared spec: of the numeric Roast files
Rakudo passes here and Raku++ does not — `S32-num/base.t`, `int.t`,
`rat.t`, `power.t`, `rounders.t`, `stringify.t`, `narrow.t`,
`negative-zero.t`, `real-bridge.t`, `is-prime.t`, `exp.t`, `cool-num.t`,
`S02-types/num.t`, `int-uint.t`, `native.t` — every one leans on a rule
below: which type an operation returns, when a Rat becomes a Num, what
divides by zero and how, what a Failure is and what throws, how a Num
prints, what `round` does at a half, and what an undefined number does
in an expression. 3 of the 28 items are neither fully documented nor
fully asserted by Roast; six Rakudo behaviours are recorded as bugs and
four as quirks, and step two should not imitate the bugs.

Every probe ran with `alarm 10` and standard input closed. A probe line
often exercises more than one item; the statement says which fields
matter.

## A. Identity and truth

### N-01  Value identity: `===`, `eqv`, `==`, WHICH                       D:partial R:yes V:spec
Numbers are value types keyed by type and value: `WHICH` is `Int|1`,
`Rat|3/2`, `Num|1`, `FatRat|1/2`; `1`, `1.0` and `1e0` are `==` but not
`===` or `eqv`; `0e0` and `-0e0` are `==` but neither `===` nor `eqv`;
`NaN === NaN` and `NaN eqv NaN` are True while `NaN == NaN` is False;
a zero-denominator `<0/0>` is `===` and `eqv` itself but never `==`;
`<1/0> == <2/0>` and `<1/0> === <2/0>` (numerators normalize to 1);
`True == 1` but `True eqv 1` and `True === 1` are False (Bool is its own
type); a Rat and a FatRat are never `eqv`.
```
say 1 === 1.0, " ", 1 == 1.0, " ", 1 eqv 1.0, " ", 1e0 === 1.0, " ", 1e0 == 1.0, " ", 0e0 === -0e0, " ", 0e0 == -0e0, " ", 0e0 eqv -0e0, " ", NaN === NaN, " ", NaN == NaN, " ", NaN eqv NaN, " ", NaN != NaN, " ", <0/0> == <0/0>, " ", <0/0> eqv <0/0>, " ", <0/0> === <0/0>, " ", <1/0> == <1/0>, " ", <1/0> == <2/0>, " ", <1/0> === <2/0>, " ", <1/0> eqv <2/0>, " ", <1/2> eqv 0.5, " ", <1/2> === 0.5, " ", True eqv 1, " ", True == 1, " ", True === 1, " ", 1.WHICH, " ", 1.5.WHICH, " ", 1e0.WHICH, " ", FatRat.new(1,2).WHICH, " ", (1/2) === (2/4), " ", 2**70 === 2**70, " ", 1.0.WHICH, " ", (1/3).FatRat eqv (1/3), " ", <1/0> eqv FatRat.new(1,0), " ", 1e0 eqv 1e0, " ", (2/2).WHICH eq (1/2+1/2).WHICH, " ", 1.WHICH.^name, " ", (0.1e0+0.2e0).WHICH, " ", (1, 1.0, 1e0).unique.elems, " ", (2/2, 1/2+1/2).unique.raku
# rakudo 2026.08: False True False False True False True False True False True True False True True True True True True True True False True False Int|1 Rat|3/2 Num|1 FatRat|1/2 True True Rat|1/1 False False True True ValueObjAt Num|0.30000000000000004 3 (1.0,).Seq
```
The NaN and signed-zero rules of `eqv` are not in the `eqv` docs.
rakupp 4.0.1-118: differs in one field — `FatRat.new(1,2).WHICH` is
`Rat|1/2`, so a FatRat and a Rat of the same value collide in `unique` and
as object-hash keys.

### N-02  Numeric.ACCEPTS: numeric equality after coercion                D:yes R:yes V:spec
`$x ~~ 1` coerces the topic with `.Numeric` and compares with `==`,
except that two NaNs match; a topic that does not coerce is False, not
an error. `1 ~~ 1.0`, `"1" ~~ 1`, `"1.0" ~~ 1`, `1 ~~ 1e0`, `Inf ~~ <1/0>`,
`NaN ~~ <0/0>` and `<0/0> ~~ NaN` are all True; `2 ~~ 1.5` is False. A
type object on the left never matches an instance.
```
say 1 ~~ 1.0, " ", "1" ~~ 1, " ", "abc" ~~ 1, " ", "1.0" ~~ 1, " ", NaN ~~ NaN, " ", 1 ~~ "1", " ", 1 ~~ 1e0, " ", 4 ~~ NaN, " ", <0/0> ~~ NaN, " ", NaN ~~ <0/0>, " ", Inf ~~ <1/0>, " ", 1 ~~ Int, " ", 1.0 ~~ Int, " ", 1 ~~ Rat, " ", 1 ~~ Real, " ", 1 ~~ Numeric, " ", 1e0 ~~ Real, " ", <1/2> ~~ Rational, " ", 1 ~~ UInt, " ", -1 ~~ UInt, " ", 1.0 ~~ Rat, " ", 1 ~~ Cool, " ", "1" ~~ Numeric, " ", 1 ~~ Str, " ", 1 ~~ 1|2, " ", 2 ~~ 1.5, " ", 1.0 ~~ 1, " ", 1e0 ~~ 1, " ", " 1 " ~~ 1, " ", Inf ~~ Inf, " ", -0e0 ~~ 0, " ", <1/0> ~~ Inf, " ", 1 ~~ 1+0i, " ", 1+0i ~~ 1, " ", Int ~~ Numeric, " ", 1 ~~ Int:D, " ", Int ~~ Int:D
# rakudo 2026.08: True True False True True True True False True True True True False False True True True True True False True True False False True False True True True True True True True True True True False
```
rakupp 4.0.1-118: differs in one field — `NaN ~~ <0/0>` is False.

### N-03  Truth                                                          D:yes R:yes V:spec
Zero of any type is False, including `-0e0`; NaN is True; a Rational is
False when its numerator is 0, so `<0/0>` is False although
`<0/0>.Num` (NaN) is True; `<1/0>` is True; type objects are False; a
numeric string is True whenever it is non-empty (`?"0"` is True).
```
say ?0, " ", ?0.0, " ", ?0e0, " ", ?-0e0, " ", ?NaN, " ", ?<0/0>, " ", ?<1/0>, " ", ?Inf, " ", ?Int, " ", ?Num, " ", ?Rat, " ", ?0.1, " ", ?FatRat.new(0, 5), " ", ?(1/2), " ", 0.Bool, " ", 5.not, " ", !0.5, " ", <0/0>.Num.Bool, " ", ?"0", " ", ?"0.0", " ", ?"0e0", " ", ?<0>, " ", ?<0.0>, " ", ?<0e0>, " ", ?<0/1>, " ", ?<0+0i>, " ", ?0i, " ", so 0.0000001, " ", ?1e-320
# rakudo 2026.08: False False False False True False True True False False False True False True False False False True True True True False False False False False False True True
```
rakupp 4.0.1-118: matches.

## B. Printing

### N-04  Int and Num as strings                                          D:partial R:yes V:spec
An Int prints its digits at any size; `.Str(:superscript)` and
`:subscript` translate them. A Num prints the shortest decimal that
reads back to the same double: no exponent from 1e-4 up to below 1e16
(`1e15` is `1000000000000000`, `1e16` is `1e+16`, `0.0001` and `1e-05`),
an `e+NN`/`e-NN` exponent with at least two digits beyond that, an
integral value without a point (`1e0` is `1`), `-0e0` as `-0`, and
`Inf`, `-Inf`, `NaN`. `.raku` appends `e0` unless an exponent or a
non-number is already there; `.gist` is `.Str`. The type object's
`.Str` is "" with an "uninitialized value in string context" warning.
```
say 42.Str, " ", 42.raku, " ", (-42).raku, " ", 42.Str(:superscript), " ", (-42).Str(:subscript), " ", (2**100).Str, " ", Int.raku, " ", Int.gist, " ", (quietly Int.Str).raku, " ", 1e0.Str, "|", 1.5e0.Str, "|", 1e20.Str, "|", 1e16.Str, "|", 1e15.Str, "|", 123456789012345678e0.Str, "|", 0.1e0.Str, "|", 1e-5.Str, "|", 0.0001e0.Str, "|", 1e-4.Str, "|", (-0e0).Str, "|", Inf.Str, "|", (-Inf).Str, "|", NaN.Str, "|", 1e0.raku, "|", 1e20.raku, "|", 1.5e0.raku, "|", Inf.raku, "|", NaN.raku, "|", (-0e0).raku, "|", (0.1e0+0.2e0).Str, "|", 1e100.Str, "|", 100e0.Str, "|", 1e0.gist, "|", (2**53+1).Num.Str, "|", 1.0e0.Str, "|", 12345.678e0.Str, "|", 1e21.Str, "|", 1.7976931348623157e308.Str, "|", 5e-324.Str, "|", 2e0.Str, "|", 1e-7.Str, "|", 123456789e0.Str, "|", 1234567890123456e0.Str, "|", 12345678901234567e0.Str, "|", 0.000001e0.Str, "|", 0.0000001e0.Str, "|", 1e-5.raku, "|", 2.5e-3.raku, "|", 1e0.Str.^name, "|", 42.gist, "|", (-0e0).gist
# rakudo 2026.08: 42 42 -42 ⁴² ₋₄₂ 1267650600228229401496703205376 Int (Int) "" 1|1.5|1e+20|1e+16|1000000000000000|1.2345678901234568e+17|0.1|1e-05|0.0001|0.0001|-0|Inf|-Inf|NaN|1e0|1e+20|1.5e0|Inf|NaN|-0e0|0.30000000000000004|1e+100|100|1|9007199254740992|1|12345.678|1e+21|1.7976931348623157e+308|5e-324|2|1e-07|123456789|1234567890123456|1.2345678901234568e+16|1e-06|1e-07|1e-05|0.0025e0|Str|42|-0
```
The exponent thresholds are what `S32-num/stringify.t` and
`S02-types/num.t` pin; the docs only say `say` "does not try very hard".
rakupp 4.0.1-118: differs in two fields — the smallest denormal prints as
`4.94065645841247e-324` (Rakudo `5e-324`) and `12345678901234567e0` prints
as `12345678901234568` where Rakudo switches to the exponent at 1e16.

### N-05  Rat and FatRat as strings                                        D:partial R:yes V:spec
`.Str` of a Rational is exact when the fraction terminates and otherwise
rounded to 6 digits after the point when the denominator is below
100,000, or to one more digit than the denominator has for a larger
one (`1/100001` prints `0.00001`, `1/3000000` `0.00000033`); trailing
zeros are dropped; a value that rounds to the next whole number prints
that (`0.9999999999999999999999` prints `1`); a FatRat with a large
denominator gets denominator digits plus whole digits plus five
(`FatRat.new(1, 10**20)` prints twenty zeros then 1). `.raku` of a Rat is
the decimal when the denominator has only 2s and 5s (`0.25`, `3.0`),
else `<num/den>`; a FatRat's is `FatRat.new(n, d)`; `.gist` is `.Str`.
A zero-denominator Rational throws `X::Numeric::DivideByZero` from
`.Str` and `.gist` but prints `<1/0>` from `.raku`. A huge quotient
that rounds to 0 prints `0`, not Inf.
```
say (1/3).Str, "|", (1/3).raku, "|", (1/3).gist, "|", 0.5.raku, "|", (2/4).raku, "|", 3.0.raku, "|", (3/1).raku, "|", 3.raku, "|", (1/4).raku, "|", (1/6).raku, "|", (-1/3).raku, "|", (-1/3).Str, "|", (7/2).raku, "|", 1.23456789.raku, "|", (1/1024).raku, "|", (1/(2**64-1)).raku, "|", (2/3).Str, "|", (1/6).Str, "|", (1/7).Str, "|", (1/100000).Str, "|", (1/100001).Str, "|", (1/1000000).Str, "|", (1/3000000).Str, "|", (123456789/1000).Str, "|", 1000000.5.Str, "|", (2/3*3).Str, "|", Rat.new(20, 10).Str, "|", 2.50.raku, "|", <1/0>.raku, "|", <0/0>.raku, "|", <-1/0>.raku, "|", (try <1/0>.Str) // $!.^name, "|", (try <0/0>.gist) // $!.^name, "|", FatRat.new(1,3).raku, "|", FatRat.new(1,3).Str, "|", FatRat.new(1, 10**20).Str, "|", (1/7).FatRat.Str, "|", FatRat.new(2, 1).raku, "|", FatRat.new(1,3).gist, "|", Rat.raku, "|", Rat.gist, "|", 0.9999999999999999999999.Str, "|", 0.9999999999999999999999.raku, "|", Rat.new(10**400, 9**999).Str, "|", <1/99999999999999999999>.Str, "|", 241025348275725.3352.Str, "|", 4.5.Str, "|", 0.1.Str, "|", (0.1+0.2).raku, "|", (1/3).Rat.raku, "|", (22/7).Str, "|", (1/3).Str.chars, "|", (1/99999).Str, "|", 1.5.Str.^name, "|", (-0.0).raku, "|", (1/8).raku, "|", (1/5).raku, "|", (1/12).raku, "|", (1/13).raku, "|", (1000001/10000).Str, "|", (4.5 ** 60).Str.chars, "|", (1/3).FatRat.raku, "|", FatRat.new(1,4).raku, "|", FatRat.new(1,4).Str, "|", (FatRat.new(1,3)+0).raku, "|", FatRat.new(1, 7).Str, "|", FatRat.new(1, 100000).Str, "|", FatRat.new(1, 1000001).Str, "|", FatRat.new(22, 7).Str, "|", (2/3).raku, "|", (2/3 + 1/3).raku, "|", (1/3).FatRat.gist, "|", (1/1000000).raku, "|", (1/1024).Str, "|", 0.000001.Str, "|", 0.0000001.Str, "|", 0.0000001.raku, "|", (1/3e0).raku, "|", (1/3).Num.Str, "|", 1e0.Rat.Str, "|", 0.30000000000000004e0.Rat.raku, "|", 0.1e0.Rat.raku
# rakudo 2026.08: 0.333333|<1/3>|0.333333|0.5|0.5|3.0|3.0|3|0.25|<1/6>|<-1/3>|-0.333333|3.5|1.23456789|0.0009765625|<1/18446744073709551615>|0.666667|0.166667|0.142857|0.00001|0.00001|0.000001|0.00000033|123456.789|1000000.5|2|2|2.5|<1/0>|<0/0>|<-1/0>|X::Numeric::DivideByZero|X::Numeric::DivideByZero|FatRat.new(1, 3)|0.333333|0.00000000000000000001|0.142857|FatRat.new(2, 1)|0.333333|Rat|(Rat)|1|<9999999999999999999999/10000000000000000000000>|0|0.00000000000000000001|241025348275725.3352|4.5|0.1|0.3|<1/3>|3.142857|8|0.00001|Str|0.0|0.125|0.2|<1/12>|<1/13>|100.0001|60|FatRat.new(1, 3)|FatRat.new(1, 4)|0.25|FatRat.new(1, 3)|0.142857|0.00001|0.000000999999|3.142857|<2/3>|1.0|0.333333|0.000001|0.000977|0.000001|0.0000001|0.0000001|0.3333333333333333e0|0.3333333333333333|1|0.3|0.1
```
The six-digit rule is asserted (`S32-num/stringify.t`) but written down
nowhere. rakupp 4.0.1-118: differs in two fields —
`0.9999999999999999999999` prints itself instead of rounding to `1`, and
`FatRat.new(1, 1000001)` prints `0.000001` instead of `0.000000999999`.

## C. Construction, undefined values, coercion

### N-06  Constructors                                                   D:yes R:yes V:spec
`Int.new` takes anything with `.Int` (a Str parses, a Rat or Num
truncates, no argument is 0) and dies `X::AdHoc` on a type object
("Cannot create an Int from a 'Str' type object"), with
`X::Str::Numeric` for an unparsable string and a Failure
`X::Numeric::CannotConvert` for Inf. `Num.new` coerces with `.Num` (a
type without `.Num` is `X::Method::NotFound`). `Rat.new(n, d)` and
`FatRat.new` reduce to lowest terms, move the sign to the numerator,
default to 0/1, refuse non-Int parts (`X::TypeCheck::Binding::Parameter`)
and extra arguments (`X::AdHoc`), normalize a zero denominator to
`<1/0>`, `<-1/0>` or `<0/0>`, and accept a denominator beyond 64 bits:
`Rat.new(1, 2**65)` is a Rat that prints its full value and degrades to
a Num only when arithmetic touches it.
```
say Int.new.raku, " ", Int.new("42"), " ", Int.new(4.7), " ", (try Int.new(Str)) // $!.^name, " ", Int.new(True), " ", Num.new.raku, " ", Num.new(⅓), " ", Num.new("42").raku, " ", Rat.new(2,4).nude.raku, " ", Rat.new(1,-2).nude.raku, " ", Rat.new(3,0).nude.raku, " ", Rat.new(-5,0).nude.raku, " ", Rat.new(0,0).nude.raku, " ", Rat.new.raku, " ", Rat.new(5).raku, " ", (try Rat.new(1.5, 2)) // $!.^name, " ", FatRat.new(2,4).nude.raku, " ", Rat.new(0, 33).nude.raku, " ", (try Rat.new(1, 2.0)) // $!.^name, " ", (try Num.new(class {}.new)) // $!.^name, " ", Rat.new(1451234131, 60).nude.raku, " ", Rat.new(2**64, 2**65).nude.raku, " ", Rat.new(1, 2**65).^name, " ", Rat.new(1, 2**65).denominator, " ", Rat.new(1, 2**65).Str, " ", (Rat.new(1, 2**65) + 0).^name, " ", (try Int.new("abc")) // $!.^name, " ", Int.new(4.7e0), " ", (try Int.new(Inf)) // $!.^name, " ", Num.new(Inf).raku, " ", FatRat.new.raku, " ", FatRat.new(3).raku, " ", (try Int.new(Int)) // $!.^name, " ", Int.new(Int.new(3)), " ", Rat.new(-1, -7).nude.raku, " ", Num.new(1/3).raku, " ", Rat.new(10, 4).raku, " ", (try Rat.new(1, 2, 3)) // $!.^name
# rakudo 2026.08: 0 42 4 X::AdHoc 1 0e0 0.3333333333333333 42e0 (1, 2) (-1, 2) (1, 0) (-1, 0) (0, 0) 0.0 5.0 X::TypeCheck::Binding::Parameter (1, 2) (0, 1) X::TypeCheck::Binding::Parameter X::Method::NotFound (1451234131, 60) (1, 2) Rat 36893488147419103232 0.000000000000000000027 Num X::Str::Numeric 4 X::Numeric::CannotConvert Inf FatRat.new(0, 1) FatRat.new(3, 1) X::AdHoc 3 (1, 7) 0.3333333333333333e0 2.5 X::AdHoc
```
rakupp 4.0.1-118: differs in seven fields — `Rat.new` accepts non-Int parts
and extra arguments where Rakudo refuses them
(`X::TypeCheck::Binding::Parameter`, `X::AdHoc`), `Num.new(class {}.new)` is
0 rather than `X::Method::NotFound`, and `Rat.new(1, 2**65)` is a Num from
the start where Rakudo keeps a Rat that prints its full value. `Int.new` now
refuses a type object, an unparsable string and Inf.

### N-07  An undefined number in numeric context                          D:partial R:partial V:spec
Prefix `+` and `-`, `.Numeric`, `.Real`, `.Int`, `+&`, `max` and `cmp`
treat a numeric type object (or an undefined typed container) as 0 with
a warning "Use of uninitialized value of type Int in numeric context";
`.Str` and `~` give "" with the string-context warning; `.Bool` is
False. Every binary arithmetic and numeric comparison operator (`+ - *
** % <=> == <` and so on) THROWS `X::Numeric::Uninitialized` with
`.type` the type object, whichever side the undefined value is on; so
does `.succ`, `.Bridge` and `div` (`X::TypeCheck::Return`). Methods
declared for instances only (`.abs`, `.sign`, `.floor`, `.Rat`, `.Num`,
`.polymod`, `.narrow`) are `X::Parameter::InvalidConcreteness`;
`.chr`, `.base`, `.round`, `Num.sin` are `X::Multi::NoMatch`. `Nil` and
`Any` in the same places are 0 with a warning, never an exception.
```
say (try { my $t = Int; $t + 1 }) // $!.^name ~ ":" ~ $!.type.^name, " ", (try { my $t = Int; +$t }) // $!.^name, " ", (try { my $t = Int; -$t }) // $!.^name, " ", (try { my $t = Int; $t == 0 }) // $!.^name, " ", (try { my $t = Int; $t < 1 }) // $!.^name, " ", (try { my $t = Int; $t * 2 }) // $!.^name, " ", (try { my $t = Num; $t + 1 }) // $!.^name, " ", (try { my $t = Rat; $t + 1 }) // $!.^name, " ", (try { my $t = FatRat; $t + 1 }) // $!.^name, " ", (try { my $t = Int; $t.Numeric }) // $!.^name, " ", (try { my $t = Int; $t.Real }) // $!.^name, " ", (try { my $t = Int; $t.Str }) // $!.^name, " ", (try { my $t = Int; $t.Int }) // $!.^name, " ", (try { my $t = Int; $t.Rat }) // $!.^name, " ", (try { my $t = Int; $t.Num }) // $!.^name, " ", (try { my $t = Int; $t.Bridge }) // $!.^name, " ", (try { my $t = Int; $t.abs }) // $!.^name, " ", (try { my $t = Int; $t.sign }) // $!.^name, " ", (try { my $t = Int; $t.succ }) // $!.^name, " ", (try { my $t = Int; $t.floor }) // $!.^name, " ", (try { my $t = Int; $t.Bool }) // $!.^name, " ", (try { my $t = Nil; $t == 0 }) // $!.^name, " ", (try { my $t = Any; $t == 0 }) // $!.^name, " ", (try { my $t = Nil; $t + 0 }) // $!.^name, " ", (try { my Int $x; $x + 1 }) // $!.^name, " ", (try { my Num $x; $x + 1 }) // $!.^name, " ", (try { my Int $x; $x == 0 }) // $!.^name, " ", (try { my Int $x; ~$x }) // $!.^name, " ", (try { my Int $x; $x.Str }) // $!.^name, " ", (try { my Int $x; +$x }) // $!.^name, " ", (try { my Num $x; +$x }) // $!.^name, " ", (try { my Rat $x; +$x }) // $!.^name, " ", (try { my Int $x; $x + $x }) // $!.^name, " ", (try { my $t = Int; $t + 1.5 }) // $!.^name, " ", (try { my $t = Int; $t + 1e0 }) // $!.^name, " ", (try { my $t = Int; 1 + $t }) // $!.^name, " ", (try { my $t = Int; $t <=> 1 }) // $!.^name, " ", (try { my $t = Int; $t cmp 1 }) // $!.^name, " ", (try { my $t = Int; $t.chr }) // $!.^name, " ", (try { my $t = Int; $t.base(2) }) // $!.^name, " ", (try { my $t = Int; $t.is-prime }) // $!.^name, " ", (try { my $t = Num; $t.sin }) // $!.^name, " ", (try { my $t = Int; $t div 1 }) // $!.^name, " ", (try { my $t = Int; $t % 1 }) // $!.^name, " ", (try { my $t = Int; $t ** 2 }) // $!.^name, " ", (try { my $t = Int; $t +& 1 }) // $!.^name, " ", (try { my $t = Int; $t max 1 }) // $!.^name, " ", (try { my $t = Int; $t.round }) // $!.^name, " ", (try { my $t = Int; $t.polymod(2) }) // $!.^name, " ", (try { my $t = Int; $t.narrow }) // $!.^name, " ", (try { my $t = Num; $t == 0 }) // $!.^name, " ", (try { my $t = Rat; $t == 0 }) // $!.^name, " ", (try { my $t = Int; $t.Bool }) // $!.^name, " ", (try { my $t = Num; ~$t }) // $!.^name, " ", (try { my $t = Int; $t ~~ 0 }) // $!.^name, " ", (try { my $t = Int; 0 ~~ $t }) // $!.^name
# rakudo 2026.08: X::Numeric::Uninitialized:Int 0 0 X::Numeric::Uninitialized X::Numeric::Uninitialized X::Numeric::Uninitialized X::Numeric::Uninitialized X::Numeric::Uninitialized X::Numeric::Uninitialized 0 0  Any X::Parameter::InvalidConcreteness X::Parameter::InvalidConcreteness X::Numeric::Uninitialized X::Parameter::InvalidConcreteness X::Parameter::InvalidConcreteness X::Numeric::Uninitialized X::Parameter::InvalidConcreteness False True True 0 X::Numeric::Uninitialized X::Numeric::Uninitialized X::Numeric::Uninitialized   0 0 0 X::Numeric::Uninitialized X::Numeric::Uninitialized X::Numeric::Uninitialized X::Numeric::Uninitialized X::Numeric::Uninitialized Less X::Multi::NoMatch X::Multi::NoMatch X::AdHoc X::Multi::NoMatch X::TypeCheck::Return X::Numeric::Uninitialized X::Numeric::Uninitialized 0 1 X::Multi::NoMatch X::Parameter::InvalidConcreteness X::Parameter::InvalidConcreteness X::Numeric::Uninitialized X::Numeric::Uninitialized False  False True
say do { my @w; CONTROL { when CX::Warn { @w.push(.message.lines[0]); .resume } }; my $t = Int; my $x = +$t; my $y = -$t; my $z = ~$t; my $n = $t.Numeric; @w.raku ~ " " ~ ($x, $y, $z, $n).raku }
# rakudo 2026.08: ["Use of uninitialized value of type Int in numeric context", "Use of uninitialized value of type Int in numeric context", "Use of uninitialized value \$t of type Int in string context.", "Use of uninitialized value of type Int in numeric context"] (0, 0, "", 0)
say (try EVAL '(Int + Int).^name') // $!.^name, " ", (try EVAL 'Int + 1') // $!.^name, " ", (try EVAL 'my $x = Int + 1; $x') // $!.^name, " ", (try EVAL 'my $t = Int; my $x = $t + 1; $x') // $!.^name, " ", (try EVAL '+Int') // $!.^name, " ", (try EVAL 'Nil == 0') // $!.^name, " ", (try EVAL 'my $n = Nil; $n == 0') // $!.^name, " ", (try EVAL 'quietly Int + 1') // $!.^name, " ", (try EVAL 'my $t := Int; $t + 1') // $!.^name, " ", (try EVAL 'sub f($t) { $t + 1 }; f(Int)') // $!.^name, " ", (try EVAL 'my Int $x; $x + 1') // $!.^name
# rakudo 2026.08: X::Numeric::Uninitialized X::Numeric::Uninitialized X::Numeric::Uninitialized X::Numeric::Uninitialized 0 True True X::Numeric::Uninitialized X::Numeric::Uninitialized X::Numeric::Uninitialized X::Numeric::Uninitialized
```
The docs (Numeric.rakudoc) still say the `:U` case "issues a warning
and returns self.new"; Roast (`S03-operators/arith.t`) pins only
`-Int` warning and yielding 0. rakupp 4.0.1-118: differs — nothing throws.
Every binary arithmetic and comparison operator on an undefined number
should be `X::Numeric::Uninitialized` and the instance-only methods
`X::Parameter::InvalidConcreteness`; here `Int + 1` is 1, `Int == 0` True,
`.succ` 1, `.abs` 0, and only the string-context warning is issued. This is
the largest single gap in the sheet.

### N-08  Num to Int, Rat and FatRat                                      D:yes R:yes V:spec
`Num.Int` truncates toward zero exactly at any magnitude
(`1e300.Int` has 301 digits) and fails `X::Numeric::CannotConvert`
(`.source` the Num, `.target` Int) for Inf, -Inf and NaN. `Num.Rat`
takes an epsilon (default 1e-6) and returns the continued-fraction
convergent that first comes within it, so `pi.Rat` is 355/113,
`exp(1).Rat` 2721/1001, `0.1e0.Rat` 1/10 and `1e-7.Rat` is 0/1; epsilon
0 gives the exact double (`pi.Rat(0)` is 245850922/78256779); a
convergent whose denominator exceeds 64 bits is still a Rat. Inf,
-Inf and NaN become `<1/0>`, `<-1/0>` and `<0/0>`; `.FatRat` mirrors
all of this. A non-Real epsilon is `X::TypeCheck::Binding::Parameter`.
```
say 42.Num.raku, " ", 42.Rat.raku, " ", 42.Rat.nude.raku, " ", 42.FatRat.raku, " ", 42.Int.raku, " ", 42.Bridge.raku, " ", Int.Int.raku, " ", 42.Numeric.raku, " ", 4.2.Numeric.raku, " ", 42.Real.raku, " ", 4.2e0.Real.raku, " ", 42.Complex.raku, " ", 4.5.Complex.raku, " ", 42.Bool.raku, " ", 42.Rat(1).raku, " ", 4.2e0.Bridge.raku, " ", 4.2.Bridge.raku, " ", 42.Bridge.^name, " ", (1/3).Bridge.raku, " ", FatRat.new(1,3).Bridge.raku, " ", 1.5e0.Int, " ", (-1.5e0).Int, " ", 1e20.Int, " ", 1e300.Int.chars, " ", (try Inf.Int / 1) // $!.^name, " ", 0.1e0.Rat.nude.raku, " ", pi.Rat.nude.raku, " ", pi.Rat(1e-10).nude.raku, " ", pi.Rat(0).nude.raku, " ", exp(1).Rat.nude.raku, " ", exp(1).Rat(1e-4).nude.raku, " ", Inf.Rat.nude.raku, " ", (-Inf).Rat.nude.raku, " ", NaN.Rat.nude.raku, " ", NaN.FatRat.nude.raku, " ", 1e0.Rat.raku, " ", 2.5e0.Rat.raku, " ", 1e0.FatRat.raku, " ", (1/3).Num, " ", <1/0>.Num, " ", <-1/0>.Num, " ", <0/0>.Num, " ", 0.1e0.Rat.^name, " ", (2**64).Num.Int, " ", 1e0.Int.^name, " ", 4.7.Int, " ", (-4.7).Int, " ", (-4.7).truncate, " ", 4.7.Num.Int, " ", 1.5.Num.raku, " ", (0.1+0.2).Num.raku, " ", (1/3).Num.Rat.nude.raku, " ", 1e-7.Rat.nude.raku, " ", 0.5e0.Rat.nude.raku, " ", 1e0.Rat.nude.raku, " ", (-2.5e0).Rat.nude.raku, " ", 1e15.Rat.nude.raku, " ", 3.14159e0.Rat.nude.raku, " ", 0.333333333e0.Rat.nude.raku, " ", 1e-3.Rat.nude.raku, " ", 1.1e0.Rat.nude.raku, " ", 1e0.Rat(1e-3).nude.raku, " ", pi.Rat(1e-1).nude.raku, " ", pi.Rat(1).nude.raku, " ", pi.FatRat.nude.raku, " ", 1e-7.Rat(1e-9).nude.raku, " ", 1e17.Int, " ", 123456789012345678e0.Int, " ", (2**53).Num.Int == 2**53, " ", (2**53+1).Num.Int == 2**53, " ", (Inf.Int).^name, " ", (NaN.Int).^name, " ", 1e-7.Rat.raku, " ", 0.3e0.Rat.raku, " ", 0.333e0.Rat.raku, " ", 1.5e0.FatRat.raku, " ", (2**80).Num.Rat.nude.raku, " ", (2**80).Num.Rat.^name, " ", 1e19.Rat.nude.raku, " ", 1e19.Rat.^name, " ", (1/(2**64)).Num.Rat.^name, " ", 1e-20.Rat.nude.raku, " ", 1e-20.Rat(1e-30).nude.raku, " ", 1e-20.Rat(1e-30).^name, " ", 0.1e0.Rat(1e-20).nude.raku, " ", 0.1e0.Rat(0).nude.raku, " ", 0.1e0.FatRat(0).nude.raku, " ", 1e0.Rat(Inf).nude.raku, " ", (try pi.Rat("x")) // $!.^name, " ", pi.Rat(1/1000).nude.raku
# rakudo 2026.08: 42e0 42.0 (42, 1) FatRat.new(42, 1) 42 42e0 Int 42 4.2 42 4.2e0 <42+0i> <4.5+0i> Bool::True 42.0 4.2e0 4.2e0 Num 0.3333333333333333e0 0.3333333333333333e0 1 -1 100000000000000000000 301 X::Numeric::CannotConvert (1, 10) (355, 113) (312689, 99532) (245850922, 78256779) (2721, 1001) (193, 71) (1, 0) (-1, 0) (0, 0) (0, 0) 1.0 2.5 FatRat.new(1, 1) 0.3333333333333333 Inf -Inf NaN Rat 18446744073709551616 Int 4 -4 -4 4 1.5e0 0.3e0 (1, 3) (0, 1) (1, 2) (1, 1) (-5, 2) (1000000000000000, 1) (9208, 2931) (1, 3) (1, 1000) (11, 10) (1, 1) (22, 7) (3, 1) (355, 113) (1, 10000000) 100000000000000000 123456789012345680 True True Failure Failure 0.0 0.3 0.333 FatRat.new(3, 2) (1208925819614629174706176, 1) Rat (10000000000000000000, 1) Rat Rat (0, 1) (1, 100000000000000000000) Rat (1, 10) (1, 10) (1, 10) (1, 1) X::TypeCheck::Binding::Parameter (333, 106)
```
rakupp 4.0.1-118: differs in four fields — `Int.Int` is 0 (Rakudo the type
object), `1e-20.Rat(1e-30)` is 0/1 where Rakudo keeps the 21-digit
denominator, and a Str epsilon is accepted instead of
`X::TypeCheck::Binding::Parameter`.

### N-09  A string operand: Failure or exception?                          D:partial R:yes V:spec
Arithmetic and comparison coerce Str operands with `.Numeric` (the
grammar is in Str.md ST-06): `"3" + "4"` is 7, `"1e3" + 0` a Num,
`"1/2" + 0` a Rat, `"0x10" + 0` 16, `"" + 1` is 1 without a warning,
`"١٢" + 0` is 12, `"½" + 0` a Rat. When the string does not parse, the
binary operators `+ - * / % ** == < <=> gcd +& %%` RETURN a Failure
(`X::Str::Numeric` with `.source`, `.pos`, `.reason`) that is False in
boolean context and throws when used as a number (`div` throws
`X::AdHoc` directly); `!=` returns a plain False. `try EXPR` sets `$!`
for the Failure from `+"abc"` and `"abc" + 3` but hands back the
Failure itself, `$!` untouched, for the one from `==` and `<`.
```
say "3" + "4", " ", ("3" + "4").^name, " ", "0x10" + 0, " ", "1e3" + 0, " ", ("1e3" + 0).^name, " ", "1/2" + 0, " ", " 3 " + 1, " ", (try "3abc" + 1) // $!.^name, " ", (quietly "" + 1), " ", do { my $n = Nil; (quietly $n + 1) }, " ", do { my $a = Any; (quietly $a + 1) }, " ", True + 1, " ", [1,2] + 1, " ", ((1..3) + 1).raku, " ", {a => 1} + 1, " ", (try +"abc") // "undef", " ", (+"abc").^name, " ", (try "abc" * 2) // $!.^name, " ", "2" * "3", " ", "2" ** "3", " ", "7" % "4", " ", "10" / "4", " ", ("10" / "4").^name, " ", -"5", " ", (-"5").^name, " ", +"5", " ", (+"5").^name, " ", +"5.0", " ", (+"5.0").^name, " ", +"5e0", " ", (+"5e0").^name, " ", "3" + 4i, " ", ("1" + "2").raku, " ", "1_000" + 0, " ", (try "1,000" + 0) // $!.^name, " ", "١٢" + 0, " ", "-0" + 0, " ", ("-0".Num).raku, " ", "Inf" + 0, " ", "NaN" + 0, " ", "-Inf" + 0, " ", "+5" + 0, " ", "−5" + 0, " ", (try "abc" - 1) // $!.^name, " ", (try 1 - "abc") // $!.^name, " ", (try "abc" +& 1) // $!.^name, " ", (try "abc" gcd 1) // $!.^name, " ", (try "abc" div 1) // $!.^name, " ", (try "abc" mod 1) // $!.^name, " ", (try "abc" ** 1) // $!.^name, " ", (try "abc" %% 1) // $!.^name, " ", ("3" + "4").WHAT.raku, " ", ("1.5" + "1.5").raku, " ", ("1.5" * 2).^name, " ", ("2e0" * 2).^name, " ", ("1/3" * 3).raku, " ", ("½" + 0).raku, " ", ("¼" + "¼").raku, " ", (try ("²" + 0).^name) // $!.^name, " ", ("3" + True), " ", (True + True), " ", (True + True).^name, " ", (False - True), " ", (True * 3), " ", (True / 2).raku, " ", (Order::Less + 0), " ", (Order::More * 2), " ", (Less <=> More), " ", ("0b101" + 0), " ", ("0o17" + 0), " ", ("1e400" + 0), " ", ("1E3" + 0), " ", ("+1.5e-3" + 0).raku, " ", ("0.5e0" + 0).^name, " ", (try "5." + 0) // $!.^name, " ", (".5" + 0).raku, " ", ("5.0e0" + 0).raku, " ", ("2" x 3) + 0, " ", (try "abc" == 3) // $!.^name, " ", (try "abc" < 3) // $!.^name, " ", (try "abc" <=> 3) // $!.^name, " ", ("abc" != 3), " ", (try ("abc" == 3).^name) // $!.^name, " ", (try ("abc" < 3).^name) // $!.^name, " ", (try ("abc" + 3).^name) // $!.^name, " ", (try so ("abc" == 3)) // $!.^name
# rakudo 2026.08: 7 Int 16 1000 Num 0.5 4 X::Str::Numeric 1 1 1 2 3 2..4 2 undef Failure X::Str::Numeric 6 8 3 2.5 Rat -5 Int 5 Int 5 Rat 5 Num 3+4i 3 1000 X::Str::Numeric 12 0 -0e0 Inf NaN -Inf 5 -5 X::Str::Numeric X::Str::Numeric X::Str::Numeric X::Str::Numeric X::AdHoc X::Str::Numeric X::Str::Numeric X::Str::Numeric Int 3.0 Rat Num 1.0 0.5 0.5 X::Str::Numeric 4 2 Int -1 3 0.5 -1 2 Less 5 15 Inf 1000 0.0015e0 Num X::Str::Numeric 0.5 5e0 222 Any Any X::Str::Numeric True Failure Failure X::Str::Numeric False
say ("abc" == 3).^name, " ", do { my $r = try "abc" == 3; ($! ?? "set" !! "unset") ~ " " ~ $r.^name }, " ", do { my $r = try +"abc"; ($! ?? "set" !! "unset") ~ " " ~ $r.^name }, " ", do { my $r = try { "abc" == 3 }; ($! ?? "set" !! "unset") ~ " " ~ $r.^name }, " ", (try ("abc" %% 2).^name) // $!.^name, " ", (try ("abc" <=> 1).^name) // $!.^name, " ", ("abc" != 1).^name, " ", ("abc" < 1).^name, " ", ("abc" + 1).^name, " ", ("abc" gcd 2).^name, " ", (try so "abc" == 3) // $!.^name, " ", (try so "abc" < 3) // $!.^name, " ", (try so "abc" + 3) // $!.^name, " ", (try ("abc" + 3).defined) // $!.^name, " ", do { my $f = "abc" == 3; $f.so; $f.exception.^name }, " ", do { my $f = "abc" < 3; $f.so; $f.exception.^name }, " ", (try ("abc" == 3) ?? "t" !! "f") // $!.^name, " ", (try ("abc" < 3 ?? "t" !! "f")) // $!.^name, " ", do { my $r = try "abc" + 3; ($! ?? "set" !! "unset") ~ " " ~ $r.^name }, " ", do { my $r = try "abc" < 3; ($! ?? "set" !! "unset") ~ " " ~ $r.^name }, " ", do { my $r = try { +"abc" }; ($! ?? "set" !! "unset") ~ " " ~ $r.^name }, " ", do { my $r = try { "abc" + 3 }; ($! ?? "set" !! "unset") ~ " " ~ $r.^name }
# rakudo 2026.08: Failure unset Failure set Any unset Failure X::Str::Numeric X::Str::Numeric Bool Failure Failure Failure False False False X::Str::Numeric X::Str::Numeric X::Str::Numeric f f set Any unset Failure set Any set Any
```
(Both lines also print "unhandled Failure detected in DESTROY"
warnings for the Failures that were never touched.) rakupp 4.0.1-118:
differs — `==`, `<`, `<=>` and `!=` on an unparsable string still THROW
`X::Str::Numeric` outright where Rakudo returns a Failure, so both probe
lines die at their first field. `+"abc"` and `"abc" + 3` are the soft
Failure already.

## D. Arithmetic

### N-10  Rational arithmetic, the 64-bit denominator and `$*RAT-OVERFLOW`  D:yes R:yes V:spec
`Int / Int` and Rat arithmetic are exact (`0.1 + 0.2 == 0.3`) and the
result is reduced; a result whose denominator reaches 2**64 or more
degrades to a Num (`1/(2**64)`, `(1/2) ** 64`, `(1/3) ** 41`, a
summed series) unless `$*RAT-OVERFLOW` says otherwise: `FatRat`
upgrades, `Failure` fails with `X::AdHoc` "Upgrading of Rat … not
allowed", `Exception` throws it, `CX::Warn` warns and degrades, and
any other object must provide `UPGRADE-RAT`. A FatRat is more
infectious than a Rat and never degrades; `FatRat.Rat` fails
`X::AdHoc` when the denominator is too big. Zero-denominator Rationals
take part in arithmetic without error (`<1/0> + 1` is `<1/0>`,
`<1/0> * 0` is `<0/0>`); `2 ** -1` and `(2/3) ** -3` are Rats,
`2 ** 0.5` a Num.
```
say 0.1 + 0.2 == 0.3, " ", (0.1+0.2).raku, " ", (1/3 + 1/3 + 1/3) == 1, " ", (1/3).^name, " ", (4/2).^name, " ", (4/2).raku, " ", (1 + 0.5).^name, " ", (1.5 + 1e0).^name, " ", (1/(2**64)).^name, " ", (1/(2**64-1)).^name, " ", (1/(2**64)).raku, " ", (1/(2**63)).^name, " ", ((1/3) * (1/(2**40))).^name, " ", do { my $s = 0; $s += 1/$_**2 for 1..1000; $s.^name }, " ", (1/3 + FatRat.new(1,3)).^name, " ", (FatRat.new(1,3) + 1e0).^name, " ", (FatRat.new(1,3) * 3).^name, " ", (FatRat.new(1,3) * 3).raku, " ", (1/3 + 1i).^name, " ", (0.5 ** 2).raku, " ", (0.5 ** -2).raku, " ", (2 ** -1).raku, " ", (2 ** -1).^name, " ", (2 ** 0.5).^name, " ", ((1/3) ** 2).raku, " ", ((2/3) ** -3).raku, " ", (9.0 ** -1).^name, " ", (9.0 ** 0.5).^name, " ", (1.015 ** 200).^name, " ", ((1/2) ** 64).^name, " ", ((1/2) ** 65).^name, " ", (FatRat.new(1,2) ** 65).^name, " ", ((1/3) ** 41).^name, " ", ((1/3) ** 40).^name, " ", (1/3 - 1/3).raku, " ", (-(1/3)).raku, " ", (-<1/0>).raku, " ", (<1/0> + 1).raku, " ", (<1/0> * 0).raku, " ", (<1/0> - <1/0>).raku, " ", (<0/0> + 1).raku, " ", (1/0).^name, " ", (0/0).^name, " ", (1/0 + 1e0), " ", (<1/0> / <1/0>).raku, " ", (1e0 / 3).^name, " ", (1/3e0).^name, " ", (2.5 * 2).raku, " ", (2.5 * 2).^name, " ", ((1/3).FatRat / (1/3)).raku, " ", ((2**64+1) / (2**64 * 3)).^name, " ", ((42 + Rat.new(1,2))/9999999999999999999).^name, " ", ((42 + FatRat.new(1,2))/9999999999999999999).^name, " ", (1/10**99).raku, " ", (0.5 ** 2).^name, " ", (2 ** 3).^name, " ", (2 ** -2).raku, " ", ((-2) ** -3).raku, " ", (10 ** -1).^name, " ", (0 ** -1).^name, " ", (try (0 ** -1).raku) // $!.^name, " ", (try (0 ** -1).Str) // $!.^name, " ", ((1/3) ** 0).raku, " ", ((1/3) ** 1).raku, " ", ((1/3) ** -1).raku, " ", ((-1/3) ** 3).raku, " ", ((-1/3) ** -3).raku, " ", (<1/0> ** 2).raku, " ", (<0/0> ** 2).raku, " ", (<1/0> ** 0).raku
# rakudo 2026.08: True 0.3 True Rat Rat 2.0 Rat Num Num Rat 5.421010862427522e-20 Rat Rat Num FatRat Num FatRat FatRat.new(1, 1) Complex 0.25 4.0 0.5 Rat Num <1/9> 3.375 Rat Num Num Num Num FatRat Num Rat 0.0 <-1/3> <-1/0> <1/0> <0/0> <0/0> <0/0> Rat Rat Inf <0/0> Num Num 5.0 Rat FatRat.new(1, 1) Num Num FatRat 1e-99 Rat Int 0.25 <1/-8> Rat Rat <1/0> X::Numeric::DivideByZero 1.0 <1/3> 3.0 <-1/27> <27/-1> <1/0> <0/0> 1.0
say (1/3).nude.raku, " ", (1/3).numerator, " ", (1/3).denominator, " ", (6/4).nude.raku, " ", (-6/4).nude.raku, " ", (0/4).nude.raku, " ", (4/-6).nude.raku, " ", (0.75).nude.raku, " ", (1e0/3).^name, " ", (1/3e0).^name, " ", (2**64 / 2).^name, " ", (1 / 2**64).^name, " ", (3 / 2**64).^name, " ", (2**64 / 2**64).raku, " ", ((2**64+1) / 2**64).^name, " ", ((2**64+1) / (2**64 * 3)).^name, " ", (1/3).nude.^name, " ", (1/3).norm.raku, " ", 1.0.nude.raku, " ", 1.50.nude.raku, " ", <1/3>.^name, " ", <1/3>.nude.raku, " ", <2/6>.nude.raku, " ", <1/0>.nude.raku, " ", (1/3).Rat.nude.raku, " ", 0.1.nude.raku, " ", 1.1e0.Rat.nude.raku, " ", (1/3).FatRat.nude.raku, " ", ((1/3).FatRat).^name, " ", (FatRat.new(1, 2**100) + 1).^name, " ", (FatRat.new(1, 2**100) + 1).Rat.^name, " ", (try FatRat.new(1, 2**100).Rat) // $!.^name, " ", FatRat.new(1, 5).Rat.^name, " ", (1/5).FatRat.^name, " ", 0.5.FatRat.nude.raku, " ", $*RAT-OVERFLOW.^name, " ", do { my $*RAT-OVERFLOW = FatRat; (1/(2**64)).^name }, " ", do { my $*RAT-OVERFLOW = FatRat; (1/(2**64)).raku }, " ", do { my $*RAT-OVERFLOW = Num; (1/(2**64)).^name }, " ", (try { my $*RAT-OVERFLOW = Failure; 1/(2**64) }) // $!.^name, " ", (try { my $*RAT-OVERFLOW = Exception; 1/(2**64) }) // $!.^name, " ", do { my @w; CONTROL { when CX::Warn { @w.push(.message.substr(0,30)); .resume } }; my $*RAT-OVERFLOW = CX::Warn; my $v = 1/(2**64); $v.^name ~ ":" ~ @w.elems ~ ":" ~ @w[0] }, " ", do { my $*RAT-OVERFLOW = FatRat; ((1/3) * (1/(2**70))).^name }, " ", do { my $*RAT-OVERFLOW = FatRat; (1/(2**64) + 1).^name }, " ", (try { my $*RAT-OVERFLOW = Int; 1/(2**64) }) // $!.^name, " ", do { my $*RAT-OVERFLOW = FatRat; ((1/2) ** 65).^name }, " ", do { my $*RAT-OVERFLOW = FatRat; (2 ** -65).^name }, " ", do { my $*RAT-OVERFLOW = FatRat; (0.1 + 1/(2**64)).^name }, " ", 0.1.^name, " ", (2**64 / 1).^name, " ", (2**64 / 1).raku, " ", ((2**65) / 2).raku, " ", (-1/(2**64)).^name, " ", do { my $*RAT-OVERFLOW = Failure; my $v = 1/(2**64); $v.so; $v.^name }, " ", (try { my $*RAT-OVERFLOW = Failure; (1/(2**64)).exception.^name }) // $!.^name, " ", (try { my $*RAT-OVERFLOW = Exception; (1/(2**64)) }) // $!.message, " ", do { my $*RAT-OVERFLOW = FatRat; (1/(2**64) * 2**64).raku }, " ", do { my $*RAT-OVERFLOW = FatRat; (1/(2**64)).Rat.^name }, " ", (try { my $*RAT-OVERFLOW = Failure; my $v = 1/(2**64); $v + 1 }) // $!.^name, " ", (2**64 - 1) / (2**64), " ", ((2**64 - 1) / (2**64)).^name, " ", (1/3 + 1/(2**63)).^name, " ", (1/3 * 1/(2**63)).^name, " ", (1/(2**63) + 1/(2**63)).^name, " ", (1/(2**63) + 1/(2**63)).raku, " ", (1/(2**63) * 2).raku, " ", ((2**64-1)/(2**64-1)).raku, " ", (Rat.new(1, 2**65) * 1).^name, " ", (Rat.new(1, 2**65) + 0).^name, " ", (Rat.new(1, 2**65) - 0).^name, " ", (Rat.new(1, 2**65) / 1).^name, " ", (Rat.new(1, 2**65) == Rat.new(1, 2**65)), " ", (Rat.new(1, 2**65) < 1), " ", Rat.new(1, 2**65).floor, " ", Rat.new(1, 2**65).round, " ", (-Rat.new(1, 2**65)).^name, " ", Rat.new(1, 2**65).abs.^name, " ", Rat.new(1, 2**65).succ.^name, " ", Rat.new(1, 2**65).Num
# rakudo 2026.08: (1, 3) 1 3 (3, 2) (-3, 2) (0, 1) (-2, 3) (3, 4) Num Num Rat Num Num 1.0 Num Num List <1/3> (1, 1) (3, 2) Rat (1, 3) (1, 3) (1, 0) (1, 3) (1, 10) (11, 10) (1, 3) FatRat FatRat Failure X::AdHoc Rat FatRat (1, 2) Num Num 5.421010862427522e-20 Num X::AdHoc X::AdHoc Num:0: Num Num X::Method::NotFound Num Num Num Rat Rat 18446744073709551616.0 18446744073709551616.0 Num Num X::AdHoc Upgrading of Rat 1 / 18446744073709551616 not allowed 1e0 Rat X::AdHoc 1 Num Num Num Rat 0.00000000000000000021684043449710088680149056017398834228515625 0.00000000000000000021684043449710088680149056017398834228515625 1.0 Num Num Num Num True True 0 0 Rat Rat Rat 2.710505431213761e-20
```
(`FatRat` upgrade with `CX::Warn` printed `Num:0:` here because the
warning was issued at the fold, not through the `CONTROL` block.)
rakupp 4.0.1-118: differs — `$*RAT-OVERFLOW` is ignored (every setting gives
a Num), `FatRat.Rat` never fails, and `0 ** -1` is a Failure
`X::Numeric::DivideByZero` where Rakudo makes `<1/0>`. `(-2) ** -3` is the
correct `-0.125`, which is the recorded Rakudo bug and is deliberately not
imitated (N-12).

### N-11  Division, modulo, divisibility, gcd and lcm                      D:partial R:yes V:bug
`div` floors (`-7 div 2` is -4) and `%`, `mod` follow the divisor's
sign; `Int / Int` is a Rat, so `1/0` is `<1/0>` and only explodes when
printed; `div`, `%` and `%%` by an Int zero return a Failure
`X::Numeric::DivideByZero` (`.using` "div", "%" or "infix:<%%>",
`.numerator` the dividend), `mod 0` throws it; Num division and `%`
by zero (`1e0/0`, `1e0/0e0`, `1/0e0`) are Failures too (`.using` "/",
"%"). `%` on Rats and Nums keeps the type (`5.5 % 2` is a Rat 1.5,
`-5.5 % 2` 0.5, `13e21 % 4e21` a Num). `mod` and `div` truncate a
non-Int divisor to an Int before use, so `7 div 2.5` is 3 and
`7 div 0.5` fails `X::Numeric::DivideByZero`, while `7 mod 2.5` is
`7 - (7 div 2) * 2.5` = -0.5: a value no modulus can have. `gcd` and
`lcm` coerce to Int (`3.5 gcd 2` is 1), `0 gcd 0` is 0, `4 lcm 0` 0.
```
say 7 div 2, " ", -7 div 2, " ", 7 div -2, " ", -7 % 2, " ", 7 % -2, " ", -7 mod 2, " ", 7 mod -2, " ", 7 %% 2, " ", 8 %% 2, " ", (try 7 div 0) // $!.^name, " ", (try 7 % 0) // $!.^name, " ", (try 7 %% 0) // $!.^name, " ", (try 7 mod 0) // $!.^name, " ", (1/0).^name, " ", (7/0).nude.raku, " ", (try 1e0/0) // $!.^name, " ", (try 1e0/0e0) // $!.^name, " ", (try 1/0e0) // $!.^name, " ", (1/0e0).^name, " ", (try 1e0 % 0) // $!.^name, " ", (try say 1/0) // $!.^name, " ", 1 % 0.5, " ", 5.5 % 2, " ", -5.5 % 2, " ", 5.5e0 % 2, " ", -5.5e0 % 2, " ", 5 % 2.5, " ", 2.5 % 1, " ", 13 % -4, " ", -13 % -4, " ", 13.0 % -4.0, " ", (4.8 % 1).^name, " ", (4 % 1.1).^name, " ", 7 mod 2.5, " ", 7.5 mod 2, " ", -7.5 mod 2, " ", (7.5 mod 2).^name, " ", 7 div 2.5, " ", 7.9 div 2, " ", (7.9 div 2).^name, " ", "7" div "2", " ", -4 gcd 6, " ", 0 gcd 0, " ", 4 lcm 6, " ", 4 lcm 0, " ", 3.5 gcd 2, " ", "6" gcd 4, " ", -4 lcm 6, " ", 4 gcd -6, " ", (2**70) gcd (2**65), " ", 13e21 % 4e21, " ", -13e21 % 4e21, " ", 7.5e0 mod 2, " ", (try 7 div 0.5) // $!.^name, " ", (try 7 div 0.4) // $!.^name, " ", 5.5 div 2, " ", (5.5 div 2).^name, " ", -7.5 div 2, " ", (try 7.5 mod 0) // $!.^name, " ", (try 7 mod 0.4) // $!.^name, " ", 2**70 % 3, " ", (2**70) div 3, " ", (2**70 %% 4), " ", 0 % 5, " ", (0 %% 5), " ", 5 %% -5, " ", -5 %% 5, " ", 5.5 %% 0.5, " ", 5e0 %% 2.5e0, " ", (try 5 %% 0.0) // $!.^name, " ", "8" %% "4", " ", 7.5e0 % -2, " ", (7.5e0 % -2).^name, " ", (7 % 2.5e0).^name, " ", (7.5 % 2).^name, " ", 1 gcd 0, " ", 0 lcm 5, " ", 6 lcm -4, " ", -6 lcm -4, " ", 7 mod 2.5e0, " ", 7e0 mod 2.5, " ", 7.5 mod 2.5, " ", 8 mod 2.5, " ", 7 mod 1.5, " ", (7 mod 2.5).^name, " ", 7 div 1.5, " ", 7 div 2e0, " ", (7 div 2e0).^name, " ", 7.5e0 div 2, " ", (7.5e0 div 2).^name, " ", -7 div 2e0, " ", 7 div -2.5, " ", 7 % 0.5e0, " ", -7 % 2e0, " ", 7e0 % -2, " ", (-7) mod 2e0, " ", 7e0 mod -2, " ", 5.5 %% 0.5e0, " ", 5 %% 2.5, " ", 5 %% 2e0, " ", 6 %% 1.5, " ", 5.5e0 %% 1.1e0, " ", 0 %% 0.5, " ", (try 0 %% 0) // $!.^name
# rakudo 2026.08: 3 -4 -4 1 -1 1 -1 False True X::Numeric::DivideByZero X::Numeric::DivideByZero X::Numeric::DivideByZero X::Numeric::DivideByZero Rat (1, 0) X::Numeric::DivideByZero X::Numeric::DivideByZero X::Numeric::DivideByZero Failure X::Numeric::DivideByZero X::Numeric::DivideByZero 0 1.5 0.5 1.5 0.5 0 0.5 -3 -1 -3 Rat Rat -0.5 1.5 0.5 Rat 3 3 Int 3 2 0 12 0 1 2 12 2 36893488147419103232 1e+21 3e+21 1.5 X::Numeric::DivideByZero X::Numeric::DivideByZero 2 Int -4 X::Numeric::DivideByZero X::Numeric::DivideByZero 1 393530540239137101141 True 0 True True True True True X::Numeric::DivideByZero True -0.5 Num Num Rat 1 0 12 12 -0.5 -0.5 0 -2 -3.5 Rat 7 3 Int 3 Int -4 -4 0 1 -1 1 -1 True True False True False True X::Numeric::DivideByZero
say do { my $f = 7 div 0; $f.so; $f.exception.^name ~ ":" ~ $f.exception.using ~ ":" ~ $f.exception.numerator }, " ", do { my $f = 7 % 0; $f.so; $f.exception.using ~ ":" ~ $f.exception.numerator }, " ", do { my $f = 7 %% 0; $f.so; $f.exception.using }, " ", (try 7 mod 0) // $!.^name ~ ":" ~ $!.numerator ~ ":" ~ $!.using.raku, " ", do { my $f = 1e0/0; $f.so; $f.exception.using ~ ":" ~ $f.exception.numerator }, " ", do { my $f = 1e0 % 0; $f.so; $f.exception.using ~ ":" ~ $f.exception.numerator }, " ", (try <1/0>.Str) // $!.numerator ~ ":" ~ $!.details ~ ":" ~ $!.using.raku, " ", (try <0/0>.gist) // $!.numerator.raku, " ", do { my $f = <1/0>.floor; $f.so; $f.exception.details ~ ":" ~ $f.exception.numerator.raku }, " ", do { my $f = <1/0>.Int; $f.so; $f.exception.details }, " ", do { my $f = <1/0>.round; $f.so; $f.exception.details }, " ", do { my $f = <0/0>.ceiling; $f.so; $f.exception.details }, " ", (try say 1/0) // $!.numerator ~ ":" ~ $!.details, " ", (try 0/0 + "x") // $!.^name, " ", (3.5 / 0).^name, " ", (3.5 / 0).nude.raku, " ", (try say 3.5 / 0) // $!.numerator, " ", (try say 0 / 0) // $!.numerator ~ ":" ~ $!.details, " ", (try 120.polymod(0).eager) // $!.^name ~ ":" ~ $!.using.raku ~ ":" ~ $!.numerator.raku, " ", (try 5.round(0)) // $!.^name ~ ":" ~ $!.using.raku, " ", (try 5 %% 0.0) // $!.using ~ ":" ~ $!.numerator, " ", (try 7 div 0.5) // $!.using ~ ":" ~ $!.numerator, " ", (try 7 mod 0.4) // $!.numerator, " ", (try 1 +< Inf) // $!.^name ~ ":" ~ $!.source
# rakudo 2026.08: X::Numeric::DivideByZero:div:7 %:7 infix:<%%> X::Numeric::DivideByZero:7:"div" /:1 %:1 1:when coercing Rational to Str:Any 0 when calling .floor on Rational:Any when calling .Int on Rational when calling .round on Rational when calling .ceiling on Rational 1:when coercing Rational to Str X::Str::Numeric Rat (1, 0) 1 0:when coercing Rational to Str X::Numeric::DivideByZero:"polymod":120 X::Numeric::DivideByZero:Any infix:<%%>:5 div:7 7 X::Numeric::CannotConvert:Inf
```
`7 mod 2.5` is the bug; the docs declare `mod` for `Int:D, Int:D` only.
The `%` identity `x - floor(x / y) * y` is the documented one and holds
for Rats and Nums. rakupp 4.0.1-118: differs on one point only — Num
division and `%` by zero give `Inf` (the 6.e behaviour the docs announce)
instead of a Failure, which takes the second probe line with it. It
reproduces `7 mod 2.5` as -0.5, and its `X::Numeric::DivideByZero` carries
no `.using`, `.numerator` or `.details`.

### N-12  Power                                                          D:partial R:yes V:bug
`Int ** Int` is exact; a negative exponent gives a Rat (`2 ** -1` is
0.5, `10 ** -1` a Rat); `0 ** -1` is the zero-denominator Rat `<1/0>`;
a result too large is a Failure `X::Numeric::Overflow` (`10 ** 600 **
600`, `2 ** 2**40`) and a Rat that cannot be formed `X::Numeric::
Underflow` (`2 ** -(10**10)`). A Num base or exponent gives a Num
(`4 ** 0.5`, `2 ** 1e400` is Inf); `NaN ** 0`, `1 ** NaN`, `1 ** Inf`
are 1; `(-8) ** (1/3)` is NaN; a Num result that underflows to zero
from a non-zero base is a Failure `X::Numeric::Underflow`
(`1e-300 ** 2`, `1e-162 ** 2`, `2e0 ** -1e20`) while `0e0 ** 1` and
`1e-160 ** 2` (a denormal) are fine. Superscripts are the same operator
(`3⁴`, `2⁻¹`, `10¹⁰⁰`). Two Rakudo bugs: a negative base to a negative
Int power builds a Rat with a NEGATIVE denominator that is not
normalized (`((-2) ** -3).nude` is `(1, -8)`, `== -0.125` is False,
`.Str` is "-1.875", `< 0` False, `.floor` -1) and `0e0 ** -1` with an
Int exponent is a Failure `X::Numeric::Underflow` where `0e0 ** -1e0`
is Inf.
```
say 2 ** 10, " ", (2 ** 10).^name, " ", 2 ** -1, " ", 0 ** 0, " ", (try (0 ** -1).raku) // $!.^name, " ", (0 ** -1).^name, " ", 4 ** 0.5, " ", (4 ** 0.5).^name, " ", (-8) ** (1/3), " ", ((-8) ** (1/3)).^name, " ", 2e0 ** 1e400, " ", (0e0 ** -1e0).raku, " ", (try (1e-300 ** 2).raku) // $!.^name, " ", (0e0 ** 1).raku, " ", (2 ** 1e400).raku, " ", Inf ** 0, " ", (-1) ** Inf, " ", 1 ** Inf, " ", 0 ** Inf, " ", NaN ** 0, " ", 1 ** NaN, " ", 0.9 ** Inf, " ", 1.1 ** Inf, " ", (-2) ** 2, " ", (-2) ** 3, " ", -2 ** 2, " ", 2 ** 3 ** 2, " ", (2 ** 200).chars, " ", (try (2 ** -(10**10)).raku) // $!.^name, " ", (try ((1/3) ** (10**10)).^name) // $!.^name, " ", (try (2e0 ** -1e20).raku) // $!.^name, " ", (1e0 ** 1e400).raku, " ", 2 ** 0.5 == sqrt(2), " ", sqrt(-1).raku, " ", (-4).sqrt.raku, " ", 4.sqrt.raku, " ", (1/4).sqrt.raku, " ", sqrt(-0e0).raku, " ", 8.roots(3).elems, " ", 8.roots(3)[0].^name, " ", (try 8.roots(0).raku) // $!.^name, " ", 3⁴, " ", 2⁻¹, " ", (-1)¹²³, " ", 10¹⁰⁰.chars, " ", 4 ** ½, " ", 27 ** ⅓, " ", (2 ** 64).raku, " ", 2 ** 3 ** 0, " ", 1e0 ** 4553535345364535345634543534, " ", (-1) ** 4553535345364535345634543533, " ", 0 ** 4553535345364535345634543534, " ", (1 ** 4553535345364535345634543534), " ", (2 ** 2.0).raku, " ", (2 ** 2e0).raku, " ", (2.0 ** 2).raku, " ", (2e0 ** 2).raku, " ", (4 ** (1/2)).raku, " ", (Inf ** -1).raku, " ", ((-Inf) ** 3).raku, " ", ((-Inf) ** 2).raku, " ", (Inf ** Inf).raku, " ", (Inf ** -Inf).raku, " ", (0 ** -Inf).raku, " ", (2 ** Inf).raku, " ", (2 ** -Inf).raku, " ", (0.5 ** Inf).raku, " ", ((-0.5) ** Inf).raku, " ", ((-2) ** 0.5).raku, " ", ((-2e0) ** 0.5e0).raku, " ", (try 0e0 ** -1) // $!.^name, " ", (try (2 ** (2**40)).^name) // $!.^name
# rakudo 2026.08: 1024 Int 0.5 1 <1/0> Rat 2 Num NaN Num Inf Inf X::Numeric::Underflow 0e0 Inf 1 1 1 0 1 1 0 Inf 4 -8 -4 512 61 X::Numeric::Underflow X::Numeric::Overflow X::Numeric::Underflow 1e0 True NaN NaN 2e0 0.5e0 -0e0 3 Complex NaN 81 0.5 -1 101 2 3 18446744073709551616 2 1 -1 0 1 4e0 4e0 4.0 4e0 2e0 Failure.new(exception => X::Numeric::Underflow.new, backtrace => Backtrace.new) -Inf Inf Inf 0e0 Inf Inf 0e0 0e0 0e0 NaN NaN Inf X::Numeric::Overflow
say ((-2) ** -3).nude.raku, " ", ((-2) ** -3) == -0.125, " ", ((-2) ** -3).raku, " ", ((-1/3) ** -3).nude.raku, " ", ((-2) ** -3).Str, " ", ((-2) ** -3).denominator, " ", (((-2) ** -3) + 0).raku, " ", ((-2) ** -3).abs.raku, " ", ((-3) ** -1).nude.raku, " ", ((-2) ** -2).nude.raku, " ", ((-0e0) ** 3).raku, " ", ((-0e0) ** 2).raku, " ", ((-0e0) ** 1).raku, " ", ((-0e0) ** -1).raku, " ", ((-2e0) ** 3).raku, " ", (0e0 ** 0e0).raku, " ", (0e0 ** 0).raku, " ", (2e0 ** 0.5e0).raku, " ", ((2 ** 0.5) ** 2).raku, " ", (1e-200 ** 2).^name, " ", (1e-160 ** 2).^name, " ", (1e-162 ** 2).^name, " ", ((-1e-200) ** 2).^name, " ", (1e200 ** 2).raku, " ", ((-1e200) ** 3).raku, " ", ((-2) ** -3).WHICH, " ", ((-2) ** -3) < 0, " ", ((-2) ** -3).floor, " ", ((-2) ** -3).sign, " ", ((-2) ** -3).Num, " ", (((-2) ** -3) * 8).raku, " ", ((-2) ** -3) === (-1/8), " ", ((-2) ** -3) == (-1/8), " ", ((-2) ** -3).nude eqv (-1/8).nude, " ", ((-2) ** -3).numerator, " ", ((-2) ** -3).Rat.nude.raku, " ", ((-2) ** -3).round(0.001), " ", ((-2) ** -3).base(10), " ", ((-2) ** -3).succ.raku
# rakudo 2026.08: (1, -8) False <1/-8> (27, -1) -1.875 -8 -0.125 <1/-8> (1, -3) (1, 4) 0e0 0e0 0e0 -Inf -8e0 1e0 1e0 1.4142135623730951e0 2.0000000000000004e0 Failure Num Failure Failure Inf -Inf Rat|1/-8 False -1 1 -0.125 -1.0 False False False 1 (1, -8) -0.125 -1.875 0.875
```
(`(-0e0) ** 3` and `(-0e0) ** 1` come out as `0e0`: Rakudo replaces a
zero result by `+0e0` on the way out, which IEEE 754 does not.)
rakupp 4.0.1-118: differs — `0 ** -1` is a Failure
`X::Numeric::DivideByZero` where Rakudo makes `<1/0>`, which shifts the rest
of the line; `0e0 ** -1` is `0e0` (wrong both ways, should be Inf); and
`10¹⁰⁰` has 1000 digits instead of 101, a rakupp bug in superscript parsing.
`(-2) ** -3` is the correct `<-1/8>` and `1e-300 ** 2`, `2e0 ** -1e20`,
`(-0e0) ** 3` are IEEE — all four deliberate.

### N-13  Rounding                                                       D:partial R:yes V:quirk
`round` adds a half and floors, so halves go up towards +Inf for every
type (`2.5.round` 3, `(-2.5).round` -2, `(-0.5e0).round` 0) and
`0.49999999999999994e0.round` is 1 because the sum rounds to 1.0 in
binary; `round($scale)` is `(x / scale + 1/2).floor * scale`, whose type
follows the arithmetic (`1234.round(100)` an Int 1200, `pi.round(0.001)`
a Rat 3.142, `42.round(42e0)` a Num, `1.5.round(-1)` 1); `round(0)`
fails `X::Numeric::DivideByZero`. `floor`, `ceiling`, `truncate`,
`round` return an Int for a Rat or Num (a 301-digit one for
`1.5e300.floor`) and pass Inf, -Inf and NaN through as Nums; on a
zero-denominator Rational they fail `X::Numeric::DivideByZero` with
`.details` "when calling .floor on Rational" and so on. A Str is
coerced first (`"2.5".round` is 3, `"17.25".round("0.1")` 17.3).
```
say 2.5.round, " ", (-2.5).round, " ", 3.5.round, " ", (-3.5).round, " ", 2.5e0.round, " ", (-2.5e0).round, " ", 0.5.round, " ", (-0.5).round, " ", 1.5.round.^name, " ", 1.5e0.round.^name, " ", 3.14159.round(0.01), " ", 3.14159.round(0.01).^name, " ", 1234.round(100), " ", 1250.round(100), " ", 1234.round(100).^name, " ", pi.round(0.001), " ", pi.round(0.001).^name, " ", 42.round(42e0).^name, " ", 42e0.round(42).^name, " ", 42.0.round(42.0).^name, " ", 2.round(3/20), " ", 987654321.round(1e5), " ", 123.456789.round(1/100), " ", 1e0.round(0.1).raku, " ", Inf.round.raku, " ", NaN.round.raku, " ", Inf.floor.raku, " ", (-Inf).ceiling.raku, " ", NaN.truncate.raku, " ", Inf.round.^name, " ", 1.5.floor, " ", (-1.5).floor, " ", 1.2.ceiling, " ", (-1.2).ceiling, " ", (-1.5).truncate, " ", 1.5e0.floor.^name, " ", 1.5e300.floor.chars, " ", (7/2).floor, " ", (7/2).ceiling, " ", (7/2).truncate, " ", (-7/2).truncate, " ", (try <1/0>.floor) // $!.^name, " ", (try <1/0>.Int) // $!.^name, " ", (try <1/0>.round) // $!.^name, " ", (try <0/0>.ceiling) // $!.^name, " ", "2.5".round, " ", "17.25".round("0.1"), " ", round(1.23456, 0.001), " ", round([<a b c>]), " ", (try 5.round(0)) // $!.^name, " ", 2.5.round(1), " ", 42.floor.^name, " ", 42.round.^name, " ", 1.5.round(-1), " ", 17.round(-5), " ", (-2.5e0).round, " ", (-0.5e0).round.raku, " ", 0.49999999999999994e0.round, " ", 2.5.round(0.5), " ", 2.75.round(0.5), " ", 2.25.round(0.5), " ", (-2.25).round(0.5), " ", 5e33.round, " ", 12345.round(1e5).raku, " ", 1.5.round(2).raku, " ", 4.5.round(3).raku, " ", 4.5e0.round(3).raku, " ", <1/2>.round, " ", <3/2>.round, " ", <-1/2>.round, " ", <-3/2>.round, " ", 0.5e0.round, " ", 1.5e0.round, " ", 4.8.round(1e0).raku, " ", 4.8e0.round(1).raku, " ", 4.8.round(1.0).raku, " ", (2**70 + 0.5).round, " ", (2**70).round(0.5).raku, " ", Inf.round(0.5).raku, " ", (try NaN.round(0)) // $!.^name, " ", 0.round(0.1).raku, " ", 5.truncate.^name, " ", (-5.5e0).ceiling, " ", 5.5e0.floor.^name, " ", 1e100.floor.chars, " ", 1e-100.ceiling, " ", 1e-100.floor, " ", 0.5.round(0.2), " ", 0.3.round(0.2), " ", 2.5.round(2), " ", 1e0.round(0.1), " ", 0.15.round(0.1), " ", 0.25.round(0.1), " ", 0.35.round(0.1), " ", 1.005.round(0.01), " ", 2.675.round(0.01), " ", 2.675e0.round(0.01), " ", 0.5e0.round(0.1).raku, " ", 0.05e0.round(0.1).raku, " ", 0.15e0.round(0.1).raku, " ", 0.25e0.round(0.1).raku, " ", (-0.25e0).round(0.1).raku, " ", 1e-20.round(0.1).raku, " ", 2.5e0.round(1e0).raku, " ", (-2.5e0).round(1e0).raku, " ", 2.5.round(1e0).raku
# rakudo 2026.08: 3 -2 4 -3 3 -2 1 0 Int Int 3.14 Rat 1200 1300 Int 3.142 Rat Num Int Rat 1.95 987700000 123.46 1.0 Inf NaN Inf -Inf NaN Num 1 -2 2 -1 -1 Int 301 3 4 3 -3 X::Numeric::DivideByZero X::Numeric::DivideByZero X::Numeric::DivideByZero X::Numeric::DivideByZero 3 17.3 1.235 3 X::Numeric::DivideByZero 3 Int Int 1 15 -2 0 1 2.5 3 2.5 -2 4999999999999999727876154935214080 0e0 2 6 6 1 2 0 -1 1 2 5e0 5 5.0 1180591620717411303425 1180591620717411303424.0 Inf X::Numeric::DivideByZero 0.0 Int -5 Int 101 1 0 0.6 0.4 2 1 0.2 0.3 0.4 1.01 2.68 2.68 0.5 0.1 0.1 0.3 -0.2 0.0 3e0 -2e0 3e0
```
The half-up rule and the `0.49999999999999994e0` case are the quirk;
the docs say only "rounds to scale". rakupp 4.0.1-118: differs in two fields
— `5.round(0)` is 5 and `NaN.round(0)` NaN where Rakudo fails. Every other
field, including the half-up rule, the binary-half case and the Inf/NaN
pass-through of the sub forms, matches.

### N-14  sign, abs, succ, pred                                          D:yes R:yes V:spec
`sign` is an Int -1, 0 or 1 for any Real, including `Inf`, `-Inf`,
`-0e0` (0), `<1/0>` and `<-1/0>`; `NaN.sign` is NaN; `<0/0>.sign` is 0;
`sign(Int)` is `X::Parameter::InvalidConcreteness`. `abs` keeps the
type (`(-0e0).abs` is `0e0`). `succ`/`pred` add or subtract one in the
type (a Rat keeps its denominator, `Inf.succ` is Inf, `NaN.succ` NaN,
`<1/0>.succ` `<1/0>`); a Num at 2**53 does not move; `True.succ` is
True.
```
say (-3).sign, " ", 0.sign, " ", Inf.sign, " ", (-Inf).sign, " ", NaN.sign.raku, " ", (-0e0).sign, " ", <-1/0>.sign, " ", <0/0>.sign, " ", <1/0>.sign, " ", (3/2).sign.^name, " ", 1.5e0.sign.^name, " ", (try sign(Int)) // $!.^name, " ", "-17".sign, " ", (-5).abs, " ", (-5).abs.^name, " ", (-1.5e0).abs.^name, " ", (-1.5).abs.^name, " ", (-0e0).abs.raku, " ", abs("-10"), " ", abs(NaN).raku, " ", (-3).succ, " ", 1.5.succ.raku, " ", 1e0.succ.raku, " ", Inf.succ, " ", (1/3).succ.raku, " ", (1/3).pred.raku, " ", NaN.succ.raku, " ", 0.pred, " ", <1/0>.succ.raku, " ", (2**64).succ, " ", FatRat.new(1,3).succ.raku, " ", True.succ, " ", (try True.succ.succ) // $!.^name, " ", 1.5.succ.^name, " ", 1e0.succ.^name, " ", Inf.sign.^name, " ", (-Inf).abs, " ", <-1/0>.abs.raku, " ", (-2**70).abs, " ", abs(-5).^name, " ", (-0.0).sign, " ", 0e0.sign, " ", (sign 3.7), " ", (sign "0"), " ", (try sign("abc")) // $!.^name, " ", (try abs("abc")) // $!.^name, " ", (quietly abs(Int)), " ", 5.succ.^name, " ", (1/2).succ.nude.raku, " ", 1e308.succ.raku, " ", 1.7976931348623157e308.succ.raku, " ", (2**53).Num.succ.raku, " ", (2**53).Num.succ == 2**53, " ", "9".succ, " ", "a9".pred, " ", (-1).succ.raku
# rakudo 2026.08: -1 0 1 -1 NaN 0 -1 0 1 Int Int X::Parameter::InvalidConcreteness -1 5 Int Num Rat 0e0 10 NaN -2 2.5 2e0 Inf <4/3> <-2/3> NaN -1 <1/0> 18446744073709551617 FatRat.new(4, 3) True True Rat Num Int Inf <1/0> 1180591620717411303424 Int 0 0 1 0 X::Str::Numeric X::Str::Numeric 0 Int (3, 2) 1e+308 1.7976931348623157e+308 9007199254740992e0 True 10 a8 0
```
rakupp 4.0.1-118: differs in two fields — `sign(Int)` is `X::Multi::NoMatch`
rather than `X::Parameter::InvalidConcreteness` and `<0/0>.sign` is NaN
rather than 0.

### N-15  Comparison                                                      D:yes R:yes V:spec
`==`, `<` and friends coerce with `.Numeric`/`.Real` and compare
exactly across Int, Rat and FatRat (`1/3 < 1/3 + FatRat.new(1, 10**30)`),
but through a Num when a Num is involved (`2**70 + 1 == 2e0**70`);
Bools count as 0 and 1. NaN is unequal to everything (`!=` True,
`<`/`>` False) and `<=>` with NaN is Nil; `cmp` sorts NaN as the string
"NaN" (after digits) and `-0e0 cmp 0e0` is Same; `Inf cmp "abc"` is
More and `-Inf cmp "abc"` Less, any other Real against a Str compares
the strings. `=~=` uses `$*TOLERANCE` (1e-15) relative to the larger
magnitude, absolutely when either side is 0, and is True for
`Inf =~= Inf`, False for NaN.
```
say "3" == 3, " ", (try "abc" == 3) // $!.^name, " ", "3" < 4, " ", Inf > Inf, " ", NaN == NaN, " ", NaN != NaN, " ", NaN < 1, " ", NaN > 1, " ", (NaN <=> 1).raku, " ", (1 <=> NaN).raku, " ", (NaN <=> NaN).raku, " ", NaN cmp 1, " ", 1 cmp NaN, " ", NaN cmp NaN, " ", 1 cmp 1.0, " ", -0e0 cmp 0e0, " ", Inf cmp "abc", " ", -Inf cmp "abc", " ", 1 cmp "abc", " ", 10 cmp "9", " ", 10 <=> "9", " ", <0/0> cmp 1, " ", 1 cmp <0/0>, " ", <1/0> cmp 5, " ", <1/0> cmp Inf, " ", 1 <=> "1", " ", 1 =~= 1+1e-16, " ", 0 =~= 1e-16, " ", 1e10 =~= 1e10+1e-5, " ", 1e10 =~= 1e10+1e-6, " ", Inf =~= Inf, " ", NaN =~= NaN, " ", 1 == 1e0 == 1/1 == FatRat.new(1), " ", <1/2> == 0.5e0, " ", 0.1 == 0.1e0, " ", (1/3) == (1/3).Num, " ", 0.1 + 0.2e0 == 0.3, " ", <1/0> == Inf, " ", <0/0> == NaN, " ", <1/0> < Inf, " ", <1/0> <= Inf, " ", <-1/0> < <1/0>, " ", True == 1, " ", 1 < 2 < 3, " ", 1 < 3 < 2, " ", (1/3) < (1/3 + FatRat.new(1, 10**30)), " ", 2**70 == 2e0**70, " ", 2**70 + 1 == 2e0**70, " ", 9007199254740993 == 9007199254740992e0, " ", 1 == True, " ", "1" == "1.0", " ", 1 <=> 2, " ", 2 <=> 1, " ", (1 <=> 1).^name, " ", 1 == 1 == 1, " ", (1 != 2).^name, " ", 1 ≤ 2, " ", 1 ≥ 2, " ", 1 ≠ 2, " ", 1 ⩵ 1, " ", 1 ≅ 1, " ", (1/3 <=> 0.3333), " ", (0.3333 <=> 1/3), " ", <1/0> <=> Inf, " ", (Inf <=> Inf).raku, " ", (-Inf <=> Inf), " ", 1 cmp 2, " ", 1.5 cmp 1.5e0, " ", (2 cmp 10), " ", ("2" cmp "10"), " ", (2 <=> "10"), " ", ("2" <=> "10"), " ", ("2" leg "10"), " ", 1 before 2, " ", 2 after 1.5, " ", (1 max 2), " ", (1 min 2e0).raku, " ", ("a" cmp 1), " ", ("10" cmp 9), " ", (9 cmp "10"), " ", (1 == "1"), " ", (try ("a" == "a").raku) // $!.^name, " ", (try "a" == 1) // $!.^name, " ", (try "" == 0) // $!.^name, " ", (1 <=> 1.0), " ", (1e0 <=> 1), " ", (<1/3> <=> <2/6>), " ", (1 cmp 1e0), " ", (2**70 cmp 2e0**70), " ", (2**70 + 1 cmp 2e0**70), " ", (2**70 + 1 <=> 2e0**70), " ", (1/3 cmp 0.3333333333333333e0), " ", (0.1 cmp 0.1e0), " ", ((0.1 + 0.2) cmp 0.3e0), " ", (Inf cmp Inf), " ", (Inf <=> -Inf), " ", (NaN cmp Inf), " ", (NaN cmp "NaN"), " ", (NaN cmp "a"), " ", (NaN cmp "z"), " ", (Inf cmp 5), " ", (-Inf cmp -5), " ", ((1, NaN, 0, Inf, -Inf).sort.raku), " ", ((3, 1, 2.5, 2e0).sort.raku), " ", ((3, "10", 2).sort.raku), " ", ((3, "10", 2).sort(*.Int).raku)
# rakudo 2026.08: True Any True False False True False False Nil Nil Nil More Less Same Same Same More Less Less Less More More Less More Same Same True True True True True False True True True True False True False False True True True True False True True True True True True Less More Order True Bool True False True True True More Less Same Order::Same Less Less Same Less More Less Less More True True 2 1 More Less More True Failure.new(exception => X::Str::Numeric.new(source => "a", pos => 0, reason => "base-10 number must begin with valid digits or '.'"), backtrace => Backtrace.new) Any True Same Same Same Same Same Same Same Same Same Same Same More More Same Less Less More Less (-Inf, 0, 1, Inf, NaN).Seq (1, 2e0, 2.5, 3).Seq ("10", 2, 3).Seq (2, 3, "10").Seq
```
(The `Any` fields are the unhandled Failure of N-09 read through
`$!.^name`.) rakupp 4.0.1-118: differs in three fields — `Inf cmp "abc"` is
Less, and `"abc" == 3`, `"a" == "a"` throw `X::Str::Numeric` instead of
returning a Failure (N-09).

### N-16  Reduction identities and one operand                            D:no R:partial V:bug
With no operands `[+]`, `[-]`, `[+|]`, `[+^]` are 0, `[*]`, `[**]` are
1, `[+&]` is -1, `[==]`, `[<]`, `[!=]`, `[eqv]`, `[===]` True, `[max]`
-Inf, `[min]` Inf, and `[/]`, `[%]`, `[%%]`, `[gcd]`, `[+<]`, `[div]`,
`[mod]` fail `X::NoZeroArgMeaning`; `[lcm] ()` dies of an ambiguous
dispatch between two zero-argument candidates. With one operand `[+]`,
`[-]`, `[/]`, `[**]`, `[%]` return it coerced to Numeric (`[-] 5` is 5,
not -5, `[+] "5"` an Int), `[%%]`, `[<]`, `[!=]` are True, `[div] 5` and
`[mod] 5` throw `X::AdHoc` (no one-argument candidate). `[max]` over a
List keeps the FIRST of equal values (`[max] 1, 1.0, 1e0` is an Int)
while the infix keeps the second (`1 max 1.0` is a Rat). A Range in the
operand list is one operand (`[+] 1..3, 4` is the Range `5..7`).
```
say ([+] ()), " ", ([*] ()), " ", ([**] ()), " ", ([-] ())
# rakudo 2026.08: 0 1 1 0
say (try [/] ()) // $!.^name, " ", (try [gcd] ()) // $!.^name, " ", (try [lcm] ()) // $!.^name, " ", (try [%] ()) // $!.^name, " ", (try [%%] ()) // $!.^name, " ", (try [+<] ()) // $!.^name, " ", (try [div] ()) // $!.^name, " ", (try [mod] ()) // $!.^name, " ", (try [div] 5) // $!.^name, " ", (try [mod] 5) // $!.^name, " ", (try [gcd] 5) // $!.^name, " ", (try [lcm] 5) // $!.^name, " ", (try [/] ()).^name, " ", (try [lcm] ()) // $!.message.lines[0]
# rakudo 2026.08: X::NoZeroArgMeaning X::NoZeroArgMeaning X::Multi::Ambiguous X::NoZeroArgMeaning X::NoZeroArgMeaning X::NoZeroArgMeaning X::AdHoc X::AdHoc X::AdHoc X::AdHoc 5 5 Nil Ambiguous call to 'infix:<lcm>(...)'; these signatures all match:
say ([+] 5), " ", ([-] 5), " ", ([/] 5), " ", ([**] 5), " ", ([%] 5), " ", ([+&] 5), " ", ([+<] 5), " ", ([%%] 5), " ", ([min] 5), " ", ([+|] 5), " ", ([-] 5).^name, " ", ([-] "5").raku, " ", ([/] "5").raku, " ", ([+] "5").raku, " ", ([+] 1.5).raku, " ", ([+] 5).^name, " ", ([**] 5).^name, " ", ([+] "5").^name
# rakudo 2026.08: 5 5 5 5 5 5 5 True 5 5 Int 5 5 5 1.5 Int Int Int
say ([==] ()), " ", ([<] ()), " ", ([<] 5), " ", ([+&] ()), " ", ([+|] ()), " ", ([+^] ()), " ", ([!=] ()), " ", ([!=] 5), " ", ([==] 1, 1, 1), " ", ([<] 1, 2, 3), " ", ([<] 1, 3, 2), " ", ([eqv] ()), " ", ([===] ()), " ", ([+&] ()).^name
# rakudo 2026.08: True True True -1 0 0 True True True True False True True Int
say ([max] ()).raku, " ", ([min] ()).raku, " ", ([max] ()).^name, " ", ([min] 1, "a").raku, " ", ([max] "a", 1).raku, " ", ([max] 1, 1.0, 1e0).^name, " ", (1 max 1.0).^name, " ", (1.0 max 1).^name, " ", ((1, 1.0, 1e0).max).^name, " ", ((1, 1.0, 1e0).min).^name, " ", (1 max 1e0).^name, " ", (1e0 max 1).^name, " ", (2 max 1.0).^name, " ", (1.0 max 2).^name
# rakudo 2026.08: -Inf Inf Num 1 "a" Int Rat Int Int Int Num Int Int Int
say ([+] 1..100), " ", ([*] 1..20), " ", ([-] 10, 2, 3), " ", ([**] 2, 3, 2), " ", ([/] 2, 3, 4), " ", ([lcm] 4, 6, 10), " ", ([gcd] 12, 18, 24), " ", ([+] "1", "2"), " ", ([*] ()).^name, " ", ([**] ()).^name, " ", ([+] ()).^name, " ", ([*] ()).raku, " ", ([+] ()).raku, " ", ([+] Nil).raku, " ", (try [+] "a") // $!.^name, " ", ([+] 1, "2", 3e0).^name, " ", ([*] 1, 2, 3.0).^name, " ", ([+] 1..3, 4).raku
# rakudo 2026.08: 5050 2432902008176640000 5 512 0.166667 60 6 3 Int Int Int 1 0 0 X::Str::Numeric Num Rat 5..7
```
(`[+] Nil` also warns "Use of Nil in numeric context".) The `[lcm] ()`
ambiguity is the bug; `S03-operators/equality.t` asserts the
zero- and one-argument `==`/`!=` forms only. rakupp 4.0.1-118: differs — a
reduce over a bare literal (`[+] 5`) or under `try` is a parse error
("expected ) (got '5')"), `[max] 1, 1.0, 1e0` is a Num, `[+] Nil` is Nil,
`[+] "a"` is the string "a", and `[+] 1..3, 4` flattens the range to 10.

### N-17  Bitwise operators                                              D:yes R:yes V:bug
`+&`, `+|`, `+^` and prefix `+^` work on two's-complement Ints of any
size (`-1 +& 0xFF` 255, `+^0` -1, `+^(2**70)` the negative), `+<`/`+>`
shift arithmetically (`-7 +> 1` is -4, `-123 +> 32` and `+> 1000` -1,
`123 +> 1000` 0), a negative count shifts the other way (`5 +< -1` is
2, `5 +> -1` 10), non-Int operands are truncated to Ints first
(`3.7 +& 1`, `1 +< 2.5`, `"3" +& 1`), Inf or NaN as an operand is
`X::Numeric::CannotConvert`, and a count that does not fit a native
int (`2**70`) throws `X::AdHoc`. One Rakudo bug: `1 +< -64` is 1
(the count wraps), where every other negative count works.
```
say -1 +& 0xFF, " ", +^0, " ", +^5, " ", -8 +> 1, " ", -7 +> 1, " ", (1 +< 100).chars, " ", 5 +< -1, " ", 5 +> -1, " ", -123 +> 32, " ", -123 +> 1000, " ", 123 +> 1000, " ", "3" +& 1, " ", 3.7 +& 1, " ", 1.5e0 +| 0, " ", 1 +< 2.5, " ", 6 +^ 3, " ", 6 +| 3, " ", 6 +& 3, " ", -6 +^ 3, " ", -6 +| 3, " ", (2**70) +& (2**70 - 1), " ", (2**70) +> 69, " ", +^(2**70), " ", (try "abc" +& 1) // $!.^name, " ", ([+&] ()), " ", 0 +< 1000, " ", (-1 +< 5), " ", (-1 +> 5), " ", 1 +< 0, " ", 15 +< 3, " ", -17 +> 3, " ", 5 +< 64, " ", (try 1 +< Inf) // $!.^name, " ", (try 1 +< NaN) // $!.^name, " ", 1 +< -64, " ", -1 +< -64, " ", +^-1, " ", 0b1010 +^ 0b0110, " ", ("6" +| "9"), " ", True +& 1, " ", 3 +& -1, " ", (3 +& 1).^name, " ", (3.5 +& 1).^name, " ", (3 +< 1.9), " ", (-3.7 +& 0xFF), " ", (2**64 +| 1) - 2**64, " ", (-(2**64)) +& 0xFFFF, " ", (-(2**64)) +> 63, " ", (-(2**64)) +> 64, " ", (-(2**64)) +> 65, " ", (2**64) +< -64, " ", 5 +> 0, " ", -5 +> 0, " ", (1 +< 63), " ", (1 +< 63).^name, " ", (7 +^ 7), " ", (+^0).^name, " ", (+^ 1.5), " ", (+^ "5"), " ", (-1) +> 100, " ", (-2) +> 100, " ", 1 +> 100, " ", (2**100) +> 100, " ", (-(2**100)) +> 100, " ", (-(2**100) - 1) +> 100, " ", (-1 +& -1), " ", (-1 +| 0), " ", (-1 +^ -1), " ", (-8 +& 7), " ", (5 +& -8), " ", (+^ -8), " ", (2**64 - 1) +^ (2**64), " ", ((2**64 - 1) +& -1), " ", (1.9 +< 1), " ", ("0x10" +| 0), " ", (try 1 +< 1e0) // $!.^name, " ", (try 1 +< "2") // $!.^name, " ", (2 +> 0.5), " ", (2 +< 0.9), " ", (2 +< -0.9), " ", (-1 +> 0.5), " ", (255 +& 0.9)
# rakudo 2026.08: 255 -1 -6 -4 -4 31 2 10 -1 -1 0 1 1 1 4 5 7 2 -7 -5 0 2 -1180591620717411303425 X::Str::Numeric -1 0 -32 -1 1 120 -3 92233720368547758080 X::Numeric::CannotConvert X::Numeric::CannotConvert 1 -1 0 12 15 1 3 Int Int 6 253 1 0 -2 -1 -1 1 5 -5 9223372036854775808 Int 0 Int -2 -6 -1 -1 0 1 -1 -2 -1 -1 0 0 0 7 36893488147419103231 18446744073709551615 2 16 2 4 2 2 2 -1 0
say (try 1 +< 2**70) // $!.^name
# rakudo 2026.08: X::AdHoc
say (try 1 +> 2**70) // $!.^name, " ", (try -1 +> 2**70) // $!.^name
# rakudo 2026.08: X::AdHoc X::AdHoc
```
rakupp 4.0.1-118: differs in one field — `1 +< -64` is 0, the correct
answer, against Rakudo's wrapped 1; that is the recorded bug. A shift count
that does not fit a native int is now `X::AdHoc` naming the exact bit width,
in both directions, and the left shift no longer hangs.

## E. Special values

### N-18  Inf, NaN and the signed zero                                  D:yes R:yes V:quirk
`Inf - Inf`, `Inf * 0`, `Inf / Inf` are NaN; `1/Inf` is `0e0` and
`-1/Inf` `-0e0`; a negative zero keeps its sign through `* -1`, `+`, `-`,
`sqrt`, `sin` and prints as `-0`, is `==` to 0, is `Same` under `cmp`
and `<=>` but not `===` or `eqv`, has `.sign` 0, `.abs` `0e0`, `.Int` 0
and `.Rat` `0.0`, and narrows to 0. `1e0 / -0e0` is a Failure
(`X::Numeric::DivideByZero`), not `-Inf`. NaN is defined and True,
`NaN.Int` fails `X::Numeric::CannotConvert`, `NaN.Rat` is `<0/0>`;
`Inf.Rat` is `<1/0>` and smartmatches `Rat`. Denormals print
(`1e-320`), `5e-324 / 2` is 0. The quirk: `(-0e0) ** 3` and
`(-0e0) ** 1` come out as `+0e0` (see N-12).
```
say Inf - Inf, " ", Inf * 0, " ", Inf / Inf, " ", (1/Inf).raku, " ", (-1/Inf).raku, " ", (0e0 * -1).raku, " ", (-0e0 + 0e0).raku, " ", (-0e0 * -1).raku, " ", (-0e0).Int, " ", (-0e0).Rat.raku, " ", (-0.0).raku, " ", (-0e0).narrow.raku, " ", (0e0 - 0e0).raku, " ", (-0e0 - 0e0).raku, " ", atan2(-0e0, 1).raku, " ", atan2(0e0, -1e0), " ", atan2(-0e0, -1e0), " ", 1e0 / Inf, " ", Inf.narrow.raku, " ", NaN.narrow.raku, " ", NaN + 1, " ", NaN.isNaN, " ", (0/0).isNaN, " ", Inf.isNaN, " ", <1/0>.isNaN, " ", 1.isNaN, " ", 1e0.isNaN, " ", NaN.Bool, " ", Inf == Inf, " ", Inf + 1 === Inf, " ", 100 / Inf, " ", Inf ** 0, " ", 0 ** Inf, " ", NaN + 1i ~~ NaN, " ", (-Inf).abs, " ", Inf.abs.raku, " ", 2e308.raku, " ", 1.7976931348623157e308 * 2, " ", (1e-320).raku, " ", 5e-324 / 2, " ", (-Inf).sign, " ", -Inf², " ", Inf.Rat.raku, " ", (Inf.Rat * 0).raku, " ", (try Inf.Rat.Str) // $!.^name, " ", Inf.Rat ~~ Rat, " ", (1 - (Inf.Rat)).raku, " ", (-0e0).raku, " ", (-0e0 == 0), " ", (-0e0 === 0e0), " ", (0e0 * -1) === -0e0, " ", ((-0e0).Str), " ", (-0e0).abs.raku, " ", (1 / -0e0).raku, " ", (try (1e0 / -0e0)) // $!.^name, " ", (-0e0).Bool, " ", (-0e0).ceiling.raku, " ", (-0e0).floor.raku, " ", (-0e0).round.raku, " ", (-0e0).truncate.raku, " ", (-0e0 + 1).raku, " ", ("-0e0".Num).raku, " ", (-0e0).Num.raku, " ", (-(0e0)).raku, " ", (0e0.abs * -1).raku, " ", (-1e0 * 0).raku, " ", (0 * -1e0).raku, " ", (0e0 / -1).raku, " ", sqrt(-0e0).raku, " ", (-0e0).sqrt.raku, " ", (-0e0) ** 2, " ", (-0e0) ** 3, " ", (-0e0).exp.raku, " ", (-0e0).sin.raku, " ", (-0e0).cos.raku, " ", ((-0e0).Rat).raku, " ", (-0e0).FatRat.raku, " ", (-0e0).Complex.raku, " ", (-0e0).WHICH, " ", 0e0.WHICH, " ", ((-0e0, 0e0).unique.elems), " ", ((-0e0) cmp 0e0), " ", ((-0e0) <=> 0e0), " ", (-0e0 eqv 0e0), " ", (-0e0 =~= 0e0), " ", (-0e0 < 0e0)
# rakudo 2026.08: NaN NaN NaN 0e0 -0e0 -0e0 0e0 0e0 0 0.0 0.0 0 0e0 -0e0 -0e0 3.141592653589793 -3.141592653589793 0 Inf NaN NaN True True False False False False True True True 0 1 0 True Inf Inf Inf Inf 1e-320 0 -1 -Inf <1/0> <0/0> X::Numeric::DivideByZero True <-1/0> -0e0 True False True -0 0e0 Failure.new(exception => X::Numeric::DivideByZero.new(using => "/", details => Any, numerator => 1e0), backtrace => Backtrace.new) X::Numeric::DivideByZero False 0 0 0 0 1e0 -0e0 -0e0 -0e0 -0e0 -0e0 -0e0 -0e0 -0e0 -0e0 0 0 1e0 -0e0 1e0 0.0 FatRat.new(0, 1) <-0+0i> Num|-0 Num|0 2 Same Same False True False
```
rakupp 4.0.1-118: differs — `1e-320` prints as `9.99988867182683e-321`, `1 /
-0e0` and `1e0 / -0e0` are `-Inf` (the 6.e rule, N-11), and the `.raku` of
the resulting Failure renders the carrier hash rather than
`Failure.new(...)`, which shifts the rest of the line. `(-0e0) ** 3` is
`-0e0`, IEEE and deliberate.

### N-19  narrow                                                          D:partial R:yes V:bug
`narrow` returns an Int for a Rat with denominator 1 and for a Num whose
`.Int` is approximately equal (`=~=`, relative 1e-15) to it, else the
value unchanged; Inf and NaN stay Nums, `<1/0>` a Rat. Because the test
is approximate, Rakudo narrows values that are NOT integers:
`1e-300.narrow` and `(1/(2**64)).narrow` are 0, `(1e15 + 0.5).narrow`
is 1000000000000000, `4.000000000000001e0.narrow` is 4, while
`(1e0 + 1e-15).narrow` stays a Num. Do not imitate that.
```
say 1e-300.narrow.raku, " ", (1/(2**64)).narrow.raku, " ", (1e15+0.5).narrow.raku, " ", 4.000000000000001e0.narrow.raku, " ", 1e-16.narrow.raku, " ", 1e-15.narrow.raku, " ", 1e-14.narrow.raku, " ", 0.5e0.narrow.raku, " ", (1e16 + 2).narrow.raku, " ", (1e16 + 2).narrow.^name, " ", 12345678.9e0.narrow.raku, " ", (2**53).Num.narrow.raku, " ", 1e300.narrow.^name, " ", 1e-300.narrow.^name, " ", (1e-300 * 2).narrow.raku, " ", 1e-30.narrow.raku, " ", 1e-10.narrow.raku, " ", 1e-5.narrow.raku, " ", 0.1e0.narrow.raku, " ", (1e10 + 0.5).narrow.raku, " ", (1e14 + 0.5).narrow.raku, " ", (1e15 + 0.5).narrow.raku, " ", 1e15.narrow.raku, " ", 4.5.narrow.^name, " ", (9/3).narrow.raku, " ", <4/2>.narrow.raku, " ", 2.0.narrow.raku, " ", (1e15 + 0.25).narrow.raku, " ", (1e15 + 0.75).narrow.raku, " ", (1e14 + 0.9).narrow.raku, " ", 0.9999999999999999e0.narrow.raku, " ", 1.0000000000000002e0.narrow.raku, " ", (-1e-300).narrow.raku, " ", 5e-324.narrow.raku, " ", (1e-15 * 0.5).narrow.raku, " ", 1e-16.narrow.^name, " ", 42.narrow.WHICH, " ", 42.0.narrow.WHICH, " ", 42e0.narrow.WHICH, " ", (42+0i).narrow.WHICH, " ", 42.5e0.narrow.WHICH, " ", (1/(2**64)).Num.narrow.raku, " ", (1e0/3).narrow.^name, " ", (2e0/3).narrow.^name, " ", 1.5e0.narrow.^name, " ", (1e0 + 1e-15).narrow.^name, " ", (1e0 + 2e-15).narrow.^name, " ", (100e0 + 1e-14).narrow.^name, " ", (100e0 + 1e-13).narrow.^name, " ", (100e0 + 1e-12).narrow.^name
# rakudo 2026.08: 0 0 1000000000000000 4 0 1e-15 1e-14 0.5e0 10000000000000002 Int 12345678.9e0 9007199254740992 Int Int 0 0 1e-10 1e-05 0.1e0 10000000000.5e0 100000000000000.5e0 1000000000000000 1000000000000000 Rat 3 2 2 1000000000000000 1000000000000000 100000000000000.9e0 0.9999999999999999e0 1 0 0 0 Int Int|42 Int|42 Int|42 Int|42 Num|42.5 0 Num Num Num Num Num Int Int Num
say (4/2).narrow.raku, " ", (5/2).narrow.^name, " ", 4e0.narrow.raku, " ", 4.5e0.narrow.^name, " ", 1e20.narrow.raku, " ", (4+0i).narrow.raku, " ", FatRat.new(4,2).narrow.raku, " ", 1e-300.narrow.^name, " ", ((.1e0+.2e0)*10).narrow.raku, " ", 2.2e0.narrow.raku, " ", (0+2i).narrow.raku, " ", (0+0i).narrow.raku, " ", 5.narrow.raku, " ", (2**70).Num.narrow.raku, " ", Inf.narrow.raku, " ", (-0e0).narrow.raku, " ", <1/0>.narrow.raku, " ", 1e16.narrow.raku, " ", (1e15+0.5).narrow.raku, " ", 1.0.narrow.^name, " ", (3/1).narrow.^name, " ", FatRat.new(1,3).narrow.^name, " ", 4e0.narrow.WHAT.gist, " ", (4e0+1e-20).narrow.raku, " ", 4.0000000000000001e0.narrow.raku, " ", 4.000000000000001e0.narrow.raku, " ", (1e0 + 1e-15).narrow.raku, " ", (1e0 + 1e-16).narrow.raku, " ", NaN.narrow.^name, " ", (2**53+1).Num.narrow.raku, " ", 2.5e0.narrow.WHAT.raku, " ", <0/0>.narrow.raku, " ", (1/(2**64)).narrow.^name, " ", 1e300.narrow.chars, " ", (1e300.narrow ~~ Int), " ", 0e0.narrow.raku, " ", (-4e0).narrow.raku, " ", 4.5.narrow.raku, " ", (9/3).narrow.raku, " ", FatRat.new(9, 3).narrow.^name, " ", (1e0).narrow.^name, " ", Rat.new(4, 2).narrow.WHICH
# rakudo 2026.08: 2 Rat 4 Num 100000000000000000000 4 2 Int 3 2.2e0 <0+2i> 0 5 1180591620717411303424 Inf 0 <1/0> 10000000000000000 1000000000000000 Int Int FatRat (Int) 4 4 4 1.000000000000001e0 1 Num 9007199254740992 Num <0/0> Int 301 True 0 -4 4.5 3 Int Int Int|2
```
rakupp 4.0.1-118: differs in two fields, both deliberate — `1e-300.narrow`
and `(1/(2**64)).narrow` keep their values where Rakudo answers 0. Rakudo's
approximate test falls back to an ABSOLUTE comparison when a side is zero,
which destroys any value under the tolerance; nothing in Roast asks for
that. The rest of the rule IS implemented, so `((.1e0+.2e0)*10).narrow` is
the Int 3 and `1e20`, `(2**70).Num`, `1e300` narrow exactly.

## F. Methods

### N-20  base and base-repeating                                        D:yes R:yes V:spec
`Int.base(2..36)` uses capitals, keeps a leading minus, and with a
digits count appends a point and that many zeros; a base outside 2..36
or a negative digits count is a Failure `X::OutOfRange` (`.what` "base
argument to base" or "digits argument to base", `.range` "2..36" in
both cases); a Str base is coerced, an unparsable one is
`X::Str::Numeric`, `"camel"` and `"beer"` render the binary in
emoji, one glyph per digit (a one-hump camel is 0 and a two-hump one 1).
A Rational renders 6 digits by default (more for a large
denominator), `*` means all digits (endless for 1/3), an explicit
count pads with zeros unless `:no-trailing-zeroes`, 0 digits rounds to
an Int, and the last digit is rounded. A Num renders about 8 digits in
base 10 (`1e-10.base(10)` is `0.00000000`), Inf and NaN fail
`X::Numeric::CannotConvert`. `base-repeating` exists on Rationals only
and returns the non-repeating part and the cycle.
```
say 255.base(16), " ", 255.base(2), " ", 255.base(36), " ", (-255).base(16), " ", 0.base(2), " ", (try 255.base(37)) // $!.^name, " ", (try 255.base(1)) // $!.^name, " ", 255.base(16, 2), " ", 255.base("16"), " ", (try 255.base(16, -1)) // $!.^name, " ", 255.base(16, *), " ", 255.base(16, 0), " ", 255.base("camel"), " ", 5.base("beer"), " ", (try 255.base("foo")) // $!.^name, " ", (1/3).base(10), " ", (1/3).base(2), " ", pi.base(10, 3), " ", (1/128).base(10, *), " ", (1/3).base(10, 2), " ", (1/2).base(10, 3), " ", (1/2).base(10, 3, :no-trailing-zeroes), " ", (1/3).base(10, 0), " ", 2.5.base(10, 0), " ", 0.1.base(2), " ", (-1/3).base(10), " ", 3.25.base(16), " ", (1/10000000000).base(3), " ", (2/3).base(10, 40), " ", (1/100001).base(10), " ", pi.base(10), " ", pi.base(16), " ", 1e0.base(2), " ", 0.5e0.base(2), " ", (try Inf.base(2)) // $!.^name, " ", (try NaN.base(16)) // $!.^name, " ", 16.99999e0.base(16, 3), " ", 16.999e0.base(16, 3), " ", (6.02214129e23 / 10 ** 23).base(3, 0), " ", 255e0.base(16, *), " ", 1e10.base(10), " ", 1e-10.base(10), " ", 1e-10.base(10, 12), " ", (19/3).base-repeating.raku, " ", (5/2).base-repeating(10).raku, " ", (1/7).base-repeating.raku, " ", 4.0.base-repeating.raku, " ", (try 4.base-repeating) // $!.^name, " ", (-19/3).base-repeating.raku, " ", (1/3).base-repeating(2).raku, " ", 255.base(16).parse-base(16), " ", :16<FF>, " ", 35.base(36, 1), " ", 121.base(11, 3), " ", (try 255.base(16, 2.5)) // $!.^name, " ", (try 1.5.base(10, -1)) // $!.^name, " ", (try 1.5.base(1)) // $!.^name, " ", (3/2).base(10, 1), " ", (49/999).base(10, 1), " ", (98/99).base(10, 1), " ", (98/99).base(10, 0), " ", (.01).base(10, 0), " ", 16.0.base(16, 3), " ", 16.5.base(16, 3), " ", (3/1024).base(16, *), " ", 255.base(10).^name, " ", (-3.5).base(16), " ", 10.5.base(2), " ", 1e0.base(16, 3), " ", 0.1e0.base(10), " ", 0.1e0.base(10, 20), " ", 1e0.base(10, 0), " ", 0.5.base(10, *), " ", 1e300.base(10).chars, " ", (2**70).base(16), " ", (1/3).FatRat.base(10), " ", FatRat.new(1, 10**30).base(10), " ", FatRat.new(1, 3).base(10, 3)
# rakudo 2026.08: FF 11111111 73 -FF 0 X::OutOfRange X::OutOfRange FF.00 FF X::OutOfRange FF FF 🐫🐫🐫🐫🐫🐫🐫🐫 🍻🍺🍻 X::Str::Numeric 0.333333 0.010101 3.142 0.0078125 0.33 0.500 0.5 0 3 0.00011 -0.333333 3.4 0.000000000000000000001 0.6666666666666666666666666666666666666667 0.00001 3.14159265 3.243F6B 1 0.1 X::Numeric::CannotConvert X::Numeric::CannotConvert 11.000 10.FFC 20 FF 10000000000 0.00000000 0.000000000100 ("6.", "3") ("2.5", "") ("0.", "142857") ("4", "") X::Method::NotFound ("-6.", "3") ("0.", "01") 255 255 Z.0 100.000 X::Multi::NoMatch X::OutOfRange X::OutOfRange 1.5 0.0 1.0 1 0 10.000 10.800 0.00C Str -3.8 1010.1 1.000 0.1 0.10000000000000000000 1 0.5 301 400000000000000000 0.333333 0.000000000000000000000000000001 0.333
```
rakupp 4.0.1-118: differs in eight fields — `:no-trailing-zeroes` is
ignored, `.base-repeating` of a whole number puts the point in the
non-repeating part (`("4.", "")` for `("4", "")`), a Rat digits count is
accepted where Rakudo refuses it, and a Num's `%.20f`-scale expansion
differs in the last digits. Everything else — the exact Rational expansion
at any length, the three default digit counts, the X::OutOfRange refusals,
Inf and NaN, and the camel and beer bases — matches.

### N-21  is-prime, expmod, lsb, msb                                     D:yes R:yes V:spec
`is-prime` is False for 0, 1 and negatives, True for a Num or Rat that
is a whole prime (`2e0`, `2.0`), False for `2.5e0`, Inf, NaN, throws
`X::Numeric::Real` for a Complex with an imaginary part, coerces a
Str. `expmod` accepts a negative exponent when a modular inverse
exists and dies `X::AdHoc` otherwise (`42.expmod(-1, 7)`,
`expmod(4, 2, 0)`), coerces Str arguments, and `3.expmod(1, 1)` is 0.
`lsb`/`msb` are Nil for 0, count from the right, and for a negative
number use the two's-complement magnitude: `(-1).msb` 0, `(-2).msb` 1,
`(-256).msb` 8, `(-255).msb` 8, `(-8).lsb` 3, `((-2)**64).msb` 64.
```
say 2.is-prime, " ", 1.is-prime, " ", 0.is-prime, " ", (-2).is-prime, " ", 2e0.is-prime, " ", 2.5e0.is-prime, " ", Inf.is-prime, " ", NaN.is-prime, " ", 2.0.is-prime, " ", 2.5.is-prime, " ", "7".is-prime, " ", <7+0i>.is-prime, " ", (try <7+1i>.is-prime) // $!.^name, " ", (2**61-1).is-prime, " ", 170141183460469231731687303715884105727.is-prime, " ", expmod(4, 2, 5), " ", 7.expmod(2, 5), " ", 7.expmod(-2, 5), " ", expmod("4", "2", "5"), " ", (try expmod(4, 2, 0)) // $!.^name, " ", 3.expmod(-4, 4), " ", (-3).expmod(-4, 4), " ", (try 42.expmod(-1, 7)) // $!.^name, " ", 0.lsb.raku, " ", 0.msb.raku, " ", 12.lsb, " ", 12.msb, " ", (-1).msb, " ", (-2).msb, " ", (-8).lsb, " ", (-8).msb, " ", (2**100).msb, " ", (2**100).lsb, " ", 1.msb, " ", 255.msb, " ", 256.msb, " ", (-256).msb, " ", (-255).msb, " ", lsb(6), " ", msb(6), " ", 120.polymod(10).raku, " ", 120.polymod(10, 10).raku, " ", 120.polymod(10 xx *).raku, " ", 120.polymod(lazy 1, 10, 10², 10³, 10⁴).raku, " ", 120.polymod(1, 10, 10², 10³, 10⁴).raku, " ", 5.polymod().raku, " ", (try (-1).polymod(10).raku) // $!.^name, " ", (try 120.polymod(0).eager.raku) // $!.^name, " ", (try 120.polymod(1/3).raku) // $!.^name, " ", (2/3).polymod(1/3).raku, " ", 5.Rat.polymod(.3, .2).raku, " ", 10e0.polymod(1.5).raku, " ", 100.polymod(1 xx *).raku, " ", 100.polymod(10, 1 xx *).raku, " ", 12.polymod(14 xx *).raku, " ", 1234567.polymod(256 xx 7).raku, " ", 42.polymod(lazy 2, 3).raku, " ", 42.polymod(2, 3).raku, " ", (2**70).polymod(2**32, 2**32).raku, " ", 1.5.polymod(1).raku, " ", (-1.5).polymod(1).^name, " ", (try 10.polymod(0.5, 0).eager.raku) // $!.^name, " ", (try 10.polymod(3, 0).eager.raku) // $!.^name, " ", (try 10.polymod(3, 0, 5).eager.raku) // $!.^name, " ", 0.polymod(10).raku, " ", 7.polymod(10, 10, 10).raku, " ", (2**64).is-prime, " ", 4.is-prime, " ", 97.is-prime, " ", is-prime("97"), " ", is-prime(97.0), " ", (try is-prime("abc")) // $!.^name, " ", 6.expmod(2, 5), " ", (2**100).expmod(3, 7), " ", 3.expmod(0, 7), " ", 3.expmod(1, 1), " ", (try 3.expmod(-1, 6)) // $!.^name, " ", (-7).lsb, " ", (-7).msb, " ", 7.msb, " ", (2**64).msb, " ", (2**64-1).msb, " ", ((-2)**64).msb, " ", (-(2**64)).msb, " ", 120.polymod(60, 60).raku, " ", 3661.polymod(60, 60).raku, " ", 5.polymod(1, 1).raku, " ", 10.polymod(2.5).raku, " ", 10.polymod(2.5, 2).raku, " ", (try 10.polymod(Inf).eager.raku) // $!.^name, " ", 10.polymod(0.5).raku, " ", 10.polymod(2, 0.5, 3).raku, " ", 10.polymod(1.5).raku, " ", 10.5.polymod(0.5).raku, " ", 10.polymod(-3).raku, " ", (2/3).polymod(1/3).^name, " ", 10e0.polymod(3).raku, " ", 10.polymod(3e0).raku
# rakudo 2026.08: True False False False True False False False True False True True X::Numeric::Real True True 1 4 4 1 X::AdHoc 1 1 X::AdHoc Nil Nil 2 3 0 1 3 3 100 100 0 7 8 8 8 1 2 (0, 12).Seq (0, 2, 1).Seq (0, 2, 1).Seq (120,).Seq (120,).Seq (5,).Seq X::OutOfRange X::Numeric::DivideByZero (120,).Seq (0, 2.0).Seq (0.2, 0, 80.0).Seq (1e0, 6e0).Seq (100,).Seq (0, 10).Seq (12,).Seq (135, 214, 18, 0, 0, 0, 0, 0).Seq (0, 0, 7).Seq (0, 0, 7).Seq (0, 0, 64).Seq (1.5,).Seq Failure (10,) X::Numeric::DivideByZero X::Numeric::DivideByZero (0, 0).Seq (7, 0, 0, 0).Seq False False True True True X::Str::Numeric 1 1 1 0 X::AdHoc 0 3 2 64 63 64 64 (0, 2, 0).Seq (1, 1, 1).Seq (5,).Seq (-2.5, 5).Seq (-2.5, 1, 2).Seq X::Numeric::DivideByZero (10,).Seq (0, 5).Seq (-5.0, 10).Seq (0, 21.0).Seq (10,).Seq Seq (1e0, 3e0).Seq (1e0, 3).Seq
```
(The `polymod` fields belong to N-22.) rakupp 4.0.1-118: differs in two
fields — `2.5e0.is-prime` and `2.5.is-prime` are True, and `expmod(4, 2, 0)`
is 0 rather than `X::AdHoc`. `msb` of a negative is now the two's-complement
length, and polymod's own fields belong to N-22.

### N-22  polymod                                                       D:partial R:yes V:bug
`polymod` returns a lazy Seq of remainders, one more than the number of
divisors; a lazy divisor list stops when the remaining value is 0; a
negative invocant is a Failure `X::OutOfRange` (`.what` "invocant to
polymod", `.range` "0..^Inf"); a zero divisor throws
`X::Numeric::DivideByZero` (`.using` "polymod") when the Seq is
reified. Two rules the docs miss: a divisor of 1 or less STOPS the
sequence (`120.polymod(1, 10, 100)` is `(120,)`, `100.polymod(1 xx *)`
`(100,)`, `10.polymod(0.5)` `(10,)`, `10.polymod(-3)` `(10,)`), which
Roast asserts (rakudo/rakudo#4523); and an Int invocant with a
non-Int divisor uses `mod`/`div` (N-11), so `10.polymod(2.5)` is
`(-2.5, 5)` and `10.polymod(1.5)` `(-5.0, 10)`, which is the bug. A
Rat or Num invocant uses `%` and is right (`(2/3).polymod(1/3)` is
`(0, 2.0)`, `10e0.polymod(1.5)` `(1e0, 6e0)`).
Probe: the `polymod` fields of N-21.
rakupp 4.0.1-118: matches on every rule — the stop-at-one divisor, the
`X::OutOfRange` Failure for a negative invocant, and the
`X::Numeric::DivideByZero` for a zero one. Two shapes still differ: the
result is a List rather than a Seq, and an Int with a non-Int divisor uses
`%` (`10.polymod(2.5)` is `(0, 5)`), which is the right answer and the
recorded Rakudo bug.

### N-23  Capture, Order, bits                                            D:partial R:partial V:quirk
`Int.Capture` and `Num.Capture` throw `X::Cannot::Capture` (`.what`
the number) while a Rational is captured as its two attributes
(`(1/2).Capture` is `\(:denominator(2), :numerator(1))`). `.Order` is
`Less`, `Same` or `More` by the sign of `.Int`, so `2.5.Order` is More,
`(-0.5).Order` Same, `Inf.Order` and `NaN.Order` fail
`X::Numeric::CannotConvert`, a Str that does not parse fails
`X::Str::Numeric`, and an Int beyond 63 bits throws `X::AdHoc`.
`.bits` is a type-object method: `Int.bits` is `Inf`, `int` 64, `int8`
8, `uint16` 16, `Num` 64, `num32` 32, `atomicint` 64; on an instance it
is `X::Parameter::InvalidConcreteness`, on Rat or Str
`X::Method::NotFound`.
```
say (try 42.Capture) // $!.^name, " ", (try 4.2.Capture) // $!.^name, " ", (try 4e0.Capture) // $!.^name, " ", 5.Order, " ", 0.Order, " ", (-1).Order, " ", 2.5.Order, " ", (-0.5).Order, " ", (try "abc".Order) // $!.^name, " ", ("abc".Order).^name, " ", (try Int.bits) // $!.^name, " ", (try Num.bits) // $!.^name, " ", (try 42.bits) // $!.^name, " ", UInt.^name, " ", UInt ~~ Int, " ", 5 ~~ UInt, " ", Int ~~ UInt, " ", do { my UInt $u = 5; $u.^name }, " ", (try 42.Capture.^name) // $!.^name, " ", Inf.Order.^name, " ", (try Inf.Order.raku) // $!.^name, " ", (try NaN.Order.raku) // $!.^name, " ", (try (2**70).Order) // $!.^name, " ", (try (2**63).Order) // $!.^name, " ", (try (2**63 - 1).Order) // $!.^name, " ", (try (-2**63).Order) // $!.^name, " ", 0.5.Order, " ", (-0.5e0).Order, " ", (try 42.Order.^name) // $!.^name, " ", (try (1/2).Capture) // $!.^name, " ", (try Int.Capture) // $!.^name, " ", (try 4.2.Capture.raku) // $!.^name
# rakudo 2026.08: X::Cannot::Capture \(:denominator(5), :numerator(21)) X::Cannot::Capture More Same Less More Same X::Str::Numeric Failure Inf 64 X::Parameter::InvalidConcreteness UInt True True True Int X::Cannot::Capture Failure X::Numeric::CannotConvert X::Numeric::CannotConvert X::AdHoc X::AdHoc More Less Same Same Order \(:denominator(2), :numerator(1)) X::Cannot::Capture \(:denominator(5), :numerator(21))
say Inf.Order.^name, " ", (try Inf.Order.raku) // $!.^name, " ", (try NaN.Order.raku) // $!.^name, " ", (try (2**70).Order) // $!.^name, " ", (try (2**63).Order) // $!.^name, " ", (try (2**63 - 1).Order) // $!.^name, " ", (try (-2**63).Order) // $!.^name, " ", 0.5.Order, " ", (-0.5e0).Order, " ", int64.bits, " ", uint.bits, " ", byte.bits, " ", atomicint.bits, " ", num64.bits, " ", (try Rat.bits) // $!.^name, " ", (try Str.bits) // $!.^name, " ", (try UInt.bits) // $!.^name, " ", (try 42.Order.^name) // $!.^name, " ", (try (1/2).Capture) // $!.^name, " ", (try Int.Capture) // $!.^name, " ", (try Int.Capture.raku) // $!.^name, " ", Int.bits, " ", int.bits, " ", int8.bits, " ", uint16.bits, " ", Num.bits, " ", num32.bits, " ", num.bits, " ", (try 42.bits) // $!.^name, " ", 5.Order, " ", 0.Order, " ", (-1).Order, " ", 2.5.Order, " ", (-0.5).Order, " ", (try "abc".Order) // $!.^name, " ", ("abc".Order).^name, " ", (try 42.Capture) // $!.^name, " ", (try 4.2.Capture) // $!.^name, " ", (try 4e0.Capture) // $!.^name
# rakudo 2026.08: Failure X::Numeric::CannotConvert X::Numeric::CannotConvert X::AdHoc X::AdHoc More Less Same Same 64 64 8 64 64 X::Method::NotFound X::Method::NotFound Inf Order \(:denominator(2), :numerator(1)) X::Cannot::Capture X::Cannot::Capture Inf 64 8 16 64 32 64 X::Parameter::InvalidConcreteness More Same Less More Same X::Str::Numeric Failure X::Cannot::Capture \(:denominator(5), :numerator(21)) X::Cannot::Capture
```
The Rational capture is the quirk. rakupp 4.0.1-118: differs — `.bits` is
missing everywhere, a Rat is not capturable, `UInt ~~ Int` is False,
`(2**70).Order` and `(2**63).Order` are More rather than `X::AdHoc`, and
`Int.Capture` is `\()`. A UInt CONTAINER now enforces the subset.

### N-24  fmt                                                            D:yes R:yes V:spec
`.fmt` is `sprintf` with the number as the one argument: `%d` truncates
a Rat or Num (`3.7` prints 3, `1e0` 1), `%x`/`%b`/`%o` and `%e`/`%g`
work on any size (`(2**64).fmt("%x")` is 32 hex digits, `%#x` prefixes),
`%.0f` rounds halves up (`2.5` prints 3), `%c` is the character, and
`%u` of a negative, `%d` of Inf, a Str, `%%` alone with an argument or
two directives for one argument are all `X::AdHoc`. Rakudo formats
`%f` through a double: `(2**70).fmt("%.2f")` loses the low digits and
`0.1e0.fmt("%.20f")` pads with zeros rather than showing the binary
expansion.
```
say 42.chr, " ", 65.chr, " ", 42.unival.raku, " ", 190.unival, " ", 12345.comb.raku, " ", 12345.flip, " ", 12345.chars, " ", 12345.substr(1, 2), " ", 42.fmt("%05d"), " ", 42.fmt("%x"), " ", 42.fmt("%b"), " ", 42.fmt("%o"), " ", 3.14159.fmt("%.2f"), " ", 42.fmt("%s"), " ", 42.fmt, " ", 255.fmt("%X"), " ", 42.fmt("%e"), " ", 42.Str.Int, " ", 1234.5.Str, " ", 42.ords.raku, " ", (2**64).fmt("%d"), " ", (try (2**64).fmt("%x")) // $!.^name, " ", 42.uc, " ", 42.contains("2"), " ", 42.starts-with("4"), " ", 42.index("2"), " ", 42.trans("4" => "9"), " ", 42.split("").raku, " ", 42.succ.^name, " ", 42.WHAT.raku, " ", 42.gist, " ", (1/3).fmt("%.3f"), " ", (try 1e0.fmt("%d")) // $!.^name, " ", (try 1.9.fmt("%d")) // $!.^name, " ", (try "abc".fmt("%d")) // $!.^name, " ", 42.fmt("%5.1f"), " ", (2**70).fmt("%e"), " ", (try Inf.fmt("%d")) // $!.^name, " ", NaN.fmt("%f"), " ", (-0e0).fmt("%f"), " ", (-0e0).fmt("%g"), " ", (try 3.7.fmt("%d")) // $!.^name, " ", (try (-3.7).fmt("%d")) // $!.^name, " ", 42.fmt("%c"), " ", 1.5.fmt("%.0f"), " ", 2.5.fmt("%.0f"), " ", 0.125.fmt("%.2f"), " ", 42.fmt("%+d"), " ", 42.fmt("%08.3f"), " ", (1/3).fmt("%.20f"), " ", 1e100.fmt("%.0f").chars, " ", 42.fmt("%i"), " ", 42.fmt("%u"), " ", (try (-42).fmt("%u")) // $!.^name, " ", 42.fmt("%5s|"), " ", 42.fmt("%-5s|"), " ", 42.fmt("%.1s"), " ", 1e0.fmt("%s"), " ", 1e0.fmt("%f"), " ", (1/3).fmt("%s"), " ", (1/3).fmt("%e"), " ", (try 2.5.fmt("%d")) // $!.^name, " ", 42.fmt("%.2e"), " ", 1234567.fmt("%.3g"), " ", 0.000012345.fmt("%g"), " ", (try 42.fmt("%%")) // $!.^name, " ", (try 42.fmt("%d %d")) // $!.^name, " ", (try 42.fmt("%3d|%-3d|")) // $!.^name, " ", Inf.fmt("%f"), " ", (-Inf).fmt("%e"), " ", NaN.fmt("%g"), " ", (2**64).fmt("%b").chars, " ", (2**64).fmt("%o"), " ", 255.fmt("%#x"), " ", 255.fmt("%#o"), " ", 255.fmt("%#b"), " ", (1/3).fmt("%.16f"), " ", 0.1e0.fmt("%.20f"), " ", (2**70).fmt("%.2f"), " ", (2**70).fmt("%s"), " ", 42.unival.^name, " ", "¾".ord.unival.raku, " ", 0x2160.unival.raku, " ", (2**64).fmt("%x").chars, " ", (-42).fmt("%x"), " ", (-42).fmt("%b"), " ", (-255).fmt("%X"), " ", 42.fmt("%d%%")
# rakudo 2026.08: * A NaN 0.75 ("1", "2", "3", "4", "5").Seq 54321 5 23 00042 2a 101010 52 3.14 42 42 FF 4.200000e+01 42 1234.5 (52, 50).Seq 18446744073709551616 10000000000000000 42 True True 1 92 ("", "4", "2", "").Seq Int Int 42 0.333 1 1 X::AdHoc  42.0 1.180592e+21 X::AdHoc NaN -0.000000 -0 3 -3 * 2 3 0.13 +42 0042.000 0.33333333333333330000 101 42 42 X::AdHoc    42| 42   | 4 1 1.000000 0.333333 3.333333e-01 2 4.20e+01 1.23e+06 1.2345e-05 X::AdHoc X::AdHoc X::AdHoc Inf -Inf NaN 65 2000000000000000000000 0xff 0377 0b11111111 0.3333333333333333 0.10000000000000000000 1180591620717411300000.00 1180591620717411303424 Num 0.75 1 17 -2a -101010 -FF 42%
```
(Rakudo also prints "negative value '-42' for %u in sprintf" on
standard error.) rakupp 4.0.1-118: differs — `"abc".fmt("%d")`,
`Inf.fmt("%d")`, `(-42).fmt("%u")`, `%%`, `%d %d` and `%3d|%-3d|` are
accepted where Rakudo refuses them, `%.0f` and `%.2f` round to even, `%.20f`
shows the binary expansion, `1e100.fmt("%.0f")` has 63 characters, and
`(2**70).fmt("%.2f")` is exact — the last three deliberately, being what the
number actually is.

### N-25  log, exp, sqrt, the trigonometric surface                     D:yes R:yes V:spec
Every one of these goes through `Num`: `log(0)` is `-Inf`, `log(-1)`
NaN, `log(x, base)` divides two logs (`log(8, 2)` is
2.9999999999999996, `1024.log2` 10, `100.log10` 2), `log(1, 1)` fails
`X::Numeric::DivideByZero`, `exp(x, base)` is `base ** x` and so keeps
an Int base exact (`exp(2, 10)` is the Int 100, `5.exp(2)` 32,
`2.exp(-1)` 1 — the INVOCANT is the exponent); `sqrt` of a negative Real is
NaN in 6.d, of `-0e0`
`-0e0`; `atan2($y)` defaults `$x` to 1; `cosec(0)`, `cotan(0)` are Inf,
`atanh(1)` Inf and `atanh(-1)` -Inf, `acosh(0.5)` and `asin(2)` NaN;
`e`, `pi`, `tau`, `π`, `τ` and `𝑒` are Nums; `roots(n)` is a list of n
Complex values.
```
say log(0), " ", log(-1).raku, " ", log(8, 2), " ", (try log(1, 1).raku) // $!.^name, " ", exp(1), " ", exp(2, 10), " ", exp(2, 10).^name, " ", 10.exp(2), " ", 2.log2, " ", 1024.log2, " ", 100.log10, " ", 1000.log10, " ", log10(0), " ", log2(NaN).raku, " ", e, " ", pi, " ", tau, " ", π == pi, " ", τ == 2*π, " ", pi.^name, " ", 0.sin.raku, " ", 0.sin.^name, " ", 1.sin.^name, " ", (1/2).cos.^name, " ", sin(pi), " ", cos(pi), " ", tan(pi/2), " ", asin(2).raku, " ", acosh(0.5).raku, " ", atanh(1), " ", atanh(-1), " ", atanh(1e0), " ", cosec(0), " ", cotan(0), " ", sec(0), " ", asinh(Inf), " ", sinh(1000), " ", 1.atan2(1), " ", atan2(1), " ", atan2(1, 1), " ", atan2(0, -1), " ", 1.atan2("1"), " ", (try atan2("a", 1)) // $!.^name, " ", 1.cis.raku, " ", 2.unpolar(0).raku, " ", "3".sqrt, " ", "-2".sign, " ", "8".log(2), " ", "3.7".floor, " ", (try "abc".floor) // $!.^name, " ", log(Inf), " ", log(-Inf).raku, " ", exp(-Inf), " ", exp(Inf), " ", log(0.5).^name, " ", log(2, 2), " ", 8.log(2), " ", 8.log(2).^name, " ", log("42", "23"), " ", 2.exp("3"), " ", (-1).sqrt.raku, " ", 2.roots(2).raku, " ", 4.log(2) == 2, " ", 1e0.log.raku, " ", (1/2).exp.^name, " ", 0.exp.raku, " ", 5.exp(2).raku, " ", 5.Rat.exp(2).raku, " ", 2.exp(2.5).raku, " ", (try log(-1, 2).raku) // $!.^name, " ", (try 0.log(0).raku) // $!.^name, " ", (log 1), " ", (log 1).raku, " ", log(2).^name, " ", 1000.log10.raku, " ", 8.log2.raku, " ", 1024.log(2).raku, " ", (1/8).log(2).raku, " ", (0.5e0).log2.raku, " ", 16.log(4).raku, " ", 27.log(3).raku, " ", 1e0.exp.raku, " ", 2.exp.raku, " ", (0.exp).^name, " ", Inf.exp(0.5).raku, " ", 2.sqrt.raku, " ", 2.Rat.sqrt.^name, " ", 0.sqrt.raku, " ", (-0.0).sqrt.raku, " ", sqrt(2**70).raku, " ", sqrt(4e0).raku, " ", sqrt(4.0).raku, " ", sqrt(NaN).raku, " ", sqrt(Inf).raku, " ", sqrt(-Inf).raku, " ", (try EVAL '𝑒 == e') // $!.^name, " ", 2.log(2).raku, " ", 2.log(2e0).raku, " ", 2.log(2.0).raku, " ", 1.log(10).raku, " ", 0.log10, " ", (-1).log10.raku, " ", 100.log(10).raku, " ", 8.log(2) == 3, " ", 1000.log10 == 3, " ", 2.exp(0).raku, " ", 0.exp(0).raku, " ", 0.exp(2).raku, " ", (-1).exp(2).raku, " ", 0.5.exp(4).raku, " ", 2.exp(-1).raku, " ", 4.sqrt.^name, " ", 4.0.sqrt.raku, " ", 2.exp(2).^name, " ", (2.exp(2)).raku, " ", (3.exp(2)).raku, " ", (2.exp(0.5)).raku, " ", (4.exp(1/2)).raku, " ", 1.exp.^name, " ", 1000.log10.^name, " ", 1000.log(10).raku, " ", log(1000, 10).raku, " ", 1000.log10.floor, " ", 999.log10.floor, " ", (10**15).log10.raku, " ", (10**16).log10.raku, " ", 2.log2.^name, " ", 8.log2 == 3, " ", (2**64).log2.raku, " ", (2**1000).log2.raku, " ", (2**1000).log.raku, " ", (10**400).log10.raku, " ", (10**400).log.raku, " ", (2**1024).log2.raku
# rakudo 2026.08: -Inf NaN 3 X::Numeric::DivideByZero 2.718281828459045 100 Int 1024 1 10 2 2.9999999999999996 -Inf NaN 2.718281828459045 3.141592653589793 6.283185307179586 True True Num 0e0 Num Num Num 1.2246467991473532e-16 -1 1.633123935319537e+16 NaN NaN Inf -Inf Inf Inf Inf 1 Inf Inf 0.7853981633974483 0.7853981633974483 0.7853981633974483 3.141592653589793 0.7853981633974483 X::Str::Numeric <0.5403023058681398+0.8414709848078965i> <2+0i> 1.7320508075688772 -1 3 3 X::Str::Numeric Inf NaN 0 Inf Num 1 3 Num 1.1920511922155705 9 NaN (<1.4142135623730951+0i>, <-1.4142135623730951+1.7319121124709868e-16i>).Seq True 0e0 Num 1e0 32 32e0 6.25 NaN NaN 0 0e0 Num 2.9999999999999996e0 3e0 10e0 -3e0 -1e0 2e0 3e0 2.718281828459045e0 7.38905609893065e0 Num 0e0 1.4142135623730951e0 Num 0e0 0e0 34359738368e0 2e0 2e0 NaN Inf NaN True 1e0 1e0 1e0 0e0 -Inf NaN 2e0 True False 0 1 1 0.5 2e0 1 Num 2e0 Int 4 8 0.25 0.0625 Num Num 2.9999999999999996e0 2.9999999999999996e0 2 2 14.999999999999998e0 16e0 Num True 64e0 1000.0000000000001e0 693.1471805599454e0 Inf Inf Inf
```
(`(2**1024).log2` is Inf because the Int overflows a double first; a
1000-bit Int still gives 1000.) rakupp 4.0.1-118: differs — `log(1, 1)` is
NaN rather than a Failure, `atan2("a", 1)` is 0, `Inf.exp(0.5)` Inf, and `𝑒`
is undeclared. `exp($base)` now uses its base everywhere, in the method, the
sub and the Complex forms.

### N-26  rand and srand                                                 D:yes R:yes V:quirk
`rand` is a Num in [0, 1); `$n.rand` scales it (negative for a negative
`$n`, `0.rand` is `0e0`, `Inf.rand` Inf, `NaN.rand` NaN, a big Int gives
a Num below it); the type object fails `X::Numeric::Uninitialized`;
`rand(10)` and `rand()` are the compile-time `X::Obsolete`. `srand`
takes an Int within a native int (a Rat, Str or Num argument is a
compile-time `X::TypeCheck::Argument`, `2**63` an `X::AdHoc`) and
returns it; after a re-seed `$n.rand` and `roll` replay, but `pick(N)`
on a Range or List does not — and in this build a bare `rand` after
`srand(42)` differs from the previous one while `5.rand` repeats, which
is recorded as measured.
```
say rand.^name, " ", 0 <= rand < 1, " ", do { srand(42); my $a = rand; srand(42); rand == $a }, " ", srand(42), " ", 10.rand.^name, " ", 0 <= 10.rand < 10, " ", (-10).rand < 0, " ", 2.5.rand.^name, " ", (try Int.rand) // $!.^name, " ", 0.rand.raku, " ", (1/2).rand.^name, " ", 5.pick, " ", 5.roll, " ", (try EVAL 'rand(10)') // $!.^name, " ", (try EVAL 'rand()') // $!.^name, " ", Inf.rand.raku, " ", NaN.rand.raku, " ", do { srand(1); my @a = (^100).pick(3); srand(1); my @b = (^100).pick(3); @a eqv @b }, " ", 10e0.rand.^name, " ", "10".rand.^name, " ", srand(0), " ", (try srand(2**70)) // $!.^name, " ", (try srand(-1)) // $!.^name, " ", (try EVAL 'srand(1.5)') // $!.^name, " ", (try EVAL 'srand("1")') // $!.^name, " ", (-10).rand.^name, " ", (2**70).rand.^name, " ", (2**70).rand < 2**70, " ", do { my @r = 3.rand xx 1000; (@r.min >= 0, @r.max < 3).raku }, " ", do { srand(7); my $a = 5.rand; srand(7); 5.rand == $a }, " ", (try EVAL 'srand(1e0)') // $!.^name, " ", (try srand(Int)) // $!.^name, " ", srand(42).^name, " ", (rand).WHAT.raku, " ", 1.rand.^name, " ", (-0e0).rand.raku, " ", (try Int.rand) // $!.message.lines[0], " ", do { srand(3); my $a = (1..6).roll; srand(3); (1..6).roll == $a }, " ", do { srand(3); my $a = <a b c>.pick; srand(3); <a b c>.pick eq $a }, " ", do { srand(3); my $a = (^10).roll(3); srand(3); (^10).roll(3) eqv $a }, " ", do { my @a = (^1000).map({ 1.rand }); @a.unique.elems > 990 }, " ", (try srand(2**63)) // $!.^name, " ", (try srand(2**63 - 1)) // $!.^name
# rakudo 2026.08: Num True False 42 Num True True Num X::Numeric::Uninitialized 0e0 Num 5 5 X::Obsolete X::Obsolete Inf NaN False Num Num 0 X::AdHoc -1 X::TypeCheck::Argument+{X::Comp} X::TypeCheck::Argument+{X::Comp} Num Num True (Bool::True, Bool::True) True X::TypeCheck::Argument+{X::Comp} X::Multi::NoMatch Int Num Num -0e0 Use of uninitialized value of type Int in numeric context False True False True X::AdHoc 9223372036854775807
say do { srand(1); my @a = (^100).pick(3); srand(1); my @b = (^100).pick(3); @a.raku ~ @b.raku }, " ", do { srand(1); my @a = (^100).roll(3); srand(1); my @b = (^100).roll(3); @a eqv @b }, " ", do { srand(1); my $a = (^100).pick; srand(1); (^100).pick == $a }, " ", do { srand(1); my @a = (1,2,3,4,5).pick(3); srand(1); my @b = (1,2,3,4,5).pick(3); @a eqv @b }, " ", do { srand(1); my @a = (^100).pick(*); srand(1); my @b = (^100).pick(*); @a eqv @b }, " ", do { srand(1); my @a = (^5).pick(3); srand(1); my @b = (^5).pick(3); @a eqv @b }, " ", do { srand(1); my @a = (^100).roll(*).head(3); srand(1); my @b = (^100).roll(*).head(3); @a eqv @b }
# rakudo 2026.08: [60, 20, 44][32, 70, 73] False True False False True False
```
rakupp 4.0.1-118: differs — `Int.rand` is 0 rather than
`X::Numeric::Uninitialized` (N-07), `srand` accepts `2**70`, 1.5, "1", 1e0
and `Int`, and every `pick`/`roll` form replays after `srand` where Rakudo's
`pick(N)` does not. `rand()` and `rand(N)` are now X::Obsolete.

### N-27  Typed and native containers                                    D:partial R:yes V:spec
Assigning a literal of the wrong kind is a compile-time
`X::Syntax::Number::LiteralType` (`my Int $x = 1.5`, `my Num $x = 5`,
`my Rat $r = 5e0`, `my int $x = NaN`); the same value from a variable is
a runtime `X::TypeCheck::Assignment` (a Str into an Int too, an `IntStr`
is fine). An out-of-range value into a native container is `X::AdHoc`
("Cannot unbox 65 bit wide bigint") at 64 bits but silently TRUNCATED
for the smaller sizes (`int8 = 300` is 44, `uint8 = -1` 255); once
stored, native arithmetic wraps (`int 2**63-1` `++` to the minimum,
`uint8 255` `++` to 0, `int 2**62 * 4` 0, `1 +< 64` 1); native `div`
and `%` by zero are `X::AdHoc`, native `/` gives a Rat that explodes on
use, `int ** negative` is 0; a Num container holds `0e0` and can be
incremented, but `$x++` on an undefined Rat, Str or UInt container is
`X::TypeCheck::Assignment` because the increment produces the Int 1.
`.int8`, `.uint8`, `.int64` and friends truncate an Int to the width.
```
say (try EVAL 'my int $x = 1.5; $x') // $!.^name, " ", (try EVAL 'my int $x = "5"; $x') // $!.^name, " ", (try EVAL 'my int $x = NaN; $x') // $!.^name, " ", (try EVAL 'my num $n = 1; $n.raku') // $!.^name, " ", (try EVAL 'my num $n = 1/3; $n.raku') // $!.^name, " ", (try EVAL 'my Int $x = NaN') // $!.^name, " ", (try EVAL 'my Int $x = Inf') // $!.^name, " ", (try EVAL 'my Int $x = 1.5') // $!.^name, " ", (try EVAL 'my Int $x = "5"') // $!.^name, " ", (try EVAL 'my Num $x = 5; $x.raku') // $!.^name, " ", (try EVAL 'my Num $x = 1/3; $x.raku') // $!.^name, " ", (try EVAL 'my Rat $r = 5; $r.raku') // $!.^name, " ", (try EVAL 'my Rat $r = 5e0') // $!.^name, " ", (try EVAL 'my Num $x = 5e0; $x = 5') // $!.^name, " ", (try EVAL 'my Int $x = 1e0') // $!.^name, " ", (try EVAL 'my Num $x = 1') // $!.^name, " ", (try EVAL 'my Rat $x = 1') // $!.^name, " ", (try EVAL 'my Rat $x = 1e0') // $!.^name, " ", (try EVAL 'my Num $x = 1.0') // $!.^name, " ", (try { my $v = 5; my Num $x = $v; $x.raku }) // $!.^name, " ", (try { my $v = 1.5; my Int $x = $v }) // $!.^name, " ", (try { my $v = 5; my Rat $r = $v }) // $!.^name, " ", (try { my $v = 5e0; my Rat $r = $v }) // $!.^name, " ", (try { my $v = 1/3; my Num $x = $v }) // $!.^name, " ", (try { my $v = "5"; my Int $x = $v }) // $!.^name, " ", (try { my $v = 5; my int $x = $v; $x }) // $!.^name, " ", (try { my $v = 1.5; my int $x = $v; $x }) // $!.^name, " ", (try { my $v = "5"; my int $x = $v; $x }) // $!.^name, " ", (try { my $v = 2**63; my int $x = $v; $x }) // $!.^name, " ", (try { my $v = 2**64; my int $x = $v; $x }) // $!.^name, " ", (try { my $v = 2**70; my uint $u = $v; $u }) // $!.^name, " ", (try { my $v = -1; my uint $u = $v; $u }) // $!.^name, " ", (try { my $v = 5; my num $n = $v; $n.raku }) // $!.^name, " ", (try { my $v = 1/3; my num $n = $v; $n.raku }) // $!.^name, " ", (try { my $v = NaN; my int $x = $v; $x }) // $!.^name, " ", (try { my UInt $u = -5 }) // $!.^name, " ", (try { my UInt $u = 0; --$u; $u }) // $!.^name, " ", (try { my UInt $u = 2**70; $u }) // $!.^name, " ", (try { my Int(Str) $x = "abc" }) // $!.^name, " ", do { my Int() $x = "42"; $x.^name }, " ", do { my Int $x = <42>; $x.^name }, " ", (try { my int $x = <42>; $x.^name }) // $!.^name, " ", do { my Real $r = 5e0; $r.^name }, " ", do { my Numeric $n = 1i; $n.^name }, " ", do { my UInt $d = 0; $d - 3 }, " ", do { my Int $x = 2**70; $x.chars }, " ", (try { my int8 $b = 300; $b }) // $!.^name, " ", (try { my uint8 $u = -1; $u }) // $!.^name, " ", (try { my $v = 300; my int8 $b = $v; $b }) // $!.^name, " ", (try { my $v = -1; my uint8 $u = $v; $u }) // $!.^name, " ", (try EVAL 'my int8 $b = 200; $b') // $!.^name, " ", (try EVAL 'my uint8 $u = 256; $u') // $!.^name, " ", (try EVAL 'my Int $x = 2**70; $x.chars') // $!.^name, " ", (try { my Int $x = "5".Int; $x }) // $!.^name, " ", (try { my Num $x = 5.Num; $x.raku }) // $!.^name
# rakudo 2026.08: X::Syntax::Number::LiteralType X::AdHoc X::Syntax::Number::LiteralType X::Syntax::Number::LiteralType X::AdHoc X::Syntax::Number::LiteralType X::Syntax::Number::LiteralType X::Syntax::Number::LiteralType X::TypeCheck::Assignment X::Syntax::Number::LiteralType X::TypeCheck::Assignment X::Syntax::Number::LiteralType X::Syntax::Number::LiteralType X::Syntax::Number::LiteralType X::Syntax::Number::LiteralType X::Syntax::Number::LiteralType X::Syntax::Number::LiteralType X::Syntax::Number::LiteralType X::Syntax::Number::LiteralType X::TypeCheck::Assignment X::TypeCheck::Assignment X::TypeCheck::Assignment X::TypeCheck::Assignment X::TypeCheck::Assignment X::TypeCheck::Assignment 5 X::AdHoc X::AdHoc X::AdHoc X::AdHoc X::AdHoc 18446744073709551615 X::AdHoc X::AdHoc X::AdHoc X::TypeCheck::Assignment X::TypeCheck::Assignment 1180591620717411303424 Any Int IntStr Int Num Complex -3 22 44 255 44 255 -56 0 22 5 5e0
say do { my Int $x; $x++; $x }, " ", do { my Int $x; ++$x }, " ", do { my Num $n; ($n++).raku ~ " " ~ $n.raku }, " ", do { my Num $n; (--$n).raku }, " ", do { my $x; ($x++).raku ~ " " ~ $x.raku }, " ", (try { my Str $s; ($s++).raku ~ " " ~ $s.raku }) // $!.^name, " ", do { my int $i = 9223372036854775807; $i++; $i }, " ", do { my int $i = -9223372036854775808; $i--; $i }, " ", do { my uint8 $u = 255; $u++; $u }, " ", do { my uint8 $u = 0; $u--; $u }, " ", do { my int8 $b = 127; $b++; $b }, " ", do { my int $x = 7; $x % -2 }, " ", do { my int $x = -7; $x % 2 }, " ", do { my int $x = 3; ($x / 2).raku }, " ", do { my int $x = 3; ($x ** -1).raku }, " ", do { my int $x = 2; ($x ** 63).raku }, " ", do { my int $x = 7; (try $x div 0) // $!.^name }, " ", do { my int $x = 7; (try $x % 0) // $!.^name }, " ", do { my int $x; $x.^name ~ ":" ~ $x }, " ", do { my num $n; $n.raku }, " ", do { my int $a = 5; my int $b = 2; ($a / $b).^name }, " ", do { my int $a = 5; ($a + 0.5).^name }, " ", 200.int8, " ", 256.uint8, " ", (2**63).int64, " ", (2**64-1).int, " ", (-1).uint8, " ", (2**64).uint, " ", "255".int8, " ", 3.7.int8, " ", (2**64 + 5).int8, " ", do { my int $x = 5; $x.WHAT.raku }, " ", do { my uint8 $u = 200; $u.^name ~ ":" ~ $u }, " ", do { my int $i = 3; my int $j = 2; ($i ** $j).raku }, " ", do { my int $i = 3; my int $j = -2; ($i ** $j).raku }, " ", do { my int $i = 2; my int $j = 70; ($i ** $j).raku }, " ", do { my int $i = 2**62; ($i * 4).raku }, " ", do { my int $i = 2**62; ($i + $i + $i).raku }, " ", do { my int8 $b = -128; $b--; $b }, " ", do { my int16 $s = 32767; $s++; $s }, " ", do { my uint16 $s = 0; $s -= 1; $s }, " ", do { my uint32 $u = 2**32 - 1; $u++; $u }, " ", do { my uint $u = 0; $u--; $u }, " ", do { my uint64 $u = 2**64 - 1; $u++; $u }, " ", do { my int $x = 1; $x +< 63 }, " ", do { my int $x = 1; ($x +< 64).raku }, " ", do { my num $n = 1e0; ($n / 0e0) ~~ Failure }, " ", do { my int $x = 5; $x.Str.^name }, " ", do { my int $i = -1; (my uint $u = $i); $u }, " ", do { my int $i = 2**62; ($i * 2).raku }, " ", do { my int $i = 2**62; ($i * 2 * 2).raku }, " ", do { my int $i = 5; ($i * 1.5).raku }, " ", do { my int $i = 5; ($i * 1.5).^name }, " ", do { my int $i = 5; my num $n = 2e0; ($i * $n).raku }, " ", do { my int $i = 7; my int $j = 2; ($i div $j).raku ~ ($i % $j).raku }, " ", do { my int $i = -7; my int $j = 2; ($i div $j).raku ~ " " ~ ($i % $j).raku }, " ", do { my int $i = 7; my int $j = -2; ($i div $j).raku ~ " " ~ ($i % $j).raku }, " ", do { my uint8 $u = 100; $u += 200; $u }, " ", do { my uint8 $u; $u -= 100; $u }, " ", do { my int $x = 2**63 - 1; ++$x; $x }, " ", do { my int $i = 5; my int $j = 0; (try $i div $j) // $!.^name }, " ", do { my int $i = 5; my int $j = 0; (try $i % $j) // $!.^name }, " ", do { my int $i = 5; my int $j = 0; ($i / $j).^name }, " ", do { my int $i = 5; my int $j = 0; (try say $i / $j) // $!.^name }, " ", do { my num $a = 5e0; my num $b = 0e0; ($a / $b).^name }, " ", do { my num $a = 5e0; my num $b = 0e0; ($a % $b).^name }, " ", do { my num $a = 5e0; my num $b = -1e20; ($a ** $b).^name }, " ", do { my int $i = 2; my int $j = -1; ($i ** $j).raku }, " ", do { my int $i = 0; my int $j = -1; (try ($i ** $j).raku) // $!.^name }, " ", do { my int $i = 5; my int $j = 2; ($i ** $j).^name }
# rakudo 2026.08: 1 1 0e0 1e0 -1e0 0 1 X::TypeCheck::Assignment -9223372036854775808 9223372036854775807 0 255 -128 -1 1 1.5 0 -9223372036854775808 X::AdHoc X::AdHoc Int:0 0e0 Rat Rat -56 0 -9223372036854775808 -1 255 0 -1 3 5 Int Int:200 9 0 0 0 -4611686018427387904 127 -32768 65535 0 18446744073709551615 0 -9223372036854775808 1 True Str 18446744073709551615 -9223372036854775808 0 7.5 Rat 10e0 31 -4 1 -4 -1 44 156 -9223372036854775808 X::AdHoc X::AdHoc Rat X::Numeric::DivideByZero Failure Failure Failure 0 0 Int
say (try { my Rat $r; ($r++).raku ~ " " ~ $r.raku }) // $!.^name, " ", (try { my Rat $r; ++$r; $r.raku }) // $!.^name, " ", (try { my Int $x; $x++; $x.raku }) // $!.^name, " ", (try { my Num $n; $n++; $n.raku }) // $!.^name, " ", (try { my Str $s; $s++; $s.raku }) // $!.^name, " ", (try { my Rat $r; $r += 1; $r.raku }) // $!.^name, " ", (try { my Rat $r; $r += 0.5; $r.raku }) // $!.^name, " ", (try { my FatRat $r; $r++; $r.raku }) // $!.^name, " ", (try { my Num $n; $n += 1; $n.raku }) // $!.^name, " ", (try { my Int $x; $x += 1.5; $x.raku }) // $!.^name, " ", (try { my Int $x; $x--; $x.raku }) // $!.^name, " ", (try { my Rat $r; $r--; $r.raku }) // $!.^name, " ", (try { my UInt $u; $u++; $u.raku }) // $!.^name, " ", (try { my UInt $u; $u--; $u.raku }) // $!.^name, " ", (try { my Real $r; $r++; $r.raku }) // $!.^name, " ", (try { my Numeric $n; $n++; $n.raku }) // $!.^name, " ", (try { my Int $x; $x *= 2; $x.raku }) // $!.^name, " ", (try { my Int $x; $x -= 1; $x.raku }) // $!.^name, " ", (try { my Num $x; $x -= 1; $x.raku }) // $!.^name, " ", (try { my Rat $x; $x -= 1; $x.raku }) // $!.^name, " ", (try { my Rat $x; $x *= 2; $x.raku }) // $!.^name
# rakudo 2026.08: X::TypeCheck::Assignment X::TypeCheck::Assignment 1 1e0 X::TypeCheck::Assignment X::TypeCheck::Assignment 0.5 X::TypeCheck::Assignment X::TypeCheck::Assignment X::TypeCheck::Assignment -1 X::TypeCheck::Assignment 1 X::TypeCheck::Assignment 1 1 2 -1 X::TypeCheck::Assignment X::TypeCheck::Assignment X::TypeCheck::Assignment
```
rakupp 4.0.1-118: differs — every literal mismatch is a runtime
`X::TypeCheck::Assignment` rather than a compile-time
`X::Syntax::Number::LiteralType`; native `int`/`uint` never wrap at 64 bits
(`2**63` into an `int` is 9223372036854775808, `1 +< 64` is
18446744073709551616) though `int8`/`uint8`/`int16` do; native `div`/`%` by
zero are `X::Numeric::DivideByZero`; `int ** negative` is a Rat; and `my Rat
$r; $r++`, `my Str $s; $s++` and `my UInt $u; $u--` are accepted.

### N-28  Infectiousness                                                  D:yes R:yes V:spec
Int < Rat < FatRat < Num < Complex: the wider operand's type wins for
`+ - * ** %`, `/` on two Ints or Rats is a Rat, `div` and `mod` stay Int,
`Bool + Bool` is an Int, an allomorph acts as its numeric part, `max`
and `min` return one of their operands unchanged (the second of two
equal ones), `gcd`/`lcm`/`+&` return Int. `Rat ** Int` is a Rat,
`Rat ** Rat` and `Rat ** Num` Nums, `FatRat ** Int` a FatRat.
```
say (1 + 1).^name, " ", (1 + 1.0).^name, " ", (1 + 1e0).^name, " ", (1.0 + 1e0).^name, " ", (1 + FatRat.new(1)).^name, " ", (1.0 + FatRat.new(1)).^name, " ", (1e0 + FatRat.new(1)).^name, " ", (1 + 1i).^name, " ", (1e0 + 1i).^name, " ", (1 * 1.0).^name, " ", (2 * 0.5).raku, " ", (2 * 0.5).^name, " ", (1 - 1.0).^name, " ", (3 / 1).^name, " ", (3 / 1).raku, " ", (3e0 / 1).^name, " ", (1 % 1.0).^name, " ", (1 ** 1.0).^name, " ", (1 ** 1).^name, " ", (1.0 ** 1e0).^name, " ", (FatRat.new(1) ** 2).^name, " ", (FatRat.new(1,3) / 0.5).^name, " ", (1 div 1).^name, " ", (1.5 div 1).^name, " ", (1 mod 1).^name, " ", (1.5 mod 1).^name, " ", (1.5e0 mod 1).^name, " ", (True + True).^name, " ", (True + True), " ", (1 + True).^name, " ", (1 max 1.0).raku, " ", (1.0 max 1).raku, " ", (1 max 1.0).^name, " ", (1 min 2e0).^name, " ", (1 gcd 1.5).^name, " ", (1 +& 1.5).^name, " ", (1 lcm 1e0).^name, " ", (7.5 % 2).raku, " ", (7.5e0 % 2).raku, " ", (7 % 2.5).raku, " ", (7 % 2.5e0).raku, " ", (-7 % 2.5).raku, " ", (7.5 % -2).raku, " ", (1.0 ** 2).raku, " ", (1e0 ** 2).raku, " ", (FatRat.new(1,2) ** 0.5).^name, " ", (FatRat.new(1,2) * 1i).^name, " ", (1 + <1/2>).^name, " ", (<1/2> + FatRat.new(1,2)).raku, " ", (FatRat.new(1,2) + <1/2>).raku, " ", (FatRat.new(1,2) == 0.5), " ", (FatRat.new(1,2) === 0.5), " ", (FatRat.new(1,2) eqv 0.5), " ", (FatRat.new(1,2) <=> 0.5), " ", (2.5e0 % 1).raku, " ", (2.5 % 1e0).raku, " ", (1e0 ** 1).raku, " ", (2 ** 1e0).raku, " ", (2.0 ** 2.0).raku, " ", (2.0 ** 0.5).raku, " ", (2 * 1.5).raku, " ", (3 * 0.5).^name, " ", (-1 * 0.5).raku, " ", (1.5 ** 2).raku, " ", (1.5 ** 2).^name, " ", (1.5 ** -2).raku, " ", (1.5 ** 2.0).^name, " ", (1.5 ** 2.5).^name, " ", (1.5 ** FatRat.new(2)).^name, " ", (FatRat.new(3,2) ** 2).raku, " ", (FatRat.new(3,2) ** -2).raku, " ", (FatRat.new(3,2) ** 2.5).^name, " ", (2 ** FatRat.new(2)).^name, " ", (2 ** FatRat.new(1,2)).^name, " ", (2 ** 0.5).raku, " ", (2 ** <1/2>).^name, " ", (4 ** <1/2>).raku, " ", (try EVAL '(Int + Int).^name') // $!.^name
# rakudo 2026.08: Int Rat Num Num FatRat FatRat Num Complex Complex Rat 1.0 Rat Rat Rat 3.0 Num Rat Num Int Num FatRat FatRat Int Int Int Rat Num Int 2 Int 1.0 1 Rat Int Int Int Int 1.5 1.5e0 2.0 2e0 0.5 -0.5 1.0 1e0 Num Complex Rat FatRat.new(1, 1) FatRat.new(1, 1) True False False Same 0.5e0 0.5e0 1e0 2e0 4e0 1.4142135623730951e0 3.0 Rat -0.5 2.25 Rat <4/9> Num Num Num FatRat.new(9, 4) FatRat.new(4, 9) Num Num Num 1.4142135623730951e0 Num 2e0 X::Numeric::Uninitialized
```
rakupp 4.0.1-118: matches on every field but the last (N-07).

## Counts

| | items |
|---|---|
| total | 28 |
| not fully stated by docs (D:yes) nor asserted by Roast (R:yes) | 3 — N-07, N-16, N-23 |
| Rakudo bugs (do not imitate) | 6 — N-11 `7 mod 2.5`, N-12 `(-2) ** -3` and `0e0 ** -1`, N-16 `[lcm] ()`, N-17 `1 +< -64`, N-19 `narrow` losing the value, N-22 `polymod` with a non-Int divisor |
| quirks (recorded, step two decides) | 4 — N-13 half-up and the binary half, N-18 `(-0e0) ** 3`, N-23 a Rat is capturable, N-26 `pick` after `srand` |
| rakupp 4.0.1-118 differs | 26 |
| rakupp 4.0.1-118 matches | 2 — N-03, N-22 (N-28 on every field but the last) |

Implemented 2026-09-22, from this sheet, against the Roast files named at the
top: S32-num/base.t 47/63 -> 63/63, exp.t 64/72 -> 72/72, narrow.t 15/17 ->
17/17, rand.t 111/113 -> 113/113, rounders.t 137/143 -> 143/143, int.t 156/165
-> 163/165, negative-zero.t 16/20 -> 18/20 — the chapter as a whole 21 -> 26
files fully passing, 3,424 -> 3,465 assertions. The items that MOVED: N-06
(`Int.new` refuses what it cannot convert), N-13 (the sub rounders pass Inf and
NaN through), N-17 (a shift count that does not fit a native int — it used to
HANG), N-18 (U+2212 as a minus), N-19 (`narrow` is approximate), N-20 (`.base`
computed exactly, with Rakudo's three default digit counts, and `base-repeating`
defaulting to base 10), N-21 (`msb` of a negative), N-22 (`polymod`'s
stop-at-one divisor and its two refusals), N-23 (a UInt container enforces the
subset), N-25 (`exp` uses its base), N-26 (`rand()` is X::Obsolete).

The item counts above are the SHEET's, not a Roast score: a probe line is one
field in dozens, and several lines still read as differing because a single
field early in them dies and carries the rest.

Two corrections to the sheet itself, found by re-running its own probes against
Rakudo: N-25 recorded `2.exp(-1)` as 0.5, but `$x.exp($base)` is `$base ** $x`
and Rakudo answers 1, as exp.t's own `(2).exp(i pi) == (1i*$pi) ** 2` implies;
and N-20's `"beer"` base alternates two glyphs (a single beer for 0, a pair for
1), which the sheet's `255.base("beer")` could not show because 255 is all ones.

Recurring rakupp gaps, for the next sitting, in the order a Roast run would meet
them: an undefined number in arithmetic must throw `X::Numeric::Uninitialized`
(N-07) — still the largest single gap, and the reason N-26 and N-28 read as
differing; `==`, `<`, `<=>` on an unparsable string must return a Failure rather
than throw (N-09, N-15), which kills three probe lines outright; `$*RAT-OVERFLOW`
must be honoured and `FatRat.Rat` must fail (N-10); the exception objects lack
`.using`, `.numerator`, `.details`, `.source`, `.pos`, `.what`, `.range`, and a
Failure's `.raku` renders the carrier hash instead of detonating, which is what
shifts the tail of N-11, N-12 and N-18; native `int`/`uint` must wrap at 64 bits
and literal mismatches be compile-time errors (N-27); `.bits`, the Rat `Capture`
and `UInt ~~ Int` are missing (N-23); a reduce over a bare literal does not parse
(N-16); `10¹⁰⁰` is mis-parsed (N-12). Where rakupp is closer to IEEE 754 than
Rakudo — `(-0e0) ** 3`, underflow to zero, `1e0 / 0e0` as Inf per the 6.e note in
the docs, exact `%.20f`, and `narrow` declining to turn 1e-300 into 0 — keep
rakupp, as the IEEE-over-Rakudo precedent in [REVIEW-GRAND.md](../REVIEW-GRAND.md)
already says.

## Method (how this sheet was produced)

As for [Supply.md](Supply.md). Eight probe rounds. Traps met, beyond
those listed in [Range.md](Range.md): a literal `Int + 1` or
`Nil == 0` is folded at compile time and its warning or exception
surfaces as a compile-time failure of the whole `-e`, so undefined
values must come from a variable; `sqrt 2, …` and every other list
operator without parentheses takes the rest of the `say` list; a lazy
`polymod` result must be reified inside the `try` (`.eager`); `1 +< Inf`
throws rather than fails, so `.^name` on it needs a `try`; `1 +< 2**70`
hangs rakupp, so the huge-count shifts went on lines of their own; a
Failure returned by `==` or `<` is not converted to `$!` by `try`, so
the `.^name` of the value was taken directly. D flags from
`doc/Type/{Int,Num,Rat,FatRat,Rational,Real,Numeric,UInt,Cool}.rakudoc`,
`doc/Language/numerics.rakudoc`, `doc/Language/operators.rakudoc` and
the `$*RAT-OVERFLOW`/`$*TOLERANCE` entries of
`doc/Language/variables.rakudoc`; R flags from `S32-num/*.t`,
`S02-types/{num,nan,infinity,int-uint,native,fatrat}.t` and
`S03-operators/{arith,numeric-shift,comparison,equality,relational}.t`.
