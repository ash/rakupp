# Regression: a METHOD name is never a listop, so a `<` after it compares.
# The lexer knows the identifiers a term follows (`all <a b>` takes a word
# list, `any /x/` a regex) and applied that to `.all` and `.any` too — so
#
#     (1, 2).all < 5
#
# opened a `<…>` word list that ran to the next `>` or, failing one, died on
# the next quote character with "unexpected operator in term position".
# `.sum <` was fine, because `sum` is not in that table. Crypt::Random's
# range checks (`crypt_random().all < 2**32`) read exactly this way.
#
# Every expectation below was checked against Rakudo.

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

ck(so((1, 2).all < 5),      True,  '.all < compares the junction');
ck(so((1, 9).all < 5),      False, '…and can be false');
ck(so((1, 9).any < 5),      True,  '.any < likewise');
ck(so((1, 9).one < 5),      True,  '.one <');
ck(so((7, 9).none < 5),     True,  '.none <');
ck(so((1, 2).all <5),       True,  'with no space before the number either');
my @w = (1, 2).all < 5, "after";
ck(@w.elems, 2, 'the comparison ends where the comma is');
ck(@w[1], "after", 'and the next element is the string, not part of a word list');
ck(so((1, 2).all <= 2),     True,  '.all <= too');
ck(so((<a b>).all ~~ Str),  True,  'a junction over words still smartmatches');

# the listop spellings keep their word lists
ck(so("a" ~~ all <a b>),    False, 'all <a b> is still a word list');
ck(so("x" ~~ any <x y>),    True,  'any <x y> too');
my $set = set <m l c>;
ck($set.elems,              3,     'set <m l c> is still three words');

say $fails ?? "FAIL ($fails)" !! "PASS";
exit $fails ?? 1 !! 0;
