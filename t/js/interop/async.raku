# --target=js interop golden: start / await / Promise. Under JavaScript a
# `start` block runs once the current synchronous code yields (concurrency,
# not parallelism), so the ordering below is deterministic there; the
# interpreter, with real threads, is not the oracle for this file.
my $p = start { say "in start"; 1 + 1 };
say "after start";
say await $p;
my @ps = (start { 2 }), (start { 3 });
say await @ps;
say await Promise.in(0.01);
my $q = Promise.new;
say $q.status;
$q.keep(5);
say $q.result, " ", $q.status, " ", ?$q;
my $then = $p.then({ "then saw " ~ .result });
say await $then;
sub slow($n) { await Promise.in(0.01); $n * 10 }
say slow(4);
say (1..3).map({ $_ + 1 });
my $b = Promise.new;
$b.break("nope");
try { await $b; CATCH { default { say "caught: ", .message } } }
say $b.status, " ", $b.cause.message;
say await Promise.allof($p, $q);
say await Promise.kept("ready");
class Fetcher {
    method get($x) { await Promise.in(0.01); "got $x" }
}
say Fetcher.new.get("it");
say await (start { my $s = 0; $s += $_ for 1..10; $s });
say "done";
