# A role whose own metaobject overrides `specialize` as well as
# `specialize_with` composes: rakupp called the specialize_with alone, outside
# the `specialize` Rakudo calls it from, so one that assigns a dynamic
# variable its `specialize` declares died "Dynamic variable $*… not found".
# Method::Also's AliasableRoleHOW is that shape, and every role with an
# `is also` method died as a class composed it (since 0b29ef00, unseen
# because t/regression/method-also-role-composition-2026-09-02.raku needs
# Method::Also, absent where the suite runs); this case needs no module.
# A metaobject with only a `specialize_with` still hears of each class
# (t/regression/role-metaclass-mixin-hooks.raku).
# Every line answers the same under Rakudo 2026.09.

sub ck($got, $want, $desc) {
    return if $got eqv $want;
    say "FAIL: $desc — got {$got.raku}, want {$want.raku}";
    exit 1;
}

my role WrapsHOW {
    method specialize(Mu \r, Mu:U \obj, *@pos, *%named) {
        my $*SPEC-ENV;
        callsame
    }
    method specialize_with(Mu $, Mu \conc, Mu \type-env, |) {
        $*SPEC-ENV := type-env;
        nextsame
    }
}
multi sub trait_mod:<is>(Method:D \m, :$wrapped!) {
    $*PACKAGE.HOW does WrapsHOW
        if $*PACKAGE.HOW ~~ Metamodel::ParametricRoleHOW && $*PACKAGE.HOW !~~ WrapsHOW;
}

role Plain { method solo is wrapped { 'x' } }
class P does Plain { }
ck P.solo, 'x', 'the class composes, with the role method';

role Typed[::T] { method t is wrapped { T.^name } }
class Q does Typed[Int] { }
ck Q.t, 'Int', '…a parameterized role too';

role Outer does Plain { method o { 'o' } }
class O does Outer { }
ck (O.solo, O.o), ('x', 'o'), '…and a role that does it';

say 'PASS';
