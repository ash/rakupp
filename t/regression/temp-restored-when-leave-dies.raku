# A `temp` or `let` of an enclosing block is restored when an inner block's
# LEAVE dies: the inner block left by the LEAVE's exception without closing
# its scope, and the enclosing block then drained its restores against that
# scope, where there were none — `my $x = 1; try { { temp $x = 2;
# { LEAVE { die "L" }; die "B" } } }; say $x` said 2. Found by the review of
# src/ (docs/dev/findings/src-review-2026-11-09.md, A10). Every line answers
# the same under Rakudo 2026.09.

sub ck($got, $want, $desc) {
    return if $got eqv $want;
    say "FAIL: $desc — got {$got.raku}, want {$want.raku}";
    exit 1;
}

{
    my $x = 1;
    try { { temp $x = 2; { LEAVE { die "L" }; die "B" } } }
    ck $x, 1, 'temp, the inner block dying and its LEAVE dying too';
}
{
    my $x = 1;
    try { { temp $x = 2; { LEAVE { die "L" }; 5 } } }
    ck $x, 1, 'temp, the LEAVE alone dying';
    ck $!.message, 'L', '…and its error is the one raised';
}
{
    my $x = 1; my $y = 1;
    try { { let $y = 2; temp $x = 2; { LEAVE { die "L" }; die "B" } } }
    ck "$x $y", '1 1', 'let beside temp';
}
{
    my $x = 1; my $left = 0;
    try { { temp $x = 2; { LEAVE { $left++ }; die "B" } } }
    ck $x, 1, 'a LEAVE that does not die, as before';
}
{
    sub f { my $x = 1; { LEAVE { $x = 9 }; die "B" }; CATCH { default { return $x } } }
    ck f(), 1, 'a CATCH sees the variable as the dying block left it';
}

say 'PASS';
