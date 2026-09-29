# Regression: `.pick`/`.roll` on an integer Range of 1,024 or more elements
# drew from the endpoints as written, not the ones the range holds.
#
# Ranges that large are sampled directly rather than flattened, and that path
# ignored `^`: `(^1024).pick` answered 1024 about once in 1,025 draws, and
# `(0^..1024).pick` answered 0. Found by S32-list/pick.t, whose
# `[+|] ((^$v).pick for ^200)` test failed two runs in three at the v5.0.0
# gates — one draw of $v sets a bit the OR must not have.
#
# Contract: exit 0 + last line PASS. Passes under both engines.
my @fail;

for (^1024, ^1025, ^4096, 0..^2048, 0^..1024, 5^..^2000) -> $r {
    my ($lo, $hi) = $r.min + $r.excludes-min, $r.max - $r.excludes-max;
    my @p = $r.pick xx 20_000;
    @fail.push("$r.raku() pick below $lo") if @p.min < $lo;
    @fail.push("$r.raku() pick above $hi") if @p.max > $hi;
    my @r = $r.roll(20_000);
    @fail.push("$r.raku() roll below $lo") if @r.min < $lo;
    @fail.push("$r.raku() roll above $hi") if @r.max > $hi;
}

# pick(*) is a permutation of exactly the elements the range holds
{
    my @all = (^1500).pick(*);
    @fail.push('pick(*) count') unless @all.elems == 1500;
    @fail.push('pick(*) set')   unless @all.sort.List eqv (^1500).List;
}

# the Roast shape: 200 draws of ^2**k OR together to 2**k - 1, never more
for 10, 11, 14 -> $k {
    my $v = 2 ** $k;
    for ^10 {
        my $o = [+|] ((^$v).pick for ^200);
        @fail.push("^{$v} set a bit above {$v - 1}") if $o +& $v;
    }
}

if @fail { note "FAILED: " ~ @fail.unique.join('; '); say 'FAIL' } else { say 'PASS' }
