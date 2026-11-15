# Regression (issue #263, its `map` shape): `for (^$n).map({…})` and
# `.grep(…)` over a Range run the block as the loop reads, as Rakudo's do.
# A map over a Range was eager: the block ran for every element before the
# loop body saw the first (`m0 m1 m2 b0 b1 b2`, where Rakudo prints
# `m0 b0 m1 b1 m2 b2`), and the whole list stayed until the loop was over,
# 4 million Hashes 1.2 GB. The loop now reads the Range as a stream of its
# Ints through a lazy view, which keeps nothing it has read.
#
# Runs clean under Rakudo too.
use Telemetry;

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}
my @o;
for (^3).map({ @o.push: "m$_"; $_ }) { @o.push: "b$_" }
ck @o.join(" "), "m0 b0 m1 b1 m2 b2", 'the block runs as the loop reads';
@o = ();
@o.push("b$_") for (^3).map({ @o.push: "m$_"; $_ });
ck @o.join(" "), "m0 b0 m1 b1 m2 b2", '…in a statement-modifier for too';
@o = ();
for (1..4).grep({ @o.push: "g$_"; $_ %% 2 }) { @o.push: "b$_" }
ck @o.join(" "), "g1 g2 b2 g3 g4 b4", '…and a grep';
@o = ();
for (^5).map({ last if $_ > 2; $_ }) { @o.push: $_ }
ck @o, [0, 1, 2], 'last in the block ends it';
@o = ();
for (^5).map({ next if $_ %% 2; $_ }) { @o.push: $_ }
ck @o, [1, 3], 'next skips one';
@o = ();
for (^3).map({ slip $_, $_ }) { @o.push: $_ }
ck @o, [0, 0, 1, 1, 2, 2], 'a Slip spreads';
ck (do for (^3).map(* + 1) { $_ * 2 }).List, (2, 4, 6), 'what the loop answers';
@o = ();
for (^4).map({ $_ }).kv -> $k, $v { @o.push: "$k:$v" }
ck @o.join(" "), "0:0 1:1 2:2 3:3", 'a .kv of the map';
@o = ();
for (5..1).map({ die }) { @o.push: $_ }
ck @o, [], 'an empty Range runs nothing';

# (unmeasured: Rakudo's own heap grows by about 100 MB on its first long loop)
{ my $s = 0; for gather { for ^1_000_000 { my %h = a => $_; take %h } } -> %h { $s += %h<a> } }
my $n = 1_500_000;
my $before = T<max-rss>;   # (KiB)
my $sum = 0;
for (^$n).map({ my %h = a => $_; %h }) -> %h { $sum += %h<a> }
my $grew = (T<max-rss> - $before) div 1024;
ck $sum, $n * ($n - 1) div 2, 'a million and a half Hashes, mapped';
if $grew < 150 { say "ok - …and none kept ($grew MiB)" }
else { $fails++; say "FAIL: the mapped Hashes grew the peak by $grew MiB" }

say $fails ?? "FAIL ($fails)" !! 'PASS';
exit $fails ?? 1 !! 0;
