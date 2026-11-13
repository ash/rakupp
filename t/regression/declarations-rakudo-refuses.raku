# Declarations Rakudo refuses, refused here too: `has $.x!` (rakudo#1427), an
# attributive parameter where no `self` is, a spaced `< 1/2>` in a signature
# (rakudo#1828), a default on a required parameter (rakudo#1426), a
# placeholder in a method (rakudo#2320) and a CStruct with no attributes
# (rakudo#2673). Each was accepted.

my $fails = 0;
sub check($got, $want, $what) {
    if $got ne $want.any { say "FAIL: $what: got $got, want {$want.join(' or ')}"; $fails++ }   # (a want may list alternatives)
}
sub refused($code) { try EVAL $code; $! ?? $!.^name.subst(/'+{' .* $/, '') !! 'ok' }

check refused('class C { has Int $.count! }'), <X::Comp::AdHoc X::AdHoc>, 'has $.x! (rakudo#1427)';
check refused('class G { has $.a is required }; 1'), 'ok', 'is required, as before';
check refused('sub ($!x) { }'), 'X::Syntax::NoSelf', 'sub ($!x) with no class';
check refused('my $c = -> $!x { }'), 'X::Syntax::NoSelf', '-> $!x with no class';
check refused('class D { has $!x; method m { my &f = sub ($!x) { } } }; 1'), 'ok', '…in a method there is a self';
check refused('-> < 1/2> { }'), 'X::Syntax::Missing', 'a spaced < 1/2> (rakudo#1828)';
check refused('-> <1/2> { }; 1'), 'ok', 'an unspaced <1/2> is a literal';
check refused('sub ($count! = Int) { }'), 'X::Parameter::Default', 'a default on $x! (rakudo#1426)';
check refused('sub ($c is required = Int) { }'), 'X::Parameter::Default', '…and on is required';
check refused('class { method z { $^a } }'), <X::AdHoc X::Comp::AdHoc>, 'a placeholder in a method (rakudo#2320)';
check refused('class A { method z { (1, 2).map({ $^x }) } }; 1'), 'ok', '…in a block inside it, as before';
check refused('class Foo is repr("CStruct") { }'), <X::AdHoc X::Comp::AdHoc>, 'a CStruct of nothing (rakudo#2673)';
try EVAL 'sub ($count! = Int) { }';
check $!.message, 'Cannot put default on required parameter $count', 'its message';

say $fails ?? "FAIL ($fails)" !! 'PASS';
exit $fails ?? 1 !! 0;
