# A role's `is required` attribute is checked on its COMPOSER's turn of the
# build walk, after the BUILD the composer took from the role.
#
# The walk gives a composed role a turn of its own for the per-class step
# only, and that turn comes BEFORE the composer's — while the role's BUILD
# runs on the composer's turn (buildall-role-hook-belongs-to-composer.raku).
# So `submethod BUILD(:$!total)` in the role had not bound anything yet when
# the required check looked, and every construction threw
# X::Attribute::Required. Cro::HTTP::Client failed every request this way:
# its default timeout is `Cro::HTTP::Client::Policy::Timeout.new(:total(Inf))`
# over `role Cro::Policy::Timeout { has Real() $.total is required;
# submethod BUILD(Real:D() :$!total, *%phases) }`.
#
# The same move also checks the roles a role does: `role R does Q` with Q's
# attribute required was never checked at all.
#
# Every expectation was checked against Rakudo v2026.09 (2026-10-18).

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eq $want { say "ok - $desc" }
    else { $fails++; note "FAIL: $desc — got '$got', want '$want'" }
}
sub outcome(&code) {
    CATCH { default { return .^name } }
    code();
    'lived'
}

# the Cro shape: a parameterized role, a coercing attribute, BUILD binding it
role Timeout[%defaults] {
    has Real() $.total is required;
    has Real() %.phases;
    submethod BUILD(Real:D() :$!total, *%phases --> Nil) {
        %!phases{$_} = %phases{$_} // %defaults{$_} for %defaults.keys;
    }
}
class ClientTimeout does Timeout[{ connection => Inf }] { }
ck ClientTimeout.new(:total(Inf)).total, 'Inf', 'the Cro timeout policy constructs';
ck ClientTimeout.new(:total(5), :connection(2)).phases<connection>, '2', '…and its BUILD ran';

role R1 { has $.t is required; submethod BUILD(:$!t) { } }
class C1 does R1 { }
ck C1.new(:t(1)).t, '1', 'a role BUILD binding its required attribute';
class P1 does R1 { }
class D1 is P1 { }
ck D1.new(:t(7)).t, '7', '…from a subclass of the composer';

role R2 { has Int:D $.t is required; submethod BUILD(:$!t) { } }
class C2 does R2 { }
ck C2.new(:t(4)).t, '4', 'a :D required attribute bound by the role BUILD';

role R3 { has $.t is required; submethod BUILD(:$t) { $!t = $t } }
class C3 does R3 { }
ck C3.new(:t(3)).t, '3', 'a role BUILD assigning its required attribute';

# what must still throw
role R4 { has $.t is required }
class C4 does R4 { has $.u }
ck outcome({ C4.new }), 'X::Attribute::Required', 'a role attribute with no BUILD is still required';
role R5 { has $.t is required; submethod BUILD() { } }
class C5 does R5 { }
ck outcome({ C5.new(:t(1)) }), 'X::Attribute::Required', 'a role BUILD that ignores the argument';
role R6 { has $.t is required; submethod TWEAK() { } }
class C6 does R6 { }
ck outcome({ C6.new }), 'X::Attribute::Required', 'a role TWEAK does not excuse it';
ck outcome({ R4.new }), 'X::Attribute::Required', 'a punned role still checks';
ck R4.new(:t(2)).t, '2', '…and accepts';

# a role's own roles are checked too
role Q7 { has $.q is required }
role R7 does Q7 { }
class C7 does R7 { }
ck outcome({ C7.new }), 'X::Attribute::Required', 'a role a role does';
ck C7.new(:q(1)).q, '1', '…and accepts';
role R8 { }
class C8 does R8 does Q7 { submethod BUILD { } }
ck outcome({ C8.new(:q(1)) }), 'X::Attribute::Required', 'the second role, under a class BUILD that binds nothing';

say $fails == 0 ?? 'PASS' !! 'FAIL';
exit $fails == 0 ?? 0 !! 1;
