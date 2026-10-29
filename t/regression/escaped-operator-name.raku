# Regression: a backslash in an operator's name escapes an angle bracket
# (issue #60).
#
# Rakudo reads `sub infix:<(\<&)>` as the operator `(<&)`: inside the angle
# brackets `\<` is `<` and `\>` is `>` (and `\\` is `\`), only an unescaped `>`
# closes the name, and any other backslash stays — `infix:<\o/>` is `\o/`,
# as roast's S06-operator-overloading/sub.t has it. The parser read the
# declaration that way already, but the lexer's scan
# of the file's own declarations (scanUserOps), the scan of a module's
# (scanOpsIn) and the `&infix:<…>` name read up to the first `>` and kept the
# backslash: `1 (<&) 2` lexed `<&) 2 …` as a word list, `infix:<(&\>)>` was
# cut at the escaped `>`, and `&infix:<(\<&)>` was an undefined routine.
# FunctionalParsers spells its pick-left operator `infix:<(\<&)>`. Every
# answer below is Rakudo 2026.09's.

sub infix:<(\<&)>($a, $b) is assoc<left> { "L($a,$b)" }
sub infix:<(&\>)>($a, $b) { "R($a,$b)" }
sub infix:<\<\<\<>($a, $b) { "S($a,$b)" }
sub infix:<\>\>\>>($a, $b) { "G($a,$b)" }
sub prefix:<\<\<>($a) { "P($a)" }
sub infix:<\o/>($a, $b) { $a * $b }   # …but no other backslash is an escape

sub ck($got, $want, $desc) {
    return if $got eqv $want;
    say "FAIL: $desc — got {$got.raku}, want {$want.raku}";
    exit 1;
}

ck (1 (<&) 2), 'L(1,2)', 'an escaped `<`';
ck (1 (&>) 2), 'R(1,2)', 'an escaped `>`, which does not close the name';
ck (1 <<< 2), 'S(1,2)', 'three escaped `<`';
ck (1 >>> 2), 'G(1,2)', 'three escaped `>`';
ck (<<5), 'P(5)', 'a prefix';
ck (21 \o/ 2), 42, '`\o/` keeps its backslash';
ck (1 (<&) 2 (<&) 3), 'L(L(1,2),3)', 'chained, by its associativity';
ck &infix:<(\<&)>(3, 4), 'L(3,4)', 'its code variable, spelled with the escape';
ck &infix:«(<&)»(3, 4), 'L(3,4)', '…and without, in guillemets';
my &f = &[(<&)];
ck f(5, 6), 'L(5,6)', '`&[(<&)]`';

# the angle brackets nearby keep their meanings
ck <a b>.List, ('a', 'b'), 'a word list';
ck (1 < 2 > 0), True, 'a chain of comparisons';

say 'PASS';
