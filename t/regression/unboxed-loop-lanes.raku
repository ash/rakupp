# Regression: the `-O` unboxed loop lanes (docs/dev/plans/UNBOX-PLAN.md).
#
# A lane runs a loop's arithmetic on raw C++ locals — `long long` or `double` —
# guarding the type tags ONCE at entry instead of per statement, and writing the
# values back only where they escape. It is a SECOND implementation of things
# the runtime already does (integer overflow, floored modulo, range bounds,
# comparison), so the failure mode is a wrong answer rather than a slow one, and
# it is invisible unless the compiled and interpreted runs are compared.
#
# Every case here is chosen for a rule the lane has to get right:
#
#   exclusive range markers      `1 ..^ 10` folds the marker into the bound
#   an empty range               `5 .. 1` must run zero times, not once
#   negative bounds              the counter is signed
#   last / next                  break and continue, and `next` must still step
#   a nested lane                an inner loop inside an outer one's lane
#   a named loop variable        `-> $i`, which is not the topic
#   the float lane               `1e0 / 4e0` is a Num; `1.0` would be a Rat
#   integer overflow             leaves the lane and the boxed path finishes it,
#                                so the answer stays exact (this is the one that
#                                silently goes wrong if the bail is mishandled)
#   a header `my`                `loop (my $k = 0; …)` scopes $k to the ENCLOSING
#                                block, so it must be written back and readable
#                                after the loop — it was not, and printed empty
#   mixed lane types             an Int counter beside a Num accumulator, each
#                                keeping its own width
#
# The values are what Rakudo prints for the same program.

my @fail;
sub check($got, $want, $what) { @fail.push("$what: got $got want $want") unless $got eqv $want }

my $a = 0; for 1 .. 10   { $a = $a + $_ }; check $a, 55, 'an inclusive range sums its bounds';
my $b = 0; for 1 ..^ 10  { $b = $b + $_ }; check $b, 45, 'an exclusive upper bound drops the last';
my $c = 0; for 0 ..^ 5   { $c = $c + $_ }; check $c, 10, 'a zero-based exclusive range';
my $d = 0; for 5 .. 1    { $d = $d + 1  }; check $d, 0,  'a reversed range runs zero times';
my $e = 0; for -3 .. 3   { $e = $e + $_ }; check $e, 0,  'negative bounds, signed counter';

my $f = 0; for 1 .. 20 { next if $_ % 2; last if $_ > 14; $f = $f + $_ }
check $f, 56, 'next skips and last stops, inside a lane';   # 2+4+…+14

my $n = 7; my $g = 0; for 1 .. $n { $g = $g + $_ }
check $g, 28, 'a range bound read from a slot';

my $h = 0; for 1 .. 5 -> $i { for 1 .. 4 -> $j { $h = $h + $i * $j } }
check $h, 150, 'a nested lane with named loop variables';

my $p = 0e0; for 1 .. 100 { $p = $p + 1e0 / 4e0 }
check $p, 25e0, 'the float lane divides and accumulates as Num';

my $q = 1; for 1 .. 25 { $q = $q * 7 }
check $q, 1341068619663964900807, 'integer overflow leaves the lane and stays exact';

my $t = 0; loop (my $k = 0; $k < 9; $k++) { $t = $t + $k }
check $t, 36, 'a C-style loop sums';
check $k, 9,  'and its header `my` is still in scope afterwards';

my $u = 0e0; my $v = 0; for 1 .. 50 { $v = $v + 1; $u = $u + $v * 1e0 }
check $u, 1275e0, 'a Num accumulator beside an Int counter';
check $v, 50,     'and the counter keeps its own width';

my $w = 0; my $i = 0; while $i < 1000 { $w = $w + $i * 2; $i = $i + 1 }
check $w, 999000, 'a while loop lane';

# The guard must FAIL and hand back to the boxed path when a slot is not the
# type the lane inferred. `$x` is a Num where an I64 lane was planned.
my $x = 3e0; my $s1 = 0; my $i1 = 0;
while $i1 < 5 { $s1 = $s1 + $x; $i1 = $i1 + 1 }
check $s1, 15e0, 'a slot of the wrong type falls back instead of reinterpreting';

# A Rat gets no lane at all, and must stay exact.
my $r = 0; my $i2 = 0;
while $i2 < 3 { $r = $r + 1.5; $i2 = $i2 + 1 }
check $r, 4.5, 'Rat arithmetic keeps its exactness (2.0 is a Rat, not a Num)';
check $r.WHAT.gist, '(Rat)', 'and its type';

# Overflow AFTER an assignment in the same iteration: the rewind has to undo
# both, or the boxed loop redoes the first one.
my $cnt = 0; my $big = 9223372036854775800;
for 1 .. 5 { $cnt = $cnt + 1; $big = $big + 1000 }
check $cnt, 5, 'the counter beside an overflowing accumulator is not double-counted';
check $big, 9223372036854780800, 'and the accumulator is exact past 2**63';

# Zero-iteration loops of all three kinds.
my $n1 = 0; while 0 { $n1 = 1 }
my $n2 = 0; loop (my $z = 0; $z < 0; $z++) { $n2 = 1 }
my $n3 = 0; for 1 .. 0 { $n3 = 1 }
check "$n1$n2$n3$z", '0000', 'an empty loop of each kind runs zero times';

my $uu = 0; my $i4 = 0; until $i4 >= 6 { $uu = $uu + $i4; $i4 = $i4 + 1 }
check $uu, 15, 'until is laned like while';

my $y = 0;
for 1 .. 6 -> $aa { next if $aa == 2;
  for 1 .. 6 -> $bb { last if $bb == 5;
    for 1 .. 6 -> $cc { next if $cc % 2; $y = $y + $aa * $bb * $cc } } }
check $y, 2280, 'three nested lanes, each with its own last or next';

sub tot($m) { my $s = 0; for 1 .. $m { $s = $s + $_ }; $s }
check (1 .. 5).map({ tot($_) }).join(','), '1,3,6,10,15',
      'a lane inside a routine, over a read-only parameter, called repeatedly';

my $ro = 41; my $acc = 0; my $i5 = 0;
while $i5 < 3 { $acc = $acc + $ro; $i5 = $i5 + 1 }
check $ro, 41, 'a slot the loop only reads is not written back';
check $acc, 123, 'and reading it works';

# An `is rw` parameter is a reference into the caller's slot, so a lane writing
# it has to write THROUGH. Nested `$_` shadows correctly, and a lane inside a
# `for` over an array must refuse rather than read that for's topic.
sub bump($n is rw) { for 1 .. 5 { $n = $n + 2 } }
my $rw = 1; bump($rw);
check $rw, 11, 'a lane writes through an `is rw` parameter';

my $nest = 0; for 1 .. 3 { for 1 .. 3 { $nest = $nest + $_ } }
check $nest, 18, 'a nested implicit $_ shadows the outer one';

my @xs = 10, 20, 30; my $outer = 0;
for @xs { my $q = 0; while $q < 2 { $outer = $outer + $_; $q = $q + 1 } }
check $outer, 120, 'a lane inside a `for` over an array leaves that topic alone';

my @cl; my $cs = 0; for 1 .. 3 -> $kk { @cl.push({ $kk }); $cs = $cs + $kk }
check $cs, 6, 'a loop whose variable is captured by a closure still computes';
check @cl.map({ .() }).join(','), '1,2,3', 'and each closure kept its own value';

# A float in BOOLEAN context is true when it is not zero. Asking the lane for it
# as an integer instead truncates, and `(long long)0.25` is 0 — so this loop ran
# zero times compiled and ten times interpreted until uCond existed.
my $ff = 25e-2; my $gg = 0;
while $ff { $gg = $gg + 1; $ff = $ff - 1e-1; last if $gg > 9 }
check $gg, 10, 'a bare Num is a truth value, not something to truncate';

my $hh = 0; my $i7 = 0; my $half = 5e-1;
while $i7 < 3 { if $half { $hh = $hh + 1 }; $i7 = $i7 + 1 }
check $hh, 3, 'and so is one under an `if`';

my $ii = 0; my $i8 = 0; my $zero = 0e0;
while $i8 < 3 { if !$zero { $ii = $ii + 1 }; $i8 = $i8 + 1 }
check $ii, 3, 'and under a negated one';

# A NESTED loop's header `my` escapes into the enclosing BODY, which is still
# inside the lane — so it is a lane local, not something to write back. Writing
# it back named a C++ variable that no longer existed, because the declaration
# that would have made one belongs to an emission the lane replaced, and the
# whole program failed to compile.
my $nn = 0;
loop (my $oa = 0; $oa < 4; $oa++) { loop (my $ob = 0; $ob < 4; $ob++) { $nn = $nn + $ob } }
check $nn, 24, 'a nested C-style loop inside a lane';
check $oa, 4,  'the replaced loop\'s header `my` survives it';

my $mm = 0;
for 0 .. 3 -> $p1 { loop (my $p2 = 0; $p2 < 3; $p2++) { $mm = $mm + $p1 * $p2 } }
check $mm, 18, 'a C-style loop nested inside a `for` lane';

my $ww = 0;
loop (my $w1 = 0; $w1 < 3; $w1++) { my $w2 = 0; while $w2 < 3 { $ww = $ww + 1; $w2 = $w2 + 1 } }
check $ww, 9, 'a while nested inside a C-style lane';

if @fail { die "FAIL:\n" ~ @fail.join("\n") }
say 'PASS';
