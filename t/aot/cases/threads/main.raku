# Native bodies called from `start` threads (t/aot/run.raku).
#aot-extra-run-env: RAKUPP_PARALLEL=1
use Aot::Threads;

my @p = (1..8).map(-> $id { start { work($id, 5) } });
say await(@p).join(' ');
say tally();
say fan-out([10, 11, 12]);
say tally();
my $w = Worker.new(label => 'w');
await (1..6).map(-> $n { start { $w.run($n) } });
say $w.finished;
say (1..20).hyper(:batch(2), :degree(4)).map({ work($_, 2) }).list.join(' ');
