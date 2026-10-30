# Regression: an emit into a supply block whose activation another thread
# was running only queued the value and returned, and the value reached its
# whenever once that thread had finished, after whatever the emitting thread
# did next. Rakudo's emit waits on the busy supply's lock and then runs the
# whenever itself, so the value is handled by the time emit returns.
# Cro::WebSocket's handler test keeps a close promise after its last emit,
# and counted the replies before the last one had been made.
#
# Expectations checked against Rakudo 2026.09
# (~/.rakubrew/versions/moar-2026.09/install/bin/rakudo). Green on both.

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

my @log; my $lock = Lock.new;
sub log($x) { $lock.protect: { @log.push: $x } }

my $a = Supplier.new; my $b = Supplier.new;
my $busy = Promise.new; my $v = $busy.vow;
my $t = supply {
    whenever $a.Supply { $v.keep(True); sleep 0.003; log "handled $_" }
    whenever $b.Supply { log "handled $_" }
}.tap;
start { $a.emit('slow') };
await $busy;
$b.emit('fast');
log 'after emit';
ck @log.List, ('handled slow', 'handled fast', 'after emit'),
    'an emit into an activation busy on another thread is handled before emit returns';
$t.close;

say $fails ?? "FAIL ($fails)" !! 'PASS';
exit $fails ?? 1 !! 0;
