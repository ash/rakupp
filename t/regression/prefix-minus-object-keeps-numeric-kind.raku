# `-$obj` negates what the object's `Numeric` answered as what it is: the
# prefix took an Int through a native int64 and everything else through a
# Num, so a `Numeric` of 2**100 gave -9223372036854775807, one of 1/3 a Num
# and one of 1+2i a real — where `+$obj` beside it returned each intact.
# Found by the review of src/ (docs/dev/findings/src-review-2026-11-09.md,
# C3). Every line answers the same under Rakudo 2026.09.

sub ck($got, $want, $desc) {
    return if $got eqv $want;
    say "FAIL: $desc — got {$got.raku}, want {$want.raku}";
    exit 1;
}

class Big { method Numeric { 2**100 } }
class Third { method Numeric { 1/3 } }
class Cplx { method Numeric { 1+2i } }
class Five { method Numeric { 5 } }
class Half { method Numeric { 2.5e0 } }
class Boxed is Int { }

ck -Big.new, -1267650600228229401496703205376, 'a big Int';
ck -Third.new, <-1/3>, 'a Rat stays a Rat';
ck -Cplx.new, -1-2i, 'a Complex';
ck -Five.new, -5, 'an Int';
ck -Half.new, -2.5e0, 'a Num';
ck -Boxed.new(7), -7, 'a class that is an Int';
ck +Big.new, 1267650600228229401496703205376, 'prefix + as before';

say 'PASS';
