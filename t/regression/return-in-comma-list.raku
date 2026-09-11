# Regression: `return` as an element of a comma list / argument list.
#
# `nqp::if(cond, (return 'early'), return '')` — paths' directory walker
# returns from the middle of an nqp::stmts list. After a comma the parser
# asked "does a term start here?", and `return` was not one: "expected )
# (got 'return')", or across a line break "Two terms in a row across lines".
# Control flow is a term in expression position, as `… or return X` already
# knew.
#
# Every expectation was checked against Rakudo.

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

sub g(*@a) { @a.elems }
sub f1() { g(1, return 'r'); 'end' }
ck(f1(), 'r', 'return as the last argument returns');
sub f2() {
    g(1,
      return 'r2'   # with a comment, across lines
    );
    'end'
}
ck(f2(), 'r2', 'the same across a line break');
sub f3() { my $x = (1, (return 'early'), return 'late'); 'end' }
ck(f3(), 'early', 'the FIRST return in the list wins');
sub f4($n) { g(1, $n > 0 ?? 2 !! return 'neg'); 'end' }
ck(f4(1), 'end', 'not taken: the call completes');
ck(f4(-1), 'neg', 'taken from inside a ternary in the list');

say $fails ?? "FAIL ($fails)" !! "PASS";
exit $fails ?? 1 !! 0;
