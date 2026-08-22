# A monomorphic method call in a hot loop — one class, one receiver type, one
# candidate, called a million times. Pass 1 gives plain SUBS a direct-arity
# overload; method calls still go through the dispatcher on every iteration,
# because nothing proves the receiver's type at compile time.
#
# "devirtualizing monomorphic method calls" is named in OPTIMIZATION.md's
# "Limits and what's next". This is its measuring stick, and the OO counterpart
# to fibcalls: same call volume, dispatched instead of direct.
class Counter {
    has $.n is rw;
    method bump($d) { $!n = $!n + $d; $!n % 1000 }
}
my $c   = Counter.new(n => 0);
my $acc = 0;
my $i   = 0;
while $i < 1_000_000 {
    $acc = $acc + $c.bump(1);
    $i = $i + 1;
}
say $acc, " ", $c.n;
