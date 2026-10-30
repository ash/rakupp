# Regression: every continuation of a promise that settled later (a `.then`,
# a `whenever $promise` in a supply or a react) started a thread of its own,
# and the thread start cost more than the work: 2000 `whenever
# $socket.write(…) {}`, Cro's TCP replier's shape, took 60 ms. Rakudo cues
# them on its thread pool; they now run on pooled workers too, and one run
# after another lands on the same thread. A whenever's delivery into a supply
# block busy on another thread no longer waits on the worker either: it
# queues behind that body, as Rakudo's continuation waits on the supply's lock
# without holding a pool thread.
#
# Expectations checked against Rakudo 2026.09
# (~/.rakubrew/versions/moar-2026.09/install/bin/rakudo). Green on both.
# Contract: exit 0 + last line PASS.
my @fail;
sub check($ok, $what) { @fail.push($what) unless $ok }
# rakupp's cooperative GIL (RAKUPP_GIL=1, RAKUPP_PARALLEL=0) keeps a thread per
# continuation: the thread counts are the parallel mode's
my $gil = (%*ENV<RAKUPP_GIL> // '0') ne '0' || (%*ENV<RAKUPP_PARALLEL> // '') eq '0';
sub few(%ids) { $gil || %ids.elems < 50 }

# one continuation after another: a few threads, not one each
my %then; my $sum = 0;
for ^200 -> $i {
    my $p = Promise.new;
    my $t = $p.then({ %then{$*THREAD.id}++; .result * 2 });
    $p.keep($i);
    $sum += await $t;
}
check $sum == 2 * [+](^200), ".then results ($sum)";
check few(%then), "200 .then continuations ran on {%then.elems} threads";

my %sup; my $got = 0;
for ^100 -> $i {
    my $p = Promise.new;
    my $r = Promise(supply { whenever $p -> $v { %sup{$*THREAD.id}++; emit $v } });
    $p.keep($i);
    $got += await $r;
}
check $got == [+](^100), "supply whenever results ($got)";
check few(%sup), "100 supply whenevers ran on {%sup.elems} threads";

my %react; my $rgot = 0;
for ^100 -> $i {
    my $p = Promise.new;
    start { sleep 0.001; $p.keep($i) };
    react { whenever $p -> $v { %react{$*THREAD.id}++; $rgot += $v; done } }
}
check $rgot == [+](^100), "react whenever results ($rgot)";
check few(%react), "100 react whenevers ran on {%react.elems} threads";

# nothing of one continuation is left for the next on the same thread
sub with-d($d) {
    my $*D = $d;
    my $p = Promise.new;
    my $t = $p.then({ $*D });
    $p.keep(1);
    await $t;
}
check (with-d('a'), with-d('b'), with-d('c')).join eq 'abc', "each .then sees its own dynamic variable";
my $dies = Promise.kept(1).then({ die "boom" });
check (try await $dies) === Nil && $dies.cause.message eq 'boom', "a .then that dies breaks its promise";
check (await Promise.kept(1).then({ 42 })) == 42, "…and the next one runs as usual";
my $rdied = do { my $p = Promise.new; start { sleep 0.01; $p.keep(1) }; try { react { whenever $p { die "react boom" } } }; $! };
check ($rdied andthen .message eq 'react boom'), "a react whenever that dies ends its react";
my $p2 = Promise.new; start { sleep 0.01; $p2.keep(7) };
my $after = 0; react { whenever $p2 -> $v { $after = $v; done } };
check $after == 7, "…and the next react's whenever runs as usual";

# a promise kept while its supply's body still runs on another thread is
# delivered after that body, in turn
my @log; my $l = Lock.new;
my $p = Promise.new;
my $s = supply {
    whenever $p { $l.protect: { @log.push: 'promise' } }
    start { sleep 0.03; $p.keep(1) }
    for ^4 { sleep 0.02; $l.protect: { @log.push: "body$_" } }
}
await Promise.anyof(Promise($s), Promise.in(5));
check @log.join(' ') eq 'body0 body1 body2 body3 promise', "busy supply: {@log.join(' ')}";

if @fail { note "FAILED: $_" for @fail; say 'FAIL' } else { say 'PASS' }
