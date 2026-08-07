# Promise combinators across threads: chains of .then, allof, and a kept
# result flowing through — scheduler and continuation plumbing under load.
my $N = (@*ARGS[0] // 6).Int;
my @chains = (^$N).map: -> $i {
    my $p = start { $i };
    $p.then({ .result * 2 }).then({ .result + 1 });
};
my @r = await @chains;
my $want = (^$N).map({ $_ * 2 + 1 }).sum;
my $all = await Promise.allof(@chains).then({ 'joined' });
if @r.sum == $want && $all eq 'joined' {
    say 'PASS';
}
else {
    say "FAIL sum={@r.sum} want=$want all=$all";
    exit 1;
}
