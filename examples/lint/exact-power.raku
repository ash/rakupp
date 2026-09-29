# Demonstrates: [exact-power]
# `1/$n` is an exact Rat, so `(1 + 1/$n) ** $n` builds (n+1)**n and n**n as
# whole integers before the result, whose denominator no longer fits 64 bits,
# becomes a Num. Past n = 10**5 that takes seconds, then minutes.
# `1e0` makes it Num arithmetic from the start: instant, and approximate.
# Run:  rakupp --lint exact-power.raku
#       rakupp --hints exact-power.raku    (says it again at run time)

for ^5 -> $p {
    my $n = 10 ** $p;
    my $e = (1 + 1/$n) ** $n;          # <-- 1e0 instead of 1, if a Num is what you want
    say "$n\t$e";
}
