# Tight integer accumulation. Without `-O` every `+`, `*`, `-` goes through the
# string-keyed runtime dispatcher (applyArith); with `-O` they inline to native
# int64 arithmetic (overflow still promotes to bignum).
my $sum = 0;
for 1 .. 5_000_000 { $sum += $_ * 2 - 1; }
say $sum;
