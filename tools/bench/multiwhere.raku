# A `where`-constrained multi candidate, called 400_000 times.
#
# The cross-engine twin of perf-guard's `multiwhere` kernel. Deliberately the
# same shape as a plain multi-method call plus ONE constraint, so the difference
# from a constraint-free multi is the constraint and nothing else. Every other
# kernel here is a loop, a string, a container or a regex; none of them dispatch
# on a constrained signature, which is where UInt and most of the ecosystem's
# validated types are actually checked.
class K {
    proto method m(|) {*}
    multi method m(Int $x where * > 0) { $x }
    multi method m(Int $x)             { 0 }
}
my $k = K.new;
my $t = 0;
my int $n = 0;
while $n < 400_000 { $t = $t + $k.m($n % 3); $n = $n + 1 }
say $t;
