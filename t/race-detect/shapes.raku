# Which scalar access shapes does the race detector see? An audit tool for
# RACE-DETECTOR-PLAN P2 (not a gate: a shape the hooks miss is a false
# negative, the safe direction). Each write shape runs in two threads at
# once; each read shape runs against a plain `$x = 1` in another thread. A
# shape the detector sees makes the run report "data race on $x" and exit 66.
#
#   rakupp t/race-detect/shapes.raku [path/to/rakupp]
#
# `$x //= 1` on the defined $x writes nothing, so it has no race to see.
sub MAIN(Str $rakupp = $?FILE.IO.parent.parent.parent.add('build/rakupp').Str) {
    my @writes = '$x = 1', '$x++', '++$x', '$x--', '$x += 1', '$x -= 1', '$x *= 2', '$x ~= "a"',
                 '$x //= 1', '$x ||= 1', '$x max= 1', '$x = [1, 2]', '$x = %(a => 1)', '($x, $y) = 1, 2',
                 '$x .= succ', '$x = $x + 1', '$x = "s" ~ $_';
    my @reads = 'my $c = $x', '$y = $x + 1', '$y = $x < 5', '$y = -$x', '$y = "$x"', '$y = f($x)',
                '$y = $x.defined', '$y = 1 if $x', '$y = $x eq "a"', '$y = $x ~ "a"', '$y = [$x]',
                '$y = $x ?? 1 !! 2', '$y = $x == 1', '$y = $x * 2', '$y = $x.Str', '$y = so $x';
    sub probe(Str $a, Str $b) {
        my $prog = 'my $x = 0; my $y = 0; sub f($v) { $v }; '
                 ~ "await (start \{ for ^30 \{ $a \} \}), (start \{ for ^30 \{ $b \} \});";
        my $p = run $rakupp, '-e', $prog, :out, :err, :env(%(|%*ENV, RAKUPP_RACE => 1));
        $p.out.slurp(:close);
        my $err = $p.err.slurp(:close);
        return 'CRASH ' if $p.signal || $p.exitcode > 128;
        $p.exitcode == 66 && $err.contains('data race on $x') ?? 'seen  ' !! 'MISSED'
    }
    my ($seen, $all) = 0, 0;
    for |@writes.map({ ('write', $_, $_) }), |@reads.map({ ('read ', '$x = 1', $_) }) -> ($what, $a, $b) {
        my $verdict = probe($a, $b);
        $all++;
        $seen++ if $verdict eq 'seen  ';
        say "$verdict $what  $b";
    }
    say "$seen of $all shapes seen";
}
