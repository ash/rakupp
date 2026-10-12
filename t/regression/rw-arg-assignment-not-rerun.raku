# Regression: an `is rw` parameter whose argument is an ASSIGNMENT,
# `f($pos = $pos + 1)` (2026-10-12).
#
# The callee's writes go back through the argument expression, and the
# write-back target was found by lvalue() of that expression — which, for an
# Assign, RUNS the assignment again. So every write re-evaluated the caller's
# `$pos = $pos + 1` (side effects and all), and that re-run wrote through the
# CALLER's own rw link, and so on to the root: a write at depth d cost d frames.
# JSON::Fast threads `int $pos is rw` down `parse-array($text, $pos = $pos + 1)`;
# its t/01-parse.t feeds 10_000-deep input, which took minutes, so
# `rakupp install JSON::Fast` (and JSON::Native on top of it) looked hung.
# Fixed by peeling the assignment to its target (peelIncDec), as for `++$pos`.
#
# Expectations checked against Rakudo 2026.08 via /opt/local/bin/raku. Green on both.

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

# --- the assignment runs once, when the argument is evaluated ---------------
{
    my $calls = 0;
    sub one() { $calls++; 1 }
    sub w($p is rw) { $p = $p + 10; $p = $p + 100 }
    my $x = 0;
    w($x = $x + one());
    ck($x, 111, 'the callee writes land in the assigned variable');
    ck($calls, 1, '…and the argument assignment is not re-run per write');
}
{
    sub bump($v is rw) { $v++ }
    my $y = 5; bump($y += 2);
    ck($y, 8, 'an op= argument hands its target container through');
}

# --- the JSON::Fast shape: a recursive descent on a cursor ------------------
{
    my $calls = 0;
    sub step() { $calls++; 1 }
    sub descend(Int $d, $p is rw) {
        $p = $p + 1;
        descend($d - 1, $p = $p + step()) if $d;
        $p = $p + 1;
    }
    my $pos = 0;
    descend(2000, $pos);
    ck($pos, 2000 + 2 * 2001, 'a 2000-deep recursion keeps the cursor exact');
    ck($calls, 2000, '…and each level evaluates its argument once');
}

say $fails ?? "FAILED $fails" !! "PASS";
exit($fails ?? 1 !! 0);
