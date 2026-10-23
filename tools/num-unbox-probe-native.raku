# NUM-UNBOX-PLAN.md's probe, step 2's row: tools/num-unbox-probe.raku's
# 1M-element sum over a `my num @a`. It is a file of its own because one
# native array makes `--exe` bundle the whole program (issue 14 of
# findings/FLOAT-HEAVY-ISSUES-2026-10-22.md), which would have moved every
# other row. Run it interpreted and with `--exe -O`:
#   ./build/rakupp tools/num-unbox-probe-native.raku
#   ./build/rakupp --exe -O tools/num-unbox-probe-native.raku -o /tmp/nupn && /tmp/nupn

sub t($name, &f, $per) {
    my $best = 1e300; my $r;
    for ^5 { my $t0 = now; $r = f(); my $d = (now - $t0) / $per * 1e9; $best = $d if $d < $best }
    printf "%-46s %8.2f ns   %s\n", $name, $best, sprintf("%.17g", $r);
}

my num @big = (^1_000_000).map(* * 0.5e0);
sub asum(@a) {
    my $s = 0e0; my $k = @a.elems; my $p = 0;
    while $p < 10 { my $i = 0; while $i < $k { $s = $s + @a[$i]; $i++ }; $p++ }
    $s
}

t 'step 2: 1M-num native array by index, per element', { asum(@big) }, 10_000_000;
