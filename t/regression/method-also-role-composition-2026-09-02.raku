#?requires Method::Also
# Regression: `is also<alias>` (Method::Also) on a DISPATCHER method inside a
# ROLE. Rakudo adds the alias to every class that COMPOSES the role — not to the
# role itself — from AliasableRoleHOW.specialize, and only for a proto/multi
# group (its `is_dispatcher` guard), never a plain method. rakupp's role
# composition dropped the alias entirely, so Method::Also #19's own t/01-basic
# died 9/11 with `No such method 'bar-ber'`. Every expectation here is the
# Rakudo 2026.08 answer. See ClassInfo::alsoRoleAliases.
use Method::Also;

my $fails = 0;
sub ok($cond, $what) { $fails++ unless $cond; note "not ok - $what" unless $cond }

# --- the core case: a proto's alias reaches the composing class, dispatching --
role Ber {
    proto method bar_ber (|) is also<bar-ber> { * }
    multi method bar_ber (Str) { 'Str' }
    multi method bar_ber (Int) { 'Int' }
}
class Bar does Ber { }
ok(Bar.bar_ber('a') eq 'Str', 'the real proto still dispatches by arg type');
ok(Bar.bar-ber('a') eq 'Str', 'the role alias reaches the class and dispatches (Str)');
ok(Bar.bar-ber(42)  eq 'Int', '…and the Int candidate too');
# the alias belongs to the CLASS, not the role (Rakudo puts it there)
ok(Ber.^lookup('bar-ber') === Nil || !Ber.^lookup('bar-ber').defined,
   'the role itself does not carry the alias');
ok(Bar.^lookup('bar-ber').defined, '…the composing class does');

# --- multiple aliases in one trait -------------------------------------------
role Multi {
    proto method run (|) is also<go do-it> { * }
    multi method run (Int $n) { $n * 2 }
}
class M does Multi { }
ok(M.go(5) == 10 && M.do-it(5) == 10, 'is also<a b> installs every alias');

# --- a PLAIN method alias in a role is NOT composed (the is_dispatcher guard) -
role Plain { method solo() is also<lonely> { 'x' } }
class P does Plain { }
ok(P.solo eq 'x', 'a plain role method still composes under its real name');
ok(!(P.^lookup('lonely').defined), 'a plain method alias is dropped, as in Rakudo');

# --- role composing a role: the alias propagates to the eventual class --------
role A2 {
    proto method bb (|) is also<b-b> { * }
    multi method bb (Int) { 'I' }
}
role B2 does A2 { }
class C2 does B2 { }
ok(C2.b-b(1) eq 'I', 'a role-of-role alias reaches the class that does the outer role');

# --- the CLASS-side path (Case A) must still work — regression guard ----------
class Direct {
    proto method m (|) is also<m-alias> { * }
    multi method m (Int) { 'int' }
    multi method m (Str) { 'str' }
}
ok(Direct.m-alias(1) eq 'int' && Direct.m-alias('x') eq 'str',
   'a class-declared proto alias is unaffected');

# --- the consuming class's own same-name method wins over the alias ----------
role Own {
    proto method z (|) is also<zed> { * }
    multi method z (Int) { 'role' }
}
class WithOwn does Own { method zed { 'mine' } }
ok(WithOwn.zed eq 'mine', "the class's own method beats a composed alias");

say $fails ?? "FAIL ($fails)" !! "PASS";
