# `.succ` / `.pred` went through toInt(), which SATURATES a bignum to INT64_MAX
# (BigInt::toLL does that on purpose — its other callers are indices and
# codepoints, where a silent wrap is garbage), and then added one to it. So
# `(10**30).succ` answered -9223372036854775808, and `(2**63-1).succ` wrapped for
# the same reason. The same truncation threw away the fractional part of a Rat or
# a Num and the imaginary part of a Complex: `2.5.succ` was 3, `1.5e0.succ` was 2,
# `(3+4i).succ` was 1.
#
# `$x++` / `$x--` were always right — they take the exact numeric tower. Only the
# named methods did not, and now they do (Rakudo's Real.succ is `self + 1`).
#
# Runs on BOTH engines and must print byte-identical output. Contract: exit 0
# with PASS as the last line.
my @fail;
sub ck($got, $want, $what) {
    @fail.push("$what: got {$got.raku} want {$want.raku}") unless $got eqv $want;
}

# ---- the reported case: past a machine word ---------------------------
ck (10**30).succ, 1000000000000000000000000000001, 'big .succ';
ck (10**30).pred,  999999999999999999999999999999, 'big .pred';
ck (2**64).succ,   18446744073709551617,           '2**64 .succ';
ck (2**64).pred,   18446744073709551615,           '2**64 .pred';
ck (10**30).succ.WHAT, Int,                        'big .succ is an Int';
ck (10**30).succ.succ, 1000000000000000000000000000002, 'big .succ twice';
ck (-(10**30)).succ, -999999999999999999999999999999, 'negative big .succ';
ck (-(10**30)).pred, -1000000000000000000000000000001, 'negative big .pred';

# ---- the int64 boundary, where a small Int had to GROW into a big -----
ck (2**63 - 1).succ, 9223372036854775808,  'int64 max .succ crosses into a big';
ck (-(2**63)).pred, -9223372036854775809,  'int64 min .pred crosses into a big';
ck (2**63 - 1).succ.WHAT, Int,             '…and is still an Int';
ck (2**63).pred, 9223372036854775807,      'back down across the boundary';

# ---- and it must agree with ++ / --, which always took the exact path -
my $inc = 10**30; $inc++;
ck (10**30).succ, $inc, '.succ agrees with ++';
my $dec = 10**30; $dec--;
ck (10**30).pred, $dec, '.pred agrees with --';
my $edge = 2**63 - 1; $edge++;
ck (2**63 - 1).succ, $edge, '.succ agrees with ++ at the boundary';

# ---- Real.succ is `self + 1`, so the fraction survives ----------------
ck 2.5.succ, 3.5,        'Rat .succ keeps the fraction';
ck 2.5.pred, 1.5,        'Rat .pred keeps the fraction';
ck 2.5.succ.WHAT, Rat,   'Rat .succ stays a Rat';
ck (1/3).succ, 4/3,      'reduced Rat .succ';
ck (1/3).pred, -2/3,     'reduced Rat .pred';
ck 1.5e0.succ, 2.5e0,    'Num .succ';
ck 1.5e0.succ.WHAT, Num, 'Num .succ stays a Num';
ck 1.5e0.pred, 0.5e0,    'Num .pred';
ck (3+4i).succ, 4+4i,    'Complex .succ moves the real part only';
ck (3+4i).pred, 2+4i,    'Complex .pred moves the real part only';

# ---- what must NOT have changed --------------------------------------
ck 5.succ, 6,            'small Int .succ';
ck 5.pred, 4,            'small Int .pred';
ck 0.pred, -1,           'zero .pred';
ck (-1).succ, 0,         'negative small .succ';
ck 5.succ.WHAT, Int,     'small Int .succ is an Int';
ck "az".succ, "ba",      'Str .succ still carries';
ck "az".pred, "ay",      'Str .pred';
ck "Az".succ, "Ba",      'Str .succ across case';
ck True.succ, True,      'Bool .succ saturates';
ck False.pred, False,    'Bool .pred saturates';
ck False.succ, True,     'Bool .succ';
ck Date.new(2026,8,31).succ.Str, '2026-09-01', 'Date .succ is still a Date';
ck Date.new(2026,8,31).pred.Str, '2026-08-30', 'Date .pred is still a Date';

if @fail { note $_ for @fail; say "FAIL"; exit 1 }
say "PASS";
