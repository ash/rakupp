# Regression: two zero-denominator Rats compared EQUAL to each other whatever
# their signs, so `<1/0> <=> <-1/0>` — +Inf against -Inf — answered Same.
#
# A zero-denominator Rat is not an error in Raku: `<1/0>` is a Rat with
# numerator 1 and denominator 0, and it numifies to Inf. Ordering two Rats is a
# cross-multiplication, `n1*d2` against `n2*d1`, and when BOTH denominators are
# zero both sides of that are zero — so every `<`, `>`, `<=`, `>=` and `<=>`
# between two such values collapsed to Same/False. Rakudo orders that pair by
# the SIGN OF THE NUMERATOR: `<1/0>` above `<0/0>` above `<-1/0>`.
#
# Only the pair is special, and that is the whole point of the fix being this
# narrow. One zero denominator against an ordinary Rat already cross-multiplies
# correctly (`<1/0> <=> 0` is `1*1` against `0*0`), and that path must not move,
# because `<0/0> <= 1` being True is the Algorithm::KDimensionalTree case that
# the equality half of this code was written for: the Canberra distance between
# two axis-projected vectors is exactly `<0/0>`, and routing it through NaN made
# the branch-pruning test permanently false.
#
# Found by the v4.0.0 release gate — S03-operators/arith.t (the `<` and `>`
# cases) and S03-operators/spaceship.t both dropped a test against v3.28.0.
#
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) {
    @fail.push("$what: got {$got.raku} want {$want.raku}") unless $got eqv $want
}

# ---- the pair that was wrong ------------------------------------------------
check <1/0>  <=> <-1/0>, More, '+Inf is more than -Inf';
check <-1/0> <=> <1/0>,  Less, 'and -Inf is less than +Inf';
check <1/0>  >   <-1/0>, True, 'the same through `>`';
check <-1/0> <   <1/0>,  True, 'and through `<`';
check <1/0>  <   <-1/0>, False, 'not the other way';

# NaN sits between them, because its numerator is 0.
check <0/0> <=> <1/0>,  Less, 'a zero numerator is below a positive one';
check <1/0> <=> <0/0>,  More, 'and a positive one is above it';
check <0/0> <   <1/0>,  True, 'the same through `<`';

# Same sign, different magnitude: still Same — both are the same infinity.
check <1/0>  <=> <2/0>,  Same, 'two positive infinities are one value';
check <-1/0> <=> <-2/0>, Same, 'and so are two negative ones';
check <0/0>  <=> <0/0>,  Same, 'NaN against NaN orders Same';

# ---- the path that must NOT have moved --------------------------------------
# One zero denominator only: the ordinary cross-multiplication, unchanged.
check <0/0> <=  1, True,  'the KDimensionalTree case still binds';
check <0/0> <   1, False, 'and its strict form still does not';
check <0/0> <=> 1, Same,  'NaN against an ordinary Rat orders Same';
check <1/0> <=> 0, More,  'but +Inf is still above zero';
check <-1/0> <=> 0, Less, 'and -Inf still below it';
check <1/0> >   1, True,  '+Inf is still above one';

# ---- equality is the OTHER rule, and keeps it -------------------------------
# `==`, `!=` and `cmp` go through the Num value, so NaN is not equal to itself.
check <0/0> == <0/0>, False, 'NaN is not equal to itself';
check <1/0> == <1/0>, True,  'but an infinity is';
check <1/0> cmp <-1/0>, More, 'cmp already ordered them and still does';
check <0/0> cmp <0/0>, Same,  'and NaN cmp NaN is Same';

# ---- the values themselves are untouched ------------------------------------
check <1/0>.numerator,   1, 'the numerator is kept as written';
check <1/0>.denominator, 0, 'and so is the zero denominator';
check <1/0>.Num,  Inf,  'it numifies to Inf';
check <-1/0>.Num, -Inf, 'and to -Inf';

if @fail {
    .say for @fail;
    say "FAIL";
    exit 1;
}
say "PASS";
