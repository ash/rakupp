# Regression: Test::Assertion supplies `is test-assertion` only to a Raku that
# lacks it, deciding by counting the core `trait_mod:<is>` candidates that
# take a Routine and `:$test-assertion!`. rakupp's list of core candidates did
# not have that one, so the module exported its own as well, and its suite —
# which checks it added no needless candidate — failed. Test::Assertion is a
# dependency of Cro::HTTP::Test, which Cro::RPC::JSON's suite needs.
#
# Expectations checked against Rakudo 2026.09
# (~/.rakubrew/versions/moar-2026.09/install/bin/rakudo). Green on both.

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

my $n = &trait_mod:<is>.candidates.grep({
    .[0].type ~~ Routine && .[1].name eq q/$test-assertion/ given .signature.params
}).elems;
ck $n, 1, 'the core has a `trait_mod:<is>(Routine, :$test-assertion!)` candidate';

use Test;
sub helper() is test-assertion { 42 }
ck helper(), 42, '…and `is test-assertion` still applies';

say $fails ?? "FAIL ($fails)" !! 'PASS';
exit $fails ?? 1 !! 0;
