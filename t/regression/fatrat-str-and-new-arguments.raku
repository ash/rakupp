# Regression (issue #173): two FatRat bugs.
#
# 1. A FatRat prints more fractional digits than a Rat. Once the denominator
#    reaches 100000, Rakudo gives a Rat #digits(denominator) + 1 places and a
#    FatRat #digits(denominator) + #digits(whole part) + 5. rakupp used the Rat
#    rule for both, so FatRat.new(113704391677318964121600,
#    18890217959702350060800) printed 6.0192207374144, not 6.019220737414433984.
# 2. Rat.new and FatRat.new bind `Int:D \nu, Int:D \de`. A Rat, Num or Str
#    argument was truncated to an integer instead of dying, so
#    FatRat.new(…, 18890217959702350060800.0) answered 12327.85….
#
# Every check verified against Rakudo 2026.09 (2026-10-17); this file runs on
# both engines.
#
# Contract: exit 0 + last line PASS.
my @fail;

sub check($got, $want, $desc) {
    @fail.push("$desc: got {$got.raku}, want {$want.raku}") unless $got eqv $want;
}
sub dies-with(&code, $type, $message, $desc) {
    my $r = try code();
    if $! {
        check $!.^name, $type, "$desc: exception type";
        check $!.message, $message, "$desc: message";
    }
    else {
        @fail.push("$desc: lived, gave {$r.raku}");
    }
}

# the issue's value: Str, gist and interpolation all take the FatRat rule
my $f = FatRat.new(113704391677318964121600, 18890217959702350060800);
check $f.nude, (1432144165008, 237928500629), 'the issue value reduces';
check $f.Str, '6.019220737414433984', 'FatRat.Str: den digits + whole digits + 5';
check $f.gist, '6.019220737414433984', 'FatRat.gist is its Str';
check "$f", '6.019220737414433984', 'an interpolated FatRat';
check $f.raku, 'FatRat.new(1432144165008, 237928500629)', 'FatRat.raku is unchanged';
check Rat.new(113704391677318964121600, 18890217959702350060800).Str, '6.0192207374144',
    'a Rat of the same value keeps den digits + 1';

# the 100000 boundary: below it both types get 6 places
check FatRat.new(1, 99999).Str, '0.00001', 'FatRat, denominator 99999';
check FatRat.new(1, 100000).Str, '0.00001', 'FatRat, denominator 100000';
check FatRat.new(1, 100001).Str, '0.0000099999', 'FatRat, denominator 100001';
check Rat.new(1, 100001).Str, '0.00001', 'Rat, denominator 100001';
check FatRat.new(22, 7).Str, '3.142857', 'a small denominator gets 6 places';
check FatRat.new(1, 3).Str, '0.333333', '1/3 as a FatRat';

# the whole part's digits count, and the sign does not
check FatRat.new(123456789 * 100001 + 7, 100001).Str, '123456789.00006999930000699993',
    'a nine-digit whole part widens the fraction';
check FatRat.new(-10**30 - 1, 7 * 10**20).Str, '-1428571428.571428571428571428572857142857142857',
    'a negative FatRat';
check FatRat.new(10**30 - 1, 10**30).Str, '0.999999999999999999999999999999',
    'a fraction just under 1 prints in full';
check FatRat.new(10**40 + 1, 10**20).Str, '100000000000000000000.00000000000000000001',
    'a large whole part with a tiny fraction';
check FatRat.new(-1, 300000).Str, '-0.000003333333', 'a negative fraction below 1';
check FatRat.new(3, -6).Str, '-0.5', 'the sign moves to the numerator';

# results of FatRat arithmetic print the same way
check (FatRat.new(1, 3) / 100000).Str, '0.000003333333', 'FatRat / Int';
check (1.FatRat / 7 / 1000000).Str, '0.0000001428571', 'Int.FatRat / Int / Int';

# a zero denominator names its numerator, as Rakudo does
dies-with { FatRat.new(3, 0).Str }, 'X::Numeric::DivideByZero',
    'Attempt to divide 1 by zero when coercing Rational to Str', 'FatRat 3/0';
dies-with { Rat.new(-3, 0).Str }, 'X::Numeric::DivideByZero',
    'Attempt to divide -1 by zero when coercing Rational to Str', 'Rat -3/0';
dies-with { FatRat.new(0, 0).Str }, 'X::Numeric::DivideByZero',
    'Attempt to divide by zero when coercing Rational to Str', 'FatRat 0/0';

# .new takes defined Ints only
dies-with { FatRat.new(113704391677318964121600, 18890217959702350060800.0) },
    'X::TypeCheck::Binding::Parameter',
    "Type check failed in binding to parameter 'de'; expected Int but got Rat (18890217959702350060...)",
    'the issue: a Rat denominator';
dies-with { FatRat.new(1.5, 2) }, 'X::TypeCheck::Binding::Parameter',
    "Type check failed in binding to parameter 'nu'; expected Int but got Rat (1.5)", 'a Rat numerator';
dies-with { Rat.new(3, 2e0) }, 'X::TypeCheck::Binding::Parameter',
    "Type check failed in binding to parameter 'de'; expected Int but got Num (2e0)", 'a Num denominator';
dies-with { FatRat.new("3", 2) }, 'X::TypeCheck::Binding::Parameter',
    "Type check failed in binding to parameter 'nu'; expected Int but got Str (\"3\")", 'a Str numerator';
dies-with { FatRat.new(Str, 2) }, 'X::TypeCheck::Binding::Parameter',
    "Type check failed in binding to parameter 'nu'; expected Int but got Str (Str)", 'a Str type object';
dies-with { FatRat.new(3, Int) }, 'X::Parameter::InvalidConcreteness',
    "Parameter 'de' of routine 'new' must be an object instance of type\n'Int', not a type object of type 'Int'. Did you forget a '.new'?",
    'an Int type object';
dies-with { Rat.new(Int, 2) }, 'X::Parameter::InvalidConcreteness',
    "Parameter 'nu' of routine 'new' must be an object instance of type\n'Int', not a type object of type 'Int'. Did you forget a '.new'?",
    'an Int type object numerator';
dies-with { FatRat.new(3, 2, 1) }, 'X::AdHoc',
    'Too many positionals passed; expected 1 to 3 arguments but got 4', 'three positionals';
try FatRat.new(1, 1.5);
check ($!.symbol, $!.expected.^name, $!.got), ('de', 'Int', 1.5), 'the type check carries symbol, expected and got';
try FatRat.new(Int, 2);
check ($!.param, $!.routine, $!.should-be-concrete), ('nu', 'new', True), 'the concreteness error carries param and routine';

# …which an Int subtype, an allomorph or an Int-valued enum is
enum E (aa => 3);
check Rat.new(True, 2).raku, '0.5', 'Bool is an Int';
check Rat.new(<3>, 2).raku, '1.5', 'IntStr is an Int';
check Rat.new(aa, 2).raku, '1.5', 'an Int-valued enum is an Int';
check FatRat.new(3).raku, 'FatRat.new(3, 1)', 'the denominator defaults to 1';
check FatRat.new().raku, 'FatRat.new(0, 1)', 'the numerator defaults to 0';
check Rat.new(3, 2, :foo).raku, '1.5', 'a named argument is ignored';

# a class doing Rational binds the same way
class R does Rational[Int, Int] { }
check R.new(1, 3).Str, '0.333333', 'a Rational class constructs';
dies-with { R.new(1, 2.5) }, 'X::TypeCheck::Binding::Parameter',
    "Type check failed in binding to parameter 'de'; expected Int but got Rat (2.5)", 'a Rational class refuses a Rat';

if @fail {
    .say for @fail;
    say "FAIL ({+@fail})";
    exit 1;
}
say "PASS";
