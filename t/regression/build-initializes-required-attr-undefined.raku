# `is required` asks whether construction INITIALIZED the attribute, not
# whether it holds a defined value: a BUILD that writes the slot satisfies it
# even when what it writes is undefined.
#
# `class E { has $.t is required; submethod BUILD(:$!t) { } }; E.new` lives
# in Rakudo: the attributive parameter initializes $!t whether or not `t` was
# passed, and the check (nqp::attrinited) sees an initialized slot. rakupp
# asked "is the slot defined?" instead and threw X::Attribute::Required, for
# that shape and for `$!t = $t` with no `t`, `$!t = Nil`, `$!t //= $t` and a
# list assignment. Construction now notes the slots it writes (noteAttrInit:
# the attribute lvalue, attrAssignLane and attrParamRef) and the check counts
# a noted write.
#
# What must still throw: a BUILD that never writes the slot (it ignores the
# argument, only reads $!t, or skips the assignment), a TWEAK that writes it
# (too late), and no BUILD at all.
#
# Left as it was, on purpose: an attributive parameter with nothing passed
# leaves the slot as it is here, where Rakudo writes Any into it — so
# `has $.t = 7; submethod BUILD(:$!t) { }` keeps 7 here (Any in Rakudo), and
# `has Int $.t; submethod BUILD(:$!t) { }` constructs here where Rakudo dies
# on its own type check.
#
# Every expectation was checked against Rakudo v2026.09 (2026-10-18).

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eq $want { say "ok - $desc" }
    else { $fails++; note "FAIL: $desc — got '$got', want '$want'" }
}
sub outcome(&code) {
    CATCH { default { return .^name } }
    my $r = code();
    'lived: ' ~ ($r // 'undef').raku
}

# a BUILD that writes the slot, undefined or not
class E1 { has $.t is required; submethod BUILD(:$!t) { } }
ck outcome({ E1.new.t }), 'lived: "undef"', 'BUILD(:$!t) with no t passed';
ck outcome({ E1.new(:t(5)).t }), 'lived: 5', '…and with one';
class E2 { has $.t is required; submethod BUILD(:$t) { $!t = $t } }
ck outcome({ E2.new.t }), 'lived: "undef"', '$!t = $t with no t passed';
class E3 { has $.t is required; submethod BUILD(:$t) { $!t = Nil } }
ck outcome({ E3.new.t }), 'lived: "undef"', '$!t = Nil';
class E4 { has $.t is required; submethod BUILD(:$t) { $!t //= $t } }
ck outcome({ E4.new.t }), 'lived: "undef"', '$!t //= $t';
class E5 { has $.t is required; submethod BUILD(:$t) { ($!t, $) = $t, 1 } }
ck outcome({ E5.new.t }), 'lived: "undef"', 'a list assignment';
class E6 { has $.t is required; submethod BUILD() { self.set }; method set { $!t = Any } }
ck outcome({ E6.new.t }), 'lived: "undef"', 'a method BUILD calls writes it';
class E7 { has @.t is required; submethod BUILD(:@!t) { } }
ck outcome({ E7.new.t }), 'lived: $[]', 'BUILD(:@!t) with nothing passed';
class E8 { has %.t is required; submethod BUILD(:%!t) { } }
ck outcome({ E8.new.t }), 'lived: ${}', 'BUILD(:%!t) with nothing passed';
class E9 { has @.t is required; submethod BUILD() { @!t = () } }
ck outcome({ E9.new.t }), 'lived: $[]', '@!t = ()';
class P10 { has $.t is required; submethod BUILD(:$!t) { } }
class E10 is P10 { }
ck outcome({ E10.new.t }), 'lived: "undef"', 'a parent BUILD, from a subclass';
role R11 { has $.t is required; submethod BUILD(:$!t) { } }
class E11 does R11 { }
ck outcome({ E11.new.t }), 'lived: "undef"', 'a role BUILD';
ck outcome({ R11.new.t }), 'lived: "undef"', 'a punned role';
class E12 { has $.t is required; submethod BUILD(:$!t) { }; method new() { self.bless } }
ck outcome({ E12.new.t }), 'lived: "undef"', 'through a custom new and bless';
class E13 { has $!t is required; submethod BUILD(:$!t = 42) { }; method t { $!t } }
ck outcome({ E13.new.t }), 'lived: 42', 'a parameter default';
class E14 { has $.t is required; submethod BUILD(:$!t) { } }
class Holder { has $.inner; submethod BUILD() { $!inner = E14.new } }
ck outcome({ Holder.new.inner.t }), 'lived: "undef"', 'constructed inside another BUILD';

# a BUILD that does not write it
class N1 { has $.t is required; submethod BUILD() { } }
ck outcome({ N1.new(:t(1)) }), 'X::Attribute::Required', 'a BUILD that ignores the argument';
ck outcome({ N1.new }), 'X::Attribute::Required', '…or has none';
class N2 { has $.t is required; submethod BUILD(:$t) { $!t = $t if $t } }
ck outcome({ N2.new }), 'X::Attribute::Required', 'an assignment that does not run';
class N3 { has $.t is required; submethod BUILD(:$t) { $!t } }
ck outcome({ N3.new }), 'X::Attribute::Required', 'a BUILD that only reads it';
class N4 { has @.t is required; submethod BUILD() { @!t.elems } }
ck outcome({ N4.new }), 'X::Attribute::Required', '…an array one';
class N5 { has $.t is required; submethod BUILD(:$x) { } }
ck outcome({ N5.new }), 'X::Attribute::Required', 'a BUILD binding something else';
class N6 { has $.t is required; submethod BUILD(*%h) { } }
ck outcome({ N6.new }), 'X::Attribute::Required', 'a slurpy that writes nothing';
class N7 { has $.t is required; submethod TWEAK() { $!t = 5 } }
ck outcome({ N7.new }), 'X::Attribute::Required', 'a TWEAK is too late';
class N8 { has $.t is required }
ck outcome({ N8.new }), 'X::Attribute::Required', 'no BUILD at all';
class N9 { has $.t is required; has $.u; submethod BUILD(:$!u) { } }
ck outcome({ N9.new(:t(1)) }), 'X::Attribute::Required', 'a BUILD binding another attribute';
class N10 { has $.t is required; submethod BUILD(:$!t!) { } }
ck outcome({ N10.new }), 'X::AdHoc', 'a required named parameter still says so first';
# (one write is noted for its own object only)
class N11 { has $.t is required; submethod BUILD() { } }
class Builder { has $.u is required; submethod BUILD(:$!u) { N11.new } }
ck outcome({ Builder.new }), 'X::Attribute::Required', 'a write to the outer object does not excuse the inner';

say $fails == 0 ?? 'PASS' !! 'FAIL';
exit $fails == 0 ?? 0 !! 1;
