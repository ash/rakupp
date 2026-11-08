# A package declaration that starts a statement is a term an infix may
# follow on its line, as Rakudo's is: `class A {} === A` was refused,
# "unexpected operator in term position", `class D {} ~~ D` "Expected a
# term", and `class {} ~ "x"` read as `class {}; ~"x"` (Roast's
# `class {} − class {}` only ran because of an exemption in the
# missing-semicolon check, which is gone). A `.method` after it, `but` and
# `does`, and a declaration that ends its line, are as before.
# Every line answers the same under Rakudo 2026.09.

use MONKEY-SEE-NO-EVAL;
sub ck($got, $want, $desc) {
    return if $got eqv $want;
    say "FAIL: $desc — got {$got.raku}, want {$want.raku}";
    exit 1;
}

ck (quietly { class A {} === A }), True, '===';
ck (quietly { class D {} ~~ D }), True, '~~';
ck (quietly { grammar G {} ~~ G }), True, 'a grammar';
ck (quietly { class {} ~ "x" }), 'x', 'an anonymous class';
ck EVAL(q[class P {} === P ?? "yes" !! "no"]), 'yes', 'a looser infix';
ck EVAL(q[class B {}.new.^name]), 'B', '.new after it';
ck EVAL(q[my $r = do { class C {} }; $r.^name]), 'C', 'in a do';
ck EVAL("class F \{ }\n42"), 42, 'a declaration ending its line';
ck EVAL(q[class G2 {} but role { }; 1]), 1, 'but';

say 'PASS';
