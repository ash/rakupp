# Parameters infer too: `f(x)` makes f a &-parameter.
sub fib(n) { n < 2 ?? n !! fib(n - 1) + fib(n - 2) }
say fib(20);
sub apply(f, x) { f(x) }
say apply(-> v { v * 3 }, 14);
my squares = (1..5).map(-> k { k * k });
say squares.sum, ' ', squares;
sub total(*nums) { nums.sum }
say total(1, 2, 3, 4);
