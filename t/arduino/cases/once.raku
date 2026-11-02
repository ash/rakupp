# stop: 1000
# No top-level loop: the program runs once, in setup(), and the board stops.
use Arduino;

sub fib(int $n --> int) { $n < 2 ?? $n !! fib($n - 1) + fib($n - 2) }

for 0..10 -> $n { print fib($n), " " }
say "";
my int8 $t = 127;
$t += 1;
say "int8 wraps: $t";
say "done";
