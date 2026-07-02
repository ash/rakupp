# Naïve recursive Fibonacci — stresses function-call overhead.
sub fib($n) {
    $n < 2 ?? $n !! fib($n - 1) + fib($n - 2);
}
say fib(29);
