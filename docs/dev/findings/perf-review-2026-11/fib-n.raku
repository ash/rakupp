sub fib(int $n --> int) { $n < 2 ?? $n !! fib($n - 1) + fib($n - 2) }
say fib(27);
