# Regression: a type declared inside a module, whose name ENDS with the
# module's (`role X::ModE::B` in `module ModE::B`, as Cro::RPC::JSON declares
# `X::Cro::RPC::JSON`), registered the module's name as one of its short
# aliases, and `ModE::B` then meant the role: `Cro::RPC::JSON.^name` answered
# X::Cro::RPC::JSON. A module or package name is never such an alias.
#
# Expectations checked against Rakudo 2026.09
# (~/.rakubrew/versions/moar-2026.09/install/bin/rakudo). Green on both.

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

module ModE::B { role X::ModE::B { } }
ck ModE::B.^name, 'ModE::B', "a module's name is the module";
ck ModE::B.HOW.^name, 'Perl6::Metamodel::ModuleHOW', '…made by ModuleHOW';
module Pond { class Pond::Frog { } }
ck Pond.^name, 'Pond', '…and a class named after its own module does not take it either';
module Forest { class Frog { }; our sub f { Frog.new.^name } }
ck Forest::f(), 'Forest::Frog', "a package's own short names still resolve inside it";

say $fails ?? "FAIL ($fails)" !! 'PASS';
exit $fails ?? 1 !! 0;
