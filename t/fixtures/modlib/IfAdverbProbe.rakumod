# Fixture for t/regression/dynamic-var-caller-chain.raku — the `use X:if(COND)`
# adverb (ecosystem `if` dist, honored natively by rakupp). EXPORT runs once
# per use-statement and bumps the CALLER's dynamic, exactly like the real
# dist's own test fixture (t/lib/Bar.rakumod in if 0.1.5).
sub EXPORT(|) {
    $*IF-PROBE-LOADED++;
    BEGIN Map.new
}
