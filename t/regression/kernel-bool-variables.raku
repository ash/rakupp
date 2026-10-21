# Regression: a loop kernel's variable may hold a Bool.
#
# The loop kernel used a Bool only as a condition, so the flag loop
# `my $found = False; for … { $found = True if … }` declined it, as did
# `my $ok = $i %% 3`, `$n + ($i %% 3)` and `$b = !$b` (ledger O29 of the
# 2026-10 parallel review). A Bool variable now has a slot of its own: 0 or 1,
# checked as a plain Bool when the loop is entered and written back as a Bool.
# `True`, `False` and `!` are values there; a Bool is the Int it numifies to
# in arithmetic, the numeric comparisons and `+=`, and "True"/"False" as
# text. `min`/`max` (which hand back an operand), `++`, and a variable whose
# type would change keep the generic path. Every answer is Rakudo 2026.09's.
#
# Runs clean under Rakudo too.

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

my $f = False;
for ^1000 -> $i { $f = True if $i == 500 }
ck ($f, $f.WHAT), (True, Bool), 'the flag loop';
my $g = False;
for ^1000 -> $i { if $i == 500 { $g = True; last } }
ck $g, True, '…with `last`';
my $z = False;
for ^0 -> $i { $z = True }
ck ($z, $z.WHAT), (False, Bool), 'a loop that does not run leaves it alone';
my $t = True;
for ^10 { $t = !$t }
ck $t, True, '`!` as a value';
my ($h, $n) = False, 0;
for ^20 -> $i { $h = $i > 9; $n++ if $h }
ck ($h, $n), (True, 10), 'a comparison stored, then a condition';

my $b = True;
my ($a1, $a2, $a3, $a4, $a5, $a6, $a7, $a8, $a9) = 0 xx 9;
for ^3 { $a1 = $b + 1; $a2 = $b * 3; $a3 = $b - True; $a4 = $b ** 2; $a5 = $b div 2;
         $a6 = $b % 2; $a7 = $b +& 3; $a8 = $b +< 2; $a9 = $b + $b }
ck ($a1, $a2, $a3, $a4, $a5, $a6, $a7, $a8, $a9), (2, 3, 0, 1, 0, 1, 1, 4, 2), 'Bool arithmetic is Int arithmetic';
my ($r, $q, $u) = 0, 0, 0e0;
for ^3 { $r = $b / 2; $q = $b * 1.5; $u = $b * 2e0 }
ck ($r, $q, $u), (0.5, 1.5, 2e0), '…and Rat and Num arithmetic';
my ($c1, $c2, $c3, $c4, $c5, $c6) = False xx 6;
for ^3 { $c1 = $b == True; $c2 = $b != False; $c3 = $b < 1; $c4 = $b == 1; $c5 = $b eq "True"; $c6 = $b ne "False" }
ck ($c1, $c2, $c3, $c4, $c5, $c6), (True, True, False, True, True, True), 'comparisons, numeric and string';

my $fb = False;
my ($s1, $s2, $s3, $s4) = "", "", "", 0;
for ^3 { $s1 = "$fb"; $s2 = $fb ~ "x"; $s3 = $fb.Str; $s4 = $fb.chars }
ck ($s1, $s2, $s3, $s4), ("False", "Falsex", "False", 5), 'as text';
my ($e, $acc) = False, "";
for ^3 -> $i { $e = $i %% 2; $acc ~= $e; $acc ~= "," }
ck $acc, "True,False,True,", '`~=` a Bool';
my ($tb, $tt, $tu) = False, "", 0;
for ^5 -> $i { $tb = $i > 2 ?? True !! False; $tt = $tb ?? "yes" !! "no"; $tu = $tb ?? 1 !! 0 }
ck ($tb, $tt, $tu), (True, "yes", 1), '`?? !!` making and taking a Bool';

my $m = 0;
for ^3 { $m = $b max False }
ck ($m, $m.WHAT), (True, Bool), '`max` hands the Bool back (the generic path)';
my $ib = True;
for ^3 { $ib = 1 }
ck ($ib, $ib.WHAT), (1, Int), 'a Bool variable given an Int';
my $bi = 0;
for ^3 -> $i { $bi = $i == 1 }
ck ($bi, $bi.WHAT), (False, Bool), 'an Int variable given a Bool';
my $pp = False;
for ^3 { $pp++ }
ck $pp, True, '`++` on a Bool';

my ($done, $k) = False, 0;
while !$done { $k++; $done = $k >= 5 }
ck ($k, $done), (5, True), 'a `while` on a flag';
my ($ok, $cnt) = False, 0;
for ^30 -> $i { unless $ok { $cnt++ }; $ok = $i > 10 }
ck ($cnt, $ok), (12, True), '`unless` on a flag';
my ($w1, $w2, $w3) = 0, 0, 0;
my ($yes, $no) = True, False;
for ^10 -> $i { given $i { when $yes { $w1++ } }; given $i { when $no { $w2++ } }; given $i { when True { $w3++ } } }
ck ($w1, $w2, $w3), (10, 0, 10), '`when` a Bool: the Bool is the answer';
my ($x, $y) = True, False;
for ^3 { $y = $x; $x = !$x }
ck ($x, $y), (False, True), 'one Bool variable from another';

my $mn = 0;
for ^1000 -> $i { my $isd = $i %% 3; $mn++ if $isd }
ck $mn, 334, '`my` a Bool in the loop';
my $sn = 0;
for ^1000 -> $i { $sn += $i %% 3 }
ck $sn, 334, '`+=` a Bool';
my $dv = 10;
for ^3 -> $i { $dv /= ($i < 5) }
ck ($dv, $dv.WHAT), (10.0, Rat), '`/=` a Bool';

# a loop compiled for a Bool, entered again with an Int in it
my $re = False;
for ^3 -> $kk { for ^5 -> $i { $re = $i > 2 }; $re = 7 if $kk == 1 }
ck $re, True, 'a later entry with another type';

say $fails ?? "FAIL ($fails)" !! 'PASS';
