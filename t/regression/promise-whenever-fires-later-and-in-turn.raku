# Regression: a `whenever $promise` in a supply or react block whose promise
# settles later ran its body INSIDE whatever kept the promise, and ran it
# beside the supply's other whenever bodies. Rakudo runs it on the thread
# pool, after `keep` returns, and one body at a time. Cro::WebSocket keeps a
# connection's close promise in the middle of passing on its input; run
# inline, Cro::RPC::JSON's actor `whenever $close { …; done }` finished the
# actor's supply before the input's end reached its `whenever $in`, and the
# actor's `last` hook never ran.
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

my $p = Promise.new; my $v = $p.vow;
my $t = supply { whenever $p { log 'supply body' }; whenever Supply.interval(10) { } }.tap;
$v.keep(1); log 'after keep';
for ^100 { last if @log >= 2; sleep 0.01 }
ck @log.List, ('after keep', 'supply body'), "a supply's promise body runs after `keep` returns";
$t.close;

@log = ();
my $p2 = Promise.new; my $v2 = $p2.vow;
react {
    whenever $p2 { log 'react body'; done }
    whenever Promise.in(0.05) { $v2.keep(2); log 'after keep 2' }
}
ck @log.List, ('after keep 2', 'react body'), "…and a react's";

my $p3 = Promise.new; my $v3 = $p3.vow;
my ($busy, $overlap) = 0, False;
my $src = Supplier.new;
my $t3 = supply {
    whenever $src.Supply { $busy++; sleep 0.1; $overlap = True if $busy > 1; $busy-- }
    whenever $p3 { $busy++; $overlap = True if $busy > 1; $busy--; log 'p3' }
}.tap;
@log = ();
start { $src.emit(1) };
sleep 0.03;
$v3.keep(1);
for ^100 { last if @log; sleep 0.01 }
ck ($overlap, @log.List), (False, ('p3',)), "…in turn with the supply's other whenever bodies";
$t3.close;

say $fails ?? "FAIL ($fails)" !! 'PASS';
exit $fails ?? 1 !! 0;
