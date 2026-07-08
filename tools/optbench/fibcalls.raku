# Naïve recursive Fibonacci — a tiny body called ~7M times. `-O` gives the sub a
# direct-`Value` overload (no per-call ValueList heap allocation) and inlines the
# `<`, `-`, `+` operators, so the whole call collapses to tight native recursion.
sub fib($n) {
    $n < 2 ?? $n !! fib($n - 1) + fib($n - 2);
}
say fib(32);
