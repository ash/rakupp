# `callsame` from a role's method does not reach the same method in a role it
# does: `role S does B` composes B's method away, so nothing is left to call,
# and Rakudo answers Nil. Here the redispatch walked from S to B, S's parent
# slot, and called B's, for a multi and a plain method alike. A class above
# the role is still next (`class M is P does S`), and a method only the inner
# role has is still found.
# Every line answers the same under Rakudo 2026.09.

sub ck($got, $want, $desc) {
    return if $got eqv $want;
    say "FAIL: $desc — got {$got.raku}, want {$want.raku}";
    exit 1;
}

role B { multi method m(Int $v) { "B" } }
role S does B { multi method m(Int $v) { callsame } }
class M does S { }
ck M.new.m(1), Nil, 'a multi';
role B2 { method n { "B2" } }
role S2 does B2 { method n { callsame } }
class M2 does S2 { }
ck M2.new.n, Nil, 'a method';
class P { method q { "P" } }
role S3 { method q { "S3:" ~ callsame } }
class M3 is P does S3 { }
ck M3.new.q, 'S3:P', 'the parent class is next';
class P6 { method n { "P6" } }
role B6 { method n { "B6" } }
role S6 does B6 { method n { "S6:" ~ callsame } }
class M6 is P6 does S6 { }
ck M6.new.n, 'S6:P6', 'past the composed role to the class';
role B7 { method z { "B7" } }
role S7 does B7 { }
class M7 does S7 { }
ck M7.new.z, 'B7', "an inner role's own method";

say 'PASS';
