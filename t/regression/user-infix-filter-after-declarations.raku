# Regression: a `multi infix:<+>` over the program's own class no longer
# slows every Int `+` in the program, and a candidate over a type declared
# after it is still decided by what that type turns out to be (2026-11-05,
# SPEED-BACKLOG 53).
#
# A candidate is noted when its sub is hoisted, above the `class Pt` or the
# `subset P of Int` its parameters name, so an undeclared name could be
# either: it armed the filter that sends a core-operand `+` through
# userInfixOverCore, and a `multi infix:<+>(Pt, Pt)` cost every Int `+` 3.8×
# (a `$t + $_` loop 8.2G -> 31.4G instructions, `[+]` 2.3×). The operator is
# armed provisionally now and decided again as each class and subset is
# declared: a class lets it go, a subset of a core type keeps it. These
# checks are the answers, which must not move; the instruction counts are in
# the commit.
#
# Expectations checked against Rakudo 2026.09
# (~/.rakubrew/versions/moar-2026.09/install/bin/rakudo). Green on both.

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

# a class and its candidate (the candidate is hoisted above the class when
# the program runs): the class takes it, an Int pair the built-in
class Pt { has $.x }
multi infix:<+>(Pt $a, Pt $b) { Pt.new(x => $a.x + $b.x) }
ck((Pt.new(x => 1) + Pt.new(x => 2)).x, 3, 'a class pair takes the candidate');
ck(([+] Pt.new(x => 1), Pt.new(x => 2), Pt.new(x => 3)).x, 6, '…in `[+]` too');
ck(2 + 3, 5, 'an Int pair is the built-in');
ck(([+] 1, 2, 3), 6, '…in `[+]` too');
ck(((1, 2) Z+ (3, 4)).List, (4, 6), '…and in `Z+`');

# a SUBSET of Int, which the hoisted candidate meets before it is declared,
# keeps the operator armed
subset Big of Int where * > 100;
multi infix:<*>(Big $a, Big $b) { "big-times" }
ck(200 * 300, "big-times", 'a subset of Int and its candidate: the candidate');
ck(2 * 3, 6, '…and the built-in where the subset does not hold');
ck(([*] 200, 300), "big-times", '…in `[*]` too');

# one candidate over a class, one over a later subset: the operator stays armed
class Qt { }
subset Small of Int where * < 0;
multi infix:<->(Qt $a, Qt $b) { "qt-minus" }
multi infix:<->(Small $a, Small $b) { "small-minus" }
ck(Qt.new - Qt.new, "qt-minus", 'a class candidate beside a subset one');
ck(-5 - -7, "small-minus", '…and the subset candidate still takes its Ints');
ck(5 - 7, -2, '…and the built-in the rest');

# a candidate with one core parameter and one class
class Rt { }
multi infix:<**>(Rt $a, Int $b) { "rt-pow" }
ck(Rt.new ** 2, "rt-pow", 'a (class, Int) candidate takes its operands');
ck(2 ** 3, 8, 'two Ints are the built-in `**`');

# a lexical class inside a block, with its candidate
{
    my class Lt { }
    multi infix:<%>(Lt $a, Lt $b) { "lt-mod" }
    ck(Lt.new % Lt.new, "lt-mod", 'a `my class` and its candidate in a block');
    ck(7 % 3, 1, '…beside an Int `%`');
}

# an anonymous binding still shadows the operator outright
{
    my &infix:<+> = -> $a, $b { "lex-plus" };
    ck(1 + 2, "lex-plus", 'an anonymous `&infix:<+>` binding');
}
ck(1 + 2, 3, '…and only in its block');

say $fails ?? "FAIL ($fails)" !! 'PASS';
exit $fails ?? 1 !! 0;
