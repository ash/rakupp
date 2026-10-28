# N workers READ one shared hash at once — `.pairs`, `for %h`, `.sort`, `.keys`
# — over a fresh hash each round, so every round races for its first Pair.
# Handing out a hash's first Pair moved its entries into the generation the
# Pairs co-own (ValueHash::aliasOf), which made a read a write: workers saw the
# hash emptied or died (issue #193). A read of a shared hash is safe in Rakudo,
# and nothing here writes one.
my $N = (@*ARGS[0] // 8).Int;
my $M = (@*ARGS[1] // 50).Int;
my $bad = 0;
for ^$M -> $r {
    my %h = (^32).map({ "k$_" => $_ + $r });
    my $sum = (^32).map(* + $r).sum;
    my $keys = (^32).map({ "k$_" }).sort.join(',');
    my $go = Promise.new;
    my @p = (^$N).map: -> $w {
        start {
            await $go;
            given $w % 4 {
                when 0 { %h.pairs.map(*.value).sum == $sum }
                when 1 { my $s = 0; for %h -> $p { $s += $p.value }; $s == $sum }
                when 2 { %h.sort(*.key).map(*.value).sum == $sum }
                default { %h.keys.sort.join(',') eq $keys }
            }
        }
    };
    $go.keep;
    for @p { $bad++ unless try .result }
}
if $bad == 0 {
    say 'PASS';
}
else {
    say "FAIL $bad of {$N * $M} reads wrong";
    exit 1;
}
