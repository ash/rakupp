# N workers write overlapping keys of one hash, guarded by a Lock —
# contended structural mutation, done the documented way.
my $N = (@*ARGS[0] // 4).Int;
my $M = (@*ARGS[1] // 1000).Int;
my $lock = Lock.new;
my %h;
await (^$N).map: -> $w {
    start {
        for ^$M -> $i {
            $lock.protect({
                %h{"k" ~ ($i % 97)}++;      # 97 hot keys, all workers collide
                %h{"w$w-$i"} = $i;          # plus distinct keys per worker
            });
        }
    }
};
my $hot = [+] (^97).map({ %h{"k$_"} // 0 });
my $distinct = %h.keys.grep(*.starts-with('w')).elems;
if $hot == $N * $M && $distinct == $N * $M {
    say 'PASS';
}
else {
    say "FAIL hot=$hot distinct=$distinct (want {$N * $M} each)";
    exit 1;
}
