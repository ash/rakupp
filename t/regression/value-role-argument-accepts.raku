# A role parameterized by a VALUE matches a parameterization by its type, as
# Rakudo asks `want.ACCEPTS(have)` of each argument: `R2[1] ~~ R2[Int]` was
# False (both are named R2[Int], so the value was not in the name to
# compare), and so were a class that does R2[1] and `.does`. The value pun
# and the class record the parameterization by type beside the one written;
# `.does` asks a pun as `~~` does. The reverse, `R2[Int] ~~ R2[1]`, and another
# value, `R2[2]`, stay False.
# Every line answers the same under Rakudo 2026.09.

sub ck($got, $want, $desc) {
    return if $got eqv $want;
    say "FAIL: $desc — got {$got.raku}, want {$want.raku}";
    exit 1;
}

role R2[\x] { }
ck R2[1].^name, 'R2[Int]', 'the name';
ck R2[1] ~~ R2[Int], True, 'R2[1] ~~ R2[Int]';
ck R2[Int] ~~ R2[1], False, 'not the reverse';
ck R2[1] ~~ R2[1], True, 'the same value';
ck R2[1] ~~ R2[2], False, 'another value';
ck R2['a'] ~~ R2[Str], True, 'a Str';
ck R2[1] ~~ R2[Cool], True, 'a wider type';
ck R2[1] ~~ R2[Str], False, 'another type';
class C does R2[1] { }
ck (C ~~ R2[Int], C.new ~~ R2[Int], C.new ~~ R2[1], C.new ~~ R2[2], C ~~ R2[Str]),
   (True, True, True, False, False), 'a class that does R2[1]';
ck (C.does(R2[1]), C.^does(R2[Int]), C.new.does(R2[Int]), C.does(R2[2]), C.new.does(R2[Str])),
   (True, True, True, False, False), '.does';
role R[::T] { }
class D does R[Int] { }
ck (D.does(R[Int]), D.does(R[Str]), D.does(R[Cool]), D.new.does(R[Int:D])), (True, False, True, False),
   '.does with a type argument';

say 'PASS';
