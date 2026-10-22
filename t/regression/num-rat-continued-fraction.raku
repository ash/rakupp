# `Num.Rat($epsilon)` walks the continued fraction of the double as Rakudo's
# Num.Rat does, step for step, and stops where it stops: the fractional part
# reaches 0, or b/d — correctly rounded, as MoarVM's div_In rounds it — is
# within epsilon.
#
# The walk used to keep its convergents in int64 and give up on overflow, so
# `1e-300.Rat(0)` was 0/1 (Rakudo: 1/10**300); it stopped when the fractional
# part fell below 1e-18, a convergent early; and it divided b/d after rounding
# each to a double, so past 2**53 it stopped where Rakudo's never does.
#
# `.Rat(0)` is NOT the double's exact binary value, in either engine:
# `0.1e0.Rat(0)` is 1/10 (FLOAT-HEAVY-ISSUES issue 1 said otherwise; Rakudo
# 2026.09 answers 1/10). Where Rakudo's walk never ends — a small epsilon whose
# b/d stays an ulp away while the fractional part, all rounding error by then,
# never reaches 0 — this one answers the exact value, which is within any
# epsilon; there Rakudo grows Ints until memory runs out, or dies storing the
# Inf a subnormal's reciprocal is.
#
# Every expected answer below is Rakudo 2026.09's, except the last three,
# where Rakudo has none.
#
# Contract: exit 0 + last line PASS.
my @fail;
sub check(Mu $got, Mu $want, $what) { @fail.push("$what: got {$got.raku} want {$want.raku}") unless $got eqv $want }

check 2.6799999999999997e0.Rat(0).nude,      (392892154991943, 146601550370128), '2.68.Rat(0)';
check 2.6799999999999997e0.Rat(1e-300).nude, (392892154991943, 146601550370128), '2.68.Rat(1e-300)';
check 0.1e0.Rat(0).nude,                     (1, 10),                 '0.1e0.Rat(0) is 1/10, as in Rakudo';
check 0.1e0.Rat.nude,                        (1, 10),                 '0.1e0.Rat';
check pi.Rat.nude,                           (355, 113),              'pi.Rat';
check pi.Rat(0).nude,                        (245850922, 78256779),   'pi.Rat(0)';
check (-pi).Rat(1e-12).nude,                 (-4272943, 1360120),     '(-pi).Rat(1e-12)';
check 4.5e0.Rat(0).nude,                     (9, 2),                  'a dyadic value is exact';
check 1e19.Rat.nude,                         (10000000000000000000, 1), 'an integral double';
check 0.333e0.Rat(1e-3).nude,                (1, 3),                  '0.333e0.Rat(1e-3)';
check 1.1597222222222223e0.Rat(0.5e0).nude,  (1, 1),                  'a wide epsilon';
check (-9.215854859051246e0).Rat(0).nude,    (-292037953, 31688645),  'negative, epsilon 0';

# past int64: the convergents are Ints
check 1e-300.Rat(0).numerator, 1, '1e-300.Rat(0) numerator';
check 1e-300.Rat(0).denominator.chars, 300, '1e-300.Rat(0) denominator';
check 9.682812659977115e-51.Rat(0).nude, (1, 103275776896251900444807164449420838385159975731200), 'a tiny value';
check 6.682158565343952e-16.Rat(1e-300).nude, (2, 2993044807964765), 'a tiny value, epsilon 1e-300';

# where Rakudo's walk never ends: the exact value
my $r = 7.01492021304424e-06.Rat(0);
check $r.nude, (8281756023521227, 1180591620717411303424), 'a walk that never converges: the exact value';
check $r.Num, 7.01492021304424e-06, '…which is the double';
check 5e-324.Rat(0).nude.map(*.chars).List, (1, 324), 'the smallest subnormal: 1/2**1074';

if @fail { .say for @fail; say "FAIL" } else { say "PASS" }
