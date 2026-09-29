# Dispatch probe kernels — docs/dev/findings/DISPATCH-PROBES.md.
#
#   rakupp tools/dispatch-probe/kernels.raku <kernel> [N]
#
# Prints "<kernel> <ms> <checksum>": the time of the kernel alone (startup and
# parsing excluded), and a checksum a variant binary must reproduce. k1-k3 are
# the shapes tools/dispatch-probe.cpp re-implements, so the engine's number and
# the ladder's rungs describe the same work; k4 and k5 are calls, which the
# ladder does not model.
#
# Each kernel is a sub of its own: a sub body is a pad owner (PADS-PLAN.md),
# and the same loop written inside a `given`/`when` block measured 3.3x slower.
my %default = k1 => 2_000_000, k2 => 2_000_000, k3 => 2_000_000, k4 => 500_000, k5 => 27;

class P { has $.x; method get() { $!x } }
sub fib($m) { $m < 2 ?? $m !! fib($m - 1) + fib($m - 2) }

sub k1($n) {    # loop floor: compare, two adds, two stores
    my $i = 0; my $s = 0;
    while $i < $n { $s = $s + $i; $i = $i + 1 }
    $s
}
sub k2($n) {    # a deeper expression tree per iteration
    my $i = 0; my $x = 7;
    while $i < $n { $x = ($x * 31 + $i) % 1000003; $i = $i + 1 }
    $x
}
sub k3($n) {    # an array read
    my @a = ^1000; my $i = 0; my $s = 0; my $j = 0;
    while $i < $n { $j = $i % 1000; $s = $s + @a[$j]; $i = $i + 1 }
    $s
}
sub k4($n) {    # a method call per iteration
    my $p = P.new(x => 3); my $i = 0; my $s = 0;
    while $i < $n { $s = $s + $p.get; $i = $i + 1 }
    $s
}
sub k5($n) { fib($n) }   # sub calls

my $k = @*ARGS[0] // 'k1';
my $n = (@*ARGS[1] // %default{$k}).Int;
my &kernel = %(:&k1, :&k2, :&k3, :&k4, :&k5){$k};
my $t0 = now;
my $sum = kernel($n);
say "$k {((now - $t0) * 1000).fmt('%.1f')} $sum";
