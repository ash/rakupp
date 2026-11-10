# A `**` quantifier's count that no native integer holds is a compile
# error, and so is `**^0`: the count was accumulated without a check, so
# `/a ** 2..99999999999999999999999/` matched ｢aaaa｣ and
# `/a ** 99999999999999999999/` quietly matched nothing, as the wrapped
# number fell; and `**^0` — nothing below zero — was read as `*`. Rakudo
# 2026.09 refuses all three (the first two with an unhandled "Cannot unbox"
# from its compiler, the third as an empty range). Found by the review of
# src/ (docs/dev/findings/src-review-2026-11-09.md, F5). Every other line
# answers the same there.

sub ck($got, $want, $desc) {
    return if $got eqv $want;
    say "FAIL: $desc — got {$got.raku}, want {$want.raku}";
    exit 1;
}

sub compiling(Str $code) {
    EVAL $code;
    return 'compiled';
    CATCH { default { return 'refused' } }
}

ck compiling(Q[my $r = /a ** 2..99999999999999999999999/;]), 'refused', 'a range end past an int64';
ck compiling(Q[my $r = /a ** 99999999999999999999/;]), 'refused', 'a count past an int64';
ck compiling(Q[my $r = /a **^0/;]), 'refused', '**^0';
ck compiling(Q[my $r = /a ** 3..2/;]), 'refused', 'an empty range, as before';

ck ~("aaaa" ~~ /a ** 2..3/), 'aaa', 'a range';
ck ~("aaaa" ~~ /a ** 3/), 'aaa', 'a count';
ck ~("aaaa" ~~ /a ** 2..*/), 'aaaa', 'an open range';
ck ~("aaaa" ~~ /a **? 2..3/), 'aa', 'a frugal one';
ck ~("aaaa" ~~ /a **^3/), 'aa', '**^3';
ck ~("a,a" ~~ /a ** 2 % ','/), 'a,a', 'with a separator';
ck ~("x**^0" ~~ / 'x**^0' /), 'x**^0', 'the same characters in a quoted string are text';
ck ~("**99999999999999999999" ~~ / "**99999999999999999999" /), '**99999999999999999999', '…in double quotes too';

say 'PASS';
