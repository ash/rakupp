# Regression: a sub named after a QUOTE FORM (`sub s`, `sub q`, `sub tr`) beats
# the quote construct — but only WHERE IT IS IN SCOPE.
#
# The veto used to be unit-wide: one `sub s` anywhere in a file disabled `s///`
# for the whole file, however far away and however deeply nested. Roast's own
# S05-substitution/subst.t is the file that catches it — it declares `sub s` in
# a block near the bottom and uses `s///` as a substitution 350 lines above —
# and it stopped parsing entirely, losing all 191 of its assertions.
#
# Rakudo's rule, measured in both directions: the routine wins from its
# DECLARATION onward, and only inside the block that declares it. Every row
# below is written so that the two readings give different answers — a
# substitution answers a Match, a call answers the string 'CALL' — so neither an
# always-quote nor an always-call implementation can pass the file.
#
# Runs clean under Rakudo too.

# each row compiles a small program of its own, because the reading under test
# is decided when a COMPILATION UNIT is lexed — two of these rows cannot coexist
# in one file by construction
use MONKEY-SEE-NO-EVAL;

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

# ---- a declaration does not reach BACKWARDS ----------------------------
# `s///` written above a unit-level `sub s` is still a substitution
{
    my $out = EVAL q:to/CODE/;
        $_ = 'aaa';
        my $r = s/a/b/;
        sub s { 'CALL' }
        ($r ~~ Match) ?? 'quote' !! 'call'
        CODE
    ck $out, 'quote', 'a use ABOVE a unit-level declaration is still the quote';
}

# ---- …and it does reach forwards --------------------------------------
{
    my $out = EVAL q:to/CODE/;
        sub s { 'CALL' }
        s()
        CODE
    ck $out, 'CALL', 'a declared `sub s` is callable after its declaration';
}

# ---- a declaration does not escape its BLOCK ---------------------------
# this is the shape roast's subst.t has, and the one that broke
{
    my $out = EVAL q:to/CODE/;
        {
            sub s { 'CALL' }
        }
        $_ = 'aaa';
        my $r = s/a/b/;
        ($r ~~ Match) ?? 'quote' !! 'call'
        CODE
    ck $out, 'quote', 'a declaration in a nested block does not escape it';
}

# ---- inside that block, after the declaration, the call wins -----------
{
    my $out = EVAL q:to/CODE/;
        {
            sub s { 'CALL' }
            s()
        }
        CODE
    ck $out, 'CALL', '…while inside the block it does win';
}

# ---- the same for `tr`, which is what the feature was built for --------
{
    my $out = EVAL q:to/CODE/;
        sub tr(&body) { 'ROW:' ~ body() }
        tr { 'cell' }
        CODE
    ck $out, 'ROW:cell', 'a declared `sub tr` takes a block, not a transliteration';
}
{
    my $out = EVAL q:to/CODE/;
        {
            sub tr(&body) { 'ROW' }
        }
        $_ = 'abc';
        tr/a/x/;
        $_
        CODE
    ck $out, 'xbc', '…and out of scope `tr///` transliterates again';
}

# ---- an ADVERB keeps the quote reading even where the sub IS in scope --
# the line the feature deliberately draws, and it must survive the scoping
{
    my $out = EVAL q:to/CODE/;
        sub s { 'CALL' }
        $_ = 'hello';
        s:g/l/L/;
        $_
        CODE
    ck $out, 'heLLo', 'an adverb keeps the quote reading where the sub is in scope';
}

say $fails ?? "\n$fails FAILED" !! "\nPASS";
exit $fails ?? 1 !! 0;
