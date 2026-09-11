# Regression: a `/` after a bare routine name (with space) opens a regex.
#
# `test-hi / << b.r >> /, \("<em>", "</em>"), …` — highlighter's suite passes
# a regex as the first argument of a listop call. The lexer knew a fixed set
# of names a regex may follow (say, ok, grep, …) and read every other
# `name /` as a division: "Missing required term after infix". Rakudo's rule
# is that a bareword followed by whitespace and `/` starts a regex; a TIGHT
# `w/2` divides, and so does a `/` after a term-word or a declared constant.
#
# Every expectation was checked against Rakudo.

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

sub test-hi($rx, $c, $d) { ($rx ~~ Regex, $c.list.elems, $d) }
ck(test-hi(/ << b.r >> /, \("<em>", "</em>"), 'x'), (True, 2, 'x'), 'parenthesised call: a regex argument');
ck((test-hi / << b.r >> /, \("<em>", "</em>"), 'y'), (True, 2, 'y'), 'listop call: the slash opens the regex');
ck((test-hi rx:i/ B.R /, \("<em>"), 'z'), (True, 1, 'z'), 'the rx form beside it');
sub w { 10 }
ck((w() / 2).Int, 5, 'a call with parens then divides');
ck((w/2).Int, 5, 'a tight slash after the name divides');
constant HALF = 4;
ck((HALF / 2).Int, 2, 'a constant is a term: the slash divides');
my \third = 9;
ck((third / 3).Int, 3, 'so is a sigilless variable');
ck(pi / pi, 1e0, 'and a term-word like pi');

say $fails ?? "FAIL ($fails)" !! "PASS";
exit $fails ?? 1 !! 0;
