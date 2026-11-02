# K5: call-heavy recursion
sub fib($n) { $n < 2 ?? $n !! fib($n - 1) + fib($n - 2) }
say fib(27);
