# Pure fan-out: independent CPU work per start block, joined with await —
# the shape parallel mode exists for. No sharing at all.
my $N = (@*ARGS[0] // 8).Int;
sub work($n) {
    my $s = 0;
    $s += $_ * $_ % 7 for 1..20000;
    $s + $n
}
my @r = await (^$N).map: -> $i { start work($i) };
my $base = do { my $s = 0; $s += $_ * $_ % 7 for 1..20000; $s };
my $want = $N * $base + ($N * ($N - 1)) div 2;
if @r.sum == $want {
    say 'PASS';
}
else {
    say "FAIL sum={@r.sum} want=$want";
    exit 1;
}
