# A signature written with a return VALUE — `--> Nil`, `--> True`, `--> 42` —
# returns that value's type, as Rakudo's does: `&f.signature.returns` was Mu
# for `sub f(Int $x --> Nil)`, and `&f.signature ~~ :(Int)` True, since the
# routine keeps the Nil as what its body answers and the Signature object
# never heard of it. A signature literal `:(Int --> Nil)` dropped it too.
# Cro's trait multis are declared `--> Nil`, so a search among
# `&trait_mod:<is>.candidates` by a signature without one matched here.
# Every line answers the same under Rakudo 2026.09.

sub ck($got, $want, $desc) {
    return if $got eqv $want;
    say "FAIL: $desc — got {$got.raku}, want {$want.raku}";
    exit 1;
}

sub f(Int $x --> Nil) { }
ck &f.signature.returns.^name, 'Nil', '--> Nil';
ck &f.signature ~~ :(Int), False, 'no match without it';
ck &f.signature ~~ :(Int --> Nil), True, 'a match with it';
ck :(Int --> Nil) ~~ :(Int), False, 'a literal signature';
ck :(Int --> Nil).returns.^name, 'Nil', '…returns Nil';
ck &f.returns.^name, 'Nil', 'the routine (as before)';
ck f(1), Nil, 'it returns Nil (as before)';
ck &f.signature.raku, ':(Int $x --> Nil)', 'its .raku (as before)';

sub k(--> True) { }
ck &k.signature.returns.^name, 'Bool', '--> True';
ck &k.signature ~~ :(), False, '…is no empty signature';
ck k(), True, 'and returns True';
sub e(--> Empty) { }
ck &e.signature.returns.^name, 'Slip', '--> Empty';
sub n(--> 42) { }
ck &n.signature.returns.^name, 'Int', '--> 42';
ck n(), 42, 'and returns 42';
sub s(--> "x") { }
ck &s.signature.returns.^name, 'Str', '--> "x"';
sub g(--> Int) { 1 }
ck &g.signature.returns.^name, 'Int', '--> Int (as before)';
sub h(Int $x) { }
ck &h.signature.returns.^name, 'Mu', 'none (as before)';

say 'PASS';
