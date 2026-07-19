# Demonstrates: [unreachable-code]
# The guard `return` fires unconditionally, so the logging line beneath it can
# never run — the `if`/`else` were meant to be the other way round.
# Run:  rakupp --lint unreachable-code.raku

sub classify($n) {
    return "non-positive" if $n <= 0;
    return "small" if $n < 10;
    return "large";
    note "classified $n";        # <-- unreachable
}

say classify(3);
say classify(42);
