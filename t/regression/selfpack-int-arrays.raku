# An untyped Array of plain machine Ints packs itself, as one of plain Nums
# does (selfPack; selfpack-arrays.raku has the Num cases): its elements are
# kept as 64-bit words, read and written as words by the interpreter's lanes,
# its loop kernels and --cnp, and anything that is no machine Int, or that
# needs an element's container, unpacks it into the Array it would have been.
# The words are a `my int @a`'s, but none of its rules may show: a Bool stays
# a Bool, a result past 64 bits is a big Int and does not wrap, a gap is
# holes and not zeros. Every value below is Rakudo 2026.09's, which
# RAKUPP_NO_SELFPACK=1 gives too. RAKUPP_SELFPACK_MIN=1 packs even the short
# arrays here.
#
# Contract: exit 0 + last line PASS.
my @fail;

sub check($got, $want, $desc) {
    @fail.push("$desc: got {$got.raku}, want {$want.raku}") unless $got eqv $want;
}

# stores that are no machine Int keep what they store
my @a = 1..10;
@a[3] = 5e0;
check (@a[3].WHAT, @a[3], @a[2].WHAT), (Num, 5e0, Int), 'a Num stored stays a Num';
my @b = 1 xx 10;
@b[0] = 1.5;
@b[1] = <2>;
@b[2] = "x";
@b[3] = True;
@b[4] = 2 ** 70;
check (@b[0].WHAT, @b[1].WHAT, @b[2], @b[3], @b[3].WHAT, @b[4]),
      (Rat, IntStr, "x", True, Bool, 1180591620717411303424), 'a Rat, an allomorph, a Str, a Bool, a big Int';
my @b2 = 0 xx 10;
loop (my $bi = 0; $bi < 10; $bi++) { @b2[$bi] = $bi %% 2 }
check (@b2[0], @b2[1], @b2[0].WHAT), (True, False, Bool), 'a Bool stored in a loop is no 1';
my @c = 1 xx 10;
@c[0] = 1 but role { method foo { 42 } };
check @c[0].foo, 42, 'an Int with a role mixed in';
my @d = 1 xx 10;
@d[0] = Nil;
check @d[0], Any, 'Nil resets an element';
enum Colour <Red Green Blue>;
my @en = 0 xx 10;
@en[1] = Green;
check (@en[1], @en[1].WHAT === Colour), (Green, True), 'an enum value keeps its type';
my int $nat = 7;
my @nt = 0 xx 10;
@nt[2] = $nat;
check (@nt[2], @nt[2].WHAT), (7, Int), 'a native int is stored as an Int';

# no wrapping: a step, an op= and a sum past 64 bits are big Ints
my @o = 9223372036854775807 xx 10;
@o[0]++;
@o[1] += 1;
@o[2] *= 2;
++@o[3];
check (@o[0], @o[1], @o[2], @o[3], @o[4]),
      (9223372036854775808, 9223372036854775808, 18446744073709551614, 9223372036854775808, 9223372036854775807),
      'a step and an op= past 64 bits';
check @o.sum, 101457092405402533880, 'a sum past 64 bits';
my @o2 = -9223372036854775808 xx 10;
@o2[0]--;
@o2[1] -= 1;
check (@o2[0], @o2[1]), (-9223372036854775809, -9223372036854775809), 'a step below 64 bits';
my @o3 = 4611686018427387904 xx 10;
loop (my $oi = 0; $oi < 10; $oi++) { @o3[$oi] += @o3[$oi] }
check (@o3[0], @o3[9]), (9223372036854775808, 9223372036854775808), 'an op= past 64 bits in a loop';
my @o4 = 1 xx 10;
loop (my $oj = 0; $oj < 10; $oj++) { @o4[$oj] = 9223372036854775807 + $oj }
check (@o4[0], @o4[1], @o4[9]), (9223372036854775807, 9223372036854775808, 9223372036854775816),
      'a store past 64 bits in a loop';

# growth: a store past the end leaves holes, not zeros
my @e = 0 xx 10;
@e[12] = 1;
check (@e.elems, @e[11], @e[11]:exists, @e[12]), (13, Any, False, 1), 'a gap is holes';
my @f = 0 xx 10;
@f[10] = 2;
@f.push(3);
@f.push(4e0);
check (@f.elems, @f[10], @f[11], @f[12].WHAT), (13, 2, 3, Num), 'stores and pushes at the end';
check (@f[100], @f[100]:exists), (Any, False), 'a read past the end';
my @f2 = 0 xx 10;
@f2.push($_) for 1..100;
check (@f2.elems, @f2[109], @f2.sum), (110, 100, 5050), 'pushes in a loop';

# containers: a bind, an rw parameter, `for` aliasing
my @g = 0 xx 10;
my $x := @g[2];
$x = 7;
my $y;
@g[1] := $y;
$y = 3e0;
check (@g[1], @g[2]), (3e0, 7), 'a bound element writes through';
my @h = 1 xx 10;
sub set-rw($z is rw) { $z = 6e0 }
set-rw(@h[1]);
check @h[1].WHAT, Num, 'an rw parameter';
my @i = 0 xx 10;
$_ += 1 for @i;
check @i.sum, 10, '$_ writes through';
my @j = 0 xx 10;
for @j <-> $v { $v = "s" }
check @j.join, "s" x 10, '<-> writes a Str through';
my @j2 = 9223372036854775807 xx 10;
$_++ for @j2;
check @j2[9], 9223372036854775808, '$_++ past 64 bits';

# what it is: an Array of Ints, however it is asked
my @k = 1..10;
check (@k.WHAT, @k.of, @k[0].WHAT, @k.elems, @k.end), (Array, Mu, Int, 10, 9), 'an Array of Mu';
check @k.raku, '[1, 2, 3, 4, 5, 6, 7, 8, 9, 10]', '.raku';
check @k.gist, '[1 2 3 4 5 6 7 8 9 10]', '.gist';
check (@k.sum, @k.min, @k.max), (55, 1, 10), '.sum, .min, .max';
check (@k.map(* + 1).sum, @k.grep(* %% 2).elems, @k.reverse[0], @k.sort({ $^b <=> $^a })[0]), (65, 5, 10, 10),
      '.map, .grep, .reverse, .sort';
check (@k[2..4], @k[*-1], @k[-1 + 1], @k.head(2).List), ((3, 4, 5), 10, 1, (1, 2)), 'slices';
check (@k eqv [1, 2, 3, 4, 5, 6, 7, 8, 9, 10]), True, 'eqv a literal';
check (@k «+» 1)[9], 11, 'a hyper';
check ([+] @k), 55, 'a reduction';
check (@k.pop, @k.shift, @k.elems), (10, 1, 8), '.pop and .shift';
@k.unshift(0);
check (@k[0], @k.elems), (0, 9), '.unshift';
check @k.splice(1, 2), [2, 3], '.splice';

# a Range fills the words directly, when it is one of machine Ints
my @r1 = 1..^20;
my @r2 = 5^..^20;
my @r3 = 'a'..'t';
my @r4 = 1.5..20;
my @r5 = 20..1;
my @r7 = 1..Inf;
my @r8 = -10..10;
my @r9 = ^20;
check (@r1.elems, @r1[0], @r1[18], @r1.sum), (19, 1, 19, 190), '1..^20';
check (@r2.elems, @r2[0], @r2[13]), (14, 6, 19), '5^..^20';
check (@r3.elems, @r3[0], @r3[19]), (20, "a", "t"), 'a Str Range';
check (@r4.elems, @r4[0], @r4[0].WHAT), (19, 1.5, Rat), 'a Rat Range';
check @r5.elems, 0, 'an empty Range';
check (@r7[0], @r7[1000]), (1, 1001), 'an endless Range';
check (@r8.elems, @r8[0], @r8.sum, @r8.min), (21, -10, 0, -10), 'a Range from below zero';
check (@r9.elems, @r9[19], @r9.WHAT, @r9.raku.substr(0, 9)), (20, 19, Array, '[0, 1, 2,'), '^20';
@r9[5] = "five";
check (@r9[4], @r9[5], @r9[6]), (4, "five", 6), 'a Str into a Range-filled Array';

# copies are copies
my @m = 1..10;
my @n = @m;
@n[0] = 99;
@m[1] = 77;
check (@m[0], @m[1], @n[0], @n[1]), (1, 77, 99, 2), 'my @n = @m copies';
my @p = 1..10;
my $item = @p;
$item[0] = 5;
check @p[0], 5, 'an itemized Array is the same Array';
my @q = 1..10;
sub bump(@z) { @z[0] = 42; @z.push("s") }
bump(@q);
check (@q[0], @q[10], @q.elems), (42, "s", 11), 'a parameter is the same Array';
my @r = 1..10;
my $cl = { @r[0] += 1 };
$cl() for ^3;
check @r[0], 4, 'a closure writes it';

# loops: kernels read, store and step the words, and bail on anything else
my @s = 1 xx 1000;
my $t = 0;
loop (my $u = 0; $u < 1000; $u++) { $t += @s[$u] }
check $t, 1000, 'a summing loop';
my @w = 0 xx 1000;
loop (my $w1 = 0; $w1 < 1000; $w1++) { @w[$w1] = $w1 * 2 }
check (@w[0], @w[999], @w.sum), (0, 1998, 999000), 'a storing loop';
my @hh = 0 xx 16;
loop (my $h1 = 0; $h1 < 1000; $h1++) { @hh[$h1 % 16]++ }
check (@hh[0], @hh[15], @hh.sum), (63, 62, 1000), 'a stepping loop';
my @acc = 0 xx 16;
loop (my $a1 = 0; $a1 < 1000; $a1++) { @acc[$a1 % 16] += $a1 }
check (@acc[0], @acc.sum), (31248, 499500), 'an op= loop';
my @mix = 0 xx 16;
loop (my $m1 = 0; $m1 < 100; $m1++) { @mix[$m1 % 16] = $m1 == 50 ?? "fifty" !! $m1 }
check (@mix[2], @mix[3], @mix[2].WHAT), (98, 99, Int), 'a Str stored mid-loop';
check @mix.grep(Str).elems, 0, '…and overwritten';
my @past = 0 xx 16;
loop (my $p1 = 0; $p1 < 20; $p1++) { @past[$p1] = $p1 }
check (@past.elems, @past[19]), (20, 19), 'a loop that stores past the end';
my @sv = 1 xx 2000;
loop (my $s1 = 2; $s1 * $s1 < 2000; $s1++) {
    if @sv[$s1] { loop (my $s2 = $s1 * $s1; $s2 < 2000; $s2 += $s1) { @sv[$s2] = 0 } }
}
check @sv[2..*].sum, 303, 'a sieve';

# one compiled loop meets the Array as words, as a list and as words again
my @fm = 5, 6, 7, 8, 9, 10, 11, 12;
my $ft = 0;
for ^60 -> $r {
    loop (my $fi = 0; $fi < 8; $fi++) { $ft += @fm[$fi]; @fm[$fi] += 1 }
    @fm = 1..8 if $r == 20;
    @fm.push("x") if $r == 40;
}
check ($ft, @fm[0], @fm[7], @fm[8], @fm.elems), (10440, 40, 47, "x", 9), 'a loop over words, then a list';
my @fp = 1..8;
for ^5 { loop (my $fj = 0; $fj < 3; $fj++) { @fp.push($fj); @fp[$fj]++ } }
check (@fp.elems, @fp[0], @fp[2], @fp[3], @fp[22]), (23, 6, 8, 4, 2), 'a loop that pushes and steps';
my @fu = 1..1000;
my $fs = 0;
for ^6 -> $r {
    loop (my $fk = 0; $fk < 1000; $fk++) { $fs += @fu[$fk] }
    $fs += @fu.join.chars if $r == 2;
}
check $fs, 3005893, 'a loop whose Array unpacks between two entries';

# the Array is one Array through every change of form: its WHICH stays
my @wn = 1e0 xx 4;
my $wn = @wn.WHICH;
@wn = 2e0 xx 10;
check $wn eq @wn.WHICH, True, 'WHICH after an assignment packs Nums';
@wn.push("x");
check $wn eq @wn.WHICH, True, 'WHICH after packed Nums unpack';
my @wi = 1, 2;
my $wi = @wi.WHICH;
@wi = 1..10;
check $wi eq @wi.WHICH, True, 'WHICH after a Range fills the words';
@wi = 3, 4, 5, 6, 7, 8, 9, 10, 11;
check $wi eq @wi.WHICH, True, 'WHICH after a list packs';
my $wj = @wi.join;
@wi.push("x");
loop (my $wk = 0; $wk < 3; $wk++) { @wi[$wk]++ }
check $wi eq @wi.WHICH, True, 'WHICH after an unpack and a loop';
@wi = 1..10;
check $wi eq @wi.WHICH, True, 'WHICH after packing again';
my @wb = 1..10;
my $wc = @wb;
my $wbw = @wb.WHICH;
@wb.push("x");
loop (my $wl = 0; $wl < 3; $wl++) { @wb[$wl]++ }
check (@wb.WHICH eq $wc.WHICH, @wb === $wc, $wbw eq @wb.WHICH), (True, True, True), 'two holders of one Array agree';

# an Array that unpacked is a plain Array again
my @un = 1..100;
my $joined = @un.join(",");
@un.push("x");
loop (my $u1 = 0; $u1 < 100; $u1++) { @un[$u1] += 1 }
check (@un[0], @un[99], @un[100], @un.elems), (2, 101, "x", 101), 'stores after an unpack';

if @fail { .say for @fail; say "FAIL" } else { say "PASS" }
