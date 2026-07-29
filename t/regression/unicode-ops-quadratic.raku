# A user's quadratic-formula program, dense with Unicode operator forms. Three
# fixes, each with its own witness below:
#
#   * a SYMBOLIC user-defined infix (`sub infix:<±>`) declared fine but died
#     "unexpected operator in term position" at the USE site — the user-infix
#     branch in parseExpr only accepted Tok::Ident, and a symbol arrives as
#     Tok::Op;
#   * `»÷»` lexed as three tokens: isLetterCP classed U+00F7 DIVISION SIGN (and
#     U+00D7 MULTIPLICATION SIGN) as LETTERS — they sit inside the Latin-1 letter
#     block but are Unicode Sm — so the hyper matcher saw a "word list";
#   * the hyper INNER was gathered as raw bytes, so `»÷»` reached the runtime as
#     an operator nothing implements ("Unsupported operator '÷'") — the inner now
#     gets the same Unicode→ASCII aliasing the top-level tokenizer does.
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) { @fail.push("$what: got {$got.raku} want {$want.raku}") unless $got eqv $want }

# a symbolic user infix, with a precedence trait
sub infix:<±>($p, $q) is equiv(&infix:<+>) { $p + $q, $p - $q }
check((5 ± 2).List, (7, 3), 'a symbolic user infix parses at the use site');
check((1 + 2 ± 1).List, (4, 2), 'and is equiv(&infix:<+>) gives it additive precedence');

# a symbolic user PREFIX too
sub prefix:<√>($x) { $x.sqrt }
check(√16, 4e0, 'a symbolic user prefix (sqrt answers a Num)');

# the two Latin-1 math signs are OPERATORS, not letters
check(4 × 3, 12, '× multiplies');
check(8 ÷ 2, 4.0, '÷ divides (to a Rat, like /)');
check((4, 6) »÷» 2, (2.0, 3.0), '»÷» is a hyper, not a word list');
check((4, 6) »×» 2, (8, 12), '»×» too');
check((4, 6) »−» 1, (3, 5), 'and the U+2212 minus as a hyper inner');
check(«a b».join(','), 'a,b', 'a real guillemet word list still lexes as words');

# the whole program, both branches
sub roots(\a, \b, \c) {
    if my $D = b² − 4 × a × c ≥ 0 {
        my @x = (−b ± √$D) »÷» (2 × a);
        "Roots are: {@x.join(', ')}";
    }
    else {
        "There are no real solutions";
    }
}
check(roots(1, -2, 3),  'There are no real solutions', 'negative discriminant');
# NB: 1.5/0.5, not 3/-1 — the program's own `my $D = … ≥ 0` assigns the
# BOOL (`≥` binds tighter than the initializer), so √$D is √True. Rakudo
# prints exactly this too; the test pins agreement with the oracle, not the
# arithmetic the program was presumably meant to do.
check(roots(1, -2, -3), 'Roots are: 1.5, 0.5',          'positive discriminant runs the hyper');

if @fail { note "FAILED:\n" ~ @fail.join("\n"); say 'FAIL' } else { say 'PASS' }
