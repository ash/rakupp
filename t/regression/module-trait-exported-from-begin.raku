# Regression: Cro::RPC::JSON declares its `is json-rpc` traits as `is export`
# multis inside a BEGIN block, and none of it worked:
#
#   * a routine `is export` declared in a nested block (a BEGIN) went into the
#     module's EXPORT packages but never reached the importer, which only
#     took exported routines from the module's top-level scope — so `is
#     json-rpc` was no trait at all and every class stayed a plain class;
#   * `Metamodel::ClassHOW` named the dist's own
#     Cro::RPC::JSON::Metamodel::ClassHOW once that was loaded: a qualified
#     name the registry did not hold resolved to any class whose name ENDED
#     with it, and the setting's HOWs have no class entry. Its `when
#     Metamodel::ClassHOW` then never matched a class.
#
# A module's own nested `Metamodel::Mine` still answers its short name, as
# in Rakudo.
#
# Expectations checked against Rakudo 2026.09
# (~/.rakubrew/versions/moar-2026.09/install/bin/rakudo). Green on both.

use lib $?FILE.IO.parent.add('lib').Str;
use RakuppBeginTraitsOther;
use RakuppBeginTraits;
use RakuppBeginTraits::Metamodel::ClassHOW;

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

class C {
    method foo is rbt-rpc { }
    method bar is rbt-rpc("baz") { }
    method qux is rbt-other { }
}
ck C.HOW ~~ RakuppBeginTraits::Metamodel::ClassHOW, True, 'a trait exported from a BEGIN block runs';
ck (C.^rbt-has('foo'), C.^rbt-has('baz'), C.^rbt-has('bar')), (True, True, False),
   '…each of its candidates';
ck @RakuppBeginTraitsOther::marked, ['qux'], "…beside another module's trait of the same name";
ck rbt-begin-sub(), 'from a BEGIN', 'a plain sub exported from a BEGIN block';

ck Metamodel::ClassHOW.^name, 'Perl6::Metamodel::ClassHOW',
   "a module's own …::Metamodel::ClassHOW does not take the setting's name";
ck Int.HOW ~~ Metamodel::ClassHOW, True, '…and a class HOW is still one';

module M {
    class Metamodel::Mine { }
    class X::Mine is Exception { }
    our sub names { Metamodel::Mine.^name, X::Mine.^name, X::AdHoc.^name, Metamodel::EnumHOW.^name }
}
ck M::names(), ('M::Metamodel::Mine', 'M::X::Mine', 'X::AdHoc', 'Perl6::Metamodel::EnumHOW'),
   "a package's own nested names still answer their short path";

say $fails ?? "FAIL ($fails)" !! 'PASS';
exit $fails ?? 1 !! 0;
