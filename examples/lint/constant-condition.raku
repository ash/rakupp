# Demonstrates: [constant-condition]
# A debugging flag left hard-coded: the `unless` can never take its branch, so
# the cache is always rebuilt.  Run:  rakupp --lint constant-condition.raku

my $cache-warm = False;

unless $cache-warm {           # ordinary: a real condition
    say "warming cache…";
}

if False {                     # <-- constant: the branch never runs
    say "using warm cache";
}
