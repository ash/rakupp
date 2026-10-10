# Regression: a `class Q` or `sub q` written INSIDE a ｢…｣ string is text, not a
# declaration. The lexer's textual pre-scan for routines and types that shadow
# a quote word (Lexer::scanDeclaredSubNames) skipped '…' and "…" but not ｢…｣,
# so `Q｢class Q { }｣` declared a Q from there to the end of the file and every
# later `Q｢…｣` died "Undefined routine 'Q'". The type half came with be813251
# (5.3.0: `my role Q[&f]` makes `Q[…]` a parameterization); the sub half is
# older. Found by t/scaling/run.raku, whose shape templates are Q｢…｣ strings.

my $fails = 0;
sub ck($ok, $desc) {
    if $ok { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc" }
}

my $a = Q｢class Q { has $.x }｣;
my $b = Q｢x｣;
ck($a eq 'class Q { has $.x }' && $b eq 'x', 'Q｢…｣ after a ｢class Q｣ string is still a quote');

my $c = Q｢sub q($x) { $x }｣;
my $d = q｢y｣;
ck($d eq 'y', 'q｢…｣ after a ｢sub q｣ string is still a quote');

# the brackets nest: past the inner closer it is still text, so a skip that
# stopped at the first ｣ would have read `grammar Q` as code
my $e = ｢outer ｢x｣ grammar Q { } still text｣;
my $f = Q｢z｣;
ck($e eq 'outer ｢x｣ grammar Q { } still text' && $f eq 'z', 'nested ｢…｣ stays text to its own closer');

say $fails ?? "FAILED $fails" !! "PASS";
