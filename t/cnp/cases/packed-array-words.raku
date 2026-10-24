# `@a[$i]` and `@a[$i] = $x` over a packed `int` / `num` array read and write
# the word with no call (rk_st_idxw, rk_st_idxsetw; FORTRAN-LESSONS item 2).
# What the box's view cannot promise is checked on every access, and each case
# below must answer as the interpreter does: a key past the end or below zero,
# a key that is no machine Int (one that turns into a Rat in mid-loop too), an
# array that grows in the loop (its words move), one unpacked in the loop,
# through a call or through another name bound to it, a computed key, a plain
# array, and a packed row of an array of arrays. The loops that call out keep
# `my $i` at the top level, where a kernel can still run them.
my num @a = (^1000).map(* * 1e-1);
my num @b = (^1000).map(-* * 1e-1);
my num $s = 0e0;
for ^1000 -> $i { $s = $s + @a[$i] * @b[$i] }
say $s;
my num $d = 0e0;
for ^999 -> $i { $d = $d + @a[$i + 1] - @a[$i] }
say $d;
my int @n = ^50;
my $t = 0;
for ^60 -> $i { $t = $t + @n[$i] }
say $t;
my $u = 0;
loop (my $i = -3; $i < 3; $i++) { $u = $u + (@n[$i] // 100) if $i >= 0; $u = $u + @n[$i + 3] }
say $u;
my int @r = ^50;
my $v0 = 0; my $i0 = 0;
while $i0 < 10 { $v0 = $v0 + @r[$i0 == 5 ?? 2.5 !! $i0]; $i0++ }
say $v0;
my $v = 0; my $i1 = 0;
while $i1 < 10 { $v = $v + @n["$i1"] + @n[True] + @n[1.9]; $i1++ }
say $v;
my int @g = ^5;
my $w = 0; my $i2 = 0;
while $i2 < 20 { @g.push($i2 * 10); $w = $w + @g[$i2] + @g.elems; $i2++ }
say $w, ' ', @g.elems, ' ', @g[24];
my int @p = ^20;
my $x = 0; my $m = 0; my $i3 = 0;
while $i3 < 20 { $m = @p.max if $i3 == 7; @p[$i3] = @p[$i3] * 2; $x = $x + @p[$i3]; $i3++ }
say $x, ' ', $m, ' ', @p[7], ' ', @p[19];
my int @q = ^10;
my @qa := @q;
my $c = 0; my $j = 0;
while $j < 10 { $c = $c + @q[$j == 3 ?? 1.5 !! $j]; @qa[$j] = 100; $c = $c + @q[$j]; $j++ }
say $c, ' ', @q[9];
my $h = @n;
my $y = 0;
for ^10 -> $i { $h = 1000 if $i == 4; $y = $y + $h[0] }
say $y;
my @plain = ^30;
my $z = 0;
for ^30 -> $i { $z = $z + @plain[$i] }
say $z;
my @rows = (my int @ = ^8), (my int @ = 10..17);
my $rr = 0;
for ^2 -> $jj { for ^8 -> $k { $rr = $rr + @rows[$jj][$k] } }
say $rr;
# stores: past the end (the array grows), a Rat key, and a store after the
# array was unpacked in the loop, through its own name and through another
my num @sa = 0e0 xx 10;
my $e = 0;
while $e < 12 { @sa[$e] = $e * 0.5e0; $e++ }
say @sa;
my int @sc = 0 xx 5;
my $e3 = 0; my $k3;
while $e3 < 5 { $k3 = $e3 == 2 ?? 1.5 !! $e3; @sc[$k3] = 10 + $e3; $e3++ }
say @sc;
my int @sd = ^6;
my $e4 = 0;
while $e4 < 6 { my $old = @sd[$e4 == 2 ?? 2.5 !! $e4]; @sd[$e4] = $old * 3; $e4++ }
say @sd;
my int @sf = ^6;
my @sfa := @sf;
my $e6 = 0;
while $e6 < 6 { my $o = @sfa[$e6 == 1 ?? 1.5 !! $e6]; @sf[$e6] = $o + 100; $e6++ }
say @sf;
my @se = ^5;
my $e5 = 0;
while $e5 < 5 { @se[$e5] = @se[$e5] * 2; $e5++ }
say @se;
