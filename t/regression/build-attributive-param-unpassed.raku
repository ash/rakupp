# Regression: an attributive parameter of BUILD that nobody passed —
# `submethod BUILD(:$!name)` with no `name` — left the attribute as it was.
# Rakudo's binder stores the parameter's default all the same: Any, or the
# parameter's type, through the attribute's type check. So `has Str $.s`
# refuses the Any, a subset's `where` refuses it, and the attribute's own
# default (`has $.m = 3`) is not applied.
#
# Cro::HTTP::Cookie declares `has CookieName $.name is required` with
# `submethod BUILD(:$!name, :$!value, …)`, and its test wants
# `Cro::HTTP::Cookie.new` without a name or a value to die; under rakupp it
# lived, and `rakupp install cro` stopped at Cro::HTTP.
#
# Expectations checked against Rakudo 2026.09
# (~/.rakubrew/versions/moar-2026.09/install/bin/rakudo). Green on both.

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}
sub outcome(&c) { (try c()) // $!.^name }

my subset CookieName of Str where /^ \w+ $/;
class Cookie {
    has CookieName $.name is required;
    has Str $.value is required;
    submethod BUILD(:$!name, :$!value) {}
}
ck outcome({ Cookie.new }), 'X::TypeCheck::Assignment', 'no name, no value: the Cookie test';
ck outcome({ Cookie.new(name => 'UID') }), 'X::TypeCheck::Assignment', 'no value';
ck outcome({ Cookie.new(value => 'TEST') }), 'X::TypeCheck::Assignment', 'no name';
ck outcome({ Cookie.new(name => 'UID', value => 'TEST').name }), 'UID', 'both';

class S1 { has Str $.s; submethod BUILD(:$!s) {} }
ck outcome({ S1.new }), 'X::TypeCheck::Assignment', 'Any into a Str attribute';
class S2 { has $.m = 3; submethod BUILD(:$!m) {} }
ck S2.new.m, Any, "the attribute's own default is not applied";
class S3 { has Int $.i; submethod BUILD(Int :$!i) {} }
ck S3.new.i, Int, "a typed parameter stores its type";
class S4 { has @.a; has %.h; submethod BUILD(:@!a, :%!h) {} }
ck (S4.new.a, S4.new.h), ([], {}), 'an array and a hash parameter store empty ones';
class S5 { has $.s = 'd'; submethod BUILD(:$!s = 'p') {} }
ck S5.new.s, 'p', "the parameter's default, as before";
class S6 { has Bool $.b; submethod BUILD(:$!b = False) {} }
ck S6.new.b, False, '…typed';
class S7 { has $.x; submethod BUILD(:$!x) {} }
ck S7.new.x, Any, 'an untyped attribute takes the Any';
ck S7.new(x => 5).x, 5, 'a passed value, as before';

say $fails ?? "FAIL ($fails)" !! 'PASS';
exit $fails ?? 1 !! 0;
