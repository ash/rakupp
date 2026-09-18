# Str — semantics sheet

Provenance: Rakudo tag `2026.08`, files `src/core.c/Str.rakumod` (4,211
lines), `Stringy.rakumod` (79), the string half of `Cool.rakumod` (585) and
`allomorphs.rakumod` (632), read in full on 2026-09-17/18. Out of scope, and
only probed where a Str method delegates to them: the `succ`/`pred` magic
(Rakudo::Internals), `ord`/`ords`/`chrs`/`uniname` (elsewhere in the
setting), the regex engine behind `match`, and the encoders behind `encode`.
Oracle: Homebrew Rakudo v2026.08. Compared against Raku++
4.0.1-7-g9f22c875 (build-arm64, 2026-09-18). Format and legend:
[README.md](README.md).

Where this sits against the declared spec: docs.raku.org documents 55 Str
methods and the Str half of Cool; Roast has 65 files under `S32-str/` that
Rakudo passes here, of which Raku++ passes 41. The 24 it does not pass
(`index`, `rindex`, `indices`, `contains`, `starts-with`, `ends-with`,
`substr`, `substr-rw`, `split`, `split-simple`, `comb`, `numeric`, `val`,
`parse-base`, `indent`, `encode`, `format`, `sprintf`, `space-chars`,
`uniparse`, `utf8-c8`, `Collation`, `windows-125x` and `sprintf-x`) are the
surface gap; what this sheet adds is the rules inside the methods: which
position is out of range and which is merely past the end, what an empty
needle finds, how a limit counts, how a Str subclass or an allomorph comes
back out, what a type object answers, and which adverb combinations are
rejected. 12 of the 66 items are neither fully stated by docs nor fully
asserted by Roast; 10 items flag a Rakudo bug and 18 flag a quirk, all
recorded so that step two does not imitate them by accident.

Probe helper. Many probes wrap each expression in
`sub f(&c) { my \r = try c(); $! ?? "T(" ~ $!.^name ~ ")" !! r ~~ Failure ?? "F(" ~ r.exception.^name ~ ")" !! r.raku }`.
`T(X)` is an exception of type `X` that `try` caught. Because `try` also
catches a *returned* Failure and sets `$!` to its exception, `T(X)` covers
both "thrown" and "returned as a Failure"; where the difference matters the
statement says which, and Roast's `fails-like` assertions are cited.
`F(X)` appears only for probes that inspect the value directly. Warnings are
collected with `CONTROL { when CX::Warn { …; .resume } }` and appear after
a `|` as a count and the unique first lines.

## A. Identity, truth, coercion

### ST-01  Construction and value identity                        D:partial R:partial V:spec
`Str.new` is `""`; `Str.new(:value(x))` coerces `x` with `.Str`. `WHICH` is
`Str|` followed by the text, so `===` and `eqv` compare by content. `.Str`,
`.Stringy` and `.gist` return the same content (a value, so `===` holds);
`.raku` quotes it (ST-62).
```
say Str.new.raku, " ", Str.new(value => 42).raku, " ", Str.new(value => 1.5).raku, " ", "abc".WHICH, " ", ("abc" === "abc"), " ", ("abc" eqv "abc"), " ", "abc".Str.^name, " ", "abc".Stringy.^name, " ", "abc".gist.raku, " ", "abc".Str.WHICH, " ", "abc".substr(0).WHICH, " ", ("abc".subst("x", "y") === "abc")
# rakudo 2026.08: "" "42" "1.5" Str|abc True True Str Str "abc" Str|abc Str|abc True
```
rakupp 4.0.1: matches.

### ST-02  A Str subclass survives its own methods                 D:no R:no V:spec
For a subclass `S is Str`, `WHICH` is `S|` plus the text, and every method
that produces a string from the invocant returns an `S`: `uc`, `substr`,
`trim`, `chomp`, `flip`, `subst`, `trans`, `indent`, `samecase`, the
elements of `comb`, `lines`, `words` and `split`, and `.Str`. The operators
`~` and `x` return a plain `Str`. An `S` is `eq` to a Str with the same
text but neither `===` nor `eqv` to it (different type). The second line of
the recorded output is the tail of the `WHICH` value, which contains the
newline of the text.
```
class S is Str {}; my $s = S.new(value => "a b\nc"); say $s.WHICH, " ", $s.uc.^name, " ", $s.substr(1).^name, " ", ($s ~ "x").^name, " ", ($s x 2).^name, " ", $s.comb[0].^name, " ", $s.lines[0].^name, " ", $s.split("b")[0].^name, " ", $s.trim.^name, " ", $s.words[0].^name, " ", $s.chomp.^name, " ", $s.flip.^name, " ", $s.subst("a", "z").^name, " ", $s.trans("a" => "z").^name, " ", $s.indent(1).^name, " ", $s.samecase("A").^name, " ", $s.Str.^name, " ", ($s === "a b\nc"), " ", ($s eqv "a b\nc"), " ", ($s eq "a b\nc"), " ", $s.raku
# rakudo 2026.08: S|a b
c S S Str Str S S S S S S S S S S S S False False True "a b\nc"
```
rakupp 4.0.1: differs — `WHICH` is `S|` plus an address, every method
returns a plain `Str` (only `.Str` keeps `S`), and `eqv` to a plain Str is
True.

### ST-03  Bool is "has characters"                                D:yes R:yes V:spec
`"0"` and `" "` are true; only `""` is false; the type object is false.
```
say "".Bool, " ", "0".Bool, " ", " ".Bool, " ", ?"0", " ", so "00", " ", Str.Bool, " ", ("0" ?? 1 !! 0)
# rakudo 2026.08: False True True True True False 1
```
rakupp 4.0.1: matches.

### ST-04  A Str cannot be captured                                 D:yes R:yes V:spec
`.Capture` throws `X::Cannot::Capture`, and so does binding a Str (or an
IntStr) to a positional or named signature. Binding a RatStr throws
`X::AdHoc` instead (its `Capture` is the generic Mu one). Roast
(`S02-types/capture.t`) asserts the Str and allomorph cases.
```
say (try "abc".Capture) // $!.^name, " ", (try { my ($a) := "abc"; "ok" }) // $!.^name, " ", (try { my (:$x) := "abc"; "ok" }) // $!.^name, " ", (try { my ($a) := <42>; "ok" }) // $!.^name, " ", (try { my ($a) := <4.2>; "ok" }) // $!.^name
# rakudo 2026.08: X::Cannot::Capture X::Cannot::Capture X::Cannot::Capture X::Cannot::Capture X::AdHoc
```
rakupp 4.0.1: differs — only the explicit `.Capture` throws; `my ($a) :=
"abc"` binds.

### ST-05  ACCEPTS is string equality of the other's .Str          D:yes R:yes V:spec
`$x ~~ "abc"` is `$x.Str eq "abc"`, so `1 ~~ "1"`, `1.0 ~~ "1"`,
`1e0 ~~ "1"` and `(1, 2) ~~ "1 2"` are True. A type object on the left is
never accepted (Nil, Int, Str all False). The `ACCEPTS` method returns a
Bool; called with an undefined argument it is False; the Str type object
accepts by type. Roast (`S03-smartmatch/any-str.t`) asserts `4 ~~ '4'` and
`Mu !~~ ''`.
```
say ("abc" ~~ "abc"), " ", (1 ~~ "1"), " ", (1.0 ~~ "1"), " ", (1e0 ~~ "1"), " ", (1/2 ~~ "0.5"), " ", ("1" ~~ 1), " ", ((1, 2) ~~ "1 2"), " ", (Nil ~~ "abc"), " ", (Int ~~ "a"), " ", (Str ~~ "abc"), " ", ("" ~~ ""), " ", ("abc" ~~ "ABC"), " ", ("a" ~~ Str), " ", (Str ~~ Str), " ", (<1> ~~ "1"), " ", ("1" ~~ <1>)
# rakudo 2026.08: True True True True True True True False False False True False True True True True
say "abc".ACCEPTS("abc"), " ", "abc".ACCEPTS(1).raku, " ", "1".ACCEPTS(1).raku, " ", "abc".ACCEPTS(Any).raku, " ", "abc".ACCEPTS(Str).raku, " ", Str.ACCEPTS("a").raku, " ", Str.ACCEPTS(Str).raku
# rakudo 2026.08: True Bool::False Bool::True Bool::False Bool::False Bool::True Bool::True
```
rakupp 4.0.2: matches — `.ACCEPTS` answers on any invocant now.
`ACCEPTS` method on Str (`X::Method::NotFound`), so the second probe dies.

### ST-06  The numeric grammar of Str.Numeric                      D:partial R:yes V:spec
Leading and trailing whitespace is ignored; `""` and all-whitespace are `0`.
Signs `+`, `-` and U+2212 MINUS; single underscores between digits; Unicode
`Nd` digits (`"١٢"` is 12); more than 18 digits stays an Int. A radix point
gives a Rat (`"3.0"` is `3.0`, not 3); `.5` is allowed; `e`/`E` with an
optional signed exponent gives a Num (only in base 10); `1e400` is Inf and
`1e-400` is `0e0`; `"-0"` is 0 but `"-0.0"` is `0.0` and `"-0e0"` keeps the
sign. Prefixes `0x 0o 0b 0d` (with an optional `_` after them), the
`:16<FF>`, `:2«101»` and `:10[1,2,3]` forms; a radix point after a prefix
gives a Rat (`0x1.8` is 1.5). `/` makes a Rat from two integers (`1/0` is
the unnormalised `<1/0>`), or a division if either side is not an Int.
`Inf`, `-Inf`, `+Inf`, `NaN`; complex `1+2i`, `2i`, `3-4\i`, `Inf\i`; the
`2*10**3` multiplier form; a single vulgar-fraction character is a Rat
(`½` is 0.5) and `Nd` digits work with a radix point too.
```
say "42".Numeric.raku, " ", "".Numeric.raku, " ", "  ".Numeric.raku, " ", " 42\n".Numeric.raku, " ", "-42".Numeric.raku, " ", "+42".Numeric.raku, " ", "−42".Numeric.raku, " ", "1_000".Numeric.raku, " ", "00042".Numeric.raku, " ", "١٢".Numeric.raku, " ", "12345678901234567890".Numeric.raku, " ", "3.0".Numeric.raku, " ", "0.1".Numeric.raku, " ", "1.10".Numeric.raku, " ", ".5".Numeric.raku, " ", "1e3".Numeric.raku, " ", "1E+3".Numeric.raku, " ", "1.5e1".Numeric.raku, " ", "1e-400".Numeric.raku, " ", "1e400".Numeric.raku, " ", "-0".Numeric.raku, " ", "-0.0".Numeric.raku, " ", "-0e0".Numeric.raku
# rakudo 2026.08: 42 0 0 42 -42 42 -42 1000 42 12 12345678901234567890 3.0 0.1 1.1 0.5 1000e0 1000e0 15e0 0e0 Inf 0 0.0 -0e0
say "0x1F".Numeric.raku, " ", "0b101".Numeric.raku, " ", "0o17".Numeric.raku, " ", "0d99".Numeric.raku, " ", "0x_1F".Numeric.raku, " ", "0x1.8".Numeric.raku, " ", "0b1.1".Numeric.raku, " ", ":16<FF>".Numeric.raku, " ", ":2«101»".Numeric.raku, " ", ":10[1,2,3]".Numeric.raku, " ", ":3<12>".Numeric.raku, " ", "1/3".Numeric.raku, " ", "2/4".Numeric.raku, " ", "1/0".Numeric.raku, " ", "-1/2".Numeric.raku, " ", "1/2e0".Numeric.raku, " ", "0x10/2".Numeric.raku, " ", "Inf".Numeric.raku, " ", "-Inf".Numeric.raku, " ", "+Inf".Numeric.raku, " ", "NaN".Numeric.raku, " ", "1+2i".Numeric.raku, " ", "2i".Numeric.raku, " ", "3-4\\i".Numeric.raku, " ", "Inf\\i".Numeric.raku, " ", "2*10**3".Numeric.raku, " ", "½".Numeric.raku, " ", "٣.٥".Numeric.raku
# rakudo 2026.08: 31 5 15 99 31 1.5 1.5 255 5 123 5 <1/3> 0.5 <1/0> -0.5 0.5e0 8.0 Inf -Inf Inf NaN <1+2i> <0+2i> <3-4i> <0+Inf\i> 2000 RatStr.new(0.5, "½") 3.5
```
rakupp 4.0.2: matches — the whole grammar parses: the prefix radix point (`0x1.8`), both extra bracketings (`:2«101»`, `:10[1,2,3]`), a `/` between anything the grammar accepts, the `2*10**3` multiplier, `Inf\i`, and a lone vulgar fraction (as the RatStr it is).
`:2«101»`, `:10[1,2,3]`, `1/2e0`, `0x10/2`, `Inf\i`, `2*10**3` and `½`
(each is an `X::Str::Numeric`); the first line matches.

### ST-07  What Str.Numeric refuses, and how                        D:partial R:partial V:spec
A string that does not parse gives a **returned** Failure (Roast
`S32-str/numeric.t` asserts `'a'.Int` is a Failure that lives until used)
wrapping `X::Str::Numeric` with `source` (the string), `pos` (where parsing
stopped) and `reason`. Refused: any trailing text (`"12abc"` at pos 2,
`"4 2"` at 1), a radix point not followed by digits (`"5."`, `"1.e3"`), an
exponent without digits, `Nl`/`No` characters (`"Ⅻ"`), a digit carrying a
combining mark (pos 1), doubled or leading or trailing underscores, a bare
prefix (`"0x"`), a digit outside the base (`"0b2"`), a `p` exponent,
`"inf"` (case matters), `"-NaN"`, a lone `i`, `"1+i"`, `"Infi"` (needs
`\i`), a space after a sign, a double sign, a lone sign or dot, `"1/"`, a
signed denominator, two radix points, a radix above 36, `:16` without
brackets, two vulgar fractions, and `2**3` or `2*3` (the multiplier needs
`*` and `**`). `:fail-or-nil` turns the Failure into Nil.
```
sub f($s) { my $r = $s.Numeric; $r ~~ Failure ?? "F(" ~ $r.exception.^name ~ "," ~ ($r.exception.?pos // "-") ~ ")" !! $r.raku }; say f("abc"), " ", f("12abc"), " ", f("4 2"), " ", f("1 2"), " ", f("5."), " ", f("1.e3"), " ", f("1e"), " ", f("0.1e"), " ", f("1e5.5"), " ", f("Ⅻ"), " ", f("7\x[308]"), " ", f("1__0"), " ", f("_1"), " ", f("1_"), " ", f("1,000"), " ", f("0x"), " ", f("0b2"), " ", f("0x1p3"), " ", f("inf"), " ", f("-NaN"), " ", f("i"), " ", f("1+i"), " ", f("Infi"), " ", f("- 5"), " ", f("--1"), " ", f("+"), " ", f("."), " ", f("1/"), " ", f("1/-2"), " ", f("1.2.3"), " ", f(":37<1>"), " ", f(":16"), " ", f(":16<>"), " ", f("½½"), " ", f("2**3"), " ", f("2*3")
# rakudo 2026.08: F(X::Str::Numeric,0) F(X::Str::Numeric,2) F(X::Str::Numeric,1) F(X::Str::Numeric,1) F(X::Str::Numeric,2) F(X::Str::Numeric,2) F(X::Str::Numeric,2) F(X::Str::Numeric,4) F(X::Str::Numeric,3) F(X::Str::Numeric,0) F(X::Str::Numeric,1) F(X::Str::Numeric,1) F(X::Str::Numeric,0) F(X::Str::Numeric,1) F(X::Str::Numeric,1) F(X::Str::Numeric,2) F(X::Str::Numeric,2) F(X::Str::Numeric,3) F(X::Str::Numeric,0) F(X::Str::Numeric,1) F(X::Str::Numeric,0) F(X::Str::Numeric,2) F(X::Str::Numeric,3) F(X::Str::Numeric,1) F(X::Str::Numeric,1) F(X::Str::Numeric,1) F(X::Str::Numeric,1) F(X::Str::Numeric,2) -0.5 F(X::Str::Numeric,3) F(X::Str::Numeric,4) F(X::Str::Numeric,3) F(X::Str::Numeric,4) F(X::Str::Numeric,0) F(X::Str::Numeric,1) F(X::Str::Numeric,3)
say "abc".Numeric.^name, " ", "abc".Numeric.exception.^name, " ", ("abc".Numeric.exception.?source // "-").raku, " ", "abc".Numeric(:fail-or-nil).raku, " ", "42".Numeric(:fail-or-nil).raku, " ", "".Numeric(:fail-or-nil).raku, " ", "abc".Numeric.defined
# rakudo 2026.08: Failure X::Str::Numeric "abc" Nil 42 0 False
```
rakupp 4.0.2: differs — the refusals are refusals now, but the exception carries neither `pos` nor `source`, and `:fail-or-nil` still returns the Failure. `"1+i"` parses (Roast `numeric.t` marks the lone-`i` forms as a Rakudo skip, so this is ahead of the oracle rather than wrong).
`:fail-or-nil` still returns the Failure, and `"1+i"` parses (to `<1+1i>`;
Roast `numeric.t` marks the lone-`i` forms as a Rakudo skip, so this is
ahead of the oracle rather than wrong).

### ST-08  Int, Num, Rat, Real, UInt, Complex go through Numeric  D:partial R:partial V:spec
`.Int` truncates a parsed Rat or Num toward zero (`"-42.7"` is -42,
`"0.9"` is 0); `.Num` returns `-0e0` for any negative zero spelling
(`"-0"`, `"-0/5"`, `"−0.0"`, `" -0 "`) although `.Numeric` of `"-0/5"` is
`0.0`; `.Rat` of `"42"` is `42.0` and of `"1e3"` is `1000.0`. A parse
failure propagates as the same `X::Str::Numeric` (thrown at use);
`"Inf".Int`, `"NaN".Int`, `"1e400".Int` and `"Inf".UInt` throw
`X::Numeric::CannotConvert`; `"1/0".Int` and `.UInt` throw
`X::Numeric::DivideByZero` while `"1/0".Rat` is `<1/0>` and `.Num` is Inf;
`"-1".UInt` is an `X::OutOfRange` Failure and `"-0.5".UInt` is 0; a complex
with a non-zero imaginary part throws `X::Numeric::Real` from `.Int`,
`.Real`, `.Num` and `.Rat`, and `"1+0i".Int` is 1.
```
say "42".Int.raku, " ", "42.7".Int.raku, " ", "-42.7".Int.raku, " ", "0.9".Int.raku, " ", "1e3".Int.raku, " ", " 42 ".Int.raku, " ", "".Int.raku, " ", "-0".Int.raku, " ", "1+0i".Int.raku, " ", "0".Num.raku, " ", "-0".Num.raku, " ", "-0/5".Num.raku, " ", "−0.0".Num.raku, " ", " -0 ".Num.raku, " ", "+0".Num.raku, " ", "-0/5".Numeric.raku, " ", "1/3".Num.raku, " ", "42".Rat.raku, " ", "1/3".Rat.raku, " ", "1e3".Rat.raku, " ", "0.1".Num.raku, " ", "42".Real.raku, " ", "1e3".Real.raku, " ", "42".UInt.raku, " ", "42".FatRat.raku, " ", "42".Complex.raku
# rakudo 2026.08: 42 42 -42 0 1000 42 0 0 1 0e0 -0e0 -0e0 -0e0 -0e0 0e0 0.0 0.3333333333333333e0 42.0 <1/3> 1000.0 0.1e0 42 1000e0 42 FatRat.new(42, 1) <42+0i>
sub f(&c) { my \r = try c(); $! ?? "T(" ~ $!.^name ~ ")" !! r ~~ Failure ?? "F(" ~ r.exception.^name ~ ")" !! r.raku }; say f({"abc".Int}), " ", f({"abc".Num}), " ", f({"abc".Rat}), " ", f({"abc".Real}), " ", f({"abc".UInt}), " ", f({"abc".Complex}), " ", f({"-1".UInt}), " ", f({"-0.5".UInt}), " ", f({"Inf".Int}), " ", f({"-Inf".Int}), " ", f({"NaN".Int}), " ", f({"1e400".Int}), " ", f({"Inf".Rat}), " ", f({"NaN".Rat}), " ", f({"Inf".UInt}), " ", f({"1/0".Int}), " ", f({"1/0".UInt}), " ", f({"1/0".Rat}), " ", f({"1/0".Num}), " ", f({"1+2i".Int}), " ", f({"1+2i".Real}), " ", f({"1+2i".Num}), " ", f({"1+2i".Rat}), " ", f({"2i".Int}), " ", f({"1+0i".Real})
# rakudo 2026.08: T(X::Str::Numeric) T(X::Str::Numeric) T(X::Str::Numeric) T(X::Str::Numeric) T(X::Str::Numeric) T(X::Str::Numeric) T(X::OutOfRange) 0 T(X::Numeric::CannotConvert) T(X::Numeric::CannotConvert) T(X::Numeric::CannotConvert) T(X::Numeric::CannotConvert) <1/0> <0/0> T(X::Numeric::CannotConvert) T(X::Numeric::DivideByZero) T(X::Numeric::DivideByZero) <1/0> Inf T(X::Numeric::Real) T(X::Numeric::Real) T(X::Numeric::Real) T(X::Numeric::Real) T(X::Numeric::Real) 1e0
```
rakupp 4.0.1: differs — `"−0.0".Num` is `0e0`, `"1/3".Num` is `1e0`,
`"Inf".UInt`/`"1/0".UInt` are 0, `"1/0".Num` is `1e0`, and a complex with
an imaginary part converts silently (`"1+2i".Int` is 1, `.Real` keeps the
complex).

### ST-09  val() makes allomorphs and keeps the text                D:yes R:yes V:spec
`val` parses with the ST-06 grammar and returns `IntStr`, `RatStr`,
`NumStr` or `ComplexStr` holding the number and the **original** string,
whitespace included (`" 42 "`); a non-number comes back as the Str it was;
`""` and `" "` are `IntStr.new(0, "")` and the Str `" "` respectively; a
vulgar-fraction character with trailing space is not a number.
`:val-or-fail` is the Str.Numeric mode and returns the bare number. A
non-Str argument is passed through with the warning "uselessly passed to
val()"; a list of arguments or a List is mapped element-wise.
```
say val("42").raku, " ", val("1.5").raku, " ", val("1e3").raku, " ", val("1+2i").raku, " ", val("2i").raku, " ", val("½").raku, " ", val("0x1F").raku, " ", val("0x1.8").raku, " ", val("1/3").raku, " ", val("1/0").raku, " ", val("Inf").raku, " ", val("NaN").raku, " ", val("1e400").raku, " ", val("-0").raku, " ", val("+42").raku, " ", val("−42").raku, " ", val("1_000").raku, " ", val("0.0").raku, " ", val("1.0e0").raku, " ", val("abc").raku, " ", val("12abc").raku, " ", val("").raku, " ", val(" ").raku, " ", val(" 42 ").raku, " ", val("½ ").raku, " ", val("42").Str.raku, " ", val("42").Numeric.raku, " ", val("½", :val-or-fail).raku
# rakudo 2026.08: IntStr.new(42, "42") RatStr.new(1.5, "1.5") NumStr.new(1000e0, "1e3") ComplexStr.new(<1+2i>, "1+2i") ComplexStr.new(<0+2i>, "2i") RatStr.new(0.5, "½") IntStr.new(31, "0x1F") RatStr.new(1.5, "0x1.8") RatStr.new(<1/3>, "1/3") RatStr.new(<1/0>, "1/0") NumStr.new(Inf, "Inf") NumStr.new(NaN, "NaN") NumStr.new(Inf, "1e400") IntStr.new(0, "-0") IntStr.new(42, "+42") IntStr.new(-42, "−42") IntStr.new(1000, "1_000") RatStr.new(0.0, "0.0") NumStr.new(1e0, "1.0e0") "abc" "12abc" IntStr.new(0, "") " " IntStr.new(42, " 42 ") "½ " "42" 42 RatStr.new(0.5, "½")
say do { my @w; CONTROL { when CX::Warn { @w.push(.message.lines[0]); .resume } }; my $a = val(42); my $b = val("1", "2"); my $c = val(("1","x")); my $d = val((a => 1)); my $e = val(1, "2"); my $f = val(Int); $a.raku ~ " " ~ $b.raku ~ " " ~ $c.raku ~ " " ~ $d.raku ~ " " ~ $e.raku ~ " " ~ $f.raku ~ " | " ~ @w.elems ~ " | " ~ @w.unique.join("/") }
# rakudo 2026.08: 42 $(IntStr.new(1, "1"), IntStr.new(2, "2")) $(IntStr.new(1, "1"), "x") :a(1) $(1, IntStr.new(2, "2")) Int | 3 | Value of type Int uselessly passed to val()
```
rakupp 4.0.2: differs only in shape — `val` builds the right allomorph for every spelling now (`S32-str/val.t` passes in full, 913 failing assertions → 0); what is left is that `.Numeric` of a whole list is not itemized the way Rakudo's is.
prefix, `val("1", "2")` returns only the first, a List argument is returned
unchanged, and there is no warning.

### ST-10  Allomorph identity, truth and order                      D:yes R:yes V:spec
`<42>` is both an Int and a Str; `WHICH` names the type and both halves.
`.narrow` is the numeric half in its narrowest type (`<1e0>` narrows to Num,
`<1.0>` stays `1.0`); `.Bool` is the numeric truth (`<0>`, `<0.0>`, `<0e0>`
and `<0+0i>` are False while `"0"` is True); `.succ`/`.pred` are numeric.
`===` needs both halves equal (`<00> === <0>` is False); `eqv` needs the
same allomorph type and both halves; `==` and `eq` compare the halves;
`cmp` compares the numbers first and the strings only on a tie
(`<1> cmp <01>` is More, `<1> cmp <1.0>` is Less), `leg` and `<=>` compare
one half each. Roast `S02-literals/allomorphic.t` asserts the `cmp` and
`eqv` cases.
```
say (<42> ~~ Int), " ", (<42> ~~ Str), " ", <42>.WHICH, " ", <4.2>.WHICH, " ", <42>.raku, " ", <0x10>.raku, " ", <00>.raku, " ", <42>.narrow.^name, " ", <1e0>.narrow.^name, " ", <1.0>.narrow.raku, " ", <42>.Str.raku, " ", <1.0>.Str.raku, " ", <42>.gist, " ", <42>.Bool, " ", <0>.Bool, " ", <0.0>.Bool, " ", <0e0>.Bool, " ", <0+0i>.Bool, " ", ?"0", " ", <42>.succ.raku, " ", <42>.pred.raku, " ", <1.5>.succ.raku
# rakudo 2026.08: True True IntStr|Int|42|Str|42 RatStr|Rat|21/5|Str|4.2 IntStr.new(42, "42") IntStr.new(16, "0x10") IntStr.new(0, "00") Int Num 1.0 "42" "1.0" 42 True False False False False True 43 41 2.5
say (<42> === 42), " ", (<42> === "42"), " ", (<42> === <42>), " ", (<00> === <0>), " ", (<42> eqv 42), " ", (<42> eqv "42"), " ", (<42> eqv <42>), " ", (<00> eqv <0>), " ", (<1> eqv <1.0>), " ", (<42> == 42), " ", (<42> eq "42"), " ", (<00> == <0>), " ", (<1> == <1.0>), " ", (<1> eq <1.0>), " ", (<1> cmp <01>), " ", (<1> cmp <1>), " ", (<2> cmp <10>), " ", ("2" cmp "10"), " ", (<1> cmp <1.0>), " ", (<1> leg <1.0>), " ", (<1> <=> <1.0>), " ", (<2> lt <10>), " ", (<2> < <10>), " ", (<1> cmp 2), " ", (<10> cmp "9")
# rakudo 2026.08: False False True False False False True False False True True True True False More Same Less More Less Less Same False True Less Less
say ("5.0" ~~ <5>), " ", (5.0 ~~ <5>), " ", (<5.0> ~~ <5>), " ", ("5" ~~ <5>), " ", (5 ~~ <5>), " ", (5e0 ~~ <5>), " ", (<5> ~~ <5e0>), " ", (<5> ~~ 5), " ", (<5> ~~ "5"), " ", (<5> ~~ "5.0"), " ", (<a> ~~ <5>), " ", ((1..2) ~~ <5>), " ", (<5> ~~ (1..9)), " ", (42 ∈ <42 43>), " ", ("42" ∈ <42 43>), " ", (<42> ∈ <42 43>), " ", (<42> ∈ (42,)), " ", <42 43>.Set.raku, " ", <1 1.0>.unique.raku, " ", <1 1>.unique.raku
# rakudo 2026.08: False True True True True True True True True False False False True False False True False Set.new(IntStr.new(43, "43"),IntStr.new(42, "42")) (IntStr.new(1, "1"), RatStr.new(1.0, "1.0")).Seq (IntStr.new(1, "1"),).Seq
```
rakupp 4.0.2: differs — the identities and the ordering hold; `.WHAT` of an allomorph's numeric half reports the allomorph rather than the plain type, and `cmp` between two allomorphs answers Same where Rakudo orders them.
for `<1e0>`, and `cmp` stops at the numeric tie (`<1> cmp <01>` and
`<1> cmp <1.0>` are Same).

### ST-11  String methods on an allomorph return plain Str       D:yes R:partial V:spec/bug
`uc`, `substr`, `comb`, `chop`, `trim`, `subst`, `flip`, `words`, `split`,
`samecase`, `chars`, `starts-with`, `index` and `~` all work on the string
half and return plain `Str` values (`<42>.split("4")` is `("", "2")`).
**Bug:** `lines` is not overridden, so `<42>.lines` returns an `IntStr`
whose numeric half is 0 — a value that is `== 0` but `eq "42"`. Arithmetic
uses the numeric half; `<1+2i>.Int` throws `X::Numeric::Real`. Mutators
rebind the variable: after `subst-mutate` or `substr-rw` the container holds
a plain `Str` (also when `subst-mutate` found nothing), `.= subst` likewise,
and `++` or `+=` leave an Int. Roast `allomorphic.t` asserts the
`substr-rw` case.
```
say <42>.uc.^name, " ", <42>.substr(1).raku, " ", <42>.comb.raku, " ", <42>.comb[0].^name, " ", <42>.chop.raku, " ", <42>.trim.^name, " ", <42>.subst("4","x").raku, " ", (<42> ~ "").^name, " ", <42>.flip.raku, " ", <42>.words.raku, " ", <42>.split("4").raku, " ", <42>.split("4")[1].^name, " ", <42>.lines.raku, " ", <42>.samecase("A").raku, " ", <42>.chars, " ", <42>.starts-with("4"), " ", <42>.index("2").raku, " ", (<42> x 2).raku, " ", <42>.Numeric.raku, " ", (<42> + 1).raku, " ", <42>.abs.raku, " ", (-<42>).raku, " ", <42>.Int.^name, " ", <1.5>.Rat.^name, " ", <1e0>.Num.^name, " ", <1+0i>.Complex.^name, " ", <1+0i>.Int.raku, " ", <1+0i>.Real.raku, " ", (try <1+2i>.Int) // $!.^name, " ", (try <1+2i>.Real) // $!.^name
# rakudo 2026.08: Str "2" ("4", "2").Seq Str "4" Str "x2" Str "24" ("42",).Seq ("", "2").Seq Str (IntStr.new(0, "42"),).Seq "42" 2 True 1 "4242" 42 43 42 -42 Int Rat Num Complex 1 1e0 X::Numeric::Real X::Numeric::Real
sub f(&c) { my \r = try c(); $! ?? "T(" ~ $!.^name ~ ")" !! r ~~ Failure ?? "F(" ~ r.exception.^name ~ ")" !! r.raku }; say f({ my $x = <42>; my $m = $x.subst-mutate("4","x"); $x.raku ~ "/" ~ $x.^name ~ "/" ~ $m.raku }), " ", f({ my $x = <42>; my $m = $x.subst-mutate("9","x"); $x.raku ~ "/" ~ $m.raku }), " ", f({ my $x = <42>; $x.substr-rw(0,1) = "9"; $x.raku ~ "/" ~ $x.^name }), " ", f({ my $x = <42>; my $p := $x.substr-rw(0, 1); $p = "9"; $x.raku }), " ", f({ my $x = <42>; $x.substr-rw = "7"; $x.raku }), " ", f({ my $x = <42>; $x .= subst("4", "x"); $x.raku }), " ", f({ my $x = <42>; $x++; $x.raku }), " ", f({ my $x = <42>; $x += 1; $x.raku }), " ", f({ my $x = <42>; $x ~= "!"; $x.raku })
# rakudo 2026.08: "\"x2\"/Str/Match.new(:orig(\"42\"), :from(0), :pos(1))" "\"42\"/Any" "\"92\"/Str" "\"92\"" "\"7\"" "\"x2\"" "43" "43" "\"42!\""
```
rakupp 4.0.2: differs — unchanged: a string method on an allomorph still returns the allomorph rather than a plain Str.
`substr-rw` on an allomorph variable throws `X::Assignment::RO`.

### ST-12  The Str type object                                     D:no R:partial V:spec/quirk
`Str.chars` and `Str.codes` are 0 with the "uninitialized value … in string
context" warning; `uc lc tc tclc fc flip Str Stringy` and `~Str` are `""`
with the same warning; `Str ~ "a"` is `"a"`; `.gist` is `(Str)` and `.raku`
is `Str`; `.Bool` False; `.Numeric` and `+Str` are 0 with the "numeric
context" warning, and `.Rat`/`.Real` are `0.0`/`0`. **Quirk:** `Str.Int`,
`Str.Num`, `Str.Version`, `Str.succ` and `Str.Date` throw
`X::Parameter::InvalidConcreteness` while `Str.Rat` answers `0.0`.
`Str.IO` is the `IO::Path` type object; `Str.match("a")` is Nil;
`Str.fmt("%s")` is `""`; `Str.ACCEPTS("a")` is True (type check);
`Str.encode` throws `X::AdHoc`; `Str.Capture` throws `X::Cannot::Capture`;
every other Str method has only defined-invocant candidates and throws
`X::Multi::NoMatch` (Roast `chop.t` asserts `Str.chop` throws).
```
say do { my @w; CONTROL { when CX::Warn { @w.push(.message.lines[0]); .resume } }; Str.chars.raku ~ " " ~ Str.codes.raku ~ " " ~ Str.uc.raku ~ " " ~ Str.lc.raku ~ " " ~ Str.tc.raku ~ " " ~ Str.tclc.raku ~ " " ~ Str.fc.raku ~ " " ~ Str.flip.raku ~ " " ~ Str.Str.raku ~ " " ~ Str.Stringy.raku ~ " " ~ (~Str).raku ~ " " ~ (Str ~ "a").raku ~ " " ~ Str.gist ~ " " ~ Str.raku ~ " " ~ Str.Bool ~ " " ~ Str.Numeric.raku ~ " " ~ (+Str).raku ~ " " ~ Str.Rat.raku ~ " " ~ Str.Real.raku ~ " | " ~ @w.elems ~ " | " ~ @w.unique.join("/") }
# rakudo 2026.08: 0 0 "" "" "" "" "" "" "" "" "" "a" (Str) Str False 0 0 0.0 0 | 16 | Use of uninitialized value of type Str in string context./Use of uninitialized value of type Str in numeric context
sub f(&c) { my \r = try c(); $! ?? "T(" ~ $!.^name ~ ")" !! r ~~ Failure ?? "F(" ~ r.exception.^name ~ ")" !! r.raku }; say f({Str.Int}), " ", f({Str.Num}), " ", f({Str.Version}), " ", f({Str.IO}), " ", f({Str.substr(0)}), " ", f({Str.comb.eager}), " ", f({Str.lines.eager}), " ", f({Str.words.eager}), " ", f({Str.split("a").eager}), " ", f({Str.index("a")}), " ", f({Str.rindex("a")}), " ", f({Str.indices("a")}), " ", f({Str.contains("a")}), " ", f({Str.starts-with("a")}), " ", f({Str.trim}), " ", f({Str.chomp}), " ", f({Str.chop}), " ", f({Str.subst("a","b")}), " ", f({Str.match("a")}), " ", f({Str.trans("a" => "b")}), " ", f({Str.indent(1)}), " ", f({Str.samecase("a")}), " ", f({Str.wordcase}), " ", f({Str.encode}), " ", f({Str.parse-base(2)}), " ", f({Str.succ}), " ", f({Str.Date}), " ", f({Str.fmt("%s")}), " ", f({Str.ACCEPTS("a")}), " ", f({Str.Capture})
# rakudo 2026.08: T(X::Parameter::InvalidConcreteness) T(X::Parameter::InvalidConcreteness) T(X::Parameter::InvalidConcreteness) IO::Path T(X::Multi::NoMatch) T(X::Multi::NoMatch) T(X::Multi::NoMatch) T(X::Multi::NoMatch) T(X::Multi::NoMatch) T(X::Multi::NoMatch) T(X::Multi::NoMatch) T(X::Multi::NoMatch) T(X::Multi::NoMatch) T(X::Multi::NoMatch) T(X::Multi::NoMatch) T(X::Multi::NoMatch) T(X::Multi::NoMatch) T(X::Multi::NoMatch) Nil T(X::Multi::NoMatch) T(X::Multi::NoMatch) T(X::Multi::NoMatch) T(X::Multi::NoMatch) T(X::AdHoc) T(X::Multi::NoMatch) T(X::Parameter::InvalidConcreteness) T(X::Parameter::InvalidConcreteness) "" Bool::True T(X::Cannot::Capture)
```
rakupp 4.0.1: differs — `.Numeric`/`.Real` give `0e0`, no warning is
resumable, and most methods answer instead of throwing (`Str.substr(0)` is
`""`, `Str.index("a")` Nil, `Str.match("a")` a Match at 0, `Str.Int` 0;
`Str.Version`/`.IO`/`.indices`/`.chop`/`.trans`/`.encode`/`.succ`/`.Date` are
`X::Method::NotFound`).

### ST-13  Concatenation                                           D:partial R:partial V:spec
`[~] ()` and `infix:<~>()` are `""`; one operand is stringified; each
operand goes through `.Str` (a Rat prints `0.333333`, a Num `1e+100`, a
List joins with spaces, a Hash with tabs); the result is always a plain
`Str`, whatever the operand types. Nil and type objects concatenate as
`""` with a "Use of … in string context" warning (also for Mu).
```
say ([~] ()).raku, " ", ([~] 1).raku, " ", ([~] 1, 2).raku, " ", (infix:<~>()).raku, " ", (infix:<~>("a")).raku, " ", (infix:<~>(1, 2, 3)).raku, " ", (1 ~ 2).raku, " ", ("a" ~ 1.5).raku, " ", ("a" ~ 1/3).raku, " ", ("a" ~ 1e100).raku, " ", ("a" ~ 1i).raku, " ", ("a" ~ True).raku, " ", ("a" ~ (1, 2)).raku, " ", ("a" ~ [1, 2]).raku, " ", ("a" ~ {x => 1}).raku, " ", ("a" ~ (1 => 2)).raku, " ", ("a" ~ Any.new).^name, " ", (~ 42).raku, " ", (~ 1e0).raku, " ", (~ (1, 2)).raku
# rakudo 2026.08: "" "1" "12" "" "a" "123" "12" "a1.5" "a0.333333" "a1e+100" "a0+1i" "aTrue" "a1 2" "a1 2" "ax\t1" "a1\t2" Str "42" "1" "1 2"
say do { my @w; CONTROL { when CX::Warn { @w.push(.message.lines[0]); .resume } }; ("a" ~ Nil).raku ~ " " ~ ("a" ~ Str).raku ~ " " ~ ("a" ~ Int).raku ~ " " ~ ("a" ~ Any).raku ~ " " ~ ("a" ~ Mu).raku ~ " " ~ (~Nil).raku ~ " " ~ (~Str).raku ~ " " ~ (Str ~ Str).raku ~ " | " ~ @w.elems ~ " | " ~ @w.unique.join("/") }
# rakudo 2026.08: "a" "a" "a" "a" "a" "" "" "" | 9 | Use of Nil in string context/Use of uninitialized value of type Str in string context./Use of uninitialized value of type Int in string context./Use of uninitialized value of type Any in string context./Use of uninitialized value of type Mu in string context.
```
rakupp 4.0.1: differs — `[~] 1` is the Int 1 and `infix:<~>()` is Any;
the warnings are not resumable.

### ST-14  Repetition                                              D:partial R:yes V:spec
The count goes through `.Int`: negative and zero give `""`, `2.9` gives 2,
`0.5` gives `""`, `True` is 1 and `False` 0, `"2"` and `<2>` are 2, a
Range or List numifies (`(1, 2)` counts 2). Inf throws `X::NYI`; NaN and
`-Inf` throw `X::Numeric::CannotConvert`; a non-numeric string throws
`X::Str::Numeric`; a type object or Nil counts 0 with a "numeric context"
warning. `[x] ()` throws `X::NoZeroArgMeaning`; `[x] "a"` is `"a"`. Counts
that would exceed 2³²−1 graphemes throw `X::AdHoc` at once (`2**70`,
`1e10`, `2**31` on a two-character string). Roast `S03-operators/repeat.t`
asserts the negative, fractional, type-object and NaN cases.
```
sub f(&c) { my \r = try c(); $! ?? "T(" ~ $!.^name ~ ")" !! r ~~ Failure ?? "F(" ~ r.exception.^name ~ ")" !! r.raku }; say f({"ab" x 3}), " ", f({("ab" x 3).^name}), " ", f({"ab" x 0}), " ", f({"ab" x -1}), " ", f({"" x 5}), " ", f({"ab" x 2.9}), " ", f({"ab" x 0.5}), " ", f({"ab" x 3/2}), " ", f({"ab" x 2e0}), " ", f({"ab" x True}), " ", f({"ab" x False}), " ", f({"ab" x "2"}), " ", f({"ab" x <2>}), " ", f({"ab" x (1..2)}), " ", f({"ab" x (1, 2)}), " ", f({"ab" x Inf}), " ", f({"ab" x -Inf}), " ", f({"ab" x NaN}), " ", f({"ab" x "x"}), " ", f({"ab" x Str.new}), " ", f({42 x 2}), " ", f({<42> x 2}), " ", f({(1,2) x 2}), " ", f({("é" x 2).chars}), " ", f({[x] ()}), " ", f({[x] "a"}), " ", f({[x] "a", 2, 2}), " ", f({("ab" x 2**70).chars}), " ", f({"ab" x -2**70}), " ", f({"" x 2**70}), " ", f({("a" x 2**20).chars})
# rakudo 2026.08: "ababab" "Str" "" "" "" "abab" "" "ab" "abab" "ab" "" "abab" "abab" "abab" "abab" T(X::NYI) T(X::Numeric::CannotConvert) T(X::Numeric::CannotConvert) T(X::Str::Numeric) "" "4242" "4242" "1 21 2" 2 T(X::NoZeroArgMeaning) "a" "aaaa" T(X::AdHoc) T(X::AdHoc) T(X::AdHoc) 1048576
say do { my @w; CONTROL { when CX::Warn { @w.push(.message.lines[0]); .resume } }; ("ab" x Int).raku ~ " " ~ ("ab" x Nil).raku ~ " " ~ ("ab" x Any).raku ~ " " ~ (Str x 2).raku ~ " | " ~ @w.elems ~ " | " ~ @w.unique.join("/") }
# rakudo 2026.08: "" "" "" "" | 4 | Use of uninitialized value of type Int in numeric context/Use of Nil in numeric context/Use of uninitialized value of type Any in numeric context/Use of uninitialized value of type Str in string context.
say (try ("ab" x 1e10).chars) // $!.^name, " ", (try ("ab" x 2**31).chars) // $!.^name
# rakudo 2026.08: X::AdHoc X::AdHoc
```
rakupp 4.0.1: differs — a List count gives `""`, Inf and the huge counts
throw `X::Numeric::CannotConvert`, the warnings are not resumable, and
`"ab" x 1e10` tries to allocate the string (killed by the alarm).

## B. Comparison and bitwise

### ST-15  String comparison is by codepoint                       D:yes R:yes V:spec
`eq ne lt le gt ge` with zero or one operand are True. `cmp`/`leg`
compare codepoint by codepoint (`"a" gt "B"`, `"Z" lt "a"`, `"é" gt
"z"`), a prefix sorts first, and NFG makes `"e\x[301]"` `eq` and `===`
`"é"`. Numbers are stringified for `eq`/`cmp` (`1.0 eq "1"` is True,
`"10" cmp 9` is Less). `eqv` and `===` need the same type: `1 eqv "1"`,
`"1" eqv <1>` and `"a" === Str` are False; `Str eqv Str` is True.
```
say ([eq] ()), " ", ([eq] "a"), " ", ([ne] ()), " ", ([lt] ()), " ", ([ge] "a"), " ", ("a" cmp "B"), " ", ("a" lt "B"), " ", ("A" lt "a"), " ", ("a" leg "B"), " ", ("abc" cmp "abd"), " ", ("a" cmp "ab"), " ", ("" cmp "a"), " ", ("é" cmp "z"), " ", ("ａ" cmp "b"), " ", ("\x[1F600]" cmp "\x[FFFF]"), " ", ("x\x[301]" cmp "x"), " ", ("x\x[301]" cmp "xa"), " ", ("e\x[301]" cmp "é"), " ", ("e\x[301]" eq "é"), " ", ("e\x[301]" === "é"), " ", (1 eq "1"), " ", (1.0 eq "1"), " ", ("1" == 1), " ", ("1.0" == 1), " ", (1 lt 2), " ", (10 lt 9), " ", ("a" cmp 1), " ", (1 cmp "a"), " ", ("10" cmp 9), " ", ("10" leg 9)
# rakudo 2026.08: True True True True True More False True More Less Less Less More More More More More Same True True True True True True True True More Less Less Less
say ("a" eqv "a"), " ", ("a" === "a"), " ", (1 eqv "1"), " ", ("1" eqv <1>), " ", (<1> eqv "1"), " ", ("a" === Str), " ", (Str === Str), " ", ("a" eqv Str), " ", (Str eqv Str), " ", ("" eqv ""), " ", ("é" eqv "e\x[301]"), " ", (Str.new eqv ""), " ", (Str.new === ""), " ", ("a".WHICH eqv "a".WHICH)
# rakudo 2026.08: True True False False False False True False True True True True True True
```
rakupp 4.0.1: matches.

### ST-16  String bitwise operators                                D:no R:yes V:spec
`~|`, `~&`, `~^` combine codepoint by codepoint. `~|` and `~^` keep the
tail of the longer operand; `~&` truncates to the shorter (`"a" ~& ""` is
`""`, `"a" ~| ""` is `"a"`); xor of equal strings is NUL characters (chars
are kept). The result is a Str even for Int operands (`3 ~| 4` is `"7"`).
`[~|] ()` and `[~^] ()` are `""`; `[~&] ()` is an `X::NoZeroArgMeaning`
Failure; with one operand each returns it. Prefix `~^` and the shifts
`~<`, `~>` throw `X::NYI`. Roast `S03-operators/bit.t` asserts the
truncation and no-truncation rules.
```
sub f(&c) { my \r = try c(); $! ?? "T(" ~ $!.^name ~ ")" !! r ~~ Failure ?? "F(" ~ r.exception.^name ~ ")" !! r.raku }; say f({"ab" ~| "  "}), " ", f({"ab" ~& "  "}), " ", f({"ab" ~^ "  "}), " ", f({"abc" ~| "A"}), " ", f({"abc" ~& "A"}), " ", f({"abc" ~^ "A"}), " ", f({"ab" ~| "abc"}), " ", f({"abc" ~& "ab"}), " ", f({"abc" ~^ "ab"}), " ", f({"a" ~| ""}), " ", f({"a" ~& ""}), " ", f({"a" ~^ ""}), " ", f({"" ~| ""}), " ", f({"ab" ~^ "ab"}), " ", f({("ab" ~^ "ab").chars}), " ", f({"é" ~^ "é"}), " ", f({"\x[100]" ~| "\x[1]"}), " ", f({("a" ~| "\x[1F600]").ords.List}), " ", f({"\x[1F600]" ~& "\x[1F600]"}), " ", f({3 ~| 4}), " ", f({(3 ~| 4).^name}), " ", f({(1 ~| 2).^name}), " ", f({[~|] ()}), " ", f({[~^] ()}), " ", f({[~&] ()}), " ", f({[~|] "a"}), " ", f({[~&] "a"}), " ", f({[~^] "a"}), " ", f({~^"a"}), " ", f({"a" ~< 1}), " ", f({"a" ~> 1})
# rakudo 2026.08: "ab" "  " "AB" "abc" "A" " bc" "abc" "ab" "\0\0c" "a" "" "a" "" "\0\0" 2 "\0" "ā" (128609,) "😀" "7" "Str" "Str" "" "" T(X::NoZeroArgMeaning) "a" "a" "a" T(X::NYI) T(X::NYI) T(X::NYI)
```
rakupp 4.0.1: differs — the zero-argument forms return Any and the shifts
are `X::Multi::NoMatch`.

### ST-17  succ and pred                                           D:yes R:partial V:spec/quirk
`succ` increments the last run of letters or digits that is not immediately
preceded by a dot, carrying within a range (`"Az"` → `"Ba"`, `"zz"` →
`"aaa"`, `"a9"` → `"b0"`, `"Zz9"` → `"AAa0"`), for Latin, Greek, Arabic-Indic
digits, circled and full-width digits alike. A run right after a dot is
skipped (`"a.9"` → `"b.9"`, `".9"` unchanged). If the string ends in
anything else (`"a-"`, `"a!"`, `"9-"`, a combining mark) nothing changes;
`""` stays `""`. Characters outside a known range (`é`, `ǅ`, `ぁ`) are
unchanged. **Quirk:** the Roman numeral `"Ⅻ"` becomes `"ⅠⅠ"`.
`pred` mirrors this and returns an `X::AdHoc` Failure on underflow: `"a"`,
`"aa"`, `"A"`, `"0"`, `"00"`, `"a0"`, `"A0"`, `"aA"`, `"a.0"`, `"0.5"`
(the run after the dot is skipped, the one before underflows). Roast
`S03-operators/autoincrement.t` asserts `'123.456'++`, `'zi'++` and that
`'A00'--` fails.
```
sub f(&c) { my \r = try c(); $! ?? "T(" ~ $!.^name ~ ")" !! r ~~ Failure ?? "F(" ~ r.exception.^name ~ ")" !! r.raku }; say f({"a".succ}), " ", f({"Az".succ}), " ", f({"zz".succ}), " ", f({"Zz".succ}), " ", f({"a9".succ}), " ", f({"z9".succ}), " ", f({"9".succ}), " ", f({"-9".succ}), " ", f({"aZ".succ}), " ", f({"9Z".succ}), " ", f({"α".succ}), " ", f({"ω".succ}), " ", f({"٩".succ}), " ", f({"⑨".succ}), " ", f({"\x[FF19]".succ}), " ", f({"Ⅻ".succ}), " ", f({"é".succ}), " ", f({"ǅ".succ}), " ", f({"ぁ".succ}), " ", f({"".succ}), " ", f({"a.b".succ}), " ", f({"12.34".succ}), " ", f({"a.9".succ}), " ", f({"9.9".succ}), " ", f({".9".succ}), " ", f({"a..9".succ}), " ", f({"img001.png".succ}), " ", f({"a-".succ}), " ", f({"a!".succ}), " ", f({"9-".succ}), " ", f({"ab.".succ}), " ", f({"ab.c".succ}), " ", f({"a b".succ}), " ", f({"a\nb".succ}), " ", f({"9\x[301]".succ}), " ", f({"a\x[301]".succ})
# rakudo 2026.08: "b" "Ba" "aaa" "AAa" "b0" "aa0" "10" "-10" "bA" "10A" "β" "αα" "١٠" "⑩" "１０" "ⅠⅠ" "é" "ǅ" "ぁ" "" "b.b" "13.34" "b.9" "10.9" ".9" "b..9" "img002.png" "a-" "a!" "9-" "ac." "ac.c" "a c" "a\nc" "9́" "á"
sub f(&c) { my \r = try c(); $! ?? "T(" ~ $!.^name ~ ")" !! r ~~ Failure ?? "F(" ~ r.exception.^name ~ ")" !! r.raku }; say f({"b".pred}), " ", f({"b0".pred}), " ", f({"ab".pred}), " ", f({"ba".pred}), " ", f({"ba0".pred}), " ", f({"Ba".pred}), " ", f({"10".pred}), " ", f({"20".pred}), " ", f({"1".pred}), " ", f({"9".pred}), " ", f({"z".pred}), " ", f({"Z".pred}), " ", f({"B".pred}), " ", f({"β".pred}), " ", f({"١٠".pred}), " ", f({"img002.png".pred}), " ", f({"1.0".pred}), " ", f({"a1".pred}), " ", f({"a0b".pred}), " ", f({"".pred}), " ", f({"-".pred}), " ", f({"a".pred}), " ", f({"aa".pred}), " ", f({"A".pred}), " ", f({"α".pred}), " ", f({"0".pred}), " ", f({"00".pred}), " ", f({"a0".pred}), " ", f({"A0".pred}), " ", f({"aA".pred}), " ", f({"Aa".pred}), " ", f({"0.0".pred}), " ", f({"0.5".pred}), " ", f({"a.0".pred}), " ", f({"a.a".pred}), " ", f({"x-0".pred}), " ", f({"0a".pred})
# rakudo 2026.08: "a" "a9" "aa" "az" "az9" "Az" "09" "19" "0" "8" "y" "Y" "A" "α" "٠٩" "img001.png" "0.0" "a0" "a0a" "" "-" T(X::AdHoc) T(X::AdHoc) T(X::AdHoc) T(X::AdHoc) T(X::AdHoc) T(X::AdHoc) T(X::AdHoc) T(X::AdHoc) T(X::AdHoc) T(X::AdHoc) T(X::AdHoc) T(X::AdHoc) T(X::AdHoc) T(X::AdHoc) T(X::AdHoc) T(X::AdHoc)
```
rakupp 4.0.1: differs — a trailing non-alphanumeric does not block the
increment (`"a-"` → `"b-"`, `"9-"` → `"10-"`, `"9\x[301]"` → `"10\x[301]"`),
and `"Ⅻ"` is left alone; `pred` matches everywhere.

## C. Case and shape

### ST-18  Case mapping is Unicode and grapheme-aware               D:yes R:yes V:spec
`tc` titlecases only the first grapheme (`ǆ` → `ǅ`, `ß` → `Ss`), `tclc`
lowercases the rest; `uc` expands (`ß` → `SS`, `ﬁ` → `FI`), `fc` folds
(`ß`, `ẞ` → `ss`, `ﬁ` → `fi`, `ǅ` → `ǆ`); `lc` of `İ` is `i` plus U+307,
final sigma is chosen (`ΣΑΣ` → `σας`); `K` (Kelvin) lowercases to `k`.
A mark after a base stays with it under `tc`; a leading lone mark is
skipped over. `flip` reverses graphemes, so a combining sequence, a ZWJ
family and `\r\n` stay intact. Cool invocants are stringified first.
```
say "ǆa".tc, " ", "ǆa".tclc, " ", "ǆǆ".tclc, " ", "a ǆ".tclc, " ", "ǆA".uc, " ", "ǅ".lc, " ", "ǅ".uc, " ", "ǅ".tc, " ", "ǅ".fc, " ", "ß".uc, " ", "ß".tc, " ", "ß".fc, " ", "ẞ".fc, " ", "aẞ".lc, " ", "ﬁ".uc, " ", "ﬁ".fc, " ", "İ".lc.ords, " ", "ı".uc, " ", "K".lc.ords, " ", "Å".lc, " ", "σς".uc, " ", "ΣΣ".lc, " ", "ΣΑΣ".lc, " ", "ΣΑΣ".tclc, " ", "ῼ".tc, " ", "ῼ".uc, " ", "ῳ".tc, " ", "ǰ".uc.ords, " ", "aBC".tclc, " ", "".tc.raku, " ", "a\x[301]bc".tc.ords, " ", "\x[301]abc".tc.ords, " ", "abc".flip, " ", "e\x[301]x".flip.ords, " ", "👨\x[200D]👩\x[200D]👧".flip.chars, " ", "\r\n".flip.ords, " ", 42.uc.^name, " ", uc("ab")
# rakudo 2026.08: ǅa ǅa ǅǆ A ǆ ǄA ǆ Ǆ ǅ ǆ SS Ss ss ss aß FI fi (105 775) I (107) å ΣΣ σς σας Σας ῼ ΩΙ ῼ (74 780) Abc "" (193 98 99) (769 97 98 99) cba (120 233) 1 (13 10) Str AB
```
rakupp 4.0.1: matches.

### ST-19  wordcase                                                D:yes R:partial V:spec/quirk
A word is a letter followed by word characters, and words are joined
through `'` and `-` (`don't`, `stop-me`, `o'brien` are single words); the
default filter is `tclc`. **Quirk:** a run that starts with a digit is not
a word, so the letters after the digit start one: `"3rd"` → `"3Rd"`,
`"1x"` → `"1X"`; `_` and `.` split words (`"a.b"` → `"A.B"`). `:filter`
replaces `tclc`; `:where` is smartmatched against each word (a regex, a
string, a type). Roast `capitalize.t` asserts the apostrophe and dash
cases only.
```
say "hello world".wordcase, " ", "don't stop-me now".wordcase, " ", "mary-jane o'brien".wordcase, " ", "MARY-JANE".wordcase, " ", "3rd place x2".wordcase, " ", "1x".wordcase, " ", "x1y2".wordcase, " ", "a1b c".wordcase, " ", "HELLO".wordcase, " ", "über straße".wordcase, " ", "ǆa".wordcase, " ", "π ω".wordcase, " ", "a_b c".wordcase, " ", "a.b c".wordcase, " ", "-a-".wordcase, " ", "a--b".wordcase, " ", "x'".wordcase, " ", "'ab".wordcase, " ", "-ab".wordcase, " ", "ab''cd".wordcase, " ", "ab-'cd".wordcase, " ", "a'b'c-d-e".wordcase, " ", "  hi  ".wordcase.raku, " ", "a\tb\nc".wordcase.raku, " ", "".wordcase.raku, " ", 42.wordcase, " ", wordcase("ab cd")
# rakudo 2026.08: Hello World Don't Stop-me Now Mary-jane O'brien Mary-jane 3Rd Place X2 1X X1y2 A1b C Hello Über Straße ǅa Π Ω A_b C A.B C -A- A--B X' 'Ab -Ab Ab''Cd Ab-'Cd A'b'c-d-e "  Hi  " "A\tB\nC" "" 42 Ab Cd
say "hello world".wordcase(:filter(&uc)), " ", "ab cd".wordcase(:filter({ "<$_>" })), " ", "ab cd".wordcase(:filter(-> $w { $w.flip })), " ", "have fun working on raku".wordcase(:where({ .chars > 3 })), " ", "a b".wordcase(:where(/^ \w $/)), " ", "ab cd".wordcase(:where("cd")), " ", "ab cd".wordcase(:where(Str)), " ", "ab cd".wordcase(:where(Int)), " ", "have fun working on raku".wordcase(:filter(&uc), :where({ .chars > 3 }))
# rakudo 2026.08: HELLO WORLD <ab> <cd> ba dc Have fun Working on Raku A B ab Cd Ab Cd ab cd HAVE fun WORKING on RAKU
```
rakupp 4.0.1: differs — words are split on whitespace only (`"3rd"`,
`"-a-"`, `"'ab"` keep their case, `"a.b"` is `"A.b"`), `ǆa` gets the
uppercase `Ǆ` rather than the titlecase, and `:filter`/`:where` are
ignored.

### ST-20  samecase is positional                                  D:yes R:yes V:spec
Character *i* of the result takes the case of character *i* of the
pattern: upper, lower, or unchanged when the pattern character has no case
(`_`, a digit, a space, a titlecase letter such as `ǅ`). When the pattern
is shorter, its last case applies to the rest; when longer, the excess is
ignored; an empty pattern returns the string unchanged. `ß` is a lowercase
pattern character and `ẞ` an uppercase one.
```
say "raKu".samecase("A_a_"), " ", "rAKU".samecase("Ab"), " ", "abcdef".samecase("Ab"), " ", "abcdef".samecase("A1"), " ", "ABCDEF".samecase("1"), " ", "ABCDEF".samecase("a1"), " ", "abc".samecase(" "), " ", "abc".samecase(""), " ", "".samecase("A").raku, " ", "abc".samecase("ABCDEFG"), " ", "ABCd".samecase("aB"), " ", "abcdef".samecase("aBcDeF"), " ", "bye bye".samecase("Hello World"), " ", "aÉ".samecase("Xx"), " ", "AbC".samecase("ǅ"), " ", "aa".samecase("ǅ"), " ", "abc".samecase("Ǆ"), " ", "abc".samecase("ǆ"), " ", "ǆa".samecase("A"), " ", "abc".samecase("ẞ"), " ", "ABC".samecase("ß"), " ", 123.samecase("A"), " ", samecase("abc", "A"), " ", "abc".samecase(42)
# rakudo 2026.08: Raku Raku Abcdef Abcdef ABCDEF aBCDEF abc abc "" ABC aBCD aBcDeF Bye byE Aé AbC aa ABC abc ǄA ABC abc 123 ABC abc
```
rakupp 4.0.1: differs — a titlecase pattern character uppercases (`"aa"`
with `"ǅ"` is `"AA"`), and `ß` does not lowercase.

### ST-21  samemark is positional                                  D:yes R:yes V:spec/quirk
Character *i* of the result keeps its base and takes the combining marks
of pattern character *i*; a pattern character without marks strips them;
when the pattern is shorter its last mark set applies to the rest (all
remaining characters get it); an empty pattern returns the string
unchanged. A character with no base (`ﬁ`, a digit, `ǅ`) still receives the
mark. **Quirk:** the `\r\n` grapheme is treated as base `\r` with the
`\n` as its mark, so `"\r\n".samemark("ä")` is `\r` plus U+308.
```
say 'åäö'.samemark('aäo'), " ", 'åäö'.samemark('a'), " ", samemark('Räku', 'a'), " ", samemark('aöä', ''), " ", "abc".samemark("ä"), " ", "abc".samemark("äo"), " ", "ábc".samemark("x"), " ", "ábc".samemark("x́"), " ", "".samemark("ä").raku, " ", "a".samemark("ậ").ords, " ", "abc".samemark("ậ").ords, " ", "abc".samemark("ậẹ").ords, " ", "a".samemark("a\x[301]\x[302]").ords, " ", "ab".samemark("\x[301]").ords, " ", "ab".samemark("\x[301]").chars, " ", "aé".samemark("x").ords, " ", "å".samemark("ä").ords, " ", "a\x[301]".samemark("e").ords, " ", "a".samemark("1").ords, " ", "ﬁ".samemark("ä").ords, " ", "ǅ".samemark("ä").ords, " ", "ab".samemark("ä ").ords, " ", "\r\n".samemark("ä").ords, " ", 42.samemark("ä").ords
# rakudo 2026.08: aäo aao Raku aöä äb̈c̈ äbc abc áb́ć "" (7853) (7853 7685 770 265 803) (7853 7685 99 803) (225 770) (97 98) 2 (97 101) (228) (97) (97) (64257 776) (453 776) (228 98) (13 776) (52 776 50 776)
```
rakupp 4.0.1: differs only on `\r\n`, which keeps both characters and
marks each.

### ST-22  samespace is word-by-word                               D:no R:partial V:spec
The inter-word whitespace of the invocant is replaced, run by run, by the
corresponding whitespace run of the pattern; when the pattern has fewer
runs the invocant's own remain; when the pattern has more, the extra are
dropped. Leading whitespace of the invocant counts as its first run (the
first "word" is empty); trailing whitespace is kept as is. This is the
engine behind `:ss` (ST-53), which Roast `S05-substitution/subst.t`
asserts.
```
say "a b c".samespace("x\ty\nz").raku, " ", "a b c".samespace("x\ty").raku, " ", "a b c d".samespace("x\ty").raku, " ", "a  b".samespace("x y z").raku, " ", "a   b".samespace("x\t\ty").raku, " ", "abc".samespace("x y").raku, " ", "a b".samespace("xy").raku, " ", "a b".samespace("").raku, " ", "a b".samespace(" x\ty").raku, " ", " a b".samespace("\tx\ty").raku, " ", " a b".samespace("x y").raku, " ", "a b ".samespace("x\ty\t").raku, " ", "a\nb".samespace("x y").raku, " ", "".samespace("x y").raku, " ", 12.samespace("x").raku
# rakudo 2026.08: "a\tb\nc" "a\tb c" "a\tb c d" "a b" "a\t\tb" "abc" "a b" "a b" "a b" "\ta\tb" " a b" "a\tb\t" "a b" "" "12"
```
rakupp 4.0.1: matches.

## D. Trimming

### ST-23  chomp removes one logical newline                        D:yes R:yes V:spec
Without a needle, one trailing newline grapheme is removed: `\n`, `\r`,
`\r\n` (one grapheme), U+85, U+0B, U+0C, U+2028, U+2029; not U+1E, and not
a newline that carries a combining mark. `"\n".chomp` and `"\r\n".chomp`
are `""`. With a needle, that exact suffix is removed once (`"abc\n\n"`
with `"\n"` leaves one `\n`); a needle longer than the string or an empty
needle leaves it unchanged; because `\r\n` is one grapheme, `"\n"` does
not match its end, and `"e"` does not match `é`. The needle must be a
defined Cool (`Str`, `Nil` → `X::Multi::NoMatch`).
```
say "abc\n".chomp.raku, " ", "abc\r".chomp.raku, " ", "abc\r\n".chomp.raku, " ", "abc\n\r".chomp.raku, " ", "abc\n\n".chomp.raku, " ", "abc\x[85]".chomp.raku, " ", "abc\x[0B]".chomp.raku, " ", "abc\x[0C]".chomp.raku, " ", "abc\x[2028]".chomp.raku, " ", "abc\x[2029]".chomp.raku, " ", "abc\x[1E]".chomp.raku, " ", "abc\n\x[301]".chomp.raku, " ", "\n".chomp.raku, " ", "\r\n".chomp.raku, " ", "".chomp.raku, " ", "abc".chomp.raku, " ", chomp("a\n").raku, " ", 123.chomp(3).raku
# rakudo 2026.08: "abc" "abc" "abc" "abc\n" "abc\n" "abc" "abc" "abc" "abc" "abc" "abc\x[1E]" "abc\n\x[301]" "" "" "" "abc" "a" "12"
say "abcdef".chomp("def").raku, " ", "abcdef".chomp("xyz").raku, " ", "abc".chomp("abcd").raku, " ", "abc".chomp("abc").raku, " ", "abc".chomp("").raku, " ", "abc\n".chomp("\n").raku, " ", "abc\r\n".chomp("\n").raku, " ", "abc\r\n".chomp("\r\n").raku, " ", "ab\r\n".chomp("\r").raku, " ", "abé".chomp("e").raku, " ", "abé".chomp("é").raku, " ", "abe\x[301]".chomp("e").raku, " ", "abc".chomp(<c>).raku, " ", "abc1".chomp(1).raku, " ", (try "abc".chomp(Str)) // $!.^name, " ", (try "abc".chomp(Nil)) // $!.^name
# rakudo 2026.08: "abc" "abcdef" "abc" "" "abc" "abc" "abc\r\n" "abc" "ab\r\n" "abé" "ab" "abé" "ab" "abc" X::Multi::NoMatch X::Multi::NoMatch
```
rakupp 4.0.1: differs — `"abc\r\n".chomp("\n")` removes the `\n` and leaves
`"abc\r"`.

### ST-24  chop                                                    D:yes R:yes V:spec
`chop` removes one grapheme (`"ae\x[301]"` → `"a"`, `"ab\r\n"` → `"ab"`);
`chop(n)` removes *n*; *n* at or beyond the length gives `""`; a negative
*n* leaves the string unchanged; a big integer (either sign) gives `""`.
Non-Int counts go through `.Int`: `1.9` and `"1.9"` are 1, `"2"` and `<2>`
are 2, `True` is 1, a Range of two elements is 2, Nil is 0 (with a
warning); Inf and NaN throw `X::Numeric::CannotConvert`, a non-numeric
string `X::Str::Numeric`, and the Int type object is
`X::Multi::Ambiguous`.
```
sub f(&c) { my \r = try c(); $! ?? "T(" ~ $!.^name ~ ")" !! r ~~ Failure ?? "F(" ~ r.exception.^name ~ ")" !! r.raku }; say f({"abc".chop}), " ", f({"".chop}), " ", f({"abc".chop(2)}), " ", f({"abc".chop(3)}), " ", f({"abc".chop(5)}), " ", f({"abc".chop(0)}), " ", f({"abc".chop(-1)}), " ", f({"abc".chop(-5)}), " ", f({"abc".chop(1.9)}), " ", f({"abc".chop("2")}), " ", f({"abc".chop("1.9")}), " ", f({"abc".chop(<2>)}), " ", f({"abc".chop(2e0)}), " ", f({"abc".chop(True)}), " ", f({"abc".chop(1..2)}), " ", f({"abc".chop(2**70)}), " ", f({"abc".chop(-2**70)}), " ", f({"abc".chop(Inf)}), " ", f({"abc".chop(NaN)}), " ", f({"abc".chop("x")}), " ", f({"abc".chop(Int)}), " ", f({"abc".chop(Nil)}), " ", f({"ab\r\n".chop}), " ", f({"aé".chop}), " ", f({"ae\x[301]".chop}), " ", f({chop("abc", 2)}), " ", f({chop("abc", "2")}), " ", f({1234.chop}), " ", f({1234.chop(2)})
# rakudo 2026.08: "ab" "" "a" "" "" "abc" "abc" "abc" "ab" "a" "ab" "a" "a" "ab" "a" "" "" T(X::Numeric::CannotConvert) T(X::Numeric::CannotConvert) T(X::Str::Numeric) T(X::Multi::Ambiguous) "abc" "ab" "a" "a" "a" "a" "123" "12"
```
rakupp 4.0.1: differs — a Range, `-2**70`, Inf, NaN, `"x"` and the Int
type object all leave the string unchanged.

### ST-25  trim, trim-leading, trim-trailing                       D:yes R:yes V:spec
Whitespace is Unicode `White_Space`: the ASCII set, U+0B, U+0C, U+85,
U+A0, U+1680, U+2000–U+200A, U+2028, U+2029, U+202F, U+205F, U+3000 — not
U+200B, U+FEFF, U+180E or the C0 separators U+1C–U+1F. A combining mark
after a space forms a grapheme with it and is trimmed with it. Cool
invocants are stringified; the type object is `X::Multi::NoMatch`.
```
say "  a b  ".trim.raku, " ", "  a b  ".trim-leading.raku, " ", "  a b  ".trim-trailing.raku, " ", "   ".trim.raku, " ", "".trim.raku, " ", "a".trim.raku, " ", "\t\n a \r\n".trim.raku, " ", "\x[0B]a\x[0C]".trim.raku, " ", "\x[85]a\x[2028]".trim.raku, " ", "\x[A0]a\x[A0]".trim.raku, " ", "\x[1680]a\x[2000]".trim.raku, " ", "\x[200A]a\x[2009]".trim.raku, " ", "\x[202F]a\x[2007]".trim.raku, " ", "\x[2029]a\x[205F]".trim.raku, " ", "\x[3000]a\x[2003]".trim.raku, " ", "\x[200B]a".trim.raku, " ", "\x[FEFF]a".trim.raku, " ", "\x[180E]a".trim.raku, " ", "\x[1C]a\x[1F]".trim.raku, " ", " \x[301]a".trim.raku, " ", "a \x[301]".trim.raku, " ", 42.trim.raku, " ", trim("  x ").raku, " ", (try trim(Str)) // $!.^name
# rakudo 2026.08: "a b" "a b  " "  a b" "" "" "a" "a" "a" "a" "a" "a" "a" "a" "a" "a" "​a" "﻿a" "᠎a" "\x[1C]a\x[1F]" "a" "a" "42" "x" X::Multi::NoMatch
```
rakupp 4.0.1: differs — a space carrying a combining mark is not trimmed
(the mark survives), and `trim(Str)` is `""`.

## E. Searching

### ST-26  index                                                   D:yes R:yes V:spec
Returns the first position at or after `$pos`, or Nil (not −1; declared
`--> Int:D` all the same). The empty needle is found at `$pos`, including
`$pos == chars`, and not beyond. A needle list finds the smallest position
of any element (`<c b>` on `"abc"` is 1); an empty list is Nil; an empty
string element is found at 0. `$pos` is coerced with `.Int` (`1.9`,
`"1"`). `:i` folds case and `:m` ignores marks (ST-33). Roast
`S32-str/index.t` asserts the empty-needle and needle-list rules.
```
say "abcabc".index("b").raku, " ", "abcabc".index("b", 2).raku, " ", "abcabc".index("x").raku, " ", "abc".index("abcd").raku, " ", "".index("a").raku, " ", "abc".index("").raku, " ", "abc".index("", 2).raku, " ", "abc".index("", 3).raku, " ", "abc".index("", 4).raku, " ", "".index("").raku, " ", "".index("", 1).raku, " ", "abc".index("a", 3).raku, " ", "abc".index("a", 10).raku, " ", "abc".index("b", 1.9).raku, " ", "abc".index("b", "1").raku, " ", 12345.index(3).raku, " ", index("abc", "c", 1).raku, " ", "abc".index("b").^name, " ", "aBc".index("b", :i).raku, " ", "abc".index("B", :!i).raku, " ", "résumé".index("e", :m).raku, " ", "abc".index("É", :i, :m).raku, " ", "aBc".index("b", 2, :i).raku
# rakudo 2026.08: 1 4 Nil Nil Nil 0 2 3 Nil 0 Nil Nil Nil 1 1 2 2 Int 1 Nil 1 Nil Nil
say "abc".index(<c b>).raku, " ", "ab".index(<b a>).raku, " ", "abc".index(<x y>).raku, " ", "abc".index(()).raku, " ", "abc".index(("",)).raku, " ", "a1b".index((1, "b")).raku, " ", "abcabc".index(("c", "B")).raku, " ", "abcabc".index(("c", "B"), :i).raku, " ", "abc".index(<c b>, 2).raku, " ", "abc".index(<c b>, 0).raku, " ", "a b".index(<a b>, 0).raku, " ", "abc".index(("c", "b"), 1).raku
# rakudo 2026.08: 1 0 Nil Nil 0 1 2 1 Nil Nil 0 Nil
```
rakupp 4.0.2: differs only in the ORDER a needle list is searched in; the positions and the refusals match.
list and a position it searches the list (`<c b>, 2` is 2).

### ST-27  Positions: out of range versus past the end             D:partial R:yes V:spec/bug
A negative position, or one above 2⁶³, on `index`, `rindex`, `indices`,
`contains` (Str or Regex needle) and `substr-eq` is a **returned**
`X::OutOfRange` Failure (Roast asserts `fails-like`) with `what` naming the
method, `got` the position and `range` `0..chars`; `-0.5` truncates to 0
and is fine. A position past the end is not an error: `index` and `rindex`
give Nil, `contains` and `substr-eq` False, `indices` `()` — except three
**bugs**: (1) `rindex` with a non-empty needle and a position at or past
the end throws `X::AdHoc` (`"abc".rindex("c", 3)`), while Roast
`rindex.t` asserts the empty-needle case at 999 is undefined; (2) `index`
with a needle *list* and a position stringifies the list into one needle
(`"abc".index(<c b>, 2)` is Nil where the docs promise a list search);
(3) `rindex` with a needle list and a position recurses without end
(killed by the alarm). A type object or a Regex as needle, or a Callable
or a third positional, is `X::Multi::NoMatch`.
```
sub f(&c) { my \r = try c(); $! ?? "T(" ~ $!.^name ~ ")" !! r ~~ Failure ?? "F(" ~ r.exception.^name ~ ";" ~ (r.exception.?what // "-") ~ ";" ~ (r.exception.?got // "-") ~ ";" ~ (r.exception.?range // "-") ~ ")" !! r.raku }; say f({"abc".index("b", -1)}), " ", f({"abc".index("b", 2**70)}), " ", f({"abc".index("b", "-1")}), " ", f({"abc".index("b", -0.5)}), " ", f({"abc".index("b", -1, :i)}), " ", f({"abc".index(<a b>, -1)}), " ", f({"abc".rindex("b", -1)}), " ", f({"abc".rindex("b", 2**70)}), " ", f({"abc".indices("b", -1)}), " ", f({"abc".indices("b", 2**70)}), " ", f({"abc".contains("b", -1)}), " ", f({"abc".contains(/b/, -1)}), " ", f({"abc".contains("b", 2**70)}), " ", f({"abc".substr-eq("b", -1)}), " ", f({"abc".substr-eq("b", 2**70)})
# rakudo 2026.08: T(X::OutOfRange) T(X::OutOfRange) T(X::OutOfRange) 1 T(X::OutOfRange) T(X::OutOfRange) T(X::OutOfRange) T(X::OutOfRange) T(X::OutOfRange) T(X::OutOfRange) T(X::OutOfRange) T(X::OutOfRange) T(X::OutOfRange) T(X::OutOfRange) T(X::OutOfRange)
sub f(&c) { my \r = try c(); $! ?? "T(" ~ $!.^name ~ ")" !! r ~~ Failure ?? "F(" ~ r.exception.^name ~ ")" !! r.raku }; say f({"abc".index("b", 3)}), " ", f({"abc".index("b", 4)}), " ", f({"abc".index("", 4)}), " ", f({"abc".contains("", 4)}), " ", f({"abc".contains("b", 4)}), " ", f({"abc".contains(/./, 4)}), " ", f({"abc".indices("b", 4)}), " ", f({"abc".indices("", 4)}), " ", f({"abc".substr-eq("", 4)}), " ", f({"abc".substr-eq("c", 3)}), " ", f({"abc".rindex("", 4)}), " ", f({"abc".rindex("c", 3)}), " ", f({"abc".rindex("c", 4)}), " ", f({"abc".rindex("c", 100)}), " ", f({"".rindex("a", 5)}), " ", f({"abc".index(Str)}), " ", f({"abc".index("b", Int)}), " ", f({"abc".index(/b/)}), " ", f({"abc".rindex(/b/)}), " ", f({"abc".indices(/b/)}), " ", f({"abc".index("b", *-1)}), " ", f({"abc".index("b", 1, 2)})
# rakudo 2026.08: Nil Nil Nil Bool::False Bool::False Bool::False () () Bool::False Bool::False Nil T(X::AdHoc) T(X::AdHoc) T(X::AdHoc) Nil T(X::Multi::NoMatch) T(X::Multi::NoMatch) T(X::Multi::NoMatch) T(X::Multi::NoMatch) T(X::Multi::NoMatch) T(X::Multi::NoMatch) T(X::Multi::NoMatch)
say (try "abc".rindex(<a b>, 1)) // $!.^name
# rakudo 2026.08: (no output; killed by the 10 s alarm)
```
rakupp 4.0.2: differs — a negative or overflowing position is the Failure it should be, for `index`, `rindex`, `indices`, `contains` and `substr-eq` alike, and past the end each answers its own "not found". The three Rakudo BUGS are deliberately not imitated: `rindex` past the end is Nil here, not `X::AdHoc`, and the needle-list forms search the list instead of stringifying it or recursing. A Regex needle is still accepted where Rakudo has no candidate.
`indices("b", -1)` search from 0 instead of failing; `contains("b",
2**70)` is False; past the end `contains("", 4)` is True and `rindex("c",
100)` is 2 (the sane reading of the Rakudo bug); a Regex needle is
accepted by `index`/`rindex`/`indices`; the needle-list-with-position
forms search the list (ST-26); `index(Str)` is
`X::TypeCheck::Binding::Parameter`.

### ST-28  rindex                                                  D:yes R:yes V:spec/quirk
Returns the last position at or before `$pos` where the needle starts
(`"abcabc".rindex("b", 3)` is 1, `"abc".rindex("bc", 1)` is 1). The empty
needle is found at `chars`, or at `$pos`; on `""` it is 0 but
`"".rindex("", 1)` is Nil. A needle list finds the largest position. The
position is `.Int`-coerced. **Quirk:** `:i` and `:m` are accepted and
silently ignored (`"aBc".rindex("b", :i)` is Nil). Roast `rindex.t`
asserts the empty-needle and list rules.
```
sub f(&c) { my \r = try c(); $! ?? "T(" ~ $!.^name ~ ")" !! r ~~ Failure ?? "F(" ~ r.exception.^name ~ ")" !! r.raku }; say f({"abcabc".rindex("b")}), " ", f({"abcabc".rindex("b", 4)}), " ", f({"abcabc".rindex("b", 3)}), " ", f({"abcabc".rindex("b", 0)}), " ", f({"abcabc".rindex("x")}), " ", f({"abc".rindex("abcd")}), " ", f({"abc".rindex("bc", 1)}), " ", f({"abc".rindex("bc", 0)}), " ", f({"abc".rindex("bc", 2)}), " ", f({"abcabc".rindex("bc", 3)}), " ", f({"abc".rindex("")}), " ", f({"abc".rindex("", 1)}), " ", f({"abc".rindex("", 3)}), " ", f({"".rindex("")}), " ", f({"".rindex("", 1)}), " ", f({"abc".rindex("c", 2.9)}), " ", f({"abc".rindex("c", "2")}), " ", f({12321.rindex(2)}), " ", f({rindex("abcabc", "b", 3)}), " ", f({"aBc".rindex("b", :i)}), " ", f({"aardvark".rindex(<d v k>)}), " ", f({"abcabc".rindex(<abc b>)}), " ", f({"abc".rindex(<x y>)}), " ", f({"abc".rindex(())}), " ", f({"abc".rindex(("",))}), " ", f({"a1b".rindex((1, "b"))})
# rakudo 2026.08: 4 4 1 Nil Nil Nil 1 Nil 1 1 3 1 3 0 Nil 2 2 3 1 Nil 7 4 Nil Nil 3 2
```
rakupp 4.0.2: differs only in refusing a Regex needle, which Rakudo has no candidate for; the positions, the empty needle and the past-the-end Nil match.

### ST-29  indices                                                 D:yes R:yes V:spec
A List of every non-overlapping start (`:overlap` steps one character
after each hit instead); the empty needle is found at every position from
`$pos` to `chars` inclusive; a miss is `()`; a start beyond the end is
`()`; `:i`/`:m` as in ST-33. Cool invocants are stringified (`12121` has
`1` at 0, 2, 4). Roast `indices.t` asserts the empty-needle positions.
```
say "banana".indices("a").raku, " ", "banana".indices("ana").raku, " ", "banana".indices("ana", :overlap).raku, " ", "banana".indices("ana", 2).raku, " ", "aaaa".indices("aa").raku, " ", "aaaa".indices("aa", :overlap).raku, " ", "aaaa".indices("aa", 1).raku, " ", "aaaa".indices("aa", 1, :overlap).raku, " ", "abc".indices("").raku, " ", "abc".indices("", :overlap).raku, " ", "abc".indices("", 2).raku, " ", "abc".indices("", 3).raku, " ", "abc".indices("", 5).raku, " ", "".indices("").raku, " ", "abc".indices("x").raku, " ", "abc".indices("abcd").raku, " ", "abc".indices("a", 5).raku, " ", "abc".indices("b", 3).raku, " ", "abc".indices("c", 2).raku, " ", "abc".indices("b", 1.5).raku, " ", "abc".indices("b", "1").raku, " ", "abc".indices("b").^name, " ", "abc".indices("x").^name, " ", "banAna".indices("a", :i).raku, " ", "tête-à-tête".indices("te", :m).raku, " ", "ÀAàa".indices("a", :i).raku, " ", "ÀAàa".indices("a", :m).raku, " ", "ÀAàa".indices("a", :i, :m).raku, " ", indices("banana", "a", 2).raku, " ", (try 12121.indices(1).raku) // $!.^name, " ", (try "abc".indices(<a b>).raku) // $!.^name
# rakudo 2026.08: (1, 3, 5) (1,) (1, 3) (3,) (0, 2) (0, 1, 2) (1,) (1, 2) (0, 1, 2, 3) (0, 1, 2, 3) (2, 3) (3,) () (0,) () () () () (2,) (1,) (1,) List List (1, 3, 5) (0, 2, 7, 9) (1, 3) (2, 3) (0, 1, 2, 3) (3, 5) (0, 2, 4) ()
```
rakupp 4.0.2: matches, including the empty needle at every position.
`indices` (`X::Method::NotFound`).

### ST-30  contains                                                D:yes R:yes V:spec/quirk
A Str needle is found at or after `$pos`; the empty needle is found at any
`$pos` up to `chars` and not beyond; Cool needles and positions are
coerced (`(5, "10.0")`); a Junction needle autothreads; a type object is
`X::Multi::NoMatch`. A Regex needle is tried from `$pos` onwards.
**Quirk:** with a position, a regex is only tried while `$pos < chars`,
so a match that can only succeed at the very end is missed:
`"abc".contains(/$/, 3)`, `"abc".contains(/<?>/, 3)` and even
`"".contains(/^$/, 0)` are False while `"".contains(/^$/)` is True.
```
say "abc".contains("b").raku, " ", "abc".contains("abcd").raku, " ", "abc".contains("").raku, " ", "".contains("").raku, " ", "abc".contains("", 3).raku, " ", "abc".contains("", 4).raku, " ", "abc".contains("c", 2).raku, " ", "abc".contains("c", 3).raku, " ", "abc".contains("b", 1.9).raku, " ", "abc".contains("b", 1, :i).raku, " ", "abc".contains("B", :i).raku, " ", "abc".contains("ä", :m).raku, " ", "abc".contains("Ä", :i).raku, " ", "abc".contains("Ä", :i, :m).raku, " ", 12345.contains(34).raku, " ", "12".contains(1.0).raku, " ", "Hello, 12345".contains(5, "10.0").raku, " ", "abc".contains(<b>).raku, " ", "abc".contains(<x b>).raku, " ", "a b".contains(<a b>).raku, " ", "abc".contains(any <x b>).raku, " ", (try "abc".contains(Str)) // $!.^name, " ", (try "abc".contains(Any)) // $!.^name
# rakudo 2026.08: Bool::True Bool::False Bool::True Bool::True Bool::True Bool::False Bool::True Bool::False Bool::True Bool::True Bool::True Bool::True Bool::False Bool::True Bool::True Bool::True Bool::True Bool::True Bool::False Bool::True any(Bool::False, Bool::True) X::Multi::NoMatch X::Multi::NoMatch
say "abc".contains(/c/).raku, " ", "abc".contains(/bc/).raku, " ", "abc".contains(/:i B/).raku, " ", "abc".contains(/B/, 0).raku, " ", "abc".contains(/c/, 2).raku, " ", "abc".contains(/c/, 3).raku, " ", "abc".contains(/b/, 1).raku, " ", "abc".contains(/b/, 2).raku, " ", "abc".contains(/^/, 0).raku, " ", "abc".contains(/^/, 1).raku, " ", "abc".contains(/^^/).raku, " ", "abc".contains(/$/, 2).raku, " ", "abc".contains(/$/, 3).raku, " ", "abc".contains(/<?>/, 3).raku, " ", "abc".contains(/./, 3).raku, " ", "".contains(/^$/).raku, " ", "".contains(/^$/, 0).raku, " ", "".contains("", 1).raku
# rakudo 2026.08: Bool::True Bool::True Bool::True Bool::False Bool::True Bool::False Bool::True Bool::False Bool::True Bool::False Bool::True Bool::True Bool::False Bool::False Bool::False Bool::True Bool::False Bool::False
```
rakupp 4.0.2: differs — the position rules match; a Callable or a third positional is `X::TypeCheck::Binding::Parameter` where Rakudo says `X::Multi::NoMatch`, and `:ignoremark` is not applied to a Regex needle.
(`contains("", 4)`), the end-of-string regex cases are True, and a
type-object needle is `X::TypeCheck::Binding::Parameter`.

### ST-31  starts-with and ends-with                               D:yes R:yes V:spec/quirk
The empty needle always matches; a needle longer than the string never
does; Cool needles are stringified (`12345.starts-with(12)`). `:i` folds
case at the compared position and `:m` compares base characters, so
`"e\x[301]".starts-with("e", :m)` is True and without `:m` False.
**Quirk:** `ends-with` computes the compare position as `chars − needle
chars`, so a fold that changes the grapheme count never matches:
`"straße".ends-with("SSE", :i)` and `"STRASSE".ends-with("ße", :i)` are
False, while `starts-with` folds fully (`"ﬁ".starts-with("fi", :i)` and
`"fi".starts-with("ﬁ", :i)` are True; `"ﬁ".starts-with("f", :i)` is
False). A Regex needle, a type object or a position argument is
`X::Multi::NoMatch`.
```
say "abc".starts-with("ab"), " ", "abc".starts-with("abc"), " ", "abc".starts-with("abcd"), " ", "abc".starts-with(""), " ", "".starts-with(""), " ", "abc".ends-with("bc"), " ", "abc".ends-with(""), " ", "abc".ends-with("abcd"), " ", "abc".starts-with("b", :i), " ", "ABC".starts-with("ab", :i), " ", "ABC".ends-with("bc", :ignorecase), " ", "abc".starts-with("B", :!i), " ", "ABC".starts-with("ab", :i, :!m), " ", "äbc".starts-with("a", :m), " ", "abc".starts-with("ä", :m), " ", "abc".starts-with("ä", :!m), " ", "abç".ends-with("c", :m), " ", "abç".ends-with("C", :i, :m), " ", "abc".ends-with("ç", :m), " ", "e\x[301]".starts-with("e"), " ", "e\x[301]".starts-with("e", :m), " ", "K".starts-with("k", :i), " ", "ﬁ".starts-with("fi", :i), " ", "fi".starts-with("ﬁ", :i), " ", "ﬁ".starts-with("f", :i), " ", "straße".ends-with("SSE", :i), " ", "STRASSE".ends-with("ße", :i), " ", 12345.starts-with(12), " ", "12".starts-with(1.0), " ", "abc".starts-with(<ab>), " ", "abc".starts-with(<x ab>), " ", "a b".starts-with(<a b>), " ", "abc".starts-with(any <x ab>), " ", "abc".starts-with("ab").^name, " ", (try "abc".starts-with(Str)) // $!.^name, " ", (try "abc".starts-with(/a/)) // $!.^name, " ", (try "abc".ends-with("bc", 1)) // $!.^name
# rakudo 2026.08: True True False True True True True False False True True False True True True False True True True False True True True True False False False True True True False True any(False, True) Bool X::Multi::NoMatch X::Multi::NoMatch X::Multi::NoMatch
```
rakupp 4.0.1: differs — `K`/`k` and `ﬁ`/`fi` do not fold under `:i`, a
Regex needle is accepted, `ends-with("bc", 1)` takes a position, and a
type-object needle is `X::TypeCheck::Binding::Parameter`.

### ST-32  substr-eq                                               D:yes R:yes V:spec
True when the needle occurs exactly at `$pos`; without `$pos` it is
`starts-with` (with the same adverbs). A position at or past the end is
False for a non-empty needle and True for the empty needle only at
`$pos == chars`; a Callable position receives the length
(`*-3`, `{ $_ - 3 }`); Cool positions are `.Int`-coerced (`3.9`, `"1"`,
`<3>`); Cool invocants are stringified (`342.substr-eq(42, 1)`). A
non-numeric string position throws `X::Str::Numeric`; a type object
either way is `X::Multi::NoMatch`.
```
sub f(&c) { my \r = try c(); $! ?? "T(" ~ $!.^name ~ ")" !! r ~~ Failure ?? "F(" ~ r.exception.^name ~ ")" !! r.raku }; say f({"foobar".substr-eq("bar", 3)}), " ", f({"foobar".substr-eq("barz", 3)}), " ", f({"foobar".substr-eq("foo", 0)}), " ", f({"foobar".substr-eq("foo")}), " ", f({"foobar".substr-eq("FOO", :i)}), " ", f({"foobar".substr-eq("r", 5)}), " ", f({"foobar".substr-eq("bar", 6)}), " ", f({"foobar".substr-eq("bar", 7)}), " ", f({"foobar".substr-eq("bar", 10)}), " ", f({"foobar".substr-eq("", 0)}), " ", f({"foobar".substr-eq("", 6)}), " ", f({"foobar".substr-eq("", 7)}), " ", f({"".substr-eq("", 0)}), " ", f({"".substr-eq("", 1)}), " ", f({"foobar".substr-eq("bar", *-3)}), " ", f({"foobar".substr-eq("bar", { $_ - 3 })}), " ", f({"foobar".substr-eq("BAR", *-3, :i)}), " ", f({"foobar".substr-eq("Bar", 3, :i)}), " ", f({"foobar".substr-eq("bar", 3, :!i)}), " ", f({"cliché".substr-eq("che", 3, :m)}), " ", f({"föobar".substr-eq("fo", :m)}), " ", f({"foobar".substr-eq("oo", 1, :i, :m)}), " ", f({"foobar".substr-eq("bar", 3.9)}), " ", f({"foobar".substr-eq("bar", <3>)}), " ", f({"foobar".substr-eq(<bar>, 3)}), " ", f({342.substr-eq(42, 1)}), " ", f({342.substr-eq(42, "1")}), " ", f({"foobar".substr-eq("bar", 3).^name}), " ", f({"foobar".substr-eq("bar", "x")}), " ", f({"foobar".substr-eq("bar", Int)}), " ", f({"foobar".substr-eq(Str, 3)})
# rakudo 2026.08: Bool::True Bool::False Bool::True Bool::True Bool::True Bool::True Bool::False Bool::False Bool::False Bool::True Bool::True Bool::False Bool::True Bool::False Bool::True Bool::True Bool::True Bool::True Bool::True Bool::True Bool::True Bool::True Bool::True Bool::True Bool::True Bool::True Bool::True "Bool" T(X::Str::Numeric) T(X::Multi::NoMatch) T(X::Multi::NoMatch)
```
rakupp 4.0.2: differs — the position refusals match; the `:ignorecase` and `:ignoremark` combinations do not.
instead of False (four cases), `("bar", "x")` and `("bar", Int)` are False
and `(Str, 3)` is True instead of throwing.

### ST-33  :ignorecase and :ignoremark                             D:partial R:yes V:spec
`:i` (`:ignorecase`) compares under full case folding, so `ß`~`ss`~`ẞ`,
`ǅ`~`dž`~`Ǆ`, `K`(Kelvin)~`k`, `ﬁ`~`fi`, `İ`~`i̇` (but not `İ`~`i`),
final and medial sigma, and Roman numerals (`Ⅻ`~`ⅻ`) all match; full-width
`ａ` does not fold to `a`. `:m` (`:ignoremark`) compares base characters,
in both directions (`"a".contains("a\x[301]", :m)` is True), and `:i:m`
combines them. Without adverbs, NFG equality holds (`"e\x[301]"` contains
`"é"`). The empty needle is found with any adverb; `indices("", :i)` lists
every position. Roast asserts these through table-driven tests in
`contains.t`, `index.t`, `indices.t`, `starts-with.t`, `ends-with.t` and
`substr-eq.t`.
```
say "STRASSE".contains("ß", :i), " ", "straße".contains("SS", :i), " ", "ss".contains("ß", :i), " ", "ß".contains("ss", :i), " ", "ẞ".contains("ß", :i), " ", "Straße".starts-with("STRASSE", :i), " ", "STRASSE".starts-with("Straße", :i), " ", "ǅ".contains("dž", :i), " ", "ǅ".contains("Ǆ", :i), " ", "ǅ".contains("ǆ"), " ", "ǅ".index("DŽ", :i).raku, " ", "İ".contains("i", :i), " ", "İ".contains("i\x[307]", :i), " ", "K".contains("k", :i), " ", "ﬁ".contains("fi", :i), " ", "ａ".contains("a", :i), " ", "Ⅻ".contains("ⅻ", :i), " ", "ΣΑΣ".contains("σας", :i), " ", "ΣΑΣ".contains("σασ", :i), " ", "σας".contains("ΣΑΣ", :i), " ", "abc".contains("A", :i, :m), " ", "ǟ".contains("a", :m), " ", "ǟ".contains("ä", :m), " ", "\x[1E69]".contains("s", :m), " ", "a".contains("a\x[301]", :m), " ", "a\x[301]".contains("a", :m), " ", "abc".index("ábc", :m).raku, " ", "ábc".index("abc").raku, " ", "ábc".index("abc", :m).raku, " ", "aéb".index("e", :m).raku, " ", "aéb".indices("e", :m).raku, " ", "e\x[301]".contains("é"), " ", "é".contains("e\x[301]"), " ", "abc".contains("", :i), " ", "abc".contains("", :m), " ", "".contains("", :i, :m), " ", "abc".indices("", :i).raku
# rakudo 2026.08: True True True True True True True False True False Nil False False True True False True True True True True True True True True True 0 Nil 0 1 (1,) True True True True True (0, 1, 2, 3)
```
rakupp 4.0.1: differs — `:i` is per-character case insensitivity without
folding (`ß`/`ss`, `ǅ`/`dž`, `K`/`k`, `ﬁ`/`fi`, `İ`, sigma variants and
`Ⅻ`/`ⅻ` fail), `"a".contains("a\x[301]", :m)` is False, and
`indices("", :i)` is `()`.

## F. Substrings

### ST-34  substr                                                  D:yes R:yes V:spec
`substr($from, $want?)`: `$want` is clamped to the end; `$from == chars`
gives `""`; `substr()` is the string. A Callable `$from` receives the
length and a Callable `$want` the remaining count (`*-2`, `{ $_ - 1 }`,
`*-0`); `*` or Inf as `$want` means "to the end". Range forms: inclusive
and exclusive ends, `^2`, `2..*`, `2..Inf`, an end past the string is
clamped, an empty or reversed range (`3..2`, `0..-1`, `6..^6`) gives `""`,
fractional ends are `.Int`-ed. Other arguments are `.Int`-coerced (`1.9`,
`2.9`, `"1"`, `<1>`, `True`, `1e0`, `3/2`, a Range as length uses its
`.Int`). The sub form and Cool invocants work; graphemes are the unit
(`"aé\r\nb".substr(1, 2)` is `"é\r\n"`). A WhateverCode whose result is
a Range `*-3..*` throws `X::Numeric::CannotConvert`, `*-3..*-1` and
`*..*` throw `X::AdHoc`, and a bare `*` is `X::Multi::NoMatch`.
```
sub f(&c) { my \r = try c(); $! ?? "T(" ~ $!.^name ~ ")" !! r ~~ Failure ?? "F(" ~ r.exception.^name ~ ")" !! r.raku }; say f({"abcdef".substr(2)}), " ", f({"abcdef".substr(2, 2)}), " ", f({"abcdef".substr(2, 100)}), " ", f({"abcdef".substr(6)}), " ", f({"abcdef".substr(6, 1)}), " ", f({"abcdef".substr(0, 0)}), " ", f({"abcdef".substr(1, 0)}), " ", f({"abcdef".substr()}), " ", f({"abcdef".substr(2, 2).^name}), " ", f({"abcdef".substr(*-2)}), " ", f({"abcdef".substr(*-2, 1)}), " ", f({"abcdef".substr(*-6)}), " ", f({"abcdef".substr(*-1)}), " ", f({"abcdef".substr(1, *-1)}), " ", f({"abcdef".substr(*-4, *-1)}), " ", f({"abcdef".substr(1, *-0)}), " ", f({"abcdef".substr(1, *)}), " ", f({"abcdef".substr(6, *)}), " ", f({"abcdef".substr(1, Inf)}), " ", f({"abcdef".substr(*-2, Inf)}), " ", f({"abcdef".substr({ $_ - 2 })}), " ", f({"abcdef".substr(1, { $_ - 1 })}), " ", f({"abcdef".substr(1.9)}), " ", f({"abcdef".substr(1, 2.9)}), " ", f({"abcdef".substr(1, 1e0)}), " ", f({"abcdef".substr(1, 3/2)}), " ", f({"abcdef".substr("1", "2")}), " ", f({"abcdef".substr(<1>)}), " ", f({"abcdef".substr(True)}), " ", f({"abcdef".substr(1, True)}), " ", f({"abcdef".substr(1, -0)}), " ", f({"abcdef".substr(2, 1..2)}), " ", f({substr("abcdef", 1, 2)}), " ", f({123456.substr(1, 2)}), " ", f({"aé\r\nb".substr(1, 2)})
# rakudo 2026.08: "cdef" "cd" "cdef" "" "" "" "" "abcdef" "Str" "ef" "e" "abcdef" "f" "bcde" "cde" "bcdef" "bcdef" "" "bcdef" "ef" "ef" "bcde" "bcdef" "bc" "b" "b" "bc" "bcdef" "bcdef" "b" "" "cd" "bc" "23" "é\r\n"
sub f(&c) { my \r = try c(); $! ?? "T(" ~ $!.^name ~ ")" !! r ~~ Failure ?? "F(" ~ r.exception.^name ~ ")" !! r.raku }; say f({"abcdef".substr(1..3)}), " ", f({"abcdef".substr(1..^3)}), " ", f({"abcdef".substr(0^..^5)}), " ", f({"abcdef".substr(^2)}), " ", f({"abcdef".substr(0..5)}), " ", f({"abcdef".substr(2..*)}), " ", f({"abcdef".substr(2..Inf)}), " ", f({"abcdef".substr(1..*)}), " ", f({"abcdef".substr(2..100)}), " ", f({"abcdef".substr(3..2)}), " ", f({"abcdef".substr(0..-1)}), " ", f({"abcdef".substr(6..6)}), " ", f({"abcdef".substr(6..^6)}), " ", f({"abcdef".substr(2..3.9)}), " ", f({"abcdef".substr(1.9..3)}), " ", f({"abcdef".substr(1..*-1)}), " ", f({"abcdef".substr(*-3..*)}), " ", f({"abcdef".substr(*-3..*-1)}), " ", f({"abcdef".substr(*..*)}), " ", f({"abcdef".substr(*)})
# rakudo 2026.08: "bcd" "bc" "bcde" "ab" "abcdef" "cdef" "cdef" "bcdef" "cdef" "" "" "" "" "cd" "bcd" "f" T(X::Numeric::CannotConvert) T(X::AdHoc) T(X::AdHoc) T(X::Multi::NoMatch)
```
rakupp 4.0.2: differs — a start past the end is the Failure it should be, an `Inf` length reaches the end, and a LENGTH callable is handed the length still available (so `{2}` means two characters); the Range and adverb shapes still differ.
a Range as the length gives `""`, `0..-1` is an `X::OutOfRange`, the
WhateverCode-range forms return the whole string, and `substr(*)` is `""`.

### ST-35  substr out of range                                     D:yes R:yes V:spec
A start below 0 or above `chars` (also via `*-10`, a Range, or the sub
form) is a **returned** `X::OutOfRange` Failure (Roast `substr.t`:
`fails-like`) with `what` "Start argument to substr", `got`, `range`
`0..chars`, and a `comment` suggesting `*-N` only when `-N` would be in
range. A negative length is an `X::OutOfRange` Failure with `what`
"Number of characters argument to substr" and `range` `0..^Inf`; NaN and
`-Inf` lengths fail the same way. A big-integer start or length throws
`X::AdHoc`; Inf, NaN, `-Inf` as start throw `X::Numeric::CannotConvert`; a
non-numeric string throws `X::Str::Numeric`; a type object throws
`X::Parameter::InvalidConcreteness`; Nil is 0 (with a warning); a Regex
throws `X::Method::NotFound`; three positionals are `X::Multi::NoMatch`.
```
sub f(&c) { my \r = try c(); $! ?? "T(" ~ $!.^name ~ ")" !! r ~~ Failure ?? "F(" ~ r.exception.^name ~ ";" ~ (r.exception.?what // "-") ~ ";" ~ (r.exception.?got // "-") ~ ";" ~ (r.exception.?range // "-") ~ ";" ~ (r.exception.?comment // "-") ~ ")" !! r.raku }; say f({"abc".substr(4)}), " ", f({"abc".substr(4, 1)}), " ", f({"abc".substr(-1)}), " ", f({"abc".substr(-3)}), " ", f({"abc".substr(-4)}), " ", f({"abc".substr(-10)}), " ", f({"abc".substr(*-4, 1)}), " ", f({"abc".substr(*-10)}), " ", f({"abc".substr(-1..2)}), " ", f({"abc".substr(5..6)}), " ", f({"abc".substr(1, -1)}), " ", f({"abc".substr(1, *-5)}), " ", f({"abc".substr(1, NaN)}), " ", f({"abc".substr(1, -Inf)}), " ", f({"abc".substr(1, -2**70)}), " ", f({"abc".substr(2**70)}), " ", f({"abc".substr(Inf)}), " ", f({"abc".substr(NaN)}), " ", f({"abc".substr(-Inf)}), " ", f({"abc".substr("x")}), " ", f({"abc".substr(1, "x")}), " ", f({"abc".substr(Str)}), " ", f({"abc".substr(1, Str)}), " ", f({"abc".substr(Nil)}), " ", f({"abc".substr(/b/)}), " ", f({"abc".substr(/b/, 1)}), " ", f({"abc".substr(1, 2, 3)}), " ", f({substr("abc", -1)})
# rakudo 2026.08: T(X::OutOfRange) T(X::OutOfRange) T(X::OutOfRange) T(X::OutOfRange) T(X::OutOfRange) T(X::OutOfRange) T(X::OutOfRange) T(X::OutOfRange) T(X::OutOfRange) T(X::OutOfRange) T(X::OutOfRange) T(X::OutOfRange) T(X::OutOfRange) T(X::OutOfRange) T(X::AdHoc) T(X::AdHoc) T(X::Numeric::CannotConvert) T(X::Numeric::CannotConvert) T(X::Numeric::CannotConvert) T(X::Str::Numeric) T(X::Str::Numeric) T(X::Parameter::InvalidConcreteness) T(X::Parameter::InvalidConcreteness) "abc" T(X::Method::NotFound) T(X::Method::NotFound) T(X::Multi::NoMatch) T(X::OutOfRange)
```
rakupp 4.0.2: differs — out-of-range starts and lengths are Failures now (`S32-str/substr.t` passes in full); a NaN/huge/non-numeric/type-object argument still reports `X::OutOfRange` where Rakudo separates `X::AdHoc`, `X::Numeric::CannotConvert`, `X::Str::Numeric` and `X::Parameter::InvalidConcreteness`.
negative starts fail), a NaN or huge length and a non-numeric or
type-object argument return `""` or the string, a Regex start is accepted,
and three positionals give `"a"`.

### ST-36  substr-rw                                               D:yes R:yes V:spec/quirk
`substr-rw` returns a Proxy over a variable. Assigning through it splices
the replacement in (the length may change, `(4)` appends, `(1, 10)` clamps,
`(2, 0)` inserts), coercing the value with `.Str` (42, 1.5, an allomorph
becomes text; Nil warns and inserts `""`); the variable then holds a new
Str — a `Str`-typed variable is fine, a Cool variable such as 1234 becomes
the Str `"1z34"`. Range, Whatever, Callable and `Int(Any)` forms mirror
`substr`. **Quirk:** the proxy reads the variable's *current* text
(`$p.Str` after `$s = "wxyz"` is `"xy"`) but every store splices into the
text captured when the proxy was made, so a second store through a bound
proxy replaces the original slice (`$p = "XYZ"; $p = "Q"` leaves `"aQd"`),
and a store after an outside assignment discards that assignment. Roast
`substr-rw.t` asserts the repeated-store case (`$ref = "fred"; $ref =
"wilma"`) whose result is the same under either reading. A literal or a
non-container invocant throws `X::AdHoc` on the store (`42.substr-rw(0)`
and `"abcd".substr-rw(1) = "x"`); out-of-range start or negative length is
an `X::OutOfRange` Failure; NaN, a big integer or a type-object start is
`X::AdHoc`.
```
sub f(&c) { my \r = try c(); $! ?? "T(" ~ $!.^name ~ ")" !! r ~~ Failure ?? "F(" ~ r.exception.^name ~ ")" !! r.raku }; say f({ my $s = "abcd"; $s.substr-rw(1, 2) = "XYZ"; $s }), " ", f({ my $s = "abcd"; $s.substr-rw(2, 0) = "-"; $s }), " ", f({ my $s = "abcd"; $s.substr-rw(1) = ""; $s }), " ", f({ my $s = "abcd"; $s.substr-rw = "Q"; $s }), " ", f({ my $s = "abcd"; $s.substr-rw(4) = "e"; $s }), " ", f({ my $s = "abcd"; $s.substr-rw(1, 10) = "e"; $s }), " ", f({ my $s = "abcd"; $s.substr-rw(1..2) = "x"; $s }), " ", f({ my $s = "abcd"; $s.substr-rw(1..^3) = "x"; $s }), " ", f({ my $s = "abcd"; $s.substr-rw(1..*) = "x"; $s }), " ", f({ my $s = "abcd"; $s.substr-rw(*-1) = "z"; $s }), " ", f({ my $s = "abcd"; $s.substr-rw(*-2, *-1) = "x"; $s }), " ", f({ my $s = "abcd"; $s.substr-rw(1, *) = "z"; $s }), " ", f({ my $s = "abc"; $s.substr-rw(1, Inf) = "x"; $s }), " ", f({ my $s = "abc"; $s.substr-rw(1, 1.5) = "x"; $s }), " ", f({ my $s = "abc"; $s.substr-rw(0.5) = "x"; $s }), " ", f({ my $s = "abcd"; $s.substr-rw("1", 1) = 42; $s }), " ", f({ my $s = "abcd"; $s.substr-rw(1, 2) = 1.5; $s }), " ", f({ my $s = "abcd"; $s.substr-rw(1, 2) = <42>; $s.^name }), " ", f({ my $s = "abcd"; $s.substr-rw(1, 2) = Nil; $s }), " ", f({ my $s = "abcd"; $s.substr-rw(1, 1) = "é"; $s.chars }), " ", f({ my $s = "abcd"; $s.substr-rw(1, 2).^name }), " ", f({ my $s = "abcd"; $s.substr-rw(1, 1).Str }), " ", f({ my $s = "abcd"; substr-rw($s, 1, 1) = "z"; $s }), " ", f({ my Str $s = "abcd"; $s.substr-rw(1, 1) = "z"; $s }), " ", f({ my $s = 1234; $s.substr-rw(1, 1) = "z"; $s.raku }), " ", f({ my $s = 42; substr-rw($s, 0, 1) = "9"; $s.raku })
# rakudo 2026.08: "aXYZd" "ab-cd" "a" "Q" "abcde" "ae" "axd" "axd" "ax" "abcz" "abxd" "az" "ax" "axc" "x" "a42cd" "a1.5d" "Str" "ad" 4 "Str" "b" "azcd" "azcd" "\"1z34\"" "\"92\""
sub f(&c) { my \r = try c(); $! ?? "T(" ~ $!.^name ~ ")" !! r ~~ Failure ?? "F(" ~ r.exception.^name ~ ")" !! r.raku }; say f({ my $s = "abcd"; my $p := $s.substr-rw(1, 2); $p = "XYZ"; my $a = $s; $p = "Q"; $a ~ "/" ~ $s ~ "/" ~ $p }), " ", f({ my $s = "abcd"; my $p := $s.substr-rw(1, 2); $p = "Q"; $p = "R"; $s }), " ", f({ my $s = "abcd"; my $p := $s.substr-rw(1, 2); $p = "QQQQ"; $p = "R"; $s }), " ", f({ my $s = "abcd"; my $p := $s.substr-rw(1, 2); $p = "XYZ"; $p.Str }), " ", f({ my $s = "abcd"; my $p := $s.substr-rw(1, 2); $p = "X"; $p.Str }), " ", f({ my $s = "abcd"; my $p := $s.substr-rw(1, 2); $s = "wxyz"; $p.Str }), " ", f({ my $s = "abcd"; my $p := $s.substr-rw(1, 2); $s = "wxyz"; $p = "Q"; $s }), " ", f({ my $s = "abcd"; my $p := $s.substr-rw(1, 2); $s = "wx"; $p.Str }), " ", f({ my $s = "abcd"; my $p := $s.substr-rw(1, 2); $s = "wx"; $p = "Q"; $s })
# rakudo 2026.08: "aXYZd/aQd/Qd" "aRd" "aRd" "XY" "Xd" "xy" "aQd" "x" "aQd"
sub f(&c) { my \r = try c(); $! ?? "T(" ~ $!.^name ~ ")" !! r ~~ Failure ?? "F(" ~ r.exception.^name ~ ")" !! r.raku }; say f({ "abcd".substr-rw(1) = "x"; "ok" }), " ", f({ 42.substr-rw(0) }), " ", f({ my $s = "abc"; $s.substr-rw(5) }), " ", f({ my $s = "abc"; $s.substr-rw(5) = "x"; $s }), " ", f({ my $s = "abc"; $s.substr-rw(-1) }), " ", f({ my $s = "abc"; $s.substr-rw(1, -1) }), " ", f({ my $s = "abc"; $s.substr-rw(1, *-5) }), " ", f({ my $s = "abc"; $s.substr-rw(1, NaN) }), " ", f({ my $s = "abc"; $s.substr-rw(1, 2**70) }), " ", f({ my $s = "abc"; $s.substr-rw(2**70) }), " ", f({ my $s = "abc"; $s.substr-rw(Str) = "x"; $s }), " ", f({ my $s = "abc"; $s.substr-rw(1, 10).Str }), " ", f({ my $s = "abc"; $s.substr-rw(3) = "d"; $s }), " ", f({ my $s = "abc"; $s.substr-rw(4) = "d"; $s })
# rakudo 2026.08: T(X::AdHoc) T(X::AdHoc) T(X::OutOfRange) T(X::OutOfRange) T(X::OutOfRange) T(X::OutOfRange) T(X::OutOfRange) T(X::AdHoc) T(X::AdHoc) T(X::AdHoc) T(X::AdHoc) "bc" "abcd" T(X::OutOfRange)
```
rakupp 4.0.1: differs — the Range, Whatever and Callable forms replace the
whole string or the wrong slice, a *bound* proxy never writes back
(`$p = "XYZ"` leaves `"abcd"`), a Cool variable throws `X::Assignment::RO`,
a start past the end is accepted (`(5) = "x"` appends), and a
type-object start is used as 0.

### ST-37  chars and codes                                         D:yes R:yes V:spec
`chars` counts graphemes and `codes` counts codepoints after NFC
composition: `"e\x[301]"` is 1/1 (it composes), `"x\x[301]"` 1/2,
`"\r\n"` 1/2, a ZWJ family 1/5, a regional-indicator pair 1, a skin-tone
sequence 1, Hangul jamo 1/1 (composed), a lone combining mark is its own
grapheme (`"\x[301]\x[301]"` is 1), a variation selector alone is 1 and
attaches to a base. `NFC`/`NFD`/`NFKC`/`NFKD` are codepoint views
(`"\x[1E69]".NFD` has 3, `"ﬁ".NFKC` has 2). Cool invocants are stringified.
```
say "e\x[301]".chars, " ", "e\x[301]".codes, " ", "x\x[301]".chars, " ", "x\x[301]".codes, " ", "a\x[301]\x[301]".codes, " ", "\r\n".chars, " ", "\r\n".codes, " ", "\r\n\r\n".chars, " ", "\n\r".chars, " ", "\n\x[301]".chars, " ", "\r\x[301]".chars, " ", "\x[301]".chars, " ", "\x[301]\x[301]".chars, " ", "a\x[300]\x[301]".chars, " ", "👨\x[200D]👩\x[200D]👧".chars, " ", "👨\x[200D]👩\x[200D]👧".codes, " ", "\x[1F469]\x[1F3FD]\x[200D]\x[1F4BB]".chars, " ", "\x[1F1FA]\x[1F1F8]".chars, " ", "\x[1F476]\x[1F3FF]".chars, " ", "\x[1100]\x[1161]\x[11A8]".chars, " ", "\x[1100]\x[1161]\x[11A8]".codes, " ", "\x[0915]\x[094D]\x[0937]".chars, " ", "\x[FE0F]".chars, " ", "a\x[FE0F]".chars, " ", "".chars, " ", chars("abc"), " ", 12345.chars, " ", 42.codes, " ", "e\x[301]".ords.raku, " ", "e\x[301]".NFC.list.raku, " ", "x\x[301]".NFC.list.raku, " ", "é".NFD.elems, " ", "\x[1E69]".NFD.list.raku, " ", "\x[1E69]".NFKD.elems, " ", "ﬁ".NFKC.elems, " ", "ﬁ".NFC.elems, " ", "\r\n".NFD.elems
# rakudo 2026.08: 1 1 1 2 2 1 2 2 2 2 2 1 1 1 1 5 1 1 1 1 1 1 1 1 0 3 5 2 (233,).Seq (233,).Seq (120, 769).Seq 2 (115, 803, 775).Seq 3 2 1 2
```
rakupp 4.0.1: matches (the normal-form views `.raku` as Lists rather than
Seqs).

### ST-38  comb                                                    D:yes R:yes V:spec/bug
No argument: the graphemes. An Int size *n*: chunks of *n* with a shorter
last chunk; a size of 1, 0 or negative means single characters; the limit
counts chunks and is `.Int`-coerced (`1.9` → 1, `2e0`, `"2"`), `0`,
negative or NaN gives nothing, `*`/Inf all, a huge limit all; a size above
2⁶³ throws `X::AdHoc`; an Int type object and the 6.e Pair form are
`X::Multi::NoMatch`; a Rat size is taken as a *pattern* (`comb(2.5)`
searches for `"2.5"`). A Str pattern: each non-overlapping occurrence, as
many times as found; the empty pattern means characters. **Bug:** with a
limit the Str search advances one character instead of the pattern
length, so `"aaaa".comb("aa", 3)` yields three overlapping `"aa"` and a
negative limit yields all overlapping matches, while `comb("aa", 0)` is
`()` and `*`/Inf keep the non-overlapping rule (Roast `comb.t` asserts
non-overlap only for the no-limit call). A Regex: the matched strings, or
Match objects with `:match`; a limit; zero-width matches give `""` at each
position and `"aaa".comb(/a*/)` ends with a trailing `""`; `$/`-style
markers `<( )>` and captures work; a limit of 0 or negative gives nothing;
a fractional limit truncates; a huge, NaN or non-numeric limit throws
`X::AdHoc`.
```
sub f(&c) { my \r = try c(); $! ?? "T(" ~ $!.^name ~ ")" !! r ~~ Failure ?? "F(" ~ r.exception.^name ~ ")" !! r.raku }; say f({"abc".comb}), " ", f({"".comb}), " ", f({"e\x[301]b".comb.elems}), " ", f({"a\r\nb".comb}), " ", f({"abc".comb.^name}), " ", f({"abcdefg".comb(3)}), " ", f({"abcdefg".comb(3, 2)}), " ", f({"abcdefg".comb(3, 0)}), " ", f({"abcdefg".comb(3, -1)}), " ", f({"abcdefg".comb(3, *)}), " ", f({"abcdefg".comb(3, Inf)}), " ", f({"abcdefg".comb(3, 1.9)}), " ", f({"abcdefg".comb(3, 2e0)}), " ", f({"abcdefg".comb(3, "2")}), " ", f({"abcdefg".comb(3, NaN)}), " ", f({"abcdefg".comb(3, 2**70)}), " ", f({"abcdefg".comb(10)}), " ", f({"abc".comb(3)}), " ", f({"".comb(3)}), " ", f({"abcdefg".comb(1, 2)}), " ", f({"abcdefg".comb(0)}), " ", f({"abc".comb(-1)}), " ", f({"abc".comb(0, 2)}), " ", f({"abc".comb(-1, 2)}), " ", f({"abcdefg".comb(2.5)}), " ", f({"abc".comb(2**70)}), " ", f({"abc".comb(1, 2**70)}), " ", f({"abc".comb(Int)}), " ", f({"abc".comb(3 => -2)}), " ", f({comb(3, "abcdefg")}), " ", f({12345.comb(2)})
# rakudo 2026.08: ("a", "b", "c").Seq ().Seq 2 ("a", "\r\n", "b").Seq "Seq" ("abc", "def", "g").Seq ("abc", "def").Seq ().Seq ().Seq ("abc", "def", "g").Seq ("abc", "def", "g").Seq ("abc",).Seq ("abc", "def").Seq ("abc", "def").Seq ().Seq ("abc", "def", "g").Seq ("abcdefg",).Seq ("abc",).Seq ().Seq ("a", "b").Seq ("a", "b", "c", "d", "e", "f", "g").Seq ("a", "b", "c").Seq ("a", "b").Seq ("a", "b").Seq ().Seq T(X::AdHoc) ("a", "b", "c").Seq T(X::Multi::NoMatch) T(X::Multi::NoMatch) ("abc", "def", "g").Seq ("12", "34", "5").Seq
sub f(&c) { my \r = try c(); $! ?? "T(" ~ $!.^name ~ ")" !! r ~~ Failure ?? "F(" ~ r.exception.^name ~ ")" !! r.raku }; say f({"aaaa".comb("aa")}), " ", f({"aaaa".comb("aa", *)}), " ", f({"aaaa".comb("aa", Inf)}), " ", f({"aaaa".comb("aa", 1)}), " ", f({"aaaa".comb("aa", 2)}), " ", f({"aaaa".comb("aa", 3)}), " ", f({"aaaa".comb("aa", 10)}), " ", f({"aaaaaa".comb("aa", 3)}), " ", f({"aaaaaa".comb("aa", 4)}), " ", f({"aaaaaa".comb("aaa")}), " ", f({"aaaaaa".comb("aaa", 3)}), " ", f({"aaa".comb("aa", 2)}), " ", f({"aaaa".comb("aa", 0)}), " ", f({"aaaa".comb("aa", -1)}), " ", f({"aaaa".comb("aa", 2.5)}), " ", f({"aaaa".comb("aa", 2**70)}), " ", f({"abab".comb("ab", 1)}), " ", f({"abab".comb("ab", 5)}), " ", f({"abcabc".comb("bc")}), " ", f({"abcabc".comb("bc", 1)}), " ", f({"abc".comb("b")}), " ", f({"abc".comb("b")[0].^name}), " ", f({"abc".comb("x")}), " ", f({"abc".comb("abc")}), " ", f({"AbAb".comb("ab")}), " ", f({"a.b".comb(".")}), " ", f({"abc".comb("")}), " ", f({"abc".comb("", 2)}), " ", f({"abc".comb("", 0)}), " ", f({"abc".comb("", -1)}), " ", f({"abc".comb("", 2**70)}), " ", f({"ab".comb(<b>)}), " ", f({"a1b1".comb(1)}), " ", f({1212.comb(12)}), " ", f({comb("ab", "abab")})
# rakudo 2026.08: ("aa", "aa").Seq ("aa", "aa").Seq ("aa", "aa").Seq ("aa",).Seq ("aa", "aa").Seq ("aa", "aa", "aa").Seq ("aa", "aa", "aa").Seq ("aa", "aa", "aa").Seq ("aa", "aa", "aa", "aa").Seq ("aaa", "aaa").Seq ("aaa", "aaa", "aaa").Seq ("aa", "aa").Seq ().Seq ("aa", "aa", "aa").Seq ("aa", "aa").Seq T(X::AdHoc) ("ab",).Seq ("ab", "ab").Seq ("bc", "bc").Seq ("bc",).Seq ("b",).Seq "Str" ().Seq ("abc",).Seq ().Seq (".",).Seq ("a", "b", "c").Seq ("a", "b").Seq ().Seq ().Seq ("a", "b", "c").Seq ("b",).Seq ("a", "1", "b", "1").Seq ("1212",).Seq ("ab", "ab").Seq
sub f(&c) { my \r = try c(); $! ?? "T(" ~ $!.^name ~ ")" !! r ~~ Failure ?? "F(" ~ r.exception.^name ~ ")" !! r.raku }; say f({"a1b22c333".comb(/\d+/)}), " ", f({"a1b22c333".comb(/\d+/, 2)}), " ", f({"a1b22c333".comb(/\d+/, :match)}), " ", f({"a1b22c333".comb(/\d+/, 2, :match).elems}), " ", f({"a1b2".comb(/\d/, :!match)}), " ", f({"abc".comb(/(\w)/, :match)[0].^name}), " ", f({"a1b22".comb(/(\d)(\d)/)[0]}), " ", f({"abc".comb(/./, :match)[1].from}), " ", f({"abc".comb(/x/)}), " ", f({"abc".comb(/x/, :match)}), " ", f({"abc".comb(/\w/).^name}), " ", f({"<>[]()".comb(/.<(.)>/)}), " ", f({"aXbXc".comb(/X <( \w /)}), " ", f({"aXbXc".comb(/ \w )> X/)}), " ", f({"abc".comb(/<?>/)}), " ", f({"aaa".comb(/a*/)}), " ", f({"abc".comb(/b*/)}), " ", f({"abc".comb(/^/)}), " ", f({"abc".comb(/$/)}), " ", f({"aaa".comb(/a+?/)}), " ", f({"abcabc".comb(/b|c/)}), " ", f({"abc".comb(/\w/, 0)}), " ", f({"abc".comb(/\w/, -1)}), " ", f({"abc".comb(/\w/, *)}), " ", f({"abc".comb(/\w/, 1.9)}), " ", f({"a1b2".comb(/\d/, "1")}), " ", f({"abc".comb(/\w/, 2**70)}), " ", f({"abc".comb(/\w/, NaN)}), " ", f({"abc".comb(/\w/, "x").eager}), " ", f({"abc".comb(/\w/, Inf, :match).elems}), " ", f({comb(/\w/, "a;b", 5)})
# rakudo 2026.08: ("1", "22", "333").Seq ("1", "22").Seq (Match.new(:orig("a1b22c333"), :from(1), :pos(2)), Match.new(:orig("a1b22c333"), :from(3), :pos(5)), Match.new(:orig("a1b22c333"), :from(6), :pos(9))).Seq 2 ("1", "2").Seq "Match" "22" 1 ().Seq ().Seq "Seq" (">", "]", ")").Seq ("b", "c").Seq ("a", "b").Seq ("", "", "", "").Seq ("aaa", "").Seq ("", "b", "", "").Seq ("",).Seq ("",).Seq ("a", "a", "a").Seq ("b", "c", "b", "c").Seq ().Seq ().Seq ("a", "b", "c").Seq ("a",).Seq ("1",).Seq T(X::AdHoc) T(X::AdHoc) T(X::AdHoc) 3 ("a", "b").Seq
```
rakupp 4.0.2: differs — the LIMIT is honoured everywhere now (Regex, Str needle and chunk size), with zero and negative meaning none and a NaN/huge/non-numeric one a misuse, and a Callable needle is refused without being called. The Rakudo overlap bug is deliberately not imitated, and a Rat size is still a size rather than a pattern.
limit of 0 or below (all matches), the Regex form ignores the limit unless
`:match` is given, and a negative/NaN/non-numeric limit means "all";
`comb(3, "2")` ignores the Str limit, `comb(3, -1)` gives all chunks,
`comb(2.5)` gives `("abc",)`, and `comb(Int)` gives the characters.

## G. Lines, words, split

### ST-39  lines                                                   D:yes R:yes V:spec/bug
Splits on `\n`, `\r`, `\r\n` (one separator), U+85, U+0B, U+0C, U+2028,
U+2029 — not U+1C–U+1E; a trailing separator does not add an empty line
(`"a\r\n\r\n"` gives `("a", "")`); `""` gives `()` and `"\n"` gives `("",)`.
`:!chomp` keeps each separator; the limit is `head`-like (`0` and negative
give `()`, `1.9` → 1, `"1"`, `*`/Inf all, NaN throws `X::AdHoc`);
`:count` returns the Int count (deprecated per docs). **Bug:**
`lines(:!count)` throws `X::TypeCheck::Return` (the false-count candidate
is declared to return an Int but returns the Seq).
```
sub f(&c) { my \r = try c(); $! ?? "T(" ~ $!.^name ~ ")" !! r ~~ Failure ?? "F(" ~ r.exception.^name ~ ")" !! r.raku }; say f({"a\nb".lines}), " ", f({"a\nb\n".lines}), " ", f({"a\n\nb".lines}), " ", f({"\n".lines}), " ", f({"\n\n".lines}), " ", f({"".lines}), " ", f({"a\rb".lines}), " ", f({"a\r\nb".lines}), " ", f({"a\n\rb".lines}), " ", f({"a\r\r".lines}), " ", f({"a\r\n\r\n".lines}), " ", f({"\r\n".lines}), " ", f({"a\x[85]b".lines}), " ", f({"a\x[0B]b".lines}), " ", f({"a\x[0C]b".lines}), " ", f({"a\x[2028]b".lines}), " ", f({"a\x[2029]b".lines}), " ", f({"a\x[1C]b\x[1D]c\x[1E]d".lines}), " ", f({"a\n\x[301]b".lines}), " ", f({"a\nb".lines.^name}), " ", f({"a\nb".lines[0].^name}), " ", f({lines("a\nb")}), " ", f({12.lines})
# rakudo 2026.08: ("a", "b").Seq ("a", "b").Seq ("a", "", "b").Seq ("",).Seq ("", "").Seq ().Seq ("a", "b").Seq ("a", "b").Seq ("a", "", "b").Seq ("a", "").Seq ("a", "").Seq ("",).Seq ("a", "b").Seq ("a", "b").Seq ("a", "b").Seq ("a", "b").Seq ("a", "b").Seq ("a\x[1C]b\x[1D]c\x[1E]d",).Seq ("a", "\x[301]b").Seq "Seq" "Str" ("a", "b").Seq ("12",).Seq
sub f(&c) { my \r = try c(); $! ?? "T(" ~ $!.^name ~ ")" !! r ~~ Failure ?? "F(" ~ r.exception.^name ~ ")" !! r.raku }; say f({"a\nb\nc".lines(2)}), " ", f({"a\nb\nc".lines(0)}), " ", f({"a\nb".lines(-1)}), " ", f({"a\nb\nc".lines(*)}), " ", f({"a\nb\nc".lines(Inf)}), " ", f({"a\nb".lines(1.9)}), " ", f({"a\nb".lines("1")}), " ", f({"a\nb".lines(2**70)}), " ", f({"a\nb".lines(NaN)}), " ", f({"a\nb".lines(:!chomp)}), " ", f({"a\r\nb\n".lines(:!chomp)}), " ", f({"a\r\n".lines(:!chomp)}), " ", f({"a\nb\nc".lines(2, :!chomp)}), " ", f({"a\nb\nc".lines(2, :chomp)}), " ", f({"a\nb\nc".lines(:count)}), " ", f({"a\nb".lines(:count).^name}), " ", f({"".lines(:count)}), " ", f({"a\nb\nc".lines(:!count)}), " ", f({"a\nb".lines(2, :count)}), " ", f({"a\nb".lines(:count, :!chomp)})
# rakudo 2026.08: ("a", "b").Seq ().Seq ().Seq ("a", "b", "c").Seq ("a", "b", "c").Seq ("a",).Seq ("a",).Seq ("a", "b").Seq T(X::AdHoc) ("a\n", "b").Seq ("a\r\n", "b\n").Seq ("a\r\n",).Seq ("a\n", "b\n").Seq ("a", "b").Seq 3 "Int" 0 T(X::TypeCheck::Return) ("a", "b").Seq 2
```
rakupp 4.0.1: differs — a negative limit gives all lines, NaN gives `()`,
and `:!count` returns the lines.

### ST-40  words, and what a word quote keeps together             D:yes R:yes V:spec
`words` splits on the ST-25 whitespace set — including U+A0, U+2007,
U+202F, U+3000, U+85 — and not on U+FEFF, U+200B, U+1C or U+180E; leading
and trailing whitespace is dropped; the limit is `head`-like (0 gives `()`,
negative `()`, `1.9` → 1, `*` all). The `< >` word quote is different: it
does *not* split on U+A0, U+2007 or U+202F (`< a b >` with a no-break
space is one word of three codepoints), it does split on U+3000, a leading
no-break space is dropped, a trailing one is kept, and `qw`, `Q:w`, `qqw`
and `<< >>` behave the same (the probe contains literal U+A0, U+2007,
U+202F, U+FEFF and U+3000 characters). Roast `words.t` asserts the
no-break-space split for `words`; `S02-literals/listquote-whitespace.t`
asserts that it does not split a word quote.
```
say "a b  c".words.raku, " ", "  a b ".words.raku, " ", "".words.raku, " ", "   ".words.raku, " ", "a\tb\nc\r\nd".words.raku, " ", "a\x[A0]b".words.raku, " ", "a\x[2007]b".words.raku, " ", "a\x[202F]b".words.raku, " ", "a\x[3000]b".words.raku, " ", "a\x[85]b\x[2028]c".words.raku, " ", "a\x[1680]b\x[2000]c\x[205F]d".words.raku, " ", "a\x[0B]b\x[0C]c".words.raku, " ", "a\x[FEFF]b".words.raku, " ", "a\x[200B]b".words.raku, " ", "a\x[1C]b".words.raku, " ", "a\x[180E]b".words.raku, " ", "a b c".words(2).raku, " ", "a b c".words(0).raku, " ", "a b".words(-1).raku, " ", "a b c".words(*).raku, " ", "a b c".words(1.9).raku, " ", "a b".words(2**70).raku, " ", words("a b", 1).raku, " ", <a b>.words.raku, " ", "a b".words.^name, " ", "a b".words[0].^name
# rakudo 2026.08: ("a", "b", "c").Seq ("a", "b").Seq ().Seq ().Seq ("a", "b", "c", "d").Seq ("a", "b").Seq ("a", "b").Seq ("a", "b").Seq ("a", "b").Seq ("a", "b", "c").Seq ("a", "b", "c", "d").Seq ("a", "b", "c").Seq ("a﻿b",).Seq ("a​b",).Seq ("a\x[1C]b",).Seq ("a᠎b",).Seq ("a", "b").Seq ().Seq ().Seq ("a", "b", "c").Seq ("a",).Seq ("a", "b").Seq ("a",).Seq ("a", "b").Seq Seq Str
say "a b c".words.raku, " ", < a b c >.elems, " ", < a b >.raku, " ", < a b >[0].ords.raku, " ", < a b >.raku, " ", < a b >.raku, " ", < a﻿b >.elems, " ", < a　b >.elems, " ", < a  b >.raku, " ", < a  b >.raku, " ", <  a >.raku, " ", < a  >.raku, " ", < a b c d >.raku, " ", <a b>.^name, " ", <1 2>.raku, " ", qw< a b c >.raku, " ", Q:w< a b c >.raku, " ", qqw< a b c >.raku, " ", << a b c >>.raku
# rakudo 2026.08: ("a", "b", "c").Seq 2 "a b" (97, 160, 98).Seq "a b" "a b" 1 2 "a  b" ("a ", "b") "a" "a " ("a b c", "d") Str "1 2" ("a b", "c") ("a b", "c") ("a b", "c") ("a b", "c")
```
rakupp 4.0.1: differs — a negative `words` limit gives all words; in a
word quote a leading no-break space is kept (`" a"`) and `<< >>` splits
on it.

### ST-41  split with a Str needle                                 D:yes R:yes V:spec
Pieces are plain Str in a Seq. `""` splits to `("",)`, or `()` with
`:skip-empty` or with the empty needle. The empty needle yields every
character with a leading and trailing `""`; `:skip-empty` removes the
empty pieces (and nothing else: separators stay). `:v` interleaves the
needle, `:k` the index (always 0 for one needle), `:kv` both, `:p` a Pair;
`:!v`/`:v(0)` mean off. Only one of `:v :k :kv :p` may be given:
`X::Adverb` with `what` "split", `source` "Str" and `nogo` naming the
combination — it is thrown at the call, not at reification. Cool
invocants and needles are stringified. Roast `split.t` is a table over all
adverb combinations and asserts the `X::Adverb` case.
```
sub f(&c) { my \r = try c(); $! ?? "T(" ~ $!.^name ~ ")" !! r ~~ Failure ?? "F(" ~ r.exception.^name ~ ")" !! r.raku }; say f({"a;b;c".split(";")}), " ", f({"a;b".split(";").^name}), " ", f({"a;b".split(";")[0].^name}), " ", f({"a;b;c".split(";", :v)}), " ", f({"a;b".split(";", :v)[1].^name}), " ", f({"a;b;c".split(";", :k)}), " ", f({"a;b;c".split(";", :kv)}), " ", f({"a;b;c".split(";", :p)}), " ", f({"abc".split("b", :p)[1]}), " ", f({"".split("x")}), " ", f({"".split("x", :skip-empty)}), " ", f({"".split("")}), " ", f({"".split("", :v)}), " ", f({"abc".split("")}), " ", f({"abc".split("", :skip-empty)}), " ", f({"abc".split("", :v)}), " ", f({"abc".split("", :k)}), " ", f({"abc".split("", :kv, :skip-empty)}), " ", f({";a;;b;".split(";")}), " ", f({";a;;b;".split(";", :skip-empty)}), " ", f({";a;;b;".split(";", :v, :skip-empty)}), " ", f({";a;;b;".split(";", :p, :skip-empty)}), " ", f({"XX".split("X", :skip-empty)}), " ", f({"XX".split("X", :v, :skip-empty)}), " ", f({"abc".split("abc")}), " ", f({"abc".split("x")}), " ", f({"a.b".split(".")}), " ", f({"aXbxc".split("x")}), " ", f({"a;b".split(";", :!v)}), " ", f({"a;b".split(";", :v(0))}), " ", f({"a;b".split(";", :skip-empty(0))}), " ", f({123.split(2)}), " ", f({"a1b".split(1)}), " ", f({"a;b".split(<;>)}), " ", f({"a;b".split(";", :v, :k).eager}), " ", f({"a;b".split(";", :v, :k, :kv, :p).eager; $!.nogo.join(",")}), " ", f({"a;b".split(";", :v, :k).eager; $!.what ~ "/" ~ $!.source})
# rakudo 2026.08: ("a", "b", "c").Seq "Seq" "Str" ("a", ";", "b", ";", "c").Seq "Str" ("a", 0, "b", 0, "c").Seq ("a", 0, ";", "b", 0, ";", "c").Seq ("a", 0 => ";", "b", 0 => ";", "c").Seq 0 => "b" ("",).Seq ().Seq ().Seq ().Seq ("", "a", "b", "c", "").Seq ("a", "b", "c").Seq ("", "a", "b", "c", "").Seq ("", "a", "b", "c", "").Seq ("a", "b", "c").Seq ("", "a", "", "b", "").Seq ("a", "b").Seq (";", "a", ";", ";", "b", ";").Seq (0 => ";", "a", 0 => ";", 0 => ";", "b", 0 => ";").Seq ().Seq ("X", "X").Seq ("", "").Seq ("abc",).Seq ("a", "b").Seq ("aXb", "c").Seq ("a", "b").Seq ("a", "b").Seq ("a", "b").Seq ("1", "3").Seq ("a", "b").Seq ("a", "b").Seq T(X::Adverb) T(X::Adverb) T(X::Adverb)
```
rakupp 4.0.1: differs — combining `:v` with `:k` is accepted (`("a", 0,
"b")`), so the exception and its attributes do not exist.

### ST-42  split with a limit                                      D:partial R:partial V:spec/bug
The limit is the number of pieces, not of separators: `("a", "b;c;d")`
for 2; 1 gives the string itself (as a List, not a Seq); 0 or negative gives
`()`; `*`, Inf and a huge Int give all; NaN throws `X::TypeCheck`
(Roast `split-simple.t`); a Str limit is coerced (`"2"`). **Bug:** a
fractional or Num limit (`2.7`, `2e0`, `2.0`, `1.5`) throws `X::AdHoc`
with a Str needle although it is truncated with a Regex or a needle list
(ST-43, ST-44). With `:v`/`:k`/`:kv`/`:p` the limit still counts pieces;
with `:skip-empty` empties are dropped before counting. The empty needle
with a limit *n*: a leading `""`, then *n−2* characters, then the rest, and
a trailing `""` only when *n* exceeds the character count by two or more;
`:skip-empty` drops the `""` pieces and `"abcd".split("", 4,
:skip-empty)` is `("a", "b", "cd")`. `:end` keeps the *last* pieces
(`("a;b;c", "d")` for 2); with `*` or a limit beyond the count it is a
plain split; **quirks:** `:end` with limit 0 returns all pieces, `:!end`
discards the limit, `:end` drops `:v`/`:k`/`:p` (they are consumed by the
inner split), and with a needle list `:end` is wrong (`"abcabc".split(<b
c>, 2, :end)` is `("a", "cabc")`).
```
sub f(&c) { my \r = try c(); $! ?? "T(" ~ $!.^name ~ ")" !! r ~~ Failure ?? "F(" ~ r.exception.^name ~ ")" !! r.raku }; say f({"a;b;c;d".split(";", 2)}), " ", f({"a;b;c;d".split(";", 1)}), " ", f({"a;b;c;d".split(";", 0)}), " ", f({"a;b;c;d".split(";", -1)}), " ", f({"a;b;c;d".split(";", 10)}), " ", f({"a;b;c;d".split(";", *)}), " ", f({"a;b;c;d".split(";", Inf)}), " ", f({"a;b".split(";", 2**70)}), " ", f({"a;b;c".split(";", "2")}), " ", f({"a;b;c;d".split(";", 2e0)}), " ", f({"a;b;c;d".split(";", 2.0)}), " ", f({"a;b;c;d".split(";", 2.7)}), " ", f({"a;b;c;d".split(";", 1.5)}), " ", f({"a;b;c;d".split(";", 0.5)}), " ", f({"a;b".split(";", NaN).eager}), " ", f({"a;b;c;d".split(";", 2, :v)}), " ", f({"a;b;c;d".split(";", 2, :k)}), " ", f({"a;b;c;d".split(";", 2, :kv)}), " ", f({"a;b;c;d".split(";", 2, :p)}), " ", f({"a;b;c".split(";", 2, :v, :k)}), " ", f({"a;;b".split(";", 2, :skip-empty)}), " ", f({";;a;b".split(";", 2, :skip-empty)}), " ", f({";a;b;c".split(";", 3, :skip-empty)}), " ", f({"a;b;;c".split(";", 3, :skip-empty)}), " ", f({"a;b;c;d".split(";", 3, :v, :skip-empty)})
# rakudo 2026.08: ("a", "b;c;d").Seq ("a;b;c;d",) ().Seq ().Seq ("a", "b", "c", "d").Seq ("a", "b", "c", "d").Seq ("a", "b", "c", "d").Seq T(X::AdHoc) ("a", "b;c").Seq T(X::AdHoc) T(X::AdHoc) T(X::AdHoc) T(X::AdHoc) T(X::AdHoc) T(X::TypeCheck) ("a", ";", "b;c;d").Seq ("a", 0, "b;c;d").Seq ("a", 0, ";", "b;c;d").Seq ("a", 0 => ";", "b;c;d").Seq T(X::Adverb) ("a", ";b").Seq (";a;b",).Seq ("a", "b;c").Seq ("a", "b", ";c").Seq ("a", ";", "b", ";", "c;d").Seq
sub f(&c) { my \r = try c(); $! ?? "T(" ~ $!.^name ~ ")" !! r ~~ Failure ?? "F(" ~ r.exception.^name ~ ")" !! r.raku }; say f({"abcd".split("", 1)}), " ", f({"abcd".split("", 2)}), " ", f({"abcd".split("", 3)}), " ", f({"abcd".split("", 4)}), " ", f({"abcd".split("", 5)}), " ", f({"abcd".split("", 6)}), " ", f({"abcd".split("", 7)}), " ", f({"abcd".split("", 0)}), " ", f({"abcd".split("", -1)}), " ", f({"abcd".split("", 2.5)}), " ", f({"abcd".split("", 2, :v)}), " ", f({"abcd".split("", 3, :v)}), " ", f({"abcd".split("", 3, :skip-empty)}), " ", f({"abcd".split("", 4, :skip-empty)}), " ", f({"abcd".split("", 5, :skip-empty)}), " ", f({"abcd".split("", 6, :skip-empty)}), " ", f({"".split("x", 2)}), " ", f({"".split("", 2)})
# rakudo 2026.08: ("abcd",) ("", "abcd").Seq ("", "a", "bcd").Seq ("", "a", "b", "cd").Seq ("", "a", "b", "c", "d").Seq ("", "a", "b", "c", "d", "").Seq ("", "a", "b", "c", "d", "").Seq ().Seq ().Seq T(X::AdHoc) ("", "abcd").Seq ("", "a", "bcd").Seq ("a", "bcd").Seq ("a", "b", "cd").Seq ("a", "b", "c", "d").Seq ("a", "b", "c", "d").Seq ("",) ()
sub f(&c) { my \r = try c(); $! ?? "T(" ~ $!.^name ~ ")" !! r ~~ Failure ?? "F(" ~ r.exception.^name ~ ")" !! r.raku }; say f({"a;b;c;d".split(";", 2, :end)}), " ", f({"a;b;c;d".split(";", 3, :end)}), " ", f({"a;b;c".split(";", 1, :end)}), " ", f({"a;b".split(";", 5, :end)}), " ", f({"a;b;c".split(";", 0, :end)}), " ", f({"a;b;c".split(";", *, :end)}), " ", f({"a;b;c".split(";", 2, :!end)}), " ", f({"a;b;c;d".split(";", 2, :end, :v)}), " ", f({"a;b;c".split(";", 2, :end, :k)}), " ", f({"a;b;c".split(";", 2, :end, :p)}), " ", f({"a;;b;c".split(";", 2, :end, :skip-empty)}), " ", f({"a;b;c;d".split(";", 2, :end, :skip-empty)}), " ", f({"a1b22c".split(/\d+/, 2, :end)}), " ", f({"a1b".split(/\d/, 2, :end, :v)}), " ", f({"abcabc".split(<b c>, 2, :end)}), " ", f({"a;b;c".split(";", 2, :end)[0].^name})
# rakudo 2026.08: ("a;b;c", "d").Seq ("a;b", "c", "d").Seq ("a;b;c",).Seq ("a", "b").Seq ("a", "b", "c").Seq ("a", "b", "c").Seq ("a", "b", "c").Seq ("a;b;c", "d").Seq ("a;b", "c").Seq ("a;b", "c").Seq ("a;;b", "c").Seq ("a;b;c", "d").Seq ("a1b", "c").Seq ("a", "b").Seq ("a", "cabc").Seq "Str"
```
rakupp 4.0.1: differs — a fractional limit is accepted (with odd values:
`2.7` gives `("a", "b")`, `1.5` the whole string), NaN gives `()`, the
empty needle with `4, :skip-empty` gives four characters, `:end` with 0
gives `()`, `:!end` keeps the limit, `:end` keeps `:v`/`:k`/`:p`, `:end`
with a Regex is ignored, and with a needle list gives `("abcab", "")`.

### ST-43  split with a Regex                                      D:yes R:yes V:spec
Pieces are the unmatched text; `:v` interleaves Match objects (whose
`orig` is the whole string), `:k` indices (0), `:p` Pairs of index and
Match; captures do not appear unless `:v`. A zero-width match splits
between every character with a leading and trailing `""`
(`/<?>/`, `/x*/`); `/^/` gives `("", "abc")` and `/$/` `("abc", "")`;
`/b*/` matches empty at 0 and at the end too; `:skip-empty` removes the
empties. `""` splits to `("",)` unless the regex matches empty or
`:skip-empty` (then `()`). The limit counts pieces; 0 and negative give
`()`; a fractional limit truncates; NaN throws `X::Numeric::CannotConvert`
(`X::TypeCheck` per Roast in the sub form); adverb exclusivity holds.
```
sub f(&c) { my \r = try c(); $! ?? "T(" ~ $!.^name ~ ")" !! r ~~ Failure ?? "F(" ~ r.exception.^name ~ ")" !! r.raku }; say f({"a1b22c".split(/\d+/)}), " ", f({"a1b".split(/\d/).^name}), " ", f({"a1b".split(/\d/)[0].^name}), " ", f({"a1b22c".split(/\d+/, :v)}), " ", f({"a1b22c".split(/\d+/, :v)[1].^name}), " ", f({"a1b22c".split(/\d+/, :k)}), " ", f({"a1b22c".split(/\d+/, :kv)}), " ", f({"a1b22c".split(/\d+/, :p)}), " ", f({"a1b".split(/\d/, :p)[1].value.^name}), " ", f({"a1b22c".split(/(\d)+/)}), " ", f({"a1b22c".split(/(\d)+/, :v)[1].list}), " ", f({"1a2".split(/\d/)}), " ", f({"1a2".split(/\d/, :skip-empty)}), " ", f({"".split(/x/)}), " ", f({"".split(/x/, :skip-empty)}), " ", f({"".split(/<?>/)}), " ", f({"abc".split(/<?>/)}), " ", f({"abc".split(/<?>/, :skip-empty)}), " ", f({"abc".split(/<?>/, :v).elems}), " ", f({"abc".split(/x*/)}), " ", f({"abc".split(/b*/)}), " ", f({"abc".split(/b*/, :skip-empty)}), " ", f({"abc".split(/^/)}), " ", f({"abc".split(/$/)}), " ", f({"abc".split(/^^/)}), " ", f({"a\nb".split(/^^/)}), " ", f({"a\nb".split(/$$/)}), " ", f({"abc".split(/<?before c>/)}), " ", f({"abc".split(/<?after a>/)}), " ", f({"aXbxc".split(/:i x/)}), " ", f({"a1b22c".split(/\d+/, 2)}), " ", f({"a1b22c".split(/\d+/, 2, :v)}), " ", f({"a1b22c".split(/\d+/, 1)}), " ", f({"a1b22c".split(/\d+/, 0)}), " ", f({"a1b22c".split(/\d+/, -1)}), " ", f({"a1b22c".split(/\d+/, *)}), " ", f({"a1b22c".split(/\d+/, 2.9)}), " ", f({"a1b".split(/\d/, NaN).eager}), " ", f({"a1b".split(/\d/, :v, :k).eager}), " ", f({split(/\d/, "a1b")})
# rakudo 2026.08: ("a", "b", "c").Seq "Seq" "Str" ("a", Match.new(:orig("a1b22c"), :from(1), :pos(2)), "b", Match.new(:orig("a1b22c"), :from(3), :pos(5)), "c").Seq "Match" ("a", 0, "b", 0, "c").Seq ("a", 0, Match.new(:orig("a1b22c"), :from(1), :pos(2)), "b", 0, Match.new(:orig("a1b22c"), :from(3), :pos(5)), "c").Seq ("a", 0 => Match.new(:orig("a1b22c"), :from(1), :pos(2)), "b", 0 => Match.new(:orig("a1b22c"), :from(3), :pos(5)), "c").Seq "Match" ("a", "b", "c").Seq ([Match.new(:orig("a1b22c"), :from(1), :pos(2))],) ("", "a", "").Seq ("a",).Seq ("",).Seq ().Seq ("", "").Seq ("", "a", "b", "c", "").Seq ("a", "b", "c").Seq 9 ("", "a", "b", "c", "").Seq ("", "a", "", "c", "").Seq ("a", "c").Seq ("", "abc").Seq ("abc", "").Seq ("", "abc").Seq ("", "a\n", "b").Seq ("a", "\nb", "").Seq ("ab", "c").Seq ("a", "bc").Seq ("a", "b", "c").Seq ("a", "b22c").Seq ("a", Match.new(:orig("a1b22c"), :from(1), :pos(2)), "b22c").Seq ("a1b22c",).Seq ().Seq ().Seq ("a", "b", "c").Seq ("a", "b22c").Seq T(X::Numeric::CannotConvert) T(X::Adverb) ("a", "b").Seq
```
rakupp 4.0.1: differs — a Match from a split carries only the matched text
as `orig`, `:v` on a capturing regex gives an empty capture list, NaN gives
`()`, and `:v, :k` together are accepted.

### ST-44  split with a list of needles                            D:yes R:partial V:spec/bug
Needles may be Str, Cool or Regex; at each position the earliest match
wins and, at the same position, the longest (`("a", "aa")` on `"aaa"`
takes `"aa"` first); `:k` gives the needle's index, `:p` index ⇒ text.
**Bug:** the interleaved values are always Str, also for a Regex needle,
although the docs promise Match objects unless every needle is Cool.
A type-object element and an empty-string element are ignored (`("",)`
and `(Str,)` leave the string whole); an empty list gives `()` (it is
treated as the empty needle with limit 0). The limit counts pieces (2 with
a fractional limit truncates); a huge limit throws `X::AdHoc`, NaN
`X::TypeCheck`. `:skip-empty` drops empties including a leading one.
**Bug:** a zero-width regex needle (`/<?>/`, `/x*/`, `/b*/`) never
advances and Rakudo loops until killed. Roast `split.t` asserts the
longest-first rule and mixed Str/Regex lists.
```
sub f(&c) { my \r = try c(); $! ?? "T(" ~ $!.^name ~ ")" !! r ~~ Failure ?? "F(" ~ r.exception.^name ~ ")" !! r.raku }; say f({"a;b,c".split(<; ,>)}), " ", f({"a;b,c".split(<; ,>, :v)}), " ", f({"a;b,c".split(<; ,>, :k)}), " ", f({"a;b,c".split(<; ,>, :kv)}), " ", f({"a;b,c".split(<; ,>, :p)}), " ", f({"aXbYc".split(<Y X>, :k)}), " ", f({"aXbYc".split(<Y X>, :p)}), " ", f({"abc".split(<b b>, :k)}), " ", f({"abc".split(("b", "b"), :v)}), " ", f({"1a2bb345".split(['a', /b+/, 4])}), " ", f({"1a2bb345".split(['a', /b+/, 4], :v)}), " ", f({"1a2bb345".split(['a', /b+/, 4], :v)[1].^name}), " ", f({"abc".split(("b", /c/), :v)[1].^name}), " ", f({"abc".split(("b", /c/), :v)[3].^name}), " ", f({"abc".split(("b",), :v)[1].^name}), " ", f({"abc".split((/b/,))}), " ", f({"a1b2c".split((/\d/,), :v)[1].^name}), " ", f({"abab".split(("ab", /ab/), :k)}), " ", f({"abc".split((/b/, "b"), :v)}), " ", f({"aXbXc".split(("X", "bX"))}), " ", f({"aXbXc".split(("bX", "X"))}), " ", f({"abXcd".split(("X", "bX"))}), " ", f({"abXcd".split(("b", "bX"))}), " ", f({"abXcd".split(("bXc", "b"))}), " ", f({"aaa".split(("a", "aa"))}), " ", f({"aaa".split(("aa", "a"), :v)}), " ", f({"abcabc".split(<bc b>, :v)}), " ", f({"abcabc".split(<b bc>, :v)}), " ", f({"aaaa".split(("a", "aa"), :skip-empty)}), " ", f({"a1b".split((1,))}), " ", f({"abc".split(["b"])}), " ", f({"abc".split(("b" => 1,))})
# rakudo 2026.08: ("a", "b", "c").Seq ("a", ";", "b", ",", "c").Seq ("a", 0, "b", 1, "c").Seq ("a", 0, ";", "b", 1, ",", "c").Seq ("a", 0 => ";", "b", 1 => ",", "c").Seq ("a", 1, "b", 0, "c").Seq ("a", 1 => "X", "b", 0 => "Y", "c").Seq ("a", 0, "c").Seq ("a", "b", "c").Seq ("1", "2", "3", "5").Seq ("1", "a", "2", "bb", "3", "4", "5").Seq "Str" "Str" "Str" "Str" ("a", "c").Seq "Str" ("", 0, "", 0, "").Seq ("a", "b", "c").Seq ("a", "", "c").Seq ("a", "", "c").Seq ("a", "cd").Seq ("a", "cd").Seq ("a", "d").Seq ("", "", "").Seq ("", "aa", "", "a", "").Seq ("a", "bc", "a", "bc", "").Seq ("a", "bc", "a", "bc", "").Seq ().Seq ("a", "b").Seq ("a", "c").Seq ("abc",).Seq
sub f(&c) { my \r = try c(); $! ?? "T(" ~ $!.^name ~ ")" !! r ~~ Failure ?? "F(" ~ r.exception.^name ~ ")" !! r.raku }; say f({"abc".split(())}), " ", f({"abc".split(("",))}), " ", f({"abc".split((Str,))}), " ", f({"abc".split((Str, "b"))}), " ", f({"abc".split((Any, "b"))}), " ", f({"abc".split((Int, "b"), :k)}), " ", f({"abc".split(("b", Str), :k)}), " ", f({"abc".split(("", "b"))}), " ", f({"abc".split(("x", "b"), :k)}), " ", f({"abc".split((/x/,))}), " ", f({"a;b,c;d".split(<; ,>, 2)}), " ", f({"a;b,c;d".split(<; ,>, 3, :v)}), " ", f({"a;b,c;d".split(<; ,>, 0)}), " ", f({"aXbXc".split(("X",), 1)}), " ", f({"aXbXc".split(("X",), 2, :v)}), " ", f({"abcabc".split(<b c>, 3)}), " ", f({"abcabc".split(<b c>, 3, :v)}), " ", f({"aaaa".split(<aa a>, 3, :v)}), " ", f({"abc".split(("b",), 2**70)}), " ", f({"a,b".split((",",), 2.5)}), " ", f({"a,b".split((",",), NaN).eager}), " ", f({";a;;b".split(<; ,>, :skip-empty)}), " ", f({";a;;b".split(<; ,>, :skip-empty, :v)}), " ", f({"bab".split(("b",), :skip-empty)}), " ", f({"bab".split(("b",), :v, :skip-empty)}), " ", f({"bb".split(("b",), :v, :skip-empty)}), " ", f({split(<; ,>, "a;b,c", 2)})
# rakudo 2026.08: ().Seq ("abc",).Seq ("abc",).Seq ("a", "c").Seq ("a", "c").Seq ("a", 1, "c").Seq ("a", 0, "c").Seq ("a", "c").Seq ("a", 1, "c").Seq ("abc",).Seq ("a", "b,c;d").Seq ("a", ";", "b", ",", "c;d").Seq ().Seq ("aXbXc",).Seq ("a", "X", "bXc").Seq ("a", "", "abc").Seq ("a", "b", "", "c", "abc").Seq ("", "aa", "", "aa", "").Seq T(X::AdHoc) ("a", "b").Seq T(X::TypeCheck) ("a", "b").Seq (";", "a", ";", ";", "b").Seq ("a",).Seq ("b", "a", "b").Seq ("b", "b").Seq ("a", "b,c").Seq
say (try "abc".split([/<?>/]).eager.raku) // $!.^name
# rakudo 2026.08: (no output; killed by the 10 s alarm)
```
rakupp 4.0.1: differs — Regex needles give Match objects (the documented
behaviour), an empty list gives the whole string, `("",)` and `(Str,)`
split into characters, a huge limit or NaN is accepted, and the zero-width
needle terminates with the whole string.

## H. Matching and substitution

### ST-45  match: patterns, Nil, and $/                            D:yes R:yes V:spec/quirk
A non-Regex pattern is stringified and matched literally, ratcheted, at
the first occurrence (`"a.b".match(".")` is at 1, `[1,2,3]` matches
`"1 2 3"`, `1.5` finds `"1.5"`, an empty string matches at 0). The result
is a Match (whose `orig` is the whole string) or Nil, and the caller's
`$/` is set to the same. A type object, Any or Nil pattern is
`X::Multi::NoMatch`. **Quirk:** an unknown named argument is ignored, so
`:i` on `.match` does nothing (`"abc".match(/B/, :i)` is Nil; use `/:i B/`).
```
sub f(&c) { my \r = try c(); $! ?? "T(" ~ $!.^name ~ ")" !! r ~~ Failure ?? "F(" ~ r.exception.^name ~ ")" !! r.raku }; say f({"properly".match("perl")}), " ", f({"properly".match(/p.../)}), " ", f({"1 2 3".match([1,2,3])}), " ", f({"a.b".match(".")}), " ", f({"a+b".match("+").from}), " ", f({"abab".match("ab").from}), " ", f({"xaaa".match("aa").from}), " ", f({"abc".match("")}), " ", f({"abc".match(/<?>/)}), " ", f({"abc".match(<b>)}), " ", f({"abc".match(1.5)}), " ", f({"a1.5b".match(1.5)}), " ", f({123.match(2)}), " ", f({"abc".match("x")}), " ", f({"abc".match(/x/)}), " ", f({"abc".match(/x/).Bool}), " ", f({"abc".match(/x/).defined}), " ", f({"abc".match(/b/).^name}), " ", f({"abc".match("b").^name}), " ", f({"abc".match(/b/).Bool}), " ", f({"abc".match(/b/); $/}), " ", f({"abc".match(/x/); $/}), " ", f({"abc".match("b"); $/}), " ", f({"abc".match(/(b)(c)/).list}), " ", f({"abc".match(/$<x>=b/).hash}), " ", f({"abc".match(/b/).prematch ~ "|" ~ "abc".match(/b/).postmatch ~ "|" ~ "abc".match(/b/).orig}), " ", f({"abc".match(/B/)}), " ", f({"abc".match(/:i B/)}), " ", f({"abc".match(/B/, :i)}), " ", f({"abc".match("B", :i)}), " ", f({"abc".match(/b/, :bogus)}), " ", f({"abc".match(Str)}), " ", f({"abc".match(Any)}), " ", f({"abc".match(Nil)})
# rakudo 2026.08: Match.new(:orig("properly"), :from(3), :pos(7)) Match.new(:orig("properly"), :from(0), :pos(4)) Match.new(:orig("1 2 3"), :from(0), :pos(5)) Match.new(:orig("a.b"), :from(1), :pos(2)) 1 0 1 Match.new(:orig("abc"), :from(0), :pos(0)) Match.new(:orig("abc"), :from(0), :pos(0)) Match.new(:orig("abc"), :from(1), :pos(2)) Nil Match.new(:orig("a1.5b"), :from(1), :pos(4)) Match.new(:orig("123"), :from(1), :pos(2)) Nil Nil Bool::False Bool::False "Match" "Match" Bool::True Match.new(:orig("abc"), :from(1), :pos(2)) Nil Match.new(:orig("abc"), :from(1), :pos(2)) (Match.new(:orig("abc"), :from(1), :pos(2)), Match.new(:orig("abc"), :from(2), :pos(3))) Map.new((:x(Match.new(:orig("abc"), :from(1), :pos(2))))) "a|c|abc" Nil Match.new(:orig("abc"), :from(1), :pos(2)) Nil Nil Match.new(:orig("abc"), :from(1), :pos(2)) T(X::Multi::NoMatch) T(X::Multi::NoMatch) T(X::Multi::NoMatch)
```
rakupp 4.0.1: differs — a literal pattern's Match has only the matched text
as `orig`, an allomorph, Rat or Int pattern/invocant is
`X::Method::NotFound`, and `:i` on `.match` is applied.

### ST-46  :g, :ov, :ex                                            D:yes R:yes V:spec
Each returns a List of Match objects and sets `$/` to that List — an empty
List, not Nil, when nothing matches. `:g` is non-overlapping (a zero-width
match advances one character: `/<?>/` gives 4 on `"abc"`); `:ov` gives one
match per start position (the longest); `:ex` gives every match at every
position. `:g, :ov` is `:ov`; `:g, :ex` is `:ex`. A false value
(`:!g`, `:g(0)`) means the plain single match. Roast `S05-modifier/`
asserts `:global`, `:overlap` and `:exhaustive`.
```
sub f(&c) { my \r = try c(); $! ?? "T(" ~ $!.^name ~ ")" !! r ~~ Failure ?? "F(" ~ r.exception.^name ~ ")" !! r.raku }; say f({"aaa".match(/aa/, :g)}), " ", f({"aaa".match(/aa/, :ov)}), " ", f({"aaa".match(/a+/, :ov)}), " ", f({"aaa".match(/a+/, :ex)}), " ", f({"aaa".match(/aa/, :g, :ov)}), " ", f({"aaa".match(/a+/, :g, :ex)}), " ", f({"abc".match(/x/, :g)}), " ", f({"abc".match(/x/, :g).^name}), " ", f({"abc".match(/x/, :ov)}), " ", f({"abc".match(/x/, :ex)}), " ", f({"abc".match(/b/, :!g)}), " ", f({"abc".match(/\w/, :g(0))}), " ", f({"abc".match(/\w/, :ov(0))}), " ", f({"abc".match(/\w/, :ex(0))}), " ", f({"abc".match(/\w/, :g).^name}), " ", f({"abc".match(/\w/, :g)[0].^name}), " ", f({"abc".match("b", :g)}), " ", f({"abcabc".match("b", :g).map(*.from).List}), " ", f({"abc".match(/b/, :g); $/}), " ", f({"abc".match(/x/, :g); $/}), " ", f({"abc".match(/\w/, :g); $/.^name}), " ", f({"abc".match(/<?>/, :g).elems}), " ", f({"abc".match(/<?>/, :ov).elems}), " ", f({"abc".match(/<?>/, :ex).elems}), " ", f({"abc".match(/x*/, :g).map(*.from).List}), " ", f({"abc".match(/b*/, :g).map(*.Str).List}), " ", f({"abc".match(/b*/, :ov).map(*.Str).List}), " ", f({"abc".match(/b*/, :ex).map(*.Str).List}), " ", f({"aaa".match(/a+/, :ex).map(*.Str).List}), " ", f({"ab".match(/.<(/, :g)})
# rakudo 2026.08: (Match.new(:orig("aaa"), :from(0), :pos(2)),) (Match.new(:orig("aaa"), :from(0), :pos(2)), Match.new(:orig("aaa"), :from(1), :pos(3))) (Match.new(:orig("aaa"), :from(0), :pos(3)), Match.new(:orig("aaa"), :from(1), :pos(3)), Match.new(:orig("aaa"), :from(2), :pos(3))) (Match.new(:orig("aaa"), :from(0), :pos(3)), Match.new(:orig("aaa"), :from(0), :pos(2)), Match.new(:orig("aaa"), :from(0), :pos(1)), Match.new(:orig("aaa"), :from(1), :pos(3)), Match.new(:orig("aaa"), :from(1), :pos(2)), Match.new(:orig("aaa"), :from(2), :pos(3))) (Match.new(:orig("aaa"), :from(0), :pos(2)), Match.new(:orig("aaa"), :from(1), :pos(3))) (Match.new(:orig("aaa"), :from(0), :pos(3)), Match.new(:orig("aaa"), :from(0), :pos(2)), Match.new(:orig("aaa"), :from(0), :pos(1)), Match.new(:orig("aaa"), :from(1), :pos(3)), Match.new(:orig("aaa"), :from(1), :pos(2)), Match.new(:orig("aaa"), :from(2), :pos(3))) () "List" () () Match.new(:orig("abc"), :from(1), :pos(2)) Match.new(:orig("abc"), :from(0), :pos(1)) Match.new(:orig("abc"), :from(0), :pos(1)) Match.new(:orig("abc"), :from(0), :pos(1)) "List" "Match" (Match.new(:orig("abc"), :from(1), :pos(2)),) (1, 4) $(Match.new(:orig("abc"), :from(1), :pos(2)),) $( ) "List" 4 4 4 (0, 1, 2, 3) ("", "b", "", "") ("", "b", "", "") ("", "b", "", "", "") ("aaa", "aa", "a", "aa", "a", "a") (Match.new(:orig("ab"), :from(1), :pos(1)),)
```
rakupp 4.0.1: differs — `:ov` and `:ex` return a single Match, `:g(0)`,
`:ov(0)` and `:ex(0)` are `X::Syntax::Regex::Adverb`, and a zero-width
pattern under `:ov`/`:ex` finds nothing.

### ST-47  :x                                                      D:yes R:yes V:spec/quirk
`:x(n)` returns a List of exactly *n* matches, or `()` if fewer exist
(never Nil); `:x(0)` is `()`; a negative *n* is `()`. A Range gives
between `min` and `max` matches (`()` if fewer than `min`), with `*`,
`Inf` or an open end meaning "all"; **quirk:** exclusive endpoints are
ignored (`2^..^4` behaves as `2..4`) and `0..*` is not "any number but at
least none" but "all". `*` and Inf give all; a Rat truncates; `True` is 1;
`:!x` is `:x(0)`. NaN and `-Inf` throw `X::AdHoc`. A non-numeric,
non-Range value is a **returned** `X::Str::Match::x` Failure with `got`,
and `$/` is set to Nil. `:x` combines with `:g`/`:ov` (same result),
`:as(Str)` (strings), `:c`/`:p` (start position). Roast
`S05-modifier/counted-match.t` and `subst.t` assert the counts and ranges.
```
sub f(&c) { my \r = try c(); $! ?? "T(" ~ $!.^name ~ ")" !! r ~~ Failure ?? "F(" ~ r.exception.^name ~ ")" !! r.raku }; say f({"aaaa".match(/a/, :x(2))}), " ", f({"aaaa".match(/a/, :x(2)).^name}), " ", f({"aaaa".match(/a/, :x(1))}), " ", f({"aaaa".match(/a/, :x(4))}), " ", f({"aaaa".match(/a/, :x(5))}), " ", f({"aaaa".match(/a/, :x(5)).^name}), " ", f({"aaaa".match(/a/, :x(0))}), " ", f({"aaaa".match(/a/, :x(0)).^name}), " ", f({"aaaa".match(/a/, :x(-1))}), " ", f({"aaaa".match(/a/, :x(2..3))}), " ", f({"aaaa".match(/a/, :x(1..1))}), " ", f({"aaaa".match(/a/, :x(0..1))}), " ", f({"aaaa".match(/a/, :x(0..0))}), " ", f({"aaaa".match(/a/, :x(2^..^4))}), " ", f({"aaaa".match(/a/, :x(3..2))}), " ", f({"aaaa".match(/a/, :x(5..6))}), " ", f({"aaaa".match(/a/, :x(2..*))}), " ", f({"aaaa".match(/a/, :x(5..*))}), " ", f({"aaaa".match(/a/, :x(0..*))}), " ", f({"aaaa".match(/a/, :x(*))}), " ", f({"aaaa".match(/a/, :x(Inf))}), " ", f({"aaaa".match(/a/, :x(2.7))}), " ", f({"aaaa".match(/a/, :x(2.0))}), " ", f({"aaaa".match(/a/, :x(True))}), " ", f({"aaaa".match(/a/, :!x)}), " ", f({"aaaa".match(/a/, :x(NaN))}), " ", f({"aaaa".match(/a/, :x(-Inf))}), " ", f({"aaaa".match(/x/, :x(1))}), " ", f({"aaaa".match(/x/, :x(0))}), " ", f({"aaaa".match(/x/, :x(*))}), " ", f({"aaaa".match(/x/, :x(0..1))}), " ", f({"aaaa".match("a", :x(2))}), " ", f({"aaaa".match(/a/, :x(2), :g)}), " ", f({"aaaa".match(/a/, :x(2), :ov)}), " ", f({"aaaa".match(/a/, :x(2), :as(Str))}), " ", f({"aaaa".match(/a/, :x(2), :c(1))}), " ", f({"aaaa".match(/a/, :x(2), :p(1))}), " ", f({"aaaa".match(/a/, :x(2)); $/}), " ", f({"aaaa".match(/a/, :x(9)); $/}), " ", f({my $r = "aaaa".match(/a/, :x("2")); $r.^name ~ ":" ~ $r.exception.^name ~ ":" ~ $r.exception.got.raku}), " ", f({"aaaa".match(/a/, :x(Str)); $/})
# rakudo 2026.08: (Match.new(:orig("aaaa"), :from(0), :pos(1)), Match.new(:orig("aaaa"), :from(1), :pos(2))) "List" (Match.new(:orig("aaaa"), :from(0), :pos(1)),) (Match.new(:orig("aaaa"), :from(0), :pos(1)), Match.new(:orig("aaaa"), :from(1), :pos(2)), Match.new(:orig("aaaa"), :from(2), :pos(3)), Match.new(:orig("aaaa"), :from(3), :pos(4))) () "List" () "List" () (Match.new(:orig("aaaa"), :from(0), :pos(1)), Match.new(:orig("aaaa"), :from(1), :pos(2)), Match.new(:orig("aaaa"), :from(2), :pos(3))) (Match.new(:orig("aaaa"), :from(0), :pos(1)),) (Match.new(:orig("aaaa"), :from(0), :pos(1)),) () (Match.new(:orig("aaaa"), :from(0), :pos(1)), Match.new(:orig("aaaa"), :from(1), :pos(2)), Match.new(:orig("aaaa"), :from(2), :pos(3)), Match.new(:orig("aaaa"), :from(3), :pos(4))) () () (Match.new(:orig("aaaa"), :from(0), :pos(1)), Match.new(:orig("aaaa"), :from(1), :pos(2)), Match.new(:orig("aaaa"), :from(2), :pos(3)), Match.new(:orig("aaaa"), :from(3), :pos(4))) () (Match.new(:orig("aaaa"), :from(0), :pos(1)), Match.new(:orig("aaaa"), :from(1), :pos(2)), Match.new(:orig("aaaa"), :from(2), :pos(3)), Match.new(:orig("aaaa"), :from(3), :pos(4))) (Match.new(:orig("aaaa"), :from(0), :pos(1)), Match.new(:orig("aaaa"), :from(1), :pos(2)), Match.new(:orig("aaaa"), :from(2), :pos(3)), Match.new(:orig("aaaa"), :from(3), :pos(4))) (Match.new(:orig("aaaa"), :from(0), :pos(1)), Match.new(:orig("aaaa"), :from(1), :pos(2)), Match.new(:orig("aaaa"), :from(2), :pos(3)), Match.new(:orig("aaaa"), :from(3), :pos(4))) (Match.new(:orig("aaaa"), :from(0), :pos(1)), Match.new(:orig("aaaa"), :from(1), :pos(2))) (Match.new(:orig("aaaa"), :from(0), :pos(1)), Match.new(:orig("aaaa"), :from(1), :pos(2))) (Match.new(:orig("aaaa"), :from(0), :pos(1)),) () T(X::AdHoc) T(X::AdHoc) () () () () (Match.new(:orig("aaaa"), :from(0), :pos(1)), Match.new(:orig("aaaa"), :from(1), :pos(2))) (Match.new(:orig("aaaa"), :from(0), :pos(1)), Match.new(:orig("aaaa"), :from(1), :pos(2))) (Match.new(:orig("aaaa"), :from(0), :pos(1)), Match.new(:orig("aaaa"), :from(1), :pos(2))) ("a", "a") (Match.new(:orig("aaaa"), :from(1), :pos(2)), Match.new(:orig("aaaa"), :from(2), :pos(3))) (Match.new(:orig("aaaa"), :from(1), :pos(2)), Match.new(:orig("aaaa"), :from(2), :pos(3))) $(Match.new(:orig("aaaa"), :from(0), :pos(1)), Match.new(:orig("aaaa"), :from(1), :pos(2))) $( ) "Failure:X::Str::Match::x:\"2\"" T(X::AdHoc)
```
rakupp 4.0.1: differs — `2..*` gives 3 and `0..*` gives `()`, `True` and
`:!x` give a single Match, NaN and `-Inf` give `()`, and the invalid value
throws `X::Str::Match::x` instead of failing.

### ST-48  :nth and its aliases                                    D:yes R:yes V:spec/quirk
`:nth(n)`, `:1st`, `:2nd`, `:3rd`, `:4th`, `:st(n)`, `:th(n)` return the
*n*-th match or Nil; a Rat truncates and a Callable is called for *n*;
`*`, Inf and `*-0` give the last match, `*-k` the *k*-th from the end, Nil
when out of range; `:nth(0)`, a negative *n*, `*+1` and `:!nth` throw
`X::AdHoc`; `:nth(Nil)` is a plain match. **Quirk:** a Str value is an
Iterable of one, so `:nth("2")` gives a one-element List. An Iterable
(list, Range, open Range) gives a List of the named matches, `()` when the
first is missing or the Range is reversed, and stops silently when the
string runs out (`:nth(2, 5)`); the list must be increasing — a
non-monotonic sequence throws `X::AdHoc` at reification (`.eager`).
`:x(n)` with an Iterable `:nth` requires exactly *n* of them to exist,
else `()`; with a single `:nth` it wraps the one match. `:g`/`:ov` do not
change a single-`nth` result; `:as(Str)` gives strings; `:c`/`:p` count
from that position. Roast `counted-match.t` asserts the lists, ranges,
`:x` combinations and the non-monotonic death.
```
sub f(&c) { my \r = try c(); $! ?? "T(" ~ $!.^name ~ ")" !! r ~~ Failure ?? "F(" ~ r.exception.^name ~ ")" !! r.raku }; say f({"abcd".match(/./, :nth(2))}), " ", f({"abcd".match(/./, :2nd)}), " ", f({"abcd".match(/./, :1st)}), " ", f({"abcd".match(/./, :3rd)}), " ", f({"abcd".match(/./, :4th)}), " ", f({"abcd".match(/./, :st(2))}), " ", f({"abcd".match(/./, :th(4))}), " ", f({"abcd".match(/./, :nth(2), :nd(3))}), " ", f({"abcd".match(/./, :nth(5))}), " ", f({"abcd".match(/./, :nth(5)).^name}), " ", f({"abcd".match(/./, :nth(2.7))}), " ", f({"abcd".match(/./, :nth({ 2 }))}), " ", f({"abcd".match(/./, :nth(*))}), " ", f({"abcd".match(/./, :nth(Inf))}), " ", f({"abcd".match(/./, :nth(*-0))}), " ", f({"abcd".match(/./, :nth(*-1))}), " ", f({"abcd".match(/./, :nth(*-2))}), " ", f({"abcd".match(/./, :nth(*-5))}), " ", f({"abcd".match(/./, :nth(*+1))}), " ", f({"abcd".match(/./, :nth(0))}), " ", f({"abcd".match(/./, :nth(-1))}), " ", f({"abcd".match(/./, :nth("2"))}), " ", f({"abcd".match(/./, :nth(Nil))}), " ", f({"abcd".match(/./, :!nth)}), " ", f({"abcd".match(/x/, :nth(1))})
# rakudo 2026.08: Match.new(:orig("abcd"), :from(1), :pos(2)) Match.new(:orig("abcd"), :from(1), :pos(2)) Match.new(:orig("abcd"), :from(0), :pos(1)) Match.new(:orig("abcd"), :from(2), :pos(3)) Match.new(:orig("abcd"), :from(3), :pos(4)) Match.new(:orig("abcd"), :from(1), :pos(2)) Match.new(:orig("abcd"), :from(3), :pos(4)) Match.new(:orig("abcd"), :from(2), :pos(3)) Nil "Nil" Match.new(:orig("abcd"), :from(1), :pos(2)) Match.new(:orig("abcd"), :from(1), :pos(2)) Match.new(:orig("abcd"), :from(3), :pos(4)) Match.new(:orig("abcd"), :from(3), :pos(4)) Match.new(:orig("abcd"), :from(3), :pos(4)) Match.new(:orig("abcd"), :from(2), :pos(3)) Match.new(:orig("abcd"), :from(1), :pos(2)) Nil Nil T(X::AdHoc) T(X::AdHoc) (Match.new(:orig("abcd"), :from(1), :pos(2)),) Match.new(:orig("abcd"), :from(0), :pos(1)) T(X::AdHoc) Nil
sub f(&c) { my \r = try c(); $! ?? "T(" ~ $!.^name ~ ")" !! r ~~ Failure ?? "F(" ~ r.exception.^name ~ ")" !! r.raku }; say f({"abcd".match(/./, :nth(1, 3))}), " ", f({"abcd".match(/./, :nth(1, 3)).^name}), " ", f({"abcd".match(/./, :nth((1, 3, 4)))}), " ", f({"abcd".match(/./, :nth(2..3))}), " ", f({"abcd".match(/./, :nth(2..3)); $/}), " ", f({"abcd".match(/./, :nth(2^..3))}), " ", f({"abcd".match(/./, :nth(2..*)).List}), " ", f({"abcd".match(/./, :nth(1..*)).elems}), " ", f({"abcd".match(/./, :nth(3..9))}), " ", f({"abcd".match(/./, :nth(9..10))}), " ", f({"abcd".match(/./, :nth(9..10)).^name}), " ", f({"abcd".match(/./, :nth(2..1))}), " ", f({"abcd".match(/./, :nth(2, 5))}), " ", f({"abcd".match(/x/, :nth(1..2))}), " ", f({"abcd".match(/./, :nth(0..2)).eager}), " ", f({"abcd".match(/./, :nth(3, 1)).eager}), " ", f({"abcd".match(/./, :nth(3, 3)).eager}), " ", f({"abcd".match(/./, :nth(1, 1)).eager}), " ", f({"abcd".match(/./, :nth(2), :x(1))}), " ", f({"abcd".match(/./, :nth(2), :x(2))}), " ", f({"abcd".match(/./, :nth(2..3), :x(1))}), " ", f({"abcd".match(/./, :nth(2..3), :x(2))}), " ", f({"abcd".match(/./, :nth(2..3), :x(3))}), " ", f({"abcd".match(/./, :nth(1..*), :x(2))}), " ", f({"abcd".match(/./, :nth(*-1), :x(1))}), " ", f({"abcd".match(/./, :nth(2), :g)}), " ", f({"abcd".match(/./, :nth(2..3), :g)}), " ", f({"abcd".match(/./, :nth(2..3), :ov)}), " ", f({"abcd".match(/./, :nth(2), :as(Str))}), " ", f({"abcd".match(/./, :nth(2..3), :as(Str))}), " ", f({"abcd".match(/./, :nth(2), :c(2))}), " ", f({"abcd".match(/./, :nth(2), :p(1))})
# rakudo 2026.08: (Match.new(:orig("abcd"), :from(0), :pos(1)), Match.new(:orig("abcd"), :from(2), :pos(3))) "List" (Match.new(:orig("abcd"), :from(0), :pos(1)), Match.new(:orig("abcd"), :from(2), :pos(3)), Match.new(:orig("abcd"), :from(3), :pos(4))) (Match.new(:orig("abcd"), :from(1), :pos(2)), Match.new(:orig("abcd"), :from(2), :pos(3))) $(Match.new(:orig("abcd"), :from(1), :pos(2)), Match.new(:orig("abcd"), :from(2), :pos(3))) (Match.new(:orig("abcd"), :from(2), :pos(3)),) (Match.new(:orig("abcd"), :from(1), :pos(2)), Match.new(:orig("abcd"), :from(2), :pos(3)), Match.new(:orig("abcd"), :from(3), :pos(4))) 4 (Match.new(:orig("abcd"), :from(2), :pos(3)), Match.new(:orig("abcd"), :from(3), :pos(4))) () "List" () (Match.new(:orig("abcd"), :from(1), :pos(2)),) () T(X::AdHoc) T(X::AdHoc) T(X::AdHoc) T(X::AdHoc) () () (Match.new(:orig("abcd"), :from(1), :pos(2)),) (Match.new(:orig("abcd"), :from(1), :pos(2)), Match.new(:orig("abcd"), :from(2), :pos(3))) () (Match.new(:orig("abcd"), :from(0), :pos(1)), Match.new(:orig("abcd"), :from(1), :pos(2))) () Match.new(:orig("abcd"), :from(1), :pos(2)) (Match.new(:orig("abcd"), :from(1), :pos(2)), Match.new(:orig("abcd"), :from(2), :pos(3))) (Match.new(:orig("abcd"), :from(1), :pos(2)), Match.new(:orig("abcd"), :from(2), :pos(3))) "b" ("b", "c") Match.new(:orig("abcd"), :from(3), :pos(4)) Match.new(:orig("abcd"), :from(2), :pos(3))
```
rakupp 4.0.1: differs — a missing *n*-th match is `()` rather than Nil, a
Rat, Callable, Inf, `*-k`, `*+1` or Str value is `X::AdHoc`, `:nth(Nil)`
is `()`, a reversed Range still matches, `:nth(2), :g` is a List, and
`:as(Str)`/`:c`/`:p` are not combined.

### ST-49  :c, :p and :as                                          D:partial R:partial V:spec/bug
`:c`/`:continue(n)` searches from *n*; `:p`/`:pos(n)` requires the match to
start at *n* (Roast `S05-modifier/pos.t`). Either beyond the end is Nil,
`:c(Nil)` is Nil while `:p(Nil)` means 0, a non-Int position (`1.9`,
`"1"`, `1e0`, a big Int) throws `X::AdHoc`, and `:p` wins over `:c`.
**Bugs:** a negative position produces a Match with `from` −1
(`"abc".match(/./, :c(-1))`), and the bare adverb `:c` means position 1
regardless of `$/` (the docs say `$/.to`); bare `:p` likewise. With `:g`
the list starts from the position (`:p` additionally needs the first match
there). `:as(Str)` returns the matched text instead of a Match (also for
the `$/` variable and each element under `:g`/`:x`/`:nth`); any Str value
selects it and any other value means Match. **Quirk:** `:as` is ignored
when combined with `:c` or `:p`.
```
sub f(&c) { my \r = try c(); $! ?? "T(" ~ $!.^name ~ ")" !! r ~~ Failure ?? "F(" ~ r.exception.^name ~ ")" !! r.raku }; say f({"a1xa2".match(/a./, :c(2))}), " ", f({"a1xa2".match(/a./, :continue(1))}), " ", f({"abcdef".match(/.*/, :p(2))}), " ", f({"abcdef".match(/c/, :pos(2))}), " ", f({"abcdef".match(/c/, :p(1))}), " ", f({"abcdef".match(/c/, :c(1))}), " ", f({"abcdef".match(/c/, :c(Nil))}), " ", f({"abcdef".match(/c/, :p(Nil))}), " ", f({"abcabc".match(/./, :c(0))}), " ", f({"abcabc".match(/./, :p(0))}), " ", f({"abc".match(/./, :c(1.9))}), " ", f({"abc".match(/./, :c("1"))}), " ", f({"abc".match(/./, :c(1e0))}), " ", f({"abc".match("c", :c(1))}), " ", f({"abc".match("c", :p(1))}), " ", f({"abc".match("c", :p(2))}), " ", f({"abc".match(/$/, :c(3))}), " ", f({"abc".match(/$/, :p(3))}), " ", f({"abc".match(/./, :c(3))}), " ", f({"abc".match(/./, :c(4))}), " ", f({"abc".match(/./, :p(4))}), " ", f({"abcdef".match(/c/, :c(10))}), " ", f({"abcdef".match(/c/, :p(10))}), " ", f({"abc".match(/./, :c(-1))}), " ", f({"abc".match(/./, :p(-1))}), " ", f({"abc".match(/./, :c(2**70))}), " ", f({"abcabc".match(/b/); "abcabc".match(/./, :c)}), " ", f({$/ = Nil; "abcabc".match(/./, :c)}), " ", f({"abc".match(/./, :p)}), " ", f({"abcabc".match(/b/); "abcabc".match(/./, :p)}), " ", f({"abc".match(/./, :c(1), :p(2))})
# rakudo 2026.08: Match.new(:orig("a1xa2"), :from(3), :pos(5)) Match.new(:orig("a1xa2"), :from(3), :pos(5)) Match.new(:orig("abcdef"), :from(2), :pos(6)) Match.new(:orig("abcdef"), :from(2), :pos(3)) Nil Match.new(:orig("abcdef"), :from(2), :pos(3)) Nil Match.new(:orig("abcdef"), :from(2), :pos(3)) Match.new(:orig("abcabc"), :from(0), :pos(1)) Match.new(:orig("abcabc"), :from(0), :pos(1)) T(X::AdHoc) T(X::AdHoc) T(X::AdHoc) Match.new(:orig("abc"), :from(2), :pos(3)) Nil Match.new(:orig("abc"), :from(2), :pos(3)) Match.new(:orig("abc"), :from(3), :pos(3)) Match.new(:orig("abc"), :from(3), :pos(3)) Nil Nil Nil Nil Nil Match.new(:orig("abc"), :from(-1), :pos(0)) Match.new(:orig("abc"), :from(-1), :pos(0)) T(X::AdHoc) Match.new(:orig("abcabc"), :from(1), :pos(2)) Match.new(:orig("abcabc"), :from(1), :pos(2)) Match.new(:orig("abc"), :from(1), :pos(2)) Match.new(:orig("abcabc"), :from(1), :pos(2)) Match.new(:orig("abc"), :from(2), :pos(3))
sub f(&c) { my \r = try c(); $! ?? "T(" ~ $!.^name ~ ")" !! r ~~ Failure ?? "F(" ~ r.exception.^name ~ ")" !! r.raku }; say f({"a1xa2".match(/a./, :c(2), :g)}), " ", f({"abcabc".match(/b/, :c(2), :g)}), " ", f({"abcabc".match(/b/, :p(2), :g)}), " ", f({"abcabc".match(/b/, :p(4), :g)}), " ", f({"abcabc".match(/b/, :c(2), :ov)}), " ", f({"abcabc".match(/b/, :c(2), :ex)}), " ", f({"abc".match(/./, :c(1), :nth(2))}), " ", f({"abc".match(/./, :p(1), :nth(2))}), " ", f({"abc".match(/./, :c(1), :x(2))}), " ", f({"abc".match(/./, :p(1), :x(2))}), " ", f({"abc".match(/./, :c(1), :g)}), " ", f({"abc".match(/./, :p(1), :ov)}), " ", f({"abc".match(/b/, :as(Str))}), " ", f({"abc".match(/b/, :as(Str)).^name}), " ", f({"abc".match(/b/, :as(Str)); $/.^name}), " ", f({"abc".match(/x/, :as(Str))}), " ", f({"abc".match(/\w/, :g, :as(Str))}), " ", f({"abc".match(/b/, :as(Str), :g)[0].^name}), " ", f({"abc".match(/b/, :as(Str), :g); $/[0].^name}), " ", f({"abc".match(/\w/, :x(2), :as(Str))}), " ", f({"abc".match(/\w/, :nth(2), :as(Str))}), " ", f({"abc".match(/./, :c(1), :as(Str))}), " ", f({"abc".match(/./, :p(1), :as(Str))}), " ", f({"abc".match(/b/, :as("x"))}), " ", f({"abc".match(/\w/, :as(Int)).^name}), " ", f({"abc".match(/b/, :as(Match)).^name}), " ", f({"abc".match(/b/, :as(Nil)).^name}), " ", f({"abc".match(/b/, :!as).^name})
# rakudo 2026.08: (Match.new(:orig("a1xa2"), :from(3), :pos(5)),) (Match.new(:orig("abcabc"), :from(4), :pos(5)),) () (Match.new(:orig("abcabc"), :from(4), :pos(5)),) (Match.new(:orig("abcabc"), :from(4), :pos(5)),) (Match.new(:orig("abcabc"), :from(4), :pos(5)),) Match.new(:orig("abc"), :from(2), :pos(3)) Match.new(:orig("abc"), :from(2), :pos(3)) (Match.new(:orig("abc"), :from(1), :pos(2)), Match.new(:orig("abc"), :from(2), :pos(3))) (Match.new(:orig("abc"), :from(1), :pos(2)), Match.new(:orig("abc"), :from(2), :pos(3))) (Match.new(:orig("abc"), :from(1), :pos(2)), Match.new(:orig("abc"), :from(2), :pos(3))) (Match.new(:orig("abc"), :from(1), :pos(2)), Match.new(:orig("abc"), :from(2), :pos(3))) "b" "Str" "Str" Nil ("a", "b", "c") "Str" "Str" ("a", "b") "b" Match.new(:orig("abc"), :from(1), :pos(2)) Match.new(:orig("abc"), :from(1), :pos(2)) "b" "Match" "Match" "Match" "Match"
```
rakupp 4.0.1: differs — `:p(1)` on a match at 2 is Nil but `:c(Nil)` is
a Match and `:p(Nil)` Nil, non-Int positions are accepted, positions
beyond the end or negative are Nil, `:c(2), :ov`/`:ex` give a single
Match, and `:as(Str)` always returns a Match.

### ST-50  subst: literal matcher, adverbs, no match               D:yes R:yes V:spec/bug
A Str matcher is literal (`"."` is a dot) and replaces the first
occurrence; `:g`/`:global` all; the replacement defaults to `""`, a
non-Str replacement is stringified (42, 1.5, `<42>`, `(1, 2)`), a type
object or Nil is `""` with a warning; an empty matcher matches at 0 (and
with `:g` between every character, like `/<?>/`); **bug:** `"".subst("",
"x")` is `"x"` but with `:g` it is `""`. A Cool matcher or invocant is
stringified; a type object matcher is `X::Multi::NoMatch`; no match
returns a new Str with the same text. The match adverbs pass through:
`:nth`/`:2nd`/`:x`/`:c`/`:p` as in ST-47–49 (`:x(3)` with two matches
changes nothing; `:nth(*)` the last), `:g, :nth(2)` is the second only,
`:g, :p(0)` needs a match at 0. `:ov` and `:ex` throw
`X::Str::Subst::Adverb` (`name` "ov"/"ex"; a false value is fine); an
invalid `:x` is a returned `X::Str::Match::x` Failure. **Bug:**
`:as(Str)` with a Regex matcher throws `X::Method::NotFound` (the strings
have no `.from`); with a Str matcher it is harmless. Roast `subst.t`
asserts the `:x`, `:nth`, `:p`, `:c` and `:g` tables.
```
sub f(&c) { my \r = try c(); $! ?? "T(" ~ $!.^name ~ ")" !! r ~~ Failure ?? "F(" ~ r.exception.^name ~ ")" !! r.raku }; say f({"a.b.c".subst(".", "-")}), " ", f({"a.b.c".subst(".", "-", :g)}), " ", f({"a.b.c".subst(/./, "-")}), " ", f({"abc".subst("b")}), " ", f({"abc".subst("x", "y")}), " ", f({"abc".subst("x", "y").^name}), " ", f({"abc".subst("b", "x").^name}), " ", f({"abc".subst("", "-")}), " ", f({"abc".subst("", "-", :g)}), " ", f({"abc".subst(/<?>/, "-", :g)}), " ", f({"abc".subst(/x*/, "-", :g)}), " ", f({"".subst("", "x")}), " ", f({"".subst("", "x", :g)}), " ", f({"".subst(/<?>/, "x")}), " ", f({"abc".subst("b", 42)}), " ", f({"abc".subst("b", 1.5)}), " ", f({"abc".subst("b", <42>)}), " ", f({"abc".subst("b", (1,2))}), " ", f({"abc".subst(1, "x")}), " ", f({123.subst(2, "x")}), " ", f({"abc".subst(<b>, "x")}), " ", f({"a.b".subst(<.>, "x", :g)}), " ", f({"abc".subst("b", "x", :global)}), " ", f({"abc".subst("b", "x", :!g)}), " ", f({"aaa".subst("a", "b", :g)}), " ", f({"abc".subst(Str, "x")}), " ", f({"abc".subst(Any, "x")}), " ", f({"abc".subst("b", Str)}), " ", f({"abc".subst("b", Nil)}), " ", f({"abc".subst("b", Any)}), " ", f({"abc".subst(/b/, Nil)})
# rakudo 2026.08: "a-b.c" "a-b-c" "-.b.c" "ac" "abc" "Str" "Str" "-abc" "a-b-c" "-a-b-c-" "-a-b-c-" "x" "" "x" "a42c" "a1.5c" "a42c" "a1 2c" "abc" "1x3" "axc" "axb" "axc" "axc" "bbb" T(X::Multi::NoMatch) T(X::Multi::NoMatch) "ac" "ac" "ac" "ac"
sub f(&c) { my \r = try c(); $! ?? "T(" ~ $!.^name ~ ")" !! r ~~ Failure ?? "F(" ~ r.exception.^name ~ ")" !! r.raku }; say f({"abcb".subst("b", "x", :nth(2))}), " ", f({"abcb".subst("b", "x", :2nd)}), " ", f({"abcb".subst("b", "x", :nth(3))}), " ", f({"abcb".subst("b", "x", :nth(*))}), " ", f({"abcbcb".subst("b", "x", :nth(*-1))}), " ", f({"abcbcb".subst("b", "x", :nth(*-2))}), " ", f({"abcb".subst("b", "x", :nth(1, 2))}), " ", f({"abcbcb".subst("b", "x", :nth(2, 3))}), " ", f({"abcbcb".subst("b", "x", :nth(2..3))}), " ", f({"abcb".subst("b", "x", :nth(2..*))}), " ", f({"abcbcb".subst("b", "x", :nth(0))}), " ", f({"abcb".subst("b", "x", :x(0))}), " ", f({"abcb".subst("b", "x", :x(2))}), " ", f({"abcb".subst("b", "x", :x(3))}), " ", f({"abcb".subst("b", "x", :x(1..3))}), " ", f({"abcbcb".subst("b", "x", :x(2..3))}), " ", f({"abcbcb".subst("b", "x", :x(4..5))}), " ", f({"abcb".subst("b", "x", :x(*))}), " ", f({"abcb".subst("b", "x", :nth(2), :x(1))}), " ", f({"abc".subst("b", "x", :nth(2), :x(1))}), " ", f({"abcbcb".subst("b", "x", :nth(2), :x(2))}), " ", f({"abcb".subst("b", "x", :c(2))}), " ", f({"abcb".subst("b", "x", :p(2))}), " ", f({"abcb".subst("b", "x", :p(3))}), " ", f({"abcbcb".subst("b", "x", :c(2), :g)}), " ", f({"abcbcb".subst("b", "x", :p(1), :g)}), " ", f({"abcbcb".subst("b", "x", :p(2), :g)}), " ", f({"abc".subst("b", "x", :g, :nth(1))}), " ", f({"abcb".subst("b", "x", :g, :nth(2))}), " ", f({"abcb".subst("b", "x", :g, :x(1))}), " ", f({"abc".subst("b", "x", :as(Str))}), " ", f({"abc".subst(/b/, "x", :as(Str), :g)}), " ", f({"abc".subst("b", "x", :ov)}), " ", f({"abc".subst("b", "x", :ov); $!.name}), " ", f({"abc".subst("b", "x", :ex)}), " ", f({"abc".subst("b", "x", :!ov)}), " ", f({"abc".subst("b", "x", :x("q"))}), " ", f({"abc".subst("b", "x", :x("q")).^name})
# rakudo 2026.08: "abcx" "abcx" "abcb" "abcx" "abcxcb" "axcbcb" "axcx" "abcxcx" "abcxcx" "abcx" T(X::AdHoc) "abcb" "axcx" "abcb" "axcx" "axcxcx" "abcbcb" "axcx" "abcb" T(X::AdHoc) "abcbcb" "abcx" "abcb" "abcx" "abcxcx" "axcxcx" "abcbcb" "axc" "abcx" "axcb" "axc" T(X::Method::NotFound) T(X::Str::Subst::Adverb) T(X::Str::Subst::Adverb) T(X::Str::Subst::Adverb) "axc" T(X::Str::Match::x) "Failure"
```
rakupp 4.0.1: differs — the empty matcher never matches, `:!g`, `:!ov`,
`:ov`, `:ex` and `:as` are `X::Syntax::Regex::Adverb`, `:nth(*)`/`*-k`
are `X::AdHoc`, a type-object matcher is a no-op, and an invalid `:x`
throws.

### ST-51  subst with a Callable replacement                       D:partial R:yes V:spec/quirk
A replacement Callable with `.count == 0` is called with no arguments,
otherwise with the Match as its one argument (so `$_` is the Match in a
block, a `-> $m` pointy works, and a two-parameter block throws
`X::AdHoc`); its return value is stringified (a List joins with spaces,
Nil or a type object gives `""` with a warning); `{ ... }` throws
`X::StubCode`; `die` propagates; `fail` propagates as `X::AdHoc`.
With a **Regex** matcher `$/` inside the block is the current Match
(`$0`, `$<foo>`, `.from`, `.orig`, `.prematch`); a WhateverCode sees it as
`$_`. **Quirk:** with a **Str** matcher `$/` is not set — inside the block
`$/` is whatever it was before (Nil, or an earlier match), although `$_`
is still the Match; the docs claim `$/` for both. Roast `subst.t` asserts
the closure cases and that a Str matcher leaves `$/` untouched.
```
sub f(&c) { my \r = try c(); $! ?? "T(" ~ $!.^name ~ ")" !! r ~~ Failure ?? "F(" ~ r.exception.^name ~ ")" !! r.raku }; say f({'abc123defg'.subst(/(\d+)/, { " before $0 after " })}), " ", f({'abc123defg'.subst(/$<foo>=\d+/, { "<$<foo>>" })}), " ", f({'abc123defg'.subst(/(\d+)/, "[ " ~ *.flip ~ " ]")}), " ", f({"abc".subst(/b/, *.uc)}), " ", f({"abc".subst(/b/, &uc)}), " ", f({"abc".subst(/b/, { .uc })}), " ", f({"abc".subst(/b/, -> $m { $m.from })}), " ", f({"abc".subst(/b/, -> { "Z" })}), " ", f({"abc".subst(/b/, -> $m, $n = 1 { "$n" })}), " ", f({"abc".subst(/b/, -> $m, $n { "" })}), " ", f({"abc".subst(/b/, { $/.to })}), " ", f({"abc".subst(/b/, { $/.Str ~ $/.^name })}), " ", f({"abc".subst(/(b)/, { $0.uc }, :g)}), " ", f({"abc".subst(/b/, { .prematch ~ .postmatch })}), " ", f({"aXbXc".subst(/X/, { $/.from }, :g)}), " ", f({"aXbXc".subst(/X/, { state $n = 0; ++$n }, :g)}), " ", f({"abcb".subst(/b/, { $++ }, :g)}), " ", f({"a1b2".subst(/\d/, { $/ + 1 }, :g)}), " ", f({my $i = 41; "x secret".subst(/secret/, {++$i})}), " ", f({"abc".subst(/b/, { "" })}), " ", f({"abc".subst(/b/, { (1, 2) })}), " ", f({"abc".subst(/b/, { 1.5e0 })}), " ", f({"abc".subst(/b/, { ... })}), " ", f({"abc".subst(/b/, { die "boom" })})
# rakudo 2026.08: "abc before 123 after defg" "abc<123>defg" "abc[ 321 ]defg" "aBc" "aBc" "aBc" "a1c" "aZc" "a1c" T(X::AdHoc) "a2c" "abMatchc" "aBc" "aacc" "a1b3c" "a1b2c" "a0c1" "a2b3" "x 42" "ac" "a1 2c" "a1.5c" T(X::StubCode) T(X::AdHoc)
sub f(&c) { my \r = try c(); $! ?? "T(" ~ $!.^name ~ ")" !! r ~~ Failure ?? "F(" ~ r.exception.^name ~ ")" !! r.raku }; say do { my @w; CONTROL { when CX::Warn { @w.push(.message.lines[0]); .resume } }; f({"abc".subst("b", { .uc })}) ~ " " ~ f({"abc".subst("b", -> $m { $m.^name })}) ~ " " ~ f({"abc".subst("b", { $_.from })}) ~ " " ~ f({"abc".subst("b", sub { "S" })}) ~ " " ~ f({"abc".subst("b", { 42 })}) ~ " " ~ f({"abc".subst("b", { $/.^name })}) ~ " " ~ f({"abc".subst("b", { $/.raku })}) ~ " " ~ f({"prior".match(/o/); "abc".subst("b", { $/.Str })}) ~ " " ~ f({"abc".subst("b", { Nil })}) ~ " " ~ f({"abc".subst("b", { Str })}) ~ " " ~ f({"abc".subst(/b/, { Nil })}) ~ " | " ~ @w.elems ~ " | " ~ @w.unique.join("/") }
# rakudo 2026.08: "aBc" "aMatchc" "a1c" "aSc" "a42c" "aNilc" "aNilc" "aoc" "ac" "ac" "ac" | 3 | Use of Nil in string context/Use of uninitialized value of type Str in string context.
sub f(&c) { my \r = try c(); $! ?? "T(" ~ $!.^name ~ ")" !! r ~~ Failure ?? "F(" ~ r.exception.^name ~ ")" !! r.raku }; say f({"abc".subst(/b/, { fail "boom" })})
# rakudo 2026.08: T(X::AdHoc)
```
rakupp 4.0.1: differs — a two-parameter block is called with no arguments
(`"ac"`), `fail` inside the block escapes `try`, and with a Str matcher
`$/` *is* set to the Match (so `{ $/.^name }` is `Match` and a prior `$/`
is not visible).

### ST-52  The caller's $/ after subst                             D:yes R:yes V:spec
With a Regex matcher `$/` becomes the Match, Nil on no match, a List for
`:g`/`:x`/an Iterable `:nth` (an empty List when `:g` or `:x` found
nothing, including `:x(0)`), and Nil when `:x` was invalid; `:as(Str)`
still leaves a Match in `$/`. With a Str matcher `$/` is never touched,
whatever the adverbs or replacement (Roast `subst.t`).
```
sub f(&c) { my \r = try c(); $! ?? "T(" ~ $!.^name ~ ")" !! r ~~ Failure ?? "F(" ~ r.exception.^name ~ ")" !! r.raku }; say f({my $r = "abc".subst(/b/, "x"); $/}), " ", f({my $r = "abc".subst(/x/, "y"); $/}), " ", f({my $r = "abc".subst(/\w/, "x", :g); $/.^name ~ $/.elems}), " ", f({my $r = "abc".subst(/x/, "y", :g); $/}), " ", f({my $r = "abc".subst(/\w/, "x", :x(2)); $/.^name ~ $/.elems}), " ", f({my $r = "abc".subst(/\w/, "x", :x(5)); $/}), " ", f({my $r = "abc".subst(/b/, "x", :x(0)); $/}), " ", f({my $r = "abc".subst(/\w/, "x", :nth(2)); $/}), " ", f({my $r = "abc".subst(/\w/, "x", :nth(2..3)); $/.elems}), " ", f({my $r = "abc".subst(/b/, "x", :nth(5)); $/}), " ", f({my $r = "abc".subst(/(b)/, "x"); $/[0].Str}), " ", f({my $r = "abc".subst(/b/, { "x" }); $/}), " ", f({my $r = "abc".subst(/b/, "x", :ii); $/}), " ", f({my $r = "abc".subst(/b/, "x", :as(Str)); $/.^name}), " ", f({my $r = "abc".subst(/\w/, "x", :x("q")); $/}), " ", f({"prior".match(/o/); my $r = "abc".subst("b", "x"); $/}), " ", f({"prior".match(/o/); my $r = "abc".subst("x", "y"); $/}), " ", f({"prior".match(/o/); my $r = "abc".subst("b", "x", :g); $/}), " ", f({"prior".match(/o/); my $r = "abc".subst("b", "x", :nth(1)); $/}), " ", f({"prior".match(/o/); my $r = "abc".subst("b", "x", :x(1)); $/}), " ", f({"prior".match(/o/); my $r = "abc".subst("b", { "x" }); $/}), " ", f({"prior".match(/o/); my $r = "abc".subst("b", "x", :ii); $/})
# rakudo 2026.08: Match.new(:orig("abc"), :from(1), :pos(2)) Nil "List3" $( ) "List2" $( ) $( ) Match.new(:orig("abc"), :from(1), :pos(2)) 2 Nil "b" Match.new(:orig("abc"), :from(1), :pos(2)) Match.new(:orig("abc"), :from(1), :pos(2)) "Match" Nil Match.new(:orig("prior"), :from(3), :pos(4)) Match.new(:orig("prior"), :from(3), :pos(4)) Match.new(:orig("prior"), :from(3), :pos(4)) Match.new(:orig("prior"), :from(3), :pos(4)) Match.new(:orig("prior"), :from(3), :pos(4)) Match.new(:orig("prior"), :from(3), :pos(4)) Match.new(:orig("prior"), :from(3), :pos(4))
```
rakupp 4.0.1: differs — a missing `:nth(5)` leaves `()`, `:as` is
`X::Syntax::Regex::Adverb`, an invalid `:x` throws, and a Str matcher with
a Callable replacement sets `$/`.

### ST-53  :ii, :ss, :mm                                           D:yes R:yes V:spec
In the method form `:ii`/`:samecase` does **not** imply `:i` (the regex
needs `:i`); it applies `samecase` (ST-20) of the matched text to the
replacement, position by position (`"Hello World"` → `"Bye byE"`: the
last case, lower, runs on; a longer replacement continues the last case;
a Str matcher and a Callable replacement work too). `:ss`/`:samespace`
turns that into word-by-word (`"Bye Bye"`) and copies the matched
whitespace runs into the replacement (`"bye  bye"`, a tab), with `:ss`
alone also copying whitespace; it does not imply `:s`. `:mm`/`:samemark`
applies `samemark` (ST-21) of the match (`"ober"` → `"öber"`, the last
mark runs on: `"xýź"`), without implying `:m`. All three combine. In the
`s///` form `:ii` implies `:i` and `:ss` implies `:s` (Roast `subst.t`).
```
say "Hello World".subst(/hello \s world/, "bye bye", :ii).raku, " ", "Hello World".subst(/:i hello \s world/, "bye bye", :ii).raku, " ", "HELLO world".subst(/:i hello \s world/, "bye bye", :samecase).raku, " ", "Hello World".subst(/:i hello \s world/, { "bye bye" }, :ii).raku, " ", "Hello World".subst("Hello World", "bye bye", :ii).raku, " ", "aBc".subst(/:i abc/, "xyz", :ii).raku, " ", "ABC".subst(/:i abc/, "xy", :ii).raku, " ", "abc".subst(/abc/, "XYZW", :ii).raku, " ", "Hello".subst(/:i hello/, "byebye", :ii).raku, " ", "HeLLo".subst(/:i hello/, "byebye", :ii).raku, " ", "HELLO".subst(/:i hello/, "byebye", :ii).raku, " ", "hELLO".subst(/:i hello/, "byebye", :ii).raku, " ", "Hello".subst(/:i hello/, "", :ii).raku, " ", "Hello World".subst(/:i hello/, "b", :ii, :g).raku, " ", "Hello World".subst(/:i hello \s world/, "bye bye", :ii, :ss).raku, " ", "Hello World".subst(/:i hello \s world/, "bye bye bye", :ii, :ss).raku, " ", "Hello World Foo".subst(/:i hello \s world \s foo/, "bye bye", :ii, :ss).raku, " ", "aB".subst(/:i ab/, "xyz", :ii, :ss).raku, " ", "A b".subst(/:i a \s b/, "xy z", :ii, :ss).raku
# rakudo 2026.08: "Hello World" "Bye byE" "BYE Bye" "Bye byE" "Bye byE" "xYz" "XY" "xyzw" "Byebye" "ByEBye" "BYEBYE" "bYEBYE" "" "B World" "Bye Bye" "Bye Bye Bye" "Bye Bye" "xYZ" "XY z"
say "Hello  World".subst(/:i:s hello world/, "bye bye").raku, " ", "Hello  World".subst(/:i:s hello world/, "bye bye", :ss).raku, " ", "Hello  World".subst(/:s hello world/, "bye bye", :ss).raku, " ", "Hello\tWorld".subst(/:i:s hello world/, "bye bye", :samespace).raku, " ", "Hello  World".subst(/:i:s hello world/, "bye\tbye", :ss).raku, " ", "Hello  World".subst(/:i:s hello world/, "byebye", :ss).raku, " ", "Hello World".subst(/:i hello \s world/, "bye bye", :ss).raku, " ", "über".subst(/:m uber/, "ober").raku, " ", "über".subst(/:m uber/, "ober", :mm).raku, " ", "über".subst(/:m uber/, "o", :samemark).raku, " ", "über".subst(/:m uber/, "ober", :mm, :ii).raku, " ", "ÜBER".subst(/:i:m uber/, "ober", :mm, :ii).raku, " ", "aé".subst(/ae/, "xy", :mm).raku, " ", "aé".subst(/:m ae/, "xy", :mm).raku, " ", "aé".subst(/:m ae/, "x", :mm).raku, " ", "aé".subst(/:m ae/, "xyz", :mm).raku, " ", "Hello World".subst(/:i hello \s world/, "bye bye", :ii, :mm).raku, " ", "Hello World".subst(/:i hello \s world/, "bye bye", :samecase, :samespace, :samemark).raku, " ", do { my $s = "Hello World"; $s ~~ s:ii/hello \s world/bye bye/; $s.raku }, " ", do { my $s = "Hello  World"; $s ~~ s:ss/hello world/bye bye/; $s.raku }
# rakudo 2026.08: "bye bye" "bye  bye" "Hello  World" "bye\tbye" "bye  bye" "byebye" "bye bye" "ober" "öber" "ö" "öber" "ÖBER" "aé" "xý" "x" "xýź" "Bye byE" "Bye Bye" "Bye byE" "Hello  World"
```
rakupp 4.0.1: differs — `:ii` implies `:i` and `:mm` implies `:m` in the
method form, the samecase of `"HELLO world"` is `"BYE BYe"`, `:ss` with a
`/:i … \s …/` matcher does not match at all, and the last mark does not
run on (`"xýz"`).

### ST-54  subst-mutate                                            D:yes R:no V:spec/bug
Replaces in place and returns the Match, Nil when nothing matched, or the
List for `:g`/`:x`/Iterable `:nth` (an empty List when none, also for
`:x` short of its count, in which case nothing changes); `$/` is set as
in ST-52 (a Str matcher leaves it alone). A `Str`-typed variable stays
valid; a Cool variable becomes a Str only when something was replaced
(123 with no match stays an Int); a literal invocant is
`X::Multi::NoMatch`; an invalid `:x` returns the Failure and leaves the
string. **Bugs:** unlike `subst`, `:ov` and `:ex` are not rejected —
`:ov` applies overlapping matches and corrupts the text
(`"aaa"` with `/aa/` → `"xax"`), `:ex` throws `X::AdHoc`; and `:as(Str)`
throws `X::Method::NotFound`.
```
sub f(&c) { my \r = try c(); $! ?? "T(" ~ $!.^name ~ ")" !! r ~~ Failure ?? "F(" ~ r.exception.^name ~ ")" !! r.raku }; say f({my $s = "Some foo"; my $m = $s.subst-mutate(/foo/, "string"); $s ~ "|" ~ $m.raku}), " ", f({my $s = "abc"; my $m = $s.subst-mutate("b", "y"); $s ~ "|" ~ $m.^name}), " ", f({my $s = "abc"; my $m = $s.subst-mutate(/x/, "y"); $s ~ "|" ~ $m.raku}), " ", f({my $s = "abc"; my $m = $s.subst-mutate("x", "y"); $m}), " ", f({my $s = "abcb"; my $m = $s.subst-mutate(/b/, "y", :g); $s ~ "|" ~ $m.raku}), " ", f({my $s = "abc"; my $m = $s.subst-mutate(/x/, "y", :g); $s ~ "|" ~ $m.raku}), " ", f({my $s = "abcb"; my $m = $s.subst-mutate("b", "y", :x(2)); $s ~ "|" ~ $m.elems}), " ", f({my $s = "abcb"; my $m = $s.subst-mutate("b", "y", :x(3)); $s ~ "|" ~ $m.raku}), " ", f({my $s = "abc"; my $m = $s.subst-mutate(/b/, "x", :x(0)); $s ~ "|" ~ $m.raku}), " ", f({my $s = "abcb"; my $m = $s.subst-mutate("b", "y", :nth(2)); $s ~ "|" ~ $m.raku}), " ", f({my $s = "abcb"; my $m = $s.subst-mutate(/b/, "x", :nth(2..*)); $s ~ "|" ~ $m.elems}), " ", f({my $s = "abc"; $s.subst-mutate(/b/, { .uc }); $s}), " ", f({my $s = "aBc"; my $m = $s.subst-mutate(/:i b/, "yz", :ii); $s ~ "|" ~ $m.raku}), " ", f({my $s = "abc"; my $m = $s.subst-mutate(/b/, "y"); $/.raku}), " ", f({my $s = "abc"; my $m = $s.subst-mutate(/x/, "y"); $/.raku}), " ", f({my $s = "abc"; $s.subst-mutate("b", "x", :g); $/.raku}), " ", f({my $s = "abc"; $s.subst-mutate(/b/, "x", :as(Str)); $s}), " ", f({my $s = "abc"; my $r := $s.subst-mutate("b", "y"); $r.^name}), " ", f({my Str $s = "abc"; $s.subst-mutate("b", "y"); $s}), " ", f({"abc".subst-mutate("b", "x"); "ok"}), " ", f({my $s = 123; my $m = $s.subst-mutate(2, "x"); $s.raku ~ "|" ~ $s.^name ~ "|" ~ $m.raku}), " ", f({my $s = 123; my $m = $s.subst-mutate(9, "x"); $s.raku ~ "|" ~ $s.^name ~ "|" ~ $m.raku}), " ", f({my $s = "abc"; my $m = $s.subst-mutate(/b/, "x", :x("q")); $s ~ "|" ~ $m.^name}), " ", f({my $s = "abc"; $s.subst-mutate("b", "x", :ov); $s}), " ", f({my $s = "aaa"; my $m = $s.subst-mutate(/aa/, "x", :ov); $s ~ "|" ~ $m.elems}), " ", f({my $s = "abab"; $s.subst-mutate(/ab/, "x", :ov); $s}), " ", f({my $s = "aaa"; my $m = $s.subst-mutate(/a+/, "x", :ex); $s ~ "|" ~ $m.elems})
# rakudo 2026.08: "Some string|Match.new(:orig(\"Some foo\"), :from(5), :pos(8))" "ayc|Match" "abc|Any" Any "aycy|\$(Match.new(:orig(\"abcb\"), :from(1), :pos(2)), Match.new(:orig(\"abcb\"), :from(3), :pos(4)))" "abc|\$( )" "aycy|2" "abcb|\$( )" "abc|\$( )" "abcy|Match.new(:orig(\"abcb\"), :from(3), :pos(4))" "abcx|1" "aBc" "aYZc|Match.new(:orig(\"aBc\"), :from(1), :pos(2))" "Match.new(:orig(\"abc\"), :from(1), :pos(2))" "Nil" "Nil" T(X::Method::NotFound) "Match" "ayc" T(X::Multi::NoMatch) "\"1x3\"|Str|Match.new(:orig(\"123\"), :from(1), :pos(2))" "123|Int|Any" "abc|Failure" "axc" "xax|2" "xx" T(X::AdHoc)
```
rakupp 4.0.1: differs — a Cool variable becomes a Str even on no match,
an invalid `:x` throws, and `:ov`/`:ex`/`:as` are
`X::Syntax::Regex::Adverb`.

## I. trans, indent, parse-base, and the rest

### ST-55  trans: characters, ranges, lists, padding               D:yes R:yes V:spec/quirk
A Pair with Str sides maps character to character: `"a" => "xyz"` maps
`a` to `x` (extra target characters are ignored), `"ab" => "xyz"` maps
`a`→`x`, `b`→`y`; `"a..c"` in either side is a range (a range needs a
character on both sides of `..`: `"xy.."` and `"..xy"` are literal dots,
`".."` alone is two dots, `"c..a"` is an empty range, and the `a` after the dots is
read as a literal key, so only `a` maps); `"abc" => ""` deletes.
**Quirk:** when the target string is shorter it *cycles*
(`"a..c" => "x..y"` maps `c` to `x`, as the docs' `'123' => 'þð'` example
shows), but when the sides are lists the last target *repeats*
(`<a b c> => <x y>` maps `c` to `y`); `:d` deletes the unmatched keys in
both forms; an empty target list deletes all. A list key element is a
substring (`["ab"] => ["1"]`), and at one position the longest key wins
(`["a", "ab"]`). Pairs may be given as several arguments or one list; a
Cool key or value is stringified (`4200.trans(42 => 14)`); `trans()` and
an empty invocant return the string. **Quirk:** with duplicate keys and
no adverbs the *last* mapping wins for single characters (`"a" => "x",
"a" => "y"` gives `y`, also when `"ab" => "1"` is followed by `"a" =>
"2"`), but the *first* wins for substrings (`["ab"] => ["1"], ["ab"] =>
["2"]`) and, with `:s`, `:c` or `:d`, for single characters too.
Roast `S05-transliteration/trans.t` asserts ranges, lists, longest-match
and `:d`.
```
say "abc".trans("a" => "x").raku, " ", "abc".trans("a" => "xyz").raku, " ", "abc".trans("ab" => "xyz").raku, " ", "abc".trans("abc" => "x").raku, " ", "abc".trans("abc" => "xy").raku, " ", "abc".trans("abc" => "xy", :d).raku, " ", "abc".trans("abc" => "").raku, " ", "abc".trans("ab" => "").raku, " ", "abc".trans("ab" => "", :d).raku, " ", "abc".trans("a" => "x", :d).raku, " ", "abc".trans("a" => "", :d).raku, " ", "abc".trans("ba" => "ab").raku, " ", "aaa".trans("a" => "bb").raku, " ", "".trans("a" => "b").raku, " ", "abc".trans().raku, " ", "abcd".trans("a..c" => "x..z").raku, " ", "abcd".trans("a..c" => "x").raku, " ", "abcd".trans("a..c" => "x..y").raku, " ", "abcd".trans("a..c" => "x..y", :d).raku, " ", "abc".trans("a..c" => "xy", :d).raku, " ", "abcz".trans("a..cz" => "1..3Z").raku, " ", "abcdef".trans("a..c" => "x..z", "d..f" => "1..3").raku, " ", "abc".trans("c..a" => "x").raku, " ", "abc".trans("a..a" => "x").raku, " ", "a.c".trans("a.c" => "xyz").raku, " ", "a.b".trans("." => "x").raku, " ", "a.b".trans(".." => "x").raku, " ", "a..b".trans("..." => "x").raku, " ", "a-c".trans("-" => "..").raku, " ", "abcd".trans("a..c" => "xy..").raku, " ", "abcd".trans("a..c" => "..xy").raku
# rakudo 2026.08: "xbc" "xbc" "xyc" "xxx" "xyx" "xy" "" "c" "c" "xbc" "bc" "bac" "bbb" "" "abc" "xyzd" "xxxd" "xyxd" "xyd" "xy" "123Z" "xyz123" "xbc" "xbc" "xyz" "axb" "axb" "axxb" "a.c" "xy.d" "..xd"
say "abcd".trans(["a"] => ["zz"]).raku, " ", "abc".trans("a" => ["xy"]).raku, " ", "abc".trans("ab" => ["x", "y"]).raku, " ", "abc".trans("ab" => ["xy"]).raku, " ", "abc".trans(["a", "b"] => "x").raku, " ", "abc".trans(["a", "b"] => "xy").raku, " ", "abc".trans(["a", "b"] => "x..y").raku, " ", "aXa".trans("X" => ["1", "2"]).raku, " ", "abcd".trans(["ab", "a"] => ["1", "2"]).raku, " ", "abcd".trans(["a", "ab"] => ["1", "2"]).raku, " ", "abc".trans(<a b c> => <x y>).raku, " ", "abc".trans(<a b c> => <x y>, :d).raku, " ", "abc".trans(<a b c> => <x>).raku, " ", "abc".trans(<a b c> => <x>, :d).raku, " ", "abc".trans(<a b c> => ()).raku, " ", "abc".trans(<a b c> => (), :d).raku, " ", "abc".trans(("a", "b") => ("x",), :d).raku, " ", "abc".trans("abc".comb => 1..3).raku, " ", "abc".trans("abc".comb => 1..2, :delete).raku, " ", "abc".trans("a".."b" => "x".."y").raku, " ", "abc".trans(("a".."b") => "x").raku, " ", "abc".trans("a" => "x", "b" => "y").raku, " ", "abc".trans(("a" => "x", "b" => "y")).raku, " ", 123.trans(1 => 9).raku, " ", 4200.trans(42 => 14).raku
# rakudo 2026.08: "zzbcd" "xybc" "xyc" "xyxyc" "xxc" "xyc" "xyc" "a1a" "1cd" "2cd" "xyy" "xy" "xxx" "x" "" "" "xc" "123" "12" "xyc" "xxc" "xyc" "xyc" "923" "1400"
say "abc".trans("a" => "x", "a" => "y").raku, " ", "abc".trans(("a" => "x", "a" => "y")).raku, " ", "abc".trans("ab" => "xy", "a" => "z").raku, " ", "abcd".trans("a" => "2", "ab" => "1").raku, " ", "abcd".trans("ab" => "1", "a" => "2").raku, " ", "abcd".trans("ab" => "1", "ab" => "2").raku, " ", "abc".trans(["a"] => ["x"], ["a"] => ["y"]).raku, " ", "abcd".trans(["ab"] => ["1"], ["ab"] => ["2"]).raku, " ", "abcd".trans(["a"] => ["2"], ["ab"] => ["1"]).raku, " ", "abc".trans("a" => "x", "a" => "y", :s).raku, " ", "abc".trans("a" => "x", "a" => "y", :c).raku, " ", "abc".trans("a" => "x", "a" => "y", :d).raku, " ", "abcd".trans("a" => "x", "ab" => "y", :d).raku
# rakudo 2026.08: "ybc" "ybc" "zyc" "11cd" "21cd" "22cd" "ybc" "1cd" "1cd" "xbc" "axx" "ybc" "ycd"
```
rakupp 4.0.1: differs — lists cycle instead of repeating the last target,
`:d` does not delete when the target is shorter, `"c..a" => "x"` maps
all three, and duplicate keys always take the first mapping.

### ST-56  trans: regex keys, callable values, :c :s :d            D:yes R:yes V:spec
A Regex key matches substrings (`/<[aeiou]> \w/ => ''`); its value may be
a Str (the whole string, not characters: `/X/ => "12"` gives `"a12b12c"`),
a list (its first element per key), or a Callable, called once per match
with the Match as `$_` and in `$/` (`{ $/ * 2 }`, `{ $0 + 1 }`, `$++`); a
Callable also works for a Str key (`"a" => &uc`, `["a"] => [{ "x" }]`),
and afterwards the caller's `$/` holds the last match (for a Str key, its
text). Among several regex keys the first that matches at a position wins.
`:c`/`:complement` replaces every *unmatched* character with the (first)
target, once per character, and `:s`/`:squash` collapses runs of the same
replacement — also across different keys with the same target
(`"ab" => "xx", :s` gives `"xcc"`); `:c, :s` gives one replacement per
unmatched run; `:d`/`:delete` removes matched characters that have no
target (with a target it changes nothing); `:c(0)`/`:!c` are off; adverbs
work with a list of Pairs. Roast `trans.t` and `with-closure.t` assert
these.
```
sub f(&c) { my \r = try c(); $! ?? "T(" ~ $!.^name ~ ")" !! r ~~ Failure ?? "F(" ~ r.exception.^name ~ ")" !! r.raku }; say f({"abcdefghij".trans(/<[aeiou]> \w/ => '')}), " ", f({"a1b2".trans(/\d/ => "#")}), " ", f({"a1b2".trans(/\d/ => { $/ * 2 })}), " ", f({"a1b2".trans(/(\d)/ => { $0 + 1 })}), " ", f({"abc".trans(/b/ => { $/.uc })}), " ", f({"abc".trans(/./ => { "<$_>" })}), " ", f({"ab cd".trans(/\w+/ => { .uc })}), " ", f({"aaa".trans(/a/ => { $++ })}), " ", f({"abc".trans(/b/ => -> $m { $m.uc })}), " ", f({"abc".trans(/b/ => { "x" }); $/}), " ", f({"abc".trans(/b/ => <x y>)}), " ", f({"aXbXc".trans(/X/ => "12")}), " ", f({"aXbXc".trans(/X/ => ("1", "2"))}), " ", f({"aXbXc".trans([/X/] => ("1", "2"))}), " ", f({"aXbXc".trans([/X/, "b"] => ("1", "2"))}), " ", f({"a1b".trans(/\d/ => "x", "a" => "y")}), " ", f({"a1b".trans(("a" => "y", /\d/ => "x"))}), " ", f({"a1b".trans(/\d/ => "x", /\d/ => "y")}), " ", f({"a1b".trans(/\d/ => "x", /1/ => "y")}), " ", f({"a1b".trans(/1/ => "y", /\d/ => "x")}), " ", f({"aba".trans(/a/ => "x", /b/ => "y", :s)}), " ", f({"a11b22".trans(/\d/ => "#", :s)}), " ", f({"a11b22".trans(/\d/ => "#", :c)}), " ", f({"a11b22".trans(/\d+/ => "#", :c)}), " ", f({"a11b22".trans(/\d+/ => "#", :c, :s)}), " ", f({"a11b22".trans(/\d/ => "#", :d)}), " ", f({"a11b22".trans(/\d/ => "", :d)}), " ", f({"abc".trans(/<[ab]>/ => "x", :c)}), " ", f({"abc".trans(/<[ab]>/ => "x", :c, :d)}), " ", f({"abbc".trans(/b/ => "x", :s)}), " ", f({"abbc".trans(/b+/ => "x", :s)}), " ", f({"abbc".trans(/b/ => "xy", :s)}), " ", f({"abc".trans("a" => { "x" })}), " ", f({"abc".trans("a" => { $_ })}), " ", f({"abc".trans("a" => { $/.Str.uc })}), " ", f({"abc".trans("a" => -> $m { $m.Str.uc })}), " ", f({"abc".trans("a" => &uc)}), " ", f({"abc".trans(["a"] => [{ "x" }])}), " ", f({"abc".trans("a" => { "x" }); $/.raku})
# rakudo 2026.08: "cdgh" "a#b#" "a2b4" "a2b3" "aBc" "<a><b><c>" "AB CD" "012" "aBc" Match.new(:orig("abc"), :from(1), :pos(2)) "axc" "a12b12c" "a1b1c" "a1b1c" "a121c" "yxb" "yxb" "axb" "axb" "ayb" "xyx" "a#b#" "#11#22" "#11#22" "#11#22" "a##b##" "ab" "abx" "abx" "axc" "axc" "axyc" "xbc" "abc" "Abc" "Abc" "Abc" "xbc" "\"a\""
say "a123b123c".trans(['a'..'z'] => 'x', :complement).raku, " ", "aaa1123bb123c".trans('a'..'z' => 'A'..'Z', :squash).raku, " ", "aaa1123bb123c".trans('a'..'z' => 'x', :complement, :squash).raku, " ", "a123b123c".trans('23' => '4').raku, " ", "a123b123c".trans('123' => 'þð').raku, " ", "a123b123c".trans('123' => 'þð', :squash).raku, " ", "a123b123c".trans('123' => 'þð', :delete).raku, " ", "aabbcc".trans("ab" => "xy", :s).raku, " ", "aabbcc".trans("ab" => "xx", :s).raku, " ", "aabbcc".trans("ab" => "x", :s).raku, " ", "aabb".trans("a" => "x", "b" => "x", :s).raku, " ", "aabb".trans("a" => "x", "b" => "y", :s).raku, " ", "aXXb".trans("X" => "y", :s).raku, " ", "aXXbXX".trans("X" => "y", :s).raku, " ", "aXYb".trans("XY" => "z", :s).raku, " ", "aXYb".trans("XY" => "zz", :s).raku, " ", "aXbXc".trans("X" => "", :s).raku, " ", "abcabc".trans("abc" => "xyz", :s).raku, " ", "abcabc".trans("abc" => "x", :s).raku, " ", "abc".trans("b" => "x", :s).raku, " ", "abc".trans("b" => "x", :d).raku, " ", "abc".trans("b" => "x", :c).raku, " ", "abc".trans("b" => "", :c).raku, " ", "abc".trans("b" => "xy", :c).raku, " ", "abc".trans("b" => "x", :c, :d).raku, " ", "abbc".trans("b" => "x", :c, :s).raku, " ", "abab".trans("a" => "x", :c).raku, " ", "abab".trans("a" => "x", :c, :s).raku, " ", "abc".trans("a" => "x", :s, :c, :d).raku, " ", "abc".trans("x" => "y", :c).raku, " ", "abc".trans("x" => "", :c).raku, " ", "abc".trans("x" => "y", :c, :s).raku, " ", "aaa".trans("a" => "", :c).raku, " ", "abc".trans("abc" => "", :c).raku, " ", "abc".trans("b" => "x", :c(0)).raku, " ", "abc".trans("b" => "x", :!c).raku, " ", "abc".trans(("b" => "x",), :c).raku
# rakudo 2026.08: "axxxbxxxc" "A1123B123C" "aaaxbbxc" "a144b144c" "aþðþbþðþc" "aþðbþðc" "aþðbþðc" "xycc" "xcc" "xcc" "x" "xy" "ayb" "ayby" "azb" "azb" "abc" "xyzxyz" "x" "axc" "axc" "xbx" "b" "xbx" "xbx" "xbbx" "axax" "axax" "ax" "yyy" "" "y" "aaa" "abc" "axc" "axc" "xbx"
```
rakupp 4.0.1: differs — a Callable value is stringified (`"sub { ... }"`)
instead of called, a list value for a regex key is joined with spaces,
`:s` does not merge runs across keys (`"xxcc"`) nor squash a multi-character
key (`"XY" => "z", :s` gives `"azzb"`), `"abc" => "x", :s` gives
`"xxxxxx"`, `"b" => "xy", :c` uses the second target character, and
`:c(0)` inserts `0`.

### ST-57  trans: what is refused, what is a no-op                 D:no R:no V:spec
A non-Pair argument (a Str, two Strs, a Pair followed by a Str, Nil)
throws `X::Str::Trans::InvalidArg`; a type object inside a key or value
list throws `X::Str::Trans::IllegalKey`. A type-object key or value
(`"a" => Any`, `/a/ => Any`, `"a" => Str`, `"a" => Nil`) is a silent
no-op; a numeric key or value is stringified (`"a" => 1.5` maps `a` to
`1`); a Callable key is a no-op; a Callable value returning Nil deletes.
`:d` or `:c` alone return the string. A named argument that is not
`c/complement`, `s/squash`, `d/delete` is ignored when Pairs are given,
and warns "Unexpected named variable(s)…" when none are — which is what
`Str => "x"` or `True => "x"` is, since an identifier before `=>` is a
named argument, not a Pair with a type-object key.
```
sub f(&c) { my \r = try c(); $! ?? "T(" ~ $!.^name ~ ")" !! r ~~ Failure ?? "F(" ~ r.exception.^name ~ ")" !! r.raku }; say f({"abc".trans("a")}), " ", f({"abc".trans("a", "b")}), " ", f({"abc".trans("a" => "x", "b")}), " ", f({"abc".trans(("a" => "x", "b"))}), " ", f({"abc".trans(Nil)}), " ", f({"abc".trans((1 => "x",))}), " ", f({"abc".trans(1 => "x")}), " ", f({"abc".trans(1.5 => "x")}), " ", f({"abc".trans("a" => 1)}), " ", f({"abc".trans("a" => 1.5)}), " ", f({"abc".trans("a" => [1])}), " ", f({"abc".trans([1] => "x")}), " ", f({"abc".trans(/a/ => 1)}), " ", f({"abc".trans(/a/ => [1, 2])}), " ", f({"abc".trans(/a/ => (1, 2))}), " ", f({"abc".trans(/(a)/ => "$0")}), " ", f({"abc".trans([Any] => "x")}), " ", f({"abc".trans(["a", Any] => "x")}), " ", f({"abc".trans(["a"] => ["x", Any])}), " ", f({"abc".trans("a" => [Any])}), " ", f({"abc".trans({;} => "x")}), " ", f({"abc".trans("a" => {;})}), " ", f({"abc".trans("a" => Any)}), " ", f({"abc".trans("a" => Str)}), " ", f({"abc".trans("a" => Nil)}), " ", f({"abc".trans(/a/ => Any)}), " ", f({"abc".trans(:d)}), " ", f({"abc".trans(:c)}), " ", f({"abc".trans("a" => "b", :foo)}), " ", f({"abc".trans(("a" => "b"), :foo)}), " ", f({"abc".trans(("a" => "b", "b" => "c"), :foo)})
# rakudo 2026.08: T(X::Str::Trans::InvalidArg) T(X::Str::Trans::InvalidArg) T(X::Str::Trans::InvalidArg) T(X::Str::Trans::InvalidArg) T(X::Str::Trans::InvalidArg) "abc" "abc" "abc" "1bc" "1bc" "1bc" "abc" "1bc" "1bc" "1bc" "abc" T(X::Str::Trans::IllegalKey) T(X::Str::Trans::IllegalKey) T(X::Str::Trans::IllegalKey) T(X::Str::Trans::IllegalKey) "abc" "bc" "abc" "abc" "abc" "abc" "abc" "abc" "bbc" "bbc" "bcc"
sub f(&c) { my \r = try c(); $! ?? "T(" ~ $!.^name ~ ")" !! r ~~ Failure ?? "F(" ~ r.exception.^name ~ ")" !! r.raku }; say do { my @w; CONTROL { when CX::Warn { @w.push(.message.lines[0]); .resume } }; f({"abc".trans(:foo)}) ~ " " ~ f({"abc".trans(:d, :foo)}) ~ " " ~ f({"abc".trans(Str => "x")}) ~ " " ~ f({"abc".trans(Any => "x")}) ~ " " ~ f({"abc".trans(Nil => "x")}) ~ " " ~ f({"abc".trans(True => "x")}) ~ " | " ~ @w.elems ~ " | " ~ @w.unique.join("/") }
# rakudo 2026.08: "abc" "abc" "abc" "abc" "abc" "abc" | 6 | Unexpected named variable(s) ':foo,' specified with .trans, did you/Unexpected named variable(s) ':Str("x"),' specified with .trans, did/Unexpected named variable(s) ':Any("x"),' specified with .trans, did/Unexpected named variable(s) ':Nil("x"),' specified with .trans, did/Unexpected named variable(s) ':True("x"),' specified with .trans, did
```
rakupp 4.0.1: differs — a non-Pair is ignored (or applied: `"a" => "x",
"b"` gives `"xbc"`), a type-object key or value deletes (`"a" => Any` gives
`"bc"`), a Callable key maps (`{;} => "x"` gives `"axc"`), and no warning
is issued.

### ST-58  indent by a positive count                              D:yes R:yes V:spec
Each line gets *n* spaces, including whitespace-only lines; only lines
that are exactly empty are left alone; line endings are the ST-39 set
(`\r\n`, U+85, U+2028 count; a trailing newline is kept). A line whose
indent is all tabs gets *n* div 8 tabs and *n* mod 8 spaces
(`$?TABSTOP` is 8: `"\ta".indent(10)` is two tabs and two spaces); a line
whose indent is a run of one horizontal character gets *n* of that
character (`"  a".indent(9)` is 11 spaces); a mixed indent gets spaces
after it. The count is `Int()`-coerced (`"2"`, `2.9` → 2, `1.5` → 1, `<2>`,
`2e0`, `True`, Nil → 0 with a warning); 0 returns the string; Inf and NaN
throw `X::Numeric::CannotConvert`, a non-numeric string
`X::Str::Numeric`, a type object `X::AdHoc`; a count above 2⁶² throws
`X::AdHoc`. Cool invocants are stringified. Roast `indent.t` asserts the
tab rules.
```
sub f(&c) { my \r = try c(); $! ?? "T(" ~ $!.^name ~ ")" !! r ~~ Failure ?? "F(" ~ r.exception.^name ~ ")" !! r.raku }; say f({"a\nb".indent(2)}), " ", f({"a\n\nb".indent(2)}), " ", f({"a\n  \nb".indent(2)}), " ", f({"a\n".indent(2)}), " ", f({"a\n\n".indent(1)}), " ", f({"\n\na".indent(1)}), " ", f({"".indent(2)}), " ", f({"\n".indent(2)}), " ", f({"a b".indent(1)}), " ", f({"a\r\nb".indent(1)}), " ", f({"a\r\n\r\nb".indent(1)}), " ", f({"a\x[85]b".indent(1)}), " ", f({"a\x[2028]b".indent(1)}), " ", f({" a".indent(1)}), " ", f({"  a\n\tb".indent(2)}), " ", f({"\ta".indent(8)}), " ", f({"\ta".indent(10)}), " ", f({"\t a".indent(1)}), " ", f({" \ta".indent(1)}), " ", f({"a".indent(8)}), " ", f({"  a".indent(9)}), " ", f({"\t\ta".indent(9)}), " ", f({"a".indent(0)}), " ", f({"abc".indent(-0)}), " ", f({"a".indent("2")}), " ", f({"a".indent(2.9)}), " ", f({"a".indent(1.5)}), " ", f({"a".indent(<2>)}), " ", f({"a".indent(2e0)}), " ", f({"a".indent(True)}), " ", f({"a".indent(1).^name}), " ", f({42.indent(1)}), " ", f({"a".indent("x")}), " ", f({"a".indent(Str)}), " ", f({"a".indent(Nil)})
# rakudo 2026.08: "  a\n  b" "  a\n\n  b" "  a\n    \n  b" "  a\n" " a\n\n" "\n\n a" "" "\n" " a b" " a\r\n b" " a\r\n\r\n b" " a\x[85] b" " a  b" "  a" "    a\n\t  b" "\t\ta" "\t\t  a" "\t  a" " \t a" "        a" "           a" "\t\t\t a" "a" "abc" "  a" "  a" " a" "  a" "  a" " a" "Str" " 42" T(X::Str::Numeric) T(X::AdHoc) "a"
sub f(&c) { my \r = try c(); $! ?? "T(" ~ $!.^name ~ ")" !! r ~~ Failure ?? "F(" ~ r.exception.^name ~ ")" !! r.raku }; say f({"a".indent(NaN)})
# rakudo 2026.08: T(X::Numeric::CannotConvert)
sub f(&c) { my \r = try c(); $! ?? "T(" ~ $!.^name ~ ")" !! r ~~ Failure ?? "F(" ~ r.exception.^name ~ ")" !! r.raku }; say f({"a".indent(Inf)})
# rakudo 2026.08: T(X::Numeric::CannotConvert)
sub f(&c) { my \r = try c(); $! ?? "T(" ~ $!.^name ~ ")" !! r ~~ Failure ?? "F(" ~ r.exception.^name ~ ")" !! r.raku }; say f({"a".indent(2**62).chars})
# rakudo 2026.08: T(X::AdHoc)
```
rakupp 4.0.2: matches — including the tab arithmetic, the whitespace-only lines, and the refusals that used to abort the process.
spaces instead of tabs, a type object or NaN is 0, and Inf or `2**62`
crash the process ("Internal error").

### ST-59  indent by a negative count or *                          D:yes R:yes V:spec
`indent(*)` removes the common indent of all lines that are not empty
(whitespace-only lines count: `" "` limits the common indent to 1, and a
whitespace-only last line is emptied); `indent(-n)` removes *n* columns.
Tabs expand to the next multiple of 8 for the count (`"\ta".indent(-2)`
leaves 6 spaces, `" \ta".indent(-1)` leaves 7, `"\t\ta".indent(-4)` leaves
a tab and 4 spaces). Removing more than a line has emits a warning "Asked
to remove N spaces, but the shortest indent is M spaces" and removes what
there is; `"a".indent(-1)` warns too. `""`, `"\n"` and `"   "` de-indent to
`""`, `"\n"` and `""`. `-Inf` throws `X::Numeric::CannotConvert`; `-2**70`
throws `X::AdHoc` after the warning. Roast `indent.t` asserts the `*`,
tab and warning-free cases.
```
sub f(&c) { my \r = try c(); $! ?? "T(" ~ $!.^name ~ ")" !! r ~~ Failure ?? "F(" ~ r.exception.^name ~ ")" !! r.raku }; say f({"  a\n    b".indent(*)}), " ", f({"  a\n    b".indent(-2)}), " ", f({"  a\n    b".indent(-1)}), " ", f({"  a\n  b".indent(*)}), " ", f({"   a\n  b".indent(*)}), " ", f({"a\n b".indent(*)}), " ", f({"  a\n\n    b".indent(*)}), " ", f({"  a\n \n    b".indent(*)}), " ", f({"a\n".indent(*)}), " ", f({"  a\n".indent(*)}), " ", f({"  a\n  ".indent(*)}), " ", f({"  a\n   ".indent(*)}), " ", f({"\n".indent(*)}), " ", f({"".indent(*)}), " ", f({"   ".indent(*)}), " ", f({"a".indent(*)}), " ", f({"  a".indent(-2).^name}), " ", f({"a\n b\n  c".indent(-1)}), " ", f({"a\n  b".indent(-2)}), " ", f({"\ta\n\t\tb".indent(*)}), " ", f({"\ta".indent(*)}), " ", f({"\t a".indent(*)}), " ", f({"  a\n\tb".indent(*)}), " ", f({"\ta\n        b".indent(-4)}), " ", f({"\ta".indent(-2)}), " ", f({"\ta".indent(-8).ords.List}), " ", f({"\t\ta".indent(-4)}), " ", f({"\t\ta".indent(-8)}), " ", f({" \ta".indent(-1)}), " ", f({" \ta".indent(-8)}), " ", f({" \t a".indent(-2)}), " ", f({"  \ta".indent(-2)}), " ", f({"  \ta".indent(-3)}), " ", f({"  \ta".indent(-8)}), " ", f({"        a".indent(-8)}), " ", f({"a".indent(-1)}), " ", f({"a".indent(-Inf)}), " ", f({"a".indent(-2**70)})
# rakudo 2026.08: "a\n  b" "a\n  b" " a\n   b" "a\nb" " a\nb" "a\n b" "a\n\n  b" " a\n\n   b" "a\n" "a\n" "a\n" "a\n " "\n" "" "" "a" "Str" "a\nb\n c" "a\nb" "a\n\tb" "a" "a" "a\n      b" "    a\n    b" "      a" (97,) "\t    a" "\ta" "       a" "a" "       a" "      a" "     a" "a" "a" "a" T(X::Numeric::CannotConvert) T(X::AdHoc)
say do { my @w; CONTROL { when CX::Warn { @w.push(.message.lines[0]); .resume } }; my $a = "  a\n b".indent(-2); my $b = "  a\n b".indent(-3); my $c = "  a\n b".indent(*); my $d = "\ta".indent(-9); my $e = "    a".indent(-6); my $g = "a\n\tb".indent(-1); $a.raku ~ " " ~ $b.raku ~ " " ~ $c.raku ~ " " ~ $d.raku ~ " " ~ $e.raku ~ " " ~ $g.raku ~ " | " ~ @w.elems ~ " | " ~ @w.join("/") }
# rakudo 2026.08: "a\nb" "a\nb" " a\nb" "a" "a" "a\n       b" | 5 | Asked to remove 2 spaces, but the shortest indent is 1 spaces/Asked to remove 3 spaces, but the shortest indent is 1 spaces/Asked to remove 9 spaces, but the shortest indent is 8 spaces/Asked to remove 6 spaces, but the shortest indent is 4 spaces/Asked to remove 1 spaces, but the shortest indent is 0 spaces
```
rakupp 4.0.2: matches — `indent(*)`, the tab columns and the warning. (Roast's indent.t exercises three outdent spellings this sheet did not record, where the column COUNT agrees and the tab/space rendering does not.)
removed whole whatever the count, whitespace-only lines are kept, there
is no warning, and `-Inf`/`-2**70` return the string.

### ST-60  parse-base                                              D:yes R:yes V:spec/quirk
Radix 2..36 (the radix must be an Int; `<2>` is fine; a Rat, Num, Str or
type object is `X::Multi::NoMatch`; an Int invocant has no `parse-base`);
digits are case-insensitive; `+`, `-` and U+2212 signs; a radix point
gives a Rat (`"1.0"` is `1.0`, `".1"` and `"-.1"` work); Arabic-Indic
digits are accepted; `e` is a digit in base 16 (`"1e3"` is 483).
**Quirk:** a fraction whose denominator exceeds 64 bits comes back as a
Num (`"1.00000000000000000001"` is `1e0`). A radix outside 2..36 is an
`X::Syntax::Number::RadixOutOfRange` Failure (`radix` attribute), checked
before the string; anything else — `""`, a digit outside the base, `0x`
prefixes, `_`, spaces, `e` in base 10, `,`, `/`, `Inf`, a mark, a trailing
or leading dot, `1..1`, doubled signs, a trailing sign — is an
`X::Str::Numeric` Failure with `pos`. The strings `"camel"` and `"beer"`
as radix read 🐪🐫 and 🍺🍻 as binary digits. Roast `parse-base.t` asserts
signs, fractions, U+2212 and the failures.
```
sub f(&c) { my \r = try c(); $! ?? "T(" ~ $!.^name ~ ")" !! r ~~ Failure ?? "F(" ~ r.exception.^name ~ "," ~ (r.exception.?pos // r.exception.?radix // "-") ~ ")" !! r.raku }; say f({"ff".parse-base(16)}), " ", f({"FF".parse-base(16)}), " ", f({"ff".parse-base(16).^name}), " ", f({"-ff".parse-base(16)}), " ", f({"+ff".parse-base(16)}), " ", f({"−1".parse-base(10)}), " ", f({"Raku".parse-base(36)}), " ", f({"ZZ".parse-base(36)}), " ", f({"10".parse-base(2)}), " ", f({"01".parse-base(2)}), " ", f({"0".parse-base(2)}), " ", f({"12345678901234567890".parse-base(10)}), " ", f({1337.base(32).parse-base(32)}), " ", f({"1.1".parse-base(2)}), " ", f({"1.1".parse-base(2).^name}), " ", f({"1.0".parse-base(2)}), " ", f({"1.0".parse-base(2).^name}), " ", f({"FF.DD".parse-base(16)}), " ", f({".1".parse-base(2)}), " ", f({"-.1".parse-base(2)}), " ", f({"+.1".parse-base(2)}), " ", f({"−.1".parse-base(2)}), " ", f({"-0".parse-base(10)}), " ", f({"-0.0".parse-base(10)}), " ", f({"0.1".parse-base(3)}), " ", f({"1.00000000000000000001".parse-base(10)}), " ", f({"1e3".parse-base(16)}), " ", f({"1.1e1".parse-base(16)}), " ", f({parse-base("11", 2)}), " ", f({"10".parse-base("2")}), " ", f({"1".parse-base(<2>)}), " ", f({10.parse-base(2)}), " ", f({"10".parse-base(10.9)}), " ", f({"ff".parse-base(16.0)}), " ", f({"1".parse-base(2e0)}), " ", f({"1".parse-base(Int)}), " ", f({"1".parse-base(Str)}), " ", f({"🐪🐫🐪".parse-base("camel")}), " ", f({"🍻🍺".parse-base("beer")}), " ", f({"🐫🐪a".parse-base("camel")}), " ", f({"🐪".parse-base("cat")}), " ", f({"1".parse-base("camel")})
# rakudo 2026.08: 255 255 "Int" -255 255 -1 1273422 1295 2 1 0 12345678901234567890 1337 1.5 "Rat" 1.0 "Rat" 255.86328125 0.5 -0.5 0.5 -0.5 0 0.0 <1/3> 1e0 483 1.117431640625 3 T(X::Multi::NoMatch) 1 T(X::Method::NotFound) T(X::Multi::NoMatch) T(X::Multi::NoMatch) T(X::Multi::NoMatch) T(X::Multi::NoMatch) T(X::Multi::NoMatch) 2 2 T(X::Str::Numeric) T(X::Multi::NoMatch) 1
sub f(&c) { my \r = try c(); $! ?? "T(" ~ $!.^name ~ ")" !! r ~~ Failure ?? "F(" ~ r.exception.^name ~ "," ~ (r.exception.?pos // r.exception.?radix // "-") ~ ")" !! r.raku }; say f({"1".parse-base(1)}), " ", f({"1".parse-base(37)}), " ", f({"1".parse-base(0)}), " ", f({"1".parse-base(-2)}), " ", f({"".parse-base(1)}), " ", f({"1".parse-base(2**70)}), " ", f({"".parse-base(2)}), " ", f({"12".parse-base(2)}), " ", f({"abz".parse-base(16)}), " ", f({"0b1".parse-base(2)}), " ", f({"0b1".parse-base(16)}), " ", f({"0x10".parse-base(16)}), " ", f({"1_0".parse-base(10)}), " ", f({" 1".parse-base(10)}), " ", f({"1 ".parse-base(10)}), " ", f({"1e3".parse-base(10)}), " ", f({"1,1".parse-base(10)}), " ", f({"1/2".parse-base(10)}), " ", f({"Inf".parse-base(10)}), " ", f({"NaN".parse-base(36)}), " ", f({"1i".parse-base(36)}), " ", f({"١".parse-base(10)}), " ", f({"Ⅻ".parse-base(10)}), " ", f({"1\x[301]".parse-base(10)}), " ", f({"1.".parse-base(2)}), " ", f({"-1.".parse-base(2)}), " ", f({"0.".parse-base(2)}), " ", f({".".parse-base(2)}), " ", f({"-".parse-base(2)}), " ", f({"+".parse-base(2)}), " ", f({"1.2".parse-base(2)}), " ", f({"1..1".parse-base(2)}), " ", f({"1.1.1".parse-base(2)}), " ", f({"+-1".parse-base(10)}), " ", f({"--1".parse-base(10)}), " ", f({"1-".parse-base(10)})
# rakudo 2026.08: T(X::Syntax::Number::RadixOutOfRange) T(X::Syntax::Number::RadixOutOfRange) T(X::Syntax::Number::RadixOutOfRange) T(X::Syntax::Number::RadixOutOfRange) T(X::Syntax::Number::RadixOutOfRange) T(X::Syntax::Number::RadixOutOfRange) T(X::Str::Numeric) T(X::Str::Numeric) T(X::Str::Numeric) T(X::Str::Numeric) 177 T(X::Str::Numeric) 10 T(X::Str::Numeric) T(X::Str::Numeric) T(X::Str::Numeric) T(X::Str::Numeric) T(X::Str::Numeric) T(X::Str::Numeric) 30191 54 1 T(X::Str::Numeric) T(X::Str::Numeric) T(X::Str::Numeric) T(X::Str::Numeric) T(X::Str::Numeric) T(X::Str::Numeric) T(X::Str::Numeric) T(X::Str::Numeric) T(X::Str::Numeric) T(X::Str::Numeric) T(X::Str::Numeric) T(X::Str::Numeric) T(X::Str::Numeric) T(X::Str::Numeric)
```
rakupp 4.0.2: differs — `parse-base` reads a Unicode minus and Nd digits from any script now (`S32-str/parse-base.t` passes in full); the radix still accepts a Str or a Rat where Rakudo wants an Int, and a `1e0`-shaped body is read as a Rat.
exact Rat, a Str/Rat/Num radix is coerced (`10.9` → 10, `16.0` → 16), an
Int/Str type object and the easter eggs are `X::Syntax::Number::RadixOutOfRange`,
Arabic-Indic digits are refused, and a trailing dot is accepted.

### ST-61  :16("FF"): the radix-call form                          D:partial R:yes V:spec/quirk
`:R(str)` parses `str` in base *R*, allowing `_` between digits and a
radix point, and no sign or whitespace. A `0x`/`0o`/`0b`/`0d` prefix (lower
case only) switches the base — but only where the prefix letter is not a
digit of *R*: `b`, `o`, `d`, `x` for *R* ≤ 11, `o` and `x` for *R* ≤ 24,
`x` for *R* ≤ 33; otherwise it is read as digits (`:16("0b1")` is 177,
`:12("0b1")` 133, `:34("0x10")` 38182); a `:R<…>` prefix with a digit 1–9
after the colon also switches (`:16(":10<12>")` is 12 — **quirk**, since
`:16(":16<F>")` and `:16(":2<11>")` work the same way and `:16(":0<1>")`
does not). `:10("1e3")` is the Num `1000e0`. A non-Str argument throws
`X::Numeric::Confused`; a type object is `X::Multi::NoMatch`; bad text is
an `X::Str::Numeric` Failure. `:R[d0, d1, …]` takes decimal digit values
(`:16[1, 2, 3]` is 291) and accepts a digit ≥ *R* (`:2[1, 2]` is 4), a
`"."` for a fraction (`:16[1, ".", 8]` is 1.5; a second `"."` throws
`X::AdHoc`), and an empty list (0). A radix outside 2..36 in the source is
an `X::Str::Numeric` Failure at run time. Roast `S02-literals/radix.t`
asserts the prefix rules and `X::Numeric::Confused`.
```
sub f(&c) { my \r = try c(); $! ?? "T(" ~ $!.^name ~ ")" !! r ~~ Failure ?? "F(" ~ r.exception.^name ~ ")" !! r.raku }; say f({:16("FF")}), " ", f({:16("ff")}), " ", f({:16("+FF")}), " ", f({:16("-FF")}), " ", f({:16("−FF")}), " ", f({:16("1_F")}), " ", f({:16("0_F")}), " ", f({:16("FF.8")}), " ", f({:16(".8")}), " ", f({:16("FF.")}), " ", f({:16("0xFF")}), " ", f({:16("0XFF")}), " ", f({:16("0xF_F")}), " ", f({:16("0x_FF")}), " ", f({:16("0x")}), " ", f({:16("0b1")}), " ", f({:16("0d10")}), " ", f({:12("0b1")}), " ", f({:11("0b1")}), " ", f({:11("0x1")}), " ", f({:11("0d1")}), " ", f({:11("0o1")}), " ", f({:24("0x10")}), " ", f({:25("0x10")}), " ", f({:33("0x10")}), " ", f({:34("0x10")}), " ", f({:36("0x1")}), " ", f({:2("0b101")}), " ", f({:2("0B1")}), " ", f({:2("0o1")}), " ", f({:2("0d1")}), " ", f({:2("0x1")}), " ", f({:8("0o17")}), " ", f({:10("0d10")}), " ", f({:16(":10<12>")}), " ", f({:16(":16<F>")}), " ", f({:16(":2<11>")}), " ", f({:16(":9<8>")}), " ", f({:16(":1<1>")}), " ", f({:16(":0<1>")}), " ", f({:16(":a<1>")}), " ", f({:16("F/2")}), " ", f({:16("1e3")}), " ", f({:10("1e3")}), " ", f({:16("")}), " ", f({:16(" F")}), " ", f({:16("F ")}), " ", f({:16("0xZZ")}), " ", f({:16(<FF>)}), " ", f({:10(42)}), " ", f({:16(1.5)}), " ", f({:16(Str)})
# rakudo 2026.08: 255 255 T(X::Str::Numeric) T(X::Str::Numeric) T(X::Str::Numeric) 31 15 255.5 T(X::Str::Numeric) T(X::Str::Numeric) 255 T(X::Str::Numeric) 255 255 T(X::Str::Numeric) 177 3344 133 1 1 1 1 16 16 16 38182 1189 5 T(X::Str::Numeric) 1 1 1 15 10 12 15 3 8 T(X::Str::Numeric) T(X::Str::Numeric) T(X::Str::Numeric) T(X::Str::Numeric) 483 1000e0 T(X::Str::Numeric) T(X::Str::Numeric) T(X::Str::Numeric) T(X::Str::Numeric) 255 T(X::Numeric::Confused) T(X::Numeric::Confused) T(X::Multi::NoMatch)
sub f(&c) { my \r = try c(); $! ?? "T(" ~ $!.^name ~ ")" !! r ~~ Failure ?? "F(" ~ r.exception.^name ~ ")" !! r.raku }; say f({:16[1, 2, 3]}), " ", f({:2[1, 1, 0]}), " ", f({:2[1, 2]}), " ", f({:10["1", "2"]}), " ", f({:10[1.5, 2]}), " ", f({:16[1, ".", 8]}), " ", f({:16[1, ".", ".", 8]}), " ", f({:16[".", 8]}), " ", f({:16[1, "."]}), " ", f({:16[]}), " ", f({EVAL ':37("1")'}), " ", f({EVAL ':1("1")'}), " ", f({EVAL ':0("1")'})
# rakudo 2026.08: 291 6 4 12 17.0 1.5 T(X::AdHoc) 0.5 1 0 T(X::Str::Numeric) T(X::Str::Numeric) T(X::Str::Numeric)
```
rakupp 4.0.1: differs — no `0x`-style or `:R<…>` prefixes, `".8"` and
`"FF."` are accepted, `:10("1e3")` is refused, a non-Str argument is
converted (`:16(1.5)` is 1.3125), the list form mis-handles fractions and
Rats, and an out-of-range radix is a compile-time
`X::Syntax::Number::RadixOutOfRange`.

### ST-62  Str.raku escapes                                        D:no R:no V:spec
`.raku` double-quotes and escapes `\0 $ @ % & { \b \n \r \t " \\` (not
`}` or `'`), prints `\r\n` as `\r\n`, and `\x[…]` for the other C0 and
C1 controls (`\e` is `\x[1B]`, U+85 is `\x[85]`). Everything else prints
verbatim: no-break space, zero-width space, soft hyphen, BOM, U+2028,
U+2060, private use, noncharacters, U+10FFFF, the wide spaces. A grapheme
that *begins* with a combining mark (canonical combining class above 0)
is hexified with its codepoints comma-joined (`\x[301]`, `\x[300,301]`,
`\x[301]a`), also U+3099 and U+93C; a spacing mark (U+93F), an enclosing
keycap (U+20E3), a variation selector, ZWJ, a tag or a jamo filler print
verbatim, alone or attached. `.gist` is the text.
```
say "a\0b".raku, " ", "\$\@\%\&\{".raku, " ", "}".raku, " ", "'".raku, " ", "\b\n\r\t\"\\".raku, " ", "\r\n".raku, " ", "\e".raku, " ", "\x[1]\x[7F]\x[85]\x[A0]\x[200B]".raku, " ", "\x[F]\x[9F]\x[AD]\x[FEFF]\x[2028]\x[2060]".raku, " ", "\x[0B]\x[0C]".raku, " ", "\x[E000]\x[FFFD]\x[FFFE]\x[10FFFF]\x[FFF9]".raku, " ", "é€😀".raku, " ", "\x[1680]\x[3000]".raku, " ", "".raku, " ", "abc".raku, " ", "ab".gist.raku
# rakudo 2026.08: "a\0b" "\$\@\%\&\{" "}" "'" "\b\n\r\t\"\\" "\r\n" "\x[1B]" "\x[1]\x[7F]\x[85] ​" "\x[F]\x[9F]­﻿ ⁠" "\x[B]\x[C]" "�￾􏿿￹" "é€😀" " 　" "" "abc" "ab"
say "\x[301]".raku, " ", "\x[301]a".raku, " ", "\x[301]\x[302]".raku, " ", "\x[300]\x[301]".raku, " ", "e\x[301]".raku, " ", "x\x[301]".raku, " ", "\x[E9]\x[301]".raku, " ", "\x[3099]".raku, " ", "\x[093C]".raku, " ", "\x[093F]".raku, " ", "a\x[093F]".raku, " ", "\x[20E3]".raku, " ", "1\x[20E3]".raku, " ", "\x[FE0F]".raku, " ", "\x[2764]\x[FE0F]".raku, " ", "\x[E0100]".raku, " ", "a\x[E0100]".raku, " ", "\x[200D]".raku, " ", "a\x[200D]b".raku, " ", "\x[1F468]\x[200D]\x[1F469]".raku, " ", "\x[1F3FB]".raku, " ", "\x[1F1FA]\x[1F1F8]".raku, " ", "\x[600]".raku, " ", "\x[1160]".raku, " ", "\x[D7B0]".raku
# rakudo 2026.08: "\x[301]" "\x[301]a" "\x[301,302]" "\x[300,301]" "é" "x́" "é́" "\x[3099]" "\x[93C]" "ि" "aि" "⃣" "1⃣" "️" "❤️" "󠄀" "a󠄀" "‍" "a‍b" "👨‍👩" "🏻" "🇺🇸" "؀" "ᅠ" "ힰ"
```
rakupp 4.0.2: matches — C1 controls are hexified and a grapheme that begins with a combining mark is written as its codepoints, comma-joined.
combining mark is never hexified.

### ST-63  encode                                                  D:yes R:yes V:spec/bug/quirk
The default and `utf8`/`UTF-8`/`UTF8` give a `utf8` Blob (NFC bytes:
`"e\x[301]"` is 2 bytes, `"x\x[301]"` 3, `"\r\n"` 2); `utf8-c8`, `latin-1`,
`ASCII` and the `windows-125x` codepages give a `Blob[uint8]`; `utf-16`
gives a `utf16` whose elements are code units (an emoji is 2), while
`utf-16le`/`utf-16be` give 2-byte units per BMP character; `utf-32` and
any unknown name including `""` throw `X::Encoding::Unknown`. **Bug:**
`encode(Str)` never returns (killed by the alarm). An unencodable
character throws `X::AdHoc` unless `:replacement`: `True` inserts `?`, a
Str inserts that string (empty allowed), Nil or `False` is the same as
none. `:strict` makes the unassigned windows-1252 positions (0x81) throw
instead of passing through. **Quirk:** the non-strict windows-1251 encoder
maps `é` to byte 233 (which decodes as `й`); with `:strict` it throws.
`:translate-nl` changes `\n` only on Windows. Roast `encode.t` asserts
the types, `:translate-nl` and the replacement rules.
```
sub f(&c) { my \r = try c(); $! ?? "T(" ~ $!.^name ~ ")" !! r ~~ Failure ?? "F(" ~ r.exception.^name ~ ")" !! r.raku }; say f({"abc".encode}), " ", f({"abc".encode.^name}), " ", f({"".encode}), " ", f({42.encode}), " ", f({"é".encode.bytes}), " ", f({"😀".encode.bytes}), " ", f({"e\x[301]".encode.bytes}), " ", f({"x\x[301]".encode.bytes}), " ", f({"\r\n".encode.bytes}), " ", f({"é".encode("utf8").^name}), " ", f({"é".encode("UTF-8").^name}), " ", f({"a".encode("UTF8").^name}), " ", f({"abc".encode("utf8-c8").^name}), " ", f({"é".encode("latin-1")}), " ", f({"é".encode("latin1").^name}), " ", f({"a".encode("Latin-1").^name}), " ", f({"a".encode("ASCII").^name}), " ", f({"é".encode("utf-16").^name}), " ", f({"é".encode("utf-16").elems}), " ", f({"😀".encode("utf16").elems}), " ", f({"é".encode("utf-16le").elems}), " ", f({"é".encode("utf-16be").elems}), " ", f({"é".encode("utf-32").elems}), " ", f({"é".encode("windows-1252")}), " ", f({"€".encode("windows-1252")}), " ", f({"\x[81]".encode("windows-1252")}), " ", f({"\x[81]".encode("windows-1252", :strict)}), " ", f({"a".encode("windows-1252", :strict)}), " ", f({"é".encode("windows-1251")}), " ", f({"abc".encode.decode}), " ", f({"é".encode("latin-1").decode("latin-1")})
# rakudo 2026.08: utf8.new(97,98,99) "utf8" utf8.new() utf8.new(52,50) 2 4 2 3 2 "utf8" "utf8" "utf8" "Blob[uint8]" Blob[uint8].new(233) "Blob[uint8]" "Blob[uint8]" "Blob[uint8]" "utf16" 1 2 2 2 T(X::Encoding::Unknown) Blob[uint8].new(233) Blob[uint8].new(128) Blob[uint8].new(129) T(X::AdHoc) Blob[uint8].new(97) Blob[uint8].new(233) "abc" "é"
sub f(&c) { my \r = try c(); $! ?? "T(" ~ $!.^name ~ ")" !! r ~~ Failure ?? "F(" ~ r.exception.^name ~ ")" !! r.raku }; say f({"é".encode("ascii")}), " ", f({"€".encode("iso-8859-1")}), " ", f({"é".encode("windows-1251", :replacement("?"))}), " ", f({"é".encode("ascii", :replacement("?"))}), " ", f({"é".encode("ascii", :replacement("xyz"))}), " ", f({"é".encode("ascii", :replacement)}), " ", f({"é".encode("ascii", :replacement(""))}), " ", f({"é".encode("ascii", :replacement(Nil))}), " ", f({"é".encode("ascii", :!replacement)}), " ", f({"é".encode("latin-1", :replacement("?"))}), " ", f({"a\nb".encode(:translate-nl)}), " ", f({"a\r\nb".encode(:translate-nl)}), " ", f({"a".encode("bogus")}), " ", f({"a".encode("")})
# rakudo 2026.08: T(X::AdHoc) T(X::AdHoc) Blob[uint8].new(233) Blob[uint8].new(63) Blob[uint8].new(120,121,122) Blob[uint8].new(63) Blob[uint8].new() T(X::AdHoc) T(X::AdHoc) Blob[uint8].new(233) utf8.new(97,10,98) utf8.new(97,13,10,98) T(X::Encoding::Unknown) T(X::Encoding::Unknown)
say (try "a".encode(Str).raku) // $!.^name
# rakudo 2026.08: (no output; killed by the 10 s alarm)
say Blob.new(233).decode("windows-1251").raku, " ", (try "й".encode("windows-1251").raku) // $!.^name, " ", (try "é".encode("windows-1251").raku) // $!.^name, " ", (try "é".encode("windows-1251", :strict).raku) // $!.^name, " ", Blob.new(233).decode("windows-1252").raku
# rakudo 2026.08: "й" Blob[uint8].new(233) Blob[uint8].new(233) X::AdHoc "é"
```
rakupp 4.0.1: differs — `utf8-c8` and `ASCII` results are typed `utf8`,
`utf-16le`/`be` and `utf-32` give one element, unencodable characters and
unknown encodings silently produce UTF-8 bytes, `:strict` is ignored,
`encode(Str)` returns, and a Blob decodes as UTF-8 whatever the name.

### ST-64  fmt and sprintf                                         D:partial R:partial V:spec/quirk
`fmt` defaults to `%s`; width and precision count graphemes (`"é".fmt("%3s")`
pads to 3, `%.1s` truncates, `%5.2s`); `%%` passes through; a numeric
string satisfies `%d`/`%f`. **Quirk:** every `fmt` error — a directive the
string cannot satisfy (`%d` on `"abc"`), an unknown directive, too many or
too few directives (`""`, `"%%"`, `42`, `"%s %s"`), a type-object format —
is a plain `X::AdHoc`, while `sprintf` reports
`X::Str::Sprintf::Directives::Count` and `::BadType`. `sprintf` as a
method uses the invocant as the format; without arguments it needs a
format without directives.
```
sub f(&c) { my \r = try c(); $! ?? "T(" ~ $!.^name ~ ")" !! r ~~ Failure ?? "F(" ~ r.exception.^name ~ ")" !! r.raku }; say f({"abc".fmt}), " ", f({"abc".fmt("%5s")}), " ", f({"abc".fmt("%-5s|")}), " ", f({"abc".fmt("%.1s")}), " ", f({"abc".fmt("%5.2s")}), " ", f({"é".fmt("%3s")}), " ", f({"e\x[301]".fmt("%3s")}), " ", f({"".fmt("%s")}), " ", f({"42".fmt("%03d")}), " ", f({"1.5".fmt("%.2f")}), " ", f({"abc".fmt("%s%%")}), " ", f({"abc".fmt("%d")}), " ", f({"abc".fmt("%x")}), " ", f({"abc".fmt("%q")}), " ", f({"abc".fmt("%s %s")}), " ", f({"abc".fmt("")}), " ", f({"abc".fmt("%%")}), " ", f({"abc".fmt(42)}), " ", f({"abc".fmt(Str)}), " ", f({"%s".sprintf("x")}), " ", f({"%s-%s".sprintf("a", "b")}), " ", f({"abc".sprintf}), " ", f({"%s".sprintf}), " ", f({"%s".sprintf(1, 2)}), " ", f({"%d".sprintf("abc")})
# rakudo 2026.08: "abc" "  abc" "abc  |" "a" "   ab" "  é" "  é" "" "042" "1.50" "abc\%" T(X::AdHoc) T(X::AdHoc) T(X::AdHoc) T(X::AdHoc) T(X::AdHoc) T(X::AdHoc) T(X::AdHoc) T(X::AdHoc) "x" "a-b" "abc" T(X::Str::Sprintf::Directives::Count) T(X::Str::Sprintf::Directives::Count) T(X::Str::Sprintf::Directives::BadType)
```
rakupp 4.0.1: differs — no error is reported: `%d` on `"abc"` is `"0"`,
`%q` and `""` give `""`, `"%s %s"` gives `"abc "`, `42` gives `"42"`, and
the `sprintf` count/type errors give `""`, `"1"` and `"0"`.

### ST-65  Version, Date, DateTime, IO, WHY                         D:yes R:? V:spec/quirk
`.Version` accepts any text (`"v1.2"` keeps the `v` as a part, `""` is
the empty version, `"x"` is a one-part version); `.Date`/`.DateTime` need
the ISO shape and throw `X::Temporal::InvalidFormat` otherwise (`"2015-1-1"`
included); a date string alone gives midnight; `.IO` makes an `IO::Path`,
`""` throws `X::AdHoc`, and `"-".IO` still works but prints a deprecation
notice (not a warning). **Quirk:** `'Life, the Universe and
Everything'.WHY` is 42; any other Str's `.WHY` is Nil.
```
sub f(&c) { my \r = try c(); $! ?? "T(" ~ $!.^name ~ ")" !! r ~~ Failure ?? "F(" ~ r.exception.^name ~ ")" !! r.raku }; say f({"1.2.3".Version}), " ", f({"1.2.3".Version.parts}), " ", f({"v1.2".Version}), " ", f({"".Version}), " ", f({"x".Version}), " ", f({"a.b".Version.parts}), " ", f({"2015-11-24".Date.year}), " ", f({"2015-1-1".Date}), " ", f({"x".Date}), " ", f({"2012-02-29T12:34:56Z".DateTime.hour}), " ", f({"2023-03-04".DateTime}), " ", f({"2015-11-24T00:00:00".DateTime.timezone}), " ", f({"/tmp".IO.^name}), " ", f({"a/b".IO.basename}), " ", f({"".IO.^name}), " ", f({'Life, the Universe and Everything'.WHY}), " ", f({"abc".WHY})
# rakudo 2026.08: v1.2.3 (1, 2, 3) Version.new('v.1.2') Version.new Version.new('x') ("a", "b") 2015 T(X::Temporal::InvalidFormat) T(X::Temporal::InvalidFormat) 12 DateTime.new(2023,3,4,0,0,0) 0 "IO::Path" "b" T(X::AdHoc) 42 Nil
say do { my @w; CONTROL { when CX::Warn { @w.push(.message.lines[0]); .resume } }; "-".IO.^name ~ " " ~ "-".IO.basename ~ " | " ~ @w.elems ~ " | " ~ @w.unique.join("/") }
# rakudo 2026.08: IO::Path - | 0 |
```
rakupp 4.0.1: differs — `"v1.2".Version` keeps the `v`, `"".IO` is an
`IO::Path`, `"2015-1-1".Date` and `"x".Date` (as year 0) are accepted, and
`.WHY` is Any.

### ST-66  The List invocant trap                                  D:yes R:no V:spec
`contains`, `index`, `rindex` and `indices` on a List or Array warn
"Calling '.contains' on a List, did you mean '$item (elem) @list'?" (and
the `.first(…, :k)`, `:end`, `.grep(…, :k)` variants) and then stringify
the list, so `(1, 12).contains(2)` is True and `<a b>.index("b")` is 2.
```
say do { my @w; CONTROL { when CX::Warn { @w.push(.message.lines[0]); .resume } }; my $a = (1, 2).contains(1); my $b = (1, 12).contains(2); my $c = (1, 2).contains(/2/); my $d = <a b>.contains("a", 0); my $e = <a b>.index("b"); my $f = <a b>.rindex("a"); my $g = try [1, 2].indices(1); $a ~ " " ~ $b ~ " " ~ $c ~ " " ~ $d ~ " " ~ $e ~ " " ~ $f ~ " " ~ $g.raku ~ " | " ~ @w.elems ~ " | " ~ @w.join("/") }
# rakudo 2026.08: True True True True 2 0 $(0,) | 5 | Calling '.contains' on a List, did you mean '$item (elem) @list'?/Calling '.contains' on a List, did you mean '$item (elem) @list'?/Calling '.index' on a List, did you mean '.first( ..., :k)'?/Calling '.rindex' on a List, did you mean '.first( ..., :k, :end)'?/Calling '.indices' on a Array, did you mean '.grep( ..., :k)'?
```
rakupp 4.0.1: differs — no warning, and `Array.indices` does not exist.

## Counts

| | items |
|---|---|
| total | 66 |
| not fully stated by docs (D:yes) nor asserted by Roast (R:yes) | 12 |
| Rakudo bugs (do not imitate) | 10 — ST-11 allomorph lines, ST-27 rindex past end / list+pos / list+pos recursion, ST-38 comb limit overlap, ST-39 :!count, ST-42 fractional limit and :end, ST-44 zero-width needle hang and Str for regex needles, ST-49 negative :c and bare :c, ST-50 :as and "" :g, ST-54 :ov/:ex accepted, ST-63 encode(Str) hang |
| quirks (recorded, step two decides) | 18 — ST-12, ST-17, ST-19, ST-21, ST-28, ST-30, ST-31, ST-36, ST-45, ST-47, ST-48, ST-51, ST-55, ST-60, ST-61, ST-63, ST-64, ST-65 |
| rakupp 4.0.1 differs (before implementation) | 60 |
| rakupp 4.0.1 matches (before implementation) | 6 — ST-01, ST-03, ST-15, ST-18, ST-22, ST-37 |
| **rakupp 4.0.2 matches** | **10** — ST-01, ST-03, ST-05, ST-06, ST-15, ST-18, ST-22, ST-58, ST-59, ST-62 |
| rakupp 4.0.2 differs | 56 — many of them by one field of a forty-field probe |

Implementation began 2026-09-18 from this sheet alone, with the Homebrew
Rakudo `v2026.08` as oracle and Roast as the gate; it is PARTIAL, and each
item's own line says where it stands. Of the 118 probes above, 11 matched
Rakudo before and **22** match now; of the 66 items, 6 matched fully and
**10** do. The Roast files this sheet names go **61 fully-passing → 67** of
109, 50,954 assertions → 51,918, with no file lost; the whole suite goes
**729 → 735** files. `S32-str/val.t` alone went from 913 failing
assertions to none, because one gap — five forms missing from the numeric
grammar — accounted for 875 of them. Also green now: `contains.t`,
`indices.t`, `parse-base.t`, `rindex.t` and `substr.t`.

Two of the three rakupp defects this sheet surfaced are fixed:
`"a".indent(Inf)` and `"a".indent(2**62)` refuse instead of aborting the
process (ST-58). Still open: `fail` inside a `subst` replacement block
escapes the surrounding `try` (ST-51), and `"ab" x 1e10` tries to allocate
the string instead of refusing (ST-14).

Where the remaining work is, in the order it would pay off: the allomorph
rules (ST-09 to ST-11, and the `.WHAT`/`cmp` of an allomorph); `substr-rw`'s
Proxy, which is 23 assertions in one file and needs the CONTAINER at the
call site (ST-36); the exception TYPES the argument checks raise, which are
one `X::OutOfRange` here where Rakudo separates four (ST-35, ST-60); `split`
with a limit and a needle list (ST-42, ST-44); the `subst` adverb family
(ST-46 to ST-54); `trans` (ST-55 to ST-57); and `encode` (ST-63).

## Method (how this sheet was produced)

1. `git show 2026.08:src/core.c/<file>` for the four files; read in full.
   Where a method delegates outside them (`succ`/`pred`, `encode`, the
   regex engine) only the observable result is recorded.
2. Each behaviour turned into a one-line probe; probes run through
   `perl -e 'alarm 10; exec @ARGV' raku -e …` on the Homebrew 2026.08 binary
   and on `build-arm64/rakupp`; outputs recorded verbatim. A probe whose
   expression may throw, fail or return Nil is wrapped in the `f` helper
   described at the top, so that one bad case cannot take the rest of the
   line with it; a lazy result is reified inside the wrapper (`.eager`,
   `.List`, `.elems`). Three Rakudo hangs (ST-27c, ST-44c, ST-63c) and
   one rakupp hang (ST-14c) are recorded as such.
3. D flag from `doc/Type/Str.rakudoc`, `doc/Type/Cool.rakudoc` and
   `doc/Type/Allomorph.rakudoc` in the local docs checkout; R flag from
   reading `S32-str/*.t` (the 65 files Rakudo passes here),
   `S02-literals/allomorphic.t`, `radix.t`, `listquote-whitespace.t`,
   `S02-types/capture.t`, `S03-operators/repeat.t`, `bit.t`, `eqv.t`,
   `autoincrement.t`, `S03-smartmatch/any-str.t`,
   `S05-substitution/subst.t`, `S05-transliteration/trans.t` and
   `with-closure.t`, `S05-modifier/counted-match.t`, `global.t`,
   `overlapping.t`, `exhaustive.t`, `pos.t`, `continue.t`; `?` where not
   looked for.
4. No probe depends on wall-clock time or threads; the `"-".IO`
   deprecation notice and the `:translate-nl` result are the only
   platform-dependent lines (macOS).
