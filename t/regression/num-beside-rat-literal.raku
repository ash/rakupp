# Regression: a Num beside a Rat literal, in the fast lanes.
#
# A decimal literal is a Rat, and Num op Rat is Num op Rat.Num: that is what
# the general path computes for `$x * 1.0000001 + 0.000000001` over a Num
# `$x`. The loop kernel, the typed subtree and the two-leaf `$x * 1.5` used to
# decline such an operand, and the loop ran ~60 times slower than the same
# line written with `e0` literals (O15 of the 2026-10 parallel review). They
# now read the literal as its .Num where a Num is beside it, and only there:
# an Int or a Rat on the other side still makes a Rat, and every operand's
# type is looked at each time it is read.
#
# Each shape runs in a `for` (the loop kernel) and in a `map` block (the
# interpreter's typed lanes), and is compared with the same arithmetic
# written in Nums. Runs clean under Rakudo too.

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

# the measured line, both lanes, against e0 literals
my $k = 1e0; for ^1000 { $k = $k * 1.0000001 + 0.000000001 }
my $m = 1e0; eager (^1000).map: { $m = $m * 1.0000001 + 0.000000001 };
my $want = 1e0; for ^1000 { $want = $want * 1.0000001e0 + 0.000000001e0 }
ck $k, $want, 'Num * Rat + Rat in a loop';
ck $m, $want, '…and in a map block';

# two leaves: `$x * 1.5`, and the literal on the left
my $t = 1e0; for ^20 { $t = $t * 1.5 }
my $u = 1e0; eager (^20).map: { $u = 0.5 * $u };
ck $t, 1.5e0 ** 20, '$x * 1.5';
ck $u, 0.5e0 ** 20, '0.5 * $x';

# Int and Rat on the other side stay exact
my $i = 3; for ^3 { $i = $i * 1.5 }
ck $i, 10.125, 'an Int times a Rat literal is a Rat';
my $r = 1/3; eager (^3).map: { $r = $r * 1.5 };
ck $r, 1.125, 'a Rat times a Rat literal is a Rat';

# a variable whose type changes under the node
my @seen; my $v = 2;
for ^6 -> $n { $v = $v * 0.5; @seen.push: $v.^name; $v = $v.Num if $n == 2 }
ck @seen.join(' '), 'Rat Rat Rat Num Num Num', 'Int, then Rat, then Num, under one node';
ck $v, 0.03125e0, '…with the value it should have';

# compound assignment
my $c = 1e0; for ^10 { $c += 0.1 }
my $ce = 1e0; for ^10 { $ce += 0.1e0 }
ck $c, $ce, '$x += 0.1 ten times';
my $d = 100e0; for ^5 { $d -= 0.25; $d *= 1.5; $d /= 2.5 }
my $de = 100e0; for ^5 { $de -= 0.25e0; $de *= 1.5e0; $de /= 2.5e0 }
ck $d, $de, '-= *= /= with Rat literals';

# comparisons read the literal as a Num too: ten steps of 0.1 fall short of 1
my $x = 0e0; my $steps = 0;
while $x < 1.0 { $x = $x + 0.1; $steps++ }
ck $steps, 11, 'while $x < 1.0 { $x = $x + 0.1 }';
my $y = 0e0; my $ysteps = 0;
eager (^20).map: { $ysteps++ if $y < 1.0; $y = $y + 0.1 };
ck $ysteps, 11, '…in a map block';
ck (my $z = 0.1e0 + 0.2) == 0.3, False, 'Num == Rat compares as Nums';

# NaN, Inf and the zeros
my $nan = NaN; for ^3 { $nan = $nan * 1.5 + 0.5 }
ck $nan.isNaN, True, 'NaN * 1.5 + 0.5';
my $nl = NaN; ck ($nl < 0.5, $nl == 0.5, $nl != 0.5), (False, False, True), 'NaN compared with a Rat';
my $inf = Inf; ck ($inf * 0.5, ($inf * 0.0).isNaN, -$inf < 0.5), (Inf, True, True), 'Inf beside a Rat literal';
my $nz = -0e0;
ck (~($nz * 1.5), ~($nz + 0.0), ~($nz - 0.0), ~(1e0 * -0.0)), ('-0', '0', '-0', '0'), 'the signed zeros';
my $q = 1e0;
ck (try { my $f = $q / 0.0; $f + 1 }) // 'failed', 'failed', 'Num / 0.0 is a divide by zero';

# a literal whose parts are past 2**53 converts exactly as .Num does
my $w = 1e0; for ^2 { $w = $w * 0.123456789012345678 }
ck $w, 0.123456789012345678.Num * 0.123456789012345678.Num, 'a literal with wide parts';

# natives
my num $nn = 2e0; for ^3 { $nn = $nn * 1.5 }
ck $nn, 6.75e0, 'my num times a Rat literal';
my num $nm = 1e0; my $nr = $nm * 0.5;
ck ($nr, $nr.WHAT), (0.5e0, Num), '…into a plain $';

# FatRat stays FatRat; a Num beside one is a Num
my $fr = FatRat.new(1, 3);
ck ($fr * 1.5).WHAT, FatRat, 'FatRat times a Rat literal';
my $fx = 3e0;
ck ($fx * FatRat.new(1, 3)).WHAT, Num, 'Num times a FatRat';

# a lexical operator still wins
{
    sub infix:<*>($a, $b) { 'mine' }
    my $s = 1e0;
    ck $s * 1.5, 'mine', 'a lexical infix:<*> over Num and Rat';
}

say $fails ?? "FAIL ($fails)" !! 'PASS';
