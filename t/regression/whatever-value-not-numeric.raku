# A numeric operator on a Whatever VALUE dies as Rakudo's does, "Cannot
# resolve caller Numeric(Whatever:D: )", where only a written star curries:
# `my $w = *; $w + 1` was 1, `$w == 1` False and `-$w` -0. The string,
# smartmatch and comparison-by-cmp operators still take it.
# Every line answers the same under Rakudo 2026.09.

use MONKEY-SEE-NO-EVAL;
sub ck($got, $want, $desc) {
    return if $got eqv $want;
    say "FAIL: $desc — got {$got.raku}, want {$want.raku}";
    exit 1;
}

my $w = *;
for '+', '-', '*', '/', '%', '**', 'div', '==', '!=', '<', '>=', '<=>', '+&', '%%' -> $op {
    try EVAL "\$w $op 2";
    ck $!.^name, 'X::Multi::NoMatch', "\$w $op 2";
}
try 2 + $w;
ck $!.message.lines[0], 'Cannot resolve caller Numeric(Whatever:D: ); none of these signatures matches:', '2 + $w';
try -$w;
ck $!.^name, 'X::Multi::NoMatch', '-$w';
try +$w;
ck $!.^name, 'X::Multi::NoMatch', '+$w';
try &infix:<+>(*, 42);
ck $!.^name, 'X::Multi::NoMatch', '&infix:<+>(*, 42) (as before)';
ck ($w cmp 2), Less, 'cmp takes it';
ck ($w eqv 2), False, 'eqv';
ck $w ~ 2, '*2', '~';
ck $w x 2, '**', 'x';

# a WRITTEN star still curries
ck (* + 1)(2), 3, '* + 1';
ck (-*)(3), -3, '-*';
ck <1 2>.map(* * 2).List, (2, 4), '.map(* * 2)';
my @a = 1, 2, 3;
ck @a[*-1], 3, '@a[*-1]';

say 'PASS';
