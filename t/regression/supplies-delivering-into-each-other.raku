# Regression: two supply blocks, each busy on a thread of its own, each
# emitting into the other. An emit into an activation another thread holds
# waits for it (emit-waits-for-busy-supply-on-another-thread.raku), and here
# each holder waits for the other, so neither would ever be free. Each wait
# used to end after 25 ms and queue its value; the wait lasts up to a second
# now, and one that closes a circle queues at once instead. Every value is
# handled, by the holder once its own body ends.
#
# Rakudo 2026.09 deadlocks here: after five seconds neither emit has
# returned and nothing is logged. The expectations are rakupp's own.

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

my @log; my $lock = Lock.new;
sub log($x) { $lock.protect: { @log.push: $x } }

my $a = Supplier.new; my $b = Supplier.new;
my $x = Supplier.new; my $y = Supplier.new;
my $in1 = Promise.new; my $v1 = $in1.vow;
my $in2 = Promise.new; my $v2 = $in2.vow;
my $s1 = supply {
    whenever $a.Supply { $v1.keep(True); await $in2; $y.emit('from 1'); log 's1 body done' }
    whenever $x.Supply { log "s1 got $_" }
}.tap;
my $s2 = supply {
    whenever $b.Supply { $v2.keep(True); await $in1; $x.emit('from 2'); log 's2 body done' }
    whenever $y.Supply { log "s2 got $_" }
}.tap;
my @w = start { $a.emit(1) }, start { $b.emit(2) };
await Promise.anyof(Promise.allof(@w), Promise.in(10));
ck @w».status.List, (Kept, Kept), 'both emits return';
my $all = Promise.in(10);
sleep 0.01 until @log == 4 || $all;
ck @log.sort.List, ('s1 body done', 's1 got from 2', 's2 body done', 's2 got from 1'),
    'each holder handles the value queued for it once its body ends';
# …and in that order on each side: the queued value after the body
for 1, 2 -> $n {
    my $other = 3 - $n;
    my ($body, $got) = @log.first("s$n body done", :k), @log.first("s$n got from $other", :k);
    ck $body.defined && $got.defined && $body < $got, True, "s$n: its body ends before the value queued for it";
}
$s1.close; $s2.close;

say $fails ?? "FAIL ($fails)" !! 'PASS';
exit $fails ?? 1 !! 0;
