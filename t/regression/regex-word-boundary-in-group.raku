# Regression: a `<<` word boundary followed by a space was read by the
# compile-time regex check as a `< … >` word list, which ran to a later
# `>>` and took the `)` between them, so
# `/ (<< [unit\s+]? [class|role] \s+) (Test) >> /` died "couldn't find
# final ')'" (issue #239).
# Every line answers the same under Rakudo 2026.09.
#
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) {
    @fail.push("$what: got {$got.raku} want {$want.raku}") unless $got eqv $want
}

my $m = "class Test" ~~ / (<< [unit\s+]? [class|role|module|package|grammar] \s+) (Test) >> /;
check ~$m, 'class Test', 'the report';
check ~$m[0], 'class ', 'its capture 0';
check ~$m[1], 'Test', 'its capture 1';
check so("a T" ~~ /(<< a) \s (T) >>/), True, 'a capture opening with <<';
check so("a b" ~~ /<< a >> \s << b >>/), True, 'boundaries around each word';
check so("ab" ~~ /<< a >>/), False, '…and they still bound';
check so("x y" ~~ /< x y >/), True, 'a word list still is one';
check so("a b" ~~ /(<<a) ' ' (b>>)/), True, 'no space after <<';

if @fail {
    .say for @fail;
    say "FAIL";
    exit 1;
}
say "PASS";
