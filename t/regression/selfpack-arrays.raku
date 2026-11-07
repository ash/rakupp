# An untyped Array of plain Nums packs itself (selfPack, JSC-VALUES-2026-11-07):
# its elements are kept as doubles, read and written as words by the
# interpreter's lanes, its loop kernels and --exe -O's unboxed loop lanes, and
# anything that is not a plain Num, or that needs an element's container,
# unpacks it into the Array it would have been. None of that may change an
# answer: every value below is Rakudo 2026.09's, which RAKUPP_NO_SELFPACK=1
# gives too. RAKUPP_SELFPACK_MIN=1 packs even the short arrays here.
#
# Contract: exit 0 + last line PASS.
my @fail;

sub check($got, $want, $desc) {
    @fail.push("$desc: got {$got.raku}, want {$want.raku}") unless $got eqv $want;
}

# stores that are not a plain Num keep what they store
my @a = (^10).map(* * 0.5e0);
@a[3] = 5;
check (@a[3].WHAT, @a[3]), (Int, 5), 'an Int stored stays an Int';
my @b = 1e0 xx 10;
@b[0] = 1.5;
@b[1] = <2e0>;
@b[2] = "x";
check (@b[0].WHAT, @b[1].WHAT, @b[2]), (Rat, NumStr, "x"), 'a Rat, an allomorph, a Str';
my @c = 1e0 xx 10;
@c[0] = 1e0 but role { method foo { 42 } };
check @c[0].foo, 42, 'a Num with a role mixed in';
my @d = 1e0 xx 10;
@d[0] = Nil;
check @d[0], Any, 'Nil resets an element';

# growth: a store past the end leaves holes, not zeros
my @e = 0e0 xx 10;
@e[12] = 1e0;
check (@e.elems, @e[11], @e[11]:exists, @e[12]), (13, Any, False, 1e0), 'a gap is holes';
my @f = 0e0 xx 10;
@f[10] = 2e0;
@f.push(3e0);
@f.push(4);
check (@f.elems, @f[10], @f[11], @f[12].WHAT), (13, 2e0, 3e0, Int), 'stores and pushes at the end';
check (@f[100], @f[100]:exists), (Any, False), 'a read past the end';

# containers: a bind, an rw parameter, `for` aliasing
my @g = 0e0 xx 10;
my $x := @g[2];
$x = 7e0;
my $y;
@g[1] := $y;
$y = 3;
check (@g[1], @g[2]), (3, 7e0), 'a bound element writes through';
my @h = 1e0 xx 10;
sub set-rw($z is rw) { $z = 6 }
set-rw(@h[1]);
check @h[1].WHAT, Int, 'an rw parameter';
my @i = 0e0 xx 10;
$_ += 1 for @i;
check @i.sum, 10e0, '$_ writes through';
my @j = 0e0 xx 10;
for @j <-> $v { $v = "s" }
check @j.join, "s" x 10, '<-> writes a Str through';

# copies and aliases
my @k = 0e0 xx 10;
my @l = @k;
@l[0] = 9e0;
check (@k[0], @l[0]), (0e0, 9e0), 'a copy is its own';
my @m = 1e0 xx 10;
my @n := @m;
@n[0] = 4e0;
my $r = @m;
$r[1] = "x";
check (@m[0], @m[1]), (4e0, "x"), 'an alias and an item share it';

# what a packed array answers as a whole
my @o = 1e0 xx 10;
check (@o.WHAT, @o.of, @o.elems, @o.end, +@o, ?@o), (Array, Mu, 10, 9, 10, True), 'its type and size';
check (@o.sum, @o.min, @o.max, [+] @o), (10e0, 1e0, 1e0, 10e0), 'its folds';
check @o.raku, '[1e0, 1e0, 1e0, 1e0, 1e0, 1e0, 1e0, 1e0, 1e0, 1e0]', '.raku';
check @o eqv [1e0 xx 10], True, 'eqv';
my @p = -0e0 xx 10;
@p[1] = NaN;
check (atan2(@p[0], -1e0), @p[1].isNaN), (-pi, True), '-0e0 and NaN keep their bits';
my Num @q = 1e0 xx 10;
@q[0] = 2e0;
check (try { @q[1] = 3; 'stored' } // 'refused'), 'refused', 'a typed array still checks';

# loops on the fast paths, and loops that leave them part way
my @u = 1e0 xx 10;
my @v = 0e0 xx 10;
{
    CONTROL { when CX::Warn { .resume } }   # (the read past the end warns)
    for 0..9 -> $i { @v[$i] = @u[$i] + @u[$i + 1] }
}
check @v.List, (|(2e0 xx 9), 1e0), 'a store then a read past the end';
my @w = 0e0 xx 4;
my @rr = 1e0 xx 20;
my $s = 0e0;
{
    CONTROL { when CX::Warn { .resume } }
    for 0..20 -> $i { @w[$i % 4] = @w[$i % 4] + @rr[$i]; $s += @w[$i % 4] }
}
check (@w.List, $s), ((5e0, 5e0, 5e0, 5e0), 65e0), 'more stores than words, then a bail';
my @aa = 1e0 xx 10;
my @bb := @aa;
for ^10 -> $i { @bb[$i] = @aa[$i] + 1e0 }
check @aa.sum, 20e0, 'a loop over two names for one array';
my @cc = 1e0 xx 10;
my $acc = 0e0;
check (try { for ^10 -> $i { $acc += @cc[$i - 1] }; 'ran' } // 'refused'), 'refused', 'a negative index';
my @dd = 1e0 xx 10;
for ^10 -> $i { @dd[$i] = $i }
check (@dd[3].WHAT, @dd.sum), (Int, 45), 'a loop storing Ints';
sub dot(@x, @y) { my $t = 0e0; for ^@x.elems -> $i { $t += @x[$i] * @y[$i] }; $t }
my @e1 = (^10).map(* * 1e0);
my @e2 = (^10).map(* * 2e0);
check dot(@e1, @e2), 570e0, 'a routine\'s loop over its parameters';
my $n = 4;
my @A = (^($n * $n)).map(* * 1e0);
my @B = (^($n * $n)).map(* * 1e0);
my @C = 0e0 xx ($n * $n);
for ^$n -> $i { for ^$n -> $j { my $t = 0e0; for ^$n -> $k { $t += @A[$i * $n + $k] * @B[$k * $n + $j] }; @C[$i * $n + $j] = $t } }
check @C.sum, 3920e0, 'a matrix product';
my @st = 0e0 xx 10;
@st[0] = 1e0;
@st[9] = 1e0;
my @sv = @st;
for ^50 { for 1..8 -> $i { @sv[$i] = 0.5e0 * (@st[$i - 1] + @st[$i + 1]) }; for 1..8 -> $i { @st[$i] = 0.5e0 * (@sv[$i - 1] + @sv[$i + 1]) } }
check (@st.sum * 1e6).round, 9985785, 'a stencil';

if @fail { .say for @fail; say 'FAIL' } else { say 'PASS' }
