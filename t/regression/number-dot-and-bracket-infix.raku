# Regression: two parse rules added for roast S02-lexical-conventions
# (minimal-whitespace.t, one-pass-parsing.t) each over-fired on their first
# draft, and the full-Roast gate caught both.
#
#  1. "Decimal point must be followed by digit" fires on a dot GLUED to an
#     integer literal and NOT followed by a name. The first draft read the
#     source byte after the dot through the token's `off`, which drifts on some
#     file parses: in S02-names-vars/perl.t it read a space out of a later
#     string literal and rejected `780.chr`. Five roast files went with it.
#     It now reads the next TOKEN.
#  2. "Missing infix inside []" fires where a `[` in infix position does not
#     spell an operator. The first draft raised it in parseExpr where the
#     bracketed-infix reader gives up — but that spot backtracks ON PURPOSE, so
#     the same `[` can be retried at a looser precedence, which is the only way
#     `2 [&f] 3 [&f] 4` and `1,2 [Z*] 3,4` parse. It now rides on the
#     statement separator check, which only speaks where a statement is over.
#
# Every expectation was checked against Rakudo 2026.08.

use MONKEY-SEE-NO-EVAL;   # the error cases go through EVAL; Rakudo wants the pragma

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}
sub dies-with($code, $type, $desc) {
    my $ex;
    { EVAL $code; CATCH { default { $ex = $_ } } }
    if $ex && $ex ~~ ::($type) { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — got {$ex ?? $ex.^name !! 'no exception'}, wanted $type" }
}

# 1. a dot glued to an integer, followed by a NAME, is a method call
ck(780.chr, "\c[COMBINING CARON]", 'a method on an integer literal');
ck(3.first, 3, '.first on an integer literal');
ck(4.Num, 4e0, '.Num on an integer literal');
ck(0x20.base(16), '20', 'a method on a hex literal');
ck(88.EVAL, 88, '.EVAL on an integer literal');
ck((1, 2, "Hello", 3/4, 4.Num).elems, 5, 'inside a list, beside a Rat');
ck(1.5.abs, 1.5, 'a rational literal already spent its dot');
ck(1e3.Int, 1000, 'a Num literal too');
ck((1..5).elems, 5, 'the range operator is not a decimal point');
ck((1...5).elems, 5, 'nor the sequence operator');

# …and the rule still fires where it should
dies-with('42. abs', 'X::Syntax::Number::IllegalDecimal', 'a space after the dot');
dies-with('42.,', 'X::Comp::Group', 'a comma after the dot');
dies-with('42.:all', 'X::Syntax::Number::IllegalDecimal', 'a colonpair after the dot');

# 2. bracketed infixes that need the backtrack-and-retry at a looser precedence
sub foo ($a, $b) { $a * $b }
ck((2 [&foo] 3 [&foo] 4), 24, 'a callable infix, twice in one expression');
ck((1,2 [Z*] 3,4).List, (3, 8), 'a metaop spelled inside the brackets');
ck((<a b> Z[~] <1 2>).List, ('a1', 'b2'), 'Z over a bracketed infix');
ck(([+] 1, 2, 3), 6, 'the prefix reduce still reduces');
ck((1 [+] 2), 3, 'a plain bracketed infix');

# …and brackets that spell nothing are named
dies-with('my @a = 1,2; @a [0]', 'X::Syntax::Missing', 'a subscript-looking bracket in infix position');

# 3. a statement-modifier keyword needs the space after it
dies-with('say "OK" if+1', 'X::Comp::AdHoc', 'no space after the keyword');
ck(do { my $n = 0; $n++ if 1; $n }, 1, 'the spaced modifier still modifies');

say $fails ?? "FAIL ($fails)" !! "PASS";
exit $fails ?? 1 !! 0;
