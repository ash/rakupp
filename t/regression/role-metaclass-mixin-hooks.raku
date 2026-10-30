# Regression: Cro::RPC::JSON keeps its JSON-RPC tables in roles it mixes into
# metaobjects, and a role's side of that did not work:
#
#   * a role's own metaobject (`$*PACKAGE.HOW` in its body, a ParametricRoleHOW)
#     was made afresh on each `.HOW`, so a role mixed into it was lost at once,
#     and `$*PACKAGE.^json-rpc-add-method` found no such method;
#   * a trait on the role DECLARATION (`role Foo is json-rpc-actor`) got the
#     group, and `.^candidates[0].HOW` was the group's HOW, not the role's;
#   * when a class composes the role, Rakudo hands it to that metaobject's
#     `specialize_with`, and the concrete role it makes is filled through
#     `add_method`; rakupp called no such hook, so the class never became an
#     actor and the concrete role's table stayed empty;
#   * `nqp::getattr($concrete-how, Metamodel::ConcreteRoleHOW, '@!roles')`;
#   * a trait that died inside a class body left the package prefix behind,
#     so the next type declared was nested inside that class.
#
# Expectations checked against Rakudo 2026.09
# (~/.rakubrew/versions/moar-2026.09/install/bin/rakudo). Green on both.

use nqp;

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

role Marked { }
# (scalars: a mixin's `%` attribute on a Rakudo metaobject is a BOOTHash)
role CHOW { has $!names; method add-name(Mu \t, $n) { $!names = ($!names // "") ~ "<$n>" }; method names(Mu \t) { $!names // "" } }
role CRHOW {
    has $!names;
    method add_method(Mu \r, \name, \code) { $!names = ($!names // "") ~ "<{name}>" if code ~~ Marked; nextsame }
    method names(Mu \t) { $!names // "" }
}
role PRHOW {
    has $!names;
    has $.auth is rw;
    method add-name(Mu \t, $n) { $!names = ($!names // "") ~ "<$n>" }
    method specialize_with(Mu \r, Mu:U \conc, Mu $, @params, |) is raw {
        my \obj = @params[0];
        obj.HOW does CHOW unless obj.HOW ~~ CHOW;
        conc.HOW does CRHOW;
        callsame();
        conc
    }
}
sub onto(Mu \pkg) {
    given pkg.HOW {
        when Metamodel::ClassHOW { $_ does CHOW unless $_ ~~ CHOW }
        when Metamodel::ParametricRoleHOW { $_ does PRHOW unless $_ ~~ PRHOW }
    }
}
multi trait_mod:<is>(Method:D \m, :$marked!) { onto($*PACKAGE); m does Marked; $*PACKAGE.^add-name(m.name) }
multi trait_mod:<is>(Mu:U \t, :$actor!) { onto(t); t.HOW.auth = $actor }

role R[::T] { method foo is marked { "foo" }; method bar { "bar" } }
role S is actor<s-auth> { method baz is marked { "baz" } }
class C does R[Int] { }
class D does S { method own is marked { } }

ck R.^candidates[0].HOW ~~ PRHOW, True, "a role's own metaobject keeps a mixin";
ck S.^candidates[0].HOW.auth, 's-auth', "…and a trait on the role declaration reaches it";
ck (C.HOW ~~ CHOW, D.HOW ~~ CHOW), (True, True), 'specialize_with runs as a class composes the role';
ck C.^mro(:concretizations).map(*.^name).List, <C R Any Mu>, '…with the concrete role in the MRO';
ck C.^mro(:concretizations)[1].^names, '<foo>', '…filled through add_method';
ck D.^mro(:concretizations)[1].^names, '<baz>', '…for a role without parameters too';
ck D.^names, '<own>', "the class's own method trait still works";
ck (C.new.foo, D.new.baz), ('foo', 'baz'), 'the methods are composed as before';
my $roles := nqp::getattr(D.^mro(:concretizations)[1].HOW, Metamodel::ConcreteRoleHOW, '@!roles');
ck $roles[0].HOW.auth, 's-auth', "a concrete role's @!roles is the role it was made from";

# (Rakudo runs a trait as it compiles, so there a dying one is a compile error
# and the EVAL answers nothing; rakupp runs it with the declaration)
my $leak = try EVAL q[
    multi trait_mod:<is>(Method:D \m, :$boom!) { die "boom" }
    sub t(&c) { try c() }
    t { my class LeakA { method m is boom { } } }
    my class LeakB { }
    LeakB.^name
];
ck $leak // 'LeakB', 'LeakB', 'a trait that died leaves no package prefix behind';

say $fails ?? "FAIL ($fails)" !! 'PASS';
exit $fails ?? 1 !! 0;
