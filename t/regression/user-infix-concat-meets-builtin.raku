# Regression: a user `multi infix:<~>` over core operands is consulted. `~`
# has an arm of its own in the binary evaluator, which concatenated before
# anything looked, and it was not among the operators userInfixOverCore
# models — so `multi infix:<~>(Str $a, Str $b where "Z")` and a block's
# `multi infix:<~>(Int, Int)` were never called. `+`, `eq` and `x` were.
#
# Every expectation below was checked against Rakudo 2026.09.

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

{
    multi infix:<~>(Str $a, Str $b where "Z") { "[" ~ callwith($a, "z") ~ "]" }
    ck("a" ~ "Z", "[az]", 'a narrower (Str, Str where …) candidate wins');
    ck("a" ~ "b", "ab", 'and leaves the calls it refuses to the built-in');
    my $x = "q";
    ck($x ~ "Z", "[qz]", 'with a variable operand');
}
ck("a" ~ "Z", "aZ", 'outside its block the operator is the core one');

{
    multi infix:<~>(Int $a, Int $b) { "ints" }
    ck(1 ~ 2, "ints", '(Int, Int) is narrower than the core (Any, Any)');
    ck("1" ~ "2", "12", 'and Str operands still concatenate');
}
ck(1 ~ 2, "12", 'and is gone again after the block');

say $fails ?? "FAIL ($fails)" !! "PASS";
exit $fails ?? 1 !! 0;
