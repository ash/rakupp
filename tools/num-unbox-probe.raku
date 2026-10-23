# NUM-UNBOX-PLAN.md's probe: one row per step's measuring stick, timed in a
# binary built with `--exe -O`. tools/num-unbox-probe.c is the C reference,
# row for row; tools/num-unbox-probe-native.raku holds the `my num @a` row,
# kept apart because one native array bundles a whole program (issue 14 of
# findings/FLOAT-HEAVY-ISSUES-2026-10-22.md).
#   ./build/rakupp --exe -O tools/num-unbox-probe.raku -o /tmp/nup && /tmp/nup
# Each row prints the best of 5, in ns per unit, and the kernel's answer,
# which has to be the C reference's to the last digit.

sub t($name, &f, $per) {
    my $best = 1e300; my $r;
    for ^5 { my $t0 = now; $r = f(); my $d = (now - $t0) / $per * 1e9; $best = $d if $d < $best }
    printf "%-46s %8.2f ns   %s\n", $name, $best, sprintf("%.17g", $r);
}

# control: a loop the F64 lane takes (UNBOX-PLAN P1)
sub scal($x) { my $s = 0e0; my $i = 0; while $i < 10_000_000 { $s = $s + $x * 1.0000001e0; $i++ }; $s }
# control: a closed integer sub, which -O gives an int64 twin (planKernels)
sub fib($n) { $n < 2 ?? $n !! fib($n - 1) + fib($n - 2) }
# step 1: the same on Num, which gets no twin
sub fibf($n) { $n < 2e0 ?? $n !! fibf($n - 1e0) + fibf($n - 2e0) }
# step 1: a leaf Num sub with a local, called from a loop (no lane survives a call)
sub lerp($a, $b, $t) { my $d = $b - $a; $a + $d * $t }
sub lerps($x) { my $s = 0e0; my $i = 0; while $i < 10_000_000 { $s = lerp($s, $x, 1e-7); $i++ }; $s }
# step 3: summing a plain Array of 1M Nums by index, ten times
my @big = (^1_000_000).map(* * 0.5e0);
sub asum(@a) {
    my $s = 0e0; my $k = @a.elems; my $p = 0;
    while $p < 10 { my $i = 0; while $i < $k { $s = $s + @a[$i]; $i++ }; $p++ }
    $s
}
# step 3: issue 21's shape, a 10-element array summed a million times
my @ten = (^10).map(* * 0.25e0);
sub sum10(@a) { my $s = 0e0; my $i = 0; my $k = @a.elems; while $i < $k { $s = $s + @a[$i]; $i++ }; $s }
sub sum10s() { my $s = 0e0; my $i = 0; while $i < 1_000_000 { $s = $s + sum10(@ten); $i++ }; $s }
# step 4: a small array built per call and read back at once
sub mk3($a, $b) { [$a + $b, $a - $b, $a * $b] }
sub mk3s() {
    my $s = 0e0; my $i = 0;
    while $i < 1_000_000 { my $v = mk3($s, 1.5e0); $s = $v[0] * 1e-9 + $v[2] * 1e-12; $i++ }
    $s
}

t 'control: F64 loop lane, per iteration', { scal(1.5e0) }, 10_000_000;
t 'control: fib(27) on Int, per call', { fib(27) }, 635_621;
t 'step 1: fib(27) on Num, per call', { fibf(27e0) }, 635_621;
t 'step 1: a leaf Num sub with a local, per call', { lerps(1.5e0) }, 10_000_000;
t 'step 3: 1M-Num Array by index, per element', { asum(@big) }, 10_000_000;
t 'step 3: 10-Num Array summed, per call', { sum10s() }, 1_000_000;
t 'step 4: 3-element array per call', { mk3s() }, 1_000_000;
