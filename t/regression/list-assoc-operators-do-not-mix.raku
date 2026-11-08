# A list-associative operator does not follow a different operator of its
# precedence, as Rakudo refuses it: `1 or 0 xor 1` answered Nil, where Rakudo
# says "Only identical operators may be list associative; since 'or' and 'xor'
# differ, they are non-associative and you need to clarify with parentheses".
# The list-associative ones are `^^`, `min`, `max` (tight or), `andthen`,
# `notandthen` (loose and), `xor`, `orelse` (loose or); a left-associative one
# AFTER one of them is fine (`1 xor 0 or 1`), and so is a chain of one.
# `1 min 2 max 3` had a message of its own; it is Rakudo's now.
# Every line answers the same under Rakudo 2026.09.

use MONKEY-SEE-NO-EVAL;
sub ck($got, $want, $desc) {
    return if $got eqv $want;
    say "FAIL: $desc — got {$got.raku}, want {$want.raku}";
    exit 1;
}
sub refused($code) {
    try EVAL $code;
    $! ?? ($!.^name, $!.message) !! 'ran'
}
sub msg($l, $r) {
    "Only identical operators may be list associative; since '$l' and '$r' differ, " ~
    "they are non-associative and you need to clarify with parentheses"
}

for <or xor>, <or orelse>, <xor orelse>, <|| ^^>, <// ^^>, <and andthen>,
    <and notandthen>, <min max>, <max min> -> ($l, $r) {
    my $r2 = refused("say (1 $l 0 $r 1)");
    ck $r2[0], 'X::Syntax::NonListAssociative', "1 $l 0 $r 1 is refused";
    ck $r2[1], msg($l, $r), "…with Rakudo's message";
}
ck (1 xor 0 or 1), 1, 'xor then or';
ck (1 orelse 0 or 1), 1, 'orelse then or';
ck (1 ^^ 0 || 1), 1, '^^ then ||';
ck (1 || 0 // 1), 1, '|| then //';
ck (1 andthen 0 and 1), 0, 'andthen then and';
ck (1 && 0 and 1), 0, '&& then and';
ck (1 or 0 or 1), 1, 'or chains';
ck (1 xor 0 xor 0), 1, 'xor chains';
ck (1 orelse 0 orelse 1), 1, 'orelse chains';
ck ((1 or 0) xor 1), Nil, 'parenthesized';
ck (1 min 2 min 3), 1, 'min chains';

say 'PASS';
