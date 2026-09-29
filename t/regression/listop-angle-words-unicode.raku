# A `<…>` word list straight after a listop name — `c <a−b>` — is lexed as
# code, because there `<` could still be less-than; the parser reads the words
# back from the tokens once it knows. The lexer had already rewritten every
# operator spelled in Unicode to its ASCII twin, so the words came back
# rewritten: `c <a−b>` passed "a-b", `c <×>` passed "*", `c <−5>` an IntStr
# whose string was "-5", `c «a−b c»` the same. And a numeric word takes
# U+2212 as a minus anywhere a sign can stand (`<5−1i>`, `<1e−5>`), not only
# in front. The lexer now keeps an
# operator token's source spelling, and the word reader uses it.
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) { @fail.push("$what: got {$got.raku} want {$want.raku}") unless $got eqv $want }

sub c(*@a) { @a.List }

check (c <a−b>), ("a−b",), 'U+2212 inside a word';
check (c <×>), ("×",), '× alone';
check (c <a×b>), ("a×b",), '× inside a word';
check (c <÷ − ≤ ≥ ≠ … ∅>), <÷ − ≤ ≥ ≠ … ∅>, 'every aliased operator';
check (c <−5>)[0].raku, 'IntStr.new(-5, "−5")', 'U+2212 minus on a number';
check (c <−1 +2 −3.5>).map(*.Str).List, ("−1", "+2", "−3.5"), 'several signed numbers';
check (c <a −b>), ("a", "−b"), 'a word starting with U+2212';
check (c «a−b c»), ("a−b", "c"), 'the guillemet form';
check (c <<x×y>>), ("x×y",), 'the double-angle form';
check (c <a b> × 2), (4,), '× after the list is still the operator';

# …and U+2212 is a minus wherever a sign stands in a numeric word, which is
# what Roast's `is-deeply <5−1i>, 5-1i` needs once the listop keeps it
check (c <5−1i>)[0], 5-1i, 'listop: Complex with U+2212';
my ($cx, $cy, $r, $e) = <5−1i>, <−5−1i>, <−1/2>, <1e−5>;
check ($cx, $cy), (5-1i, -5-1i), 'Complex with U+2212';
check $r, -0.5, 'Rat with U+2212';
check $e.Num, 1e-5, 'exponent with U+2212';
check (<5−>, <1−2>).map(*.^name).List, <Str Str>, 'no number, no allomorph';

# the operators themselves are untouched
check (5 − 3, 2 × 3, 1 ≤ 2, 1 ≠ 2), (2, 6, True, True), 'Unicode operators';
my $x = 5; $x −= 2;
check $x, 3, '−=';
check (1…3).List, (1, 2, 3), '…';

if @fail { .say for @fail; say "FAIL" } else { say "PASS" }
