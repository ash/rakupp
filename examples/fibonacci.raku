#!/usr/bin/env raku
# Three ways to reach the Fibonacci numbers, and a check that they agree.
# Shows off lazy self-referential sequences, a fast-doubling recursion that
# reaches deep into the sequence in a handful of steps, and Raku's
# arbitrary-precision integers (the 500th Fibonacci number has 105 digits).

# (a) The whole sequence as a lazy, self-referential list: each term is the
# sum of the previous two, and `... *` lets it run forever without computing
# more than you ask for.
my @fib = 1, 1, * + * ... *;

say 'The lazy sequence, first 16 terms:';
say @fib[^16];
say '';

# (b) Fast doubling: from (F(n), F(n+1)) you can jump straight to
# (F(2n), F(2n+1)), so F(n) costs only about log2(n) steps of big-integer
# arithmetic instead of n. Returns the pair (F(n), F(n+1)).
sub fib-pair($n) {
    return (0, 1) if $n == 0;
    my ($a, $b) = fib-pair($n div 2);
    my $c = $a * (2 * $b - $a);       # F(2k)   = F(k) * (2 F(k+1) - F(k))
    my $d = $a * $a + $b * $b;        # F(2k+1) = F(k)^2 + F(k+1)^2
    $n %% 2 ?? ($c, $d) !! ($d, $c + $d);
}
sub fib($n) { fib-pair($n)[0] }

# (c) The two methods must agree. @fib is 1-indexed against the usual F(n)
# (F(1) = F(2) = 1), so @fib[n-1] is F(n).
say 'Lazy list and fast doubling agree for F(1)..F(30):';
my $agree = True;
for 1 .. 30 -> $n {
    $agree = False unless @fib[$n - 1] == fib($n);
}
say "  ", $agree ?? 'all 30 match' !! 'MISMATCH';
say '';

# Arbitrary precision: no 64-bit ceiling, so these print in full.
say 'Big Fibonacci numbers, in full:';
for 100, 250, 500 -> $n {
    my $f = fib($n);
    say "  F($n) has {$f.Str.chars} digits: $f";
}
say '';

# Consecutive ratios F(n+1)/F(n) close in on the golden ratio.
say 'Ratios F(n+1)/F(n) approach the golden ratio phi:';
my $phi = (1 + 5.sqrt) / 2;
for 5, 10, 20, 40 -> $n {
    my $ratio = fib($n + 1) / fib($n);
    say sprintf('  F(%d+1)/F(%d) = %.12f   (phi - ratio = %.2e)',
                $n, $n, $ratio, $phi - $ratio);
}
say sprintf('  phi        = %.12f', $phi);
