# Regression: an operator declared by ASSIGNING to a code variable —
# `my &infix:<plus> = sub ($a, $b) {…}` — is a real operator, exactly as
# `sub infix:<plus>($a, $b) {…}` is.
#
# The operator table is built while the file is PARSED, and only the routine
# spelling ever reached it. The assignment spelling declared an ordinary
# variable, so a later `2 plus 3` was not an expression at all: it parsed as two
# statements, `2` and a call to `plus(3)`, and the line SILENTLY answered its
# left operand. A wrong answer with no error is the worst shape a bug can take,
# and this one had a wide reach — a module that builds its operators inside
# `sub EXPORT` can spell them no other way, which is what every one of the
# natural-language modules (Chinese, French, Japanese, Korean, Spanish) does.
#
# Every row is written so the two readings differ: the operator answers 5 where
# the broken parse answered 2.
#
# Runs clean under Rakudo too.

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

# ---- the four scopes and shapes of the assignment spelling --------------
{
    my &infix:<pl> = sub ($a, $b) { $a + $b };
    ck (2 pl 3), 5, 'my &infix:<op> = sub';
}
{
    my &infix:<pl> = -> $a, $b { $a + $b };
    ck (2 pl 3), 5, '…and a pointy block behind it';
}
{
    our &infix:<pl> = sub ($a, $b) { $a + $b };
    ck (2 pl 3), 5, '…and `our` scope';
}
{
    # a non-ASCII name is the case the language modules actually use
    my &infix:<加> = sub ($a, $b) { $a + $b };
    ck (2 加 3), 5, '…and a non-ASCII operator name';
}

# ---- the other categories ----------------------------------------------
{
    my &prefix:<neg> = sub ($a) { -$a };
    ck (neg 7), -7, 'my &prefix:<op>';
}
{
    my &postfix:<dbl> = sub ($a) { $a * 2 };
    ck (21dbl), 42, 'my &postfix:<op>';
}

# ---- the routine spelling still means the same thing --------------------
{
    sub infix:<pl2>($a, $b) { $a + $b }
    ck (2 pl2 3), 5, 'the `sub infix:<op>` spelling is unchanged';
}

# ---- and an ordinary code variable is still an ordinary code variable ---
# the guard that keeps this from reading every `my &cb` as an operator
{
    my &cb = sub { 9 };
    ck cb(), 9, 'a plain `my &cb` is not an operator';
}

say $fails ?? "\n$fails FAILED" !! "\nPASS";
exit $fails ?? 1 !! 0;
