# Regression: binding an anonymous `&infix:<op>` must not tax, or break, the
# built-in operators it does not spell.
#
# Env::define arms g_lexShadowMask, a 64-slot filter over the operator
# spelling, and a hit sends the operator around evalBinary's fast shape to a
# scope-chain lookup. Two faults rode on it (BinaryHeap 2.0.3, whose role
# parameter is `&infix:<tops>`):
#
#   · the slot hash is (first byte, last byte, length): `tops` shares `>`'s
#     slot and `precedes` shares `*`'s, so every `>` of such a program took
#     the slow path;
#   · the Unicode-twin test (`×` arrives as `*`) admitted `*` `-` `/` `>=`
#     `<=` `!=` whenever ANY bit was armed, so every `*` and `-` did too.
#
# A plain arithmetic loop ran ~4x slower for a `my &infix:<tops>` somewhere in
# the program. Now a filter hit is confirmed against the exact spellings armed,
# and the twin is tested under its own spelling.
#
# The same collision also broke a real shadow: with `*`'s slot armed by
# `precedes`, the lookup tried `&infix:<*>`, found nothing, and never tried the
# twin, so a `my sub infix:<×>` in scope answered the built-in product. (A `×`
# node now asks for `&infix:<×>` by the name it was written with, and a `*`
# node never does: unicode-operator-names-apart.raku.)
#
# Runs clean under Rakudo too.

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

# ---- cost: an unrelated binding leaves the built-ins on their fast path ----
# Measured before anything below arms the filter; the arming is an EVAL so it
# happens at run time, between the two measurements.
sub loop-time() {
    my $best = Inf;
    for ^3 {
        my $t = now;
        my int $p = 0;
        my int $hits = 0;
        for ^150_000 { $p = ($p * 2) + 1; $p = $p - 1 if $p > 1000; $p = 0 if $p > 2000; $hits++ }
        $best = min($best, now - $t);
    }
    $best
}
my $unarmed = loop-time();
EVAL 'my &infix:<tops> = * cmp * == More; my &infix:<precedes> = * cmp * == Less; 1';
my $armed = loop-time();
my $ratio = $armed / $unarmed;
ck $ratio < 2, True, "a `tops`/`precedes` binding leaves `>` `*` `-` fast (ratio {$ratio.fmt('%.2f')})";

# ---- the colliding spellings still mean the built-ins -------------------
proto sub infix:<tops>($, $) {*}
role R[&infix:<tops>] { method t($a, $b) { $a tops $b } }
class Mx does R[* cmp * == More] {}
ck (Mx.t(3, 2), Mx.t(2, 3)), (True, False), 'a role parameter &infix:<tops> is the operator';
ck ((3 > 2), (2 > 3), 6 * 7), (True, False, 42), '…and `>` and `*` beside it are the built-ins';
{
    my &infix:<precedes> = * cmp * == Less;
    ck ((2 precedes 3), 6 * 7, 10 - 4), (True, 42, 6), 'my &infix:<precedes> leaves `*` and `-` alone';
}

# ---- real shadows still shadow ------------------------------------------
{
    my &infix:<+> = -> $a, $b { "plus($a,$b)" };
    ck 1 + 2, 'plus(1,2)', 'my &infix:<+> shadows `+` in its scope';
}
ck 1 + 2, 3, '…and only there';
{
    my &infix:<≥> = -> $a, $b { "ge($a,$b)" };
    ck 2 ≥ 3, 'ge(2,3)', 'my &infix:<≥> shadows `≥`';
}
ck 2 ≥ 3, False, '…and only there';
{
    multi sub infix:<+>(Int $a, Int $b where * > 100) { "big($a,$b)" }
    ck (1 + 200, 1 + 2), ('big(1,200)', 3), 'a narrower user multi infix:<+> still meets the built-in';
}

# ---- `×` finds its shadow with `*` armed in another scope ---------------
{ my &infix:<*> = -> $a, $b { "star($a,$b)" }; ck 6 * 7, 'star(6,7)', 'my &infix:<*> shadows `*`' }
{
    my sub infix:<×>($a, $b) { "times($a,$b)" };
    ck 2 × 3, 'times(2,3)', 'my sub infix:<×> shadows `×` with `*` armed in another scope';
}
ck (2 × 3, 6 * 7), (6, 42), '…and only there';

# ---- past the exact table's capacity a shadow still shadows -------------
for ^70 -> $i { EVAL "my \&infix:<op$i> = -> \$a, \$b \{ $i \}; 1" }
{
    my &infix:<-> = -> $a, $b { "minus($a,$b)" };
    ck 6 - 7, 'minus(6,7)', 'with 70 more spellings armed, my &infix:<-> shadows `-`';
}
ck (6 - 7, 3 > 2), (-1, True), '…and only there';

say $fails ?? "FAIL ($fails)" !! 'PASS';
