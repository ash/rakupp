# Regression guard for sort's comparator fast path. A two-parameter block
# whose body is ONE `<=>`/`cmp`/`leg` between its parameters — `{ $^a <=> $^b }`,
# `{ $^b cmp $^a }`, `-> $x, $y { $y <=> $x }` — is answered by the operator
# itself instead of by calling the block per comparison (mutsu's bench-array:
# 96 ms -> under 1 ms for 10k elements). The answers must be the block's:
# mixed numeric types, strings, the swapped (descending) spelling, stability,
# and a user operator that takes the comparison over.
#
# Every expectation below was checked against Rakudo 2026.09.

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

my @p = 3, 1, 2, 1.5, "10", 2;
ck(@p.sort({ $^a <=> $^b }).List, (1, 1.5, 2, 2, 3, "10"), '<=> ascending over mixed numerics');
ck(@p.sort({ $^b <=> $^a }).List, ("10", 3, 2, 2, 1.5, 1), 'and descending');
ck(@p.sort(-> $x, $y { $y <=> $x }).List, ("10", 3, 2, 2, 1.5, 1), 'a pointy block');
ck(<b a C c B>.sort({ $^a cmp $^b }).List, <B C a b c>, 'cmp over strings');
ck(<b a C c B>.sort({ $^b leg $^a }).List, <c b a C B>, 'leg descending');
ck([10, 2, 33].sort({ $^a cmp $^b }).List, (2, 10, 33), 'cmp over Ints is numeric');
ck((1..10000).pick(*).sort({ $^b <=> $^a }).head(3).List, (10000, 9999, 9998), 'a large Int list descending');
ck((2, 1, 3).sort({ $^a <=> $^b }).WHAT, Seq, 'the result is a Seq');

# stability: equal keys keep their order, in both directions
my @kv = (1..6).map({ $_ => $_ % 2 });
ck(@kv.sort({ $^b.value <=> $^a.value }).map(*.key).List, (1, 3, 5, 2, 4, 6), 'stable descending (a method call, the slow path)');
my @twos = 2, 1, 2, 1;
ck(@twos.sort({ $^b <=> $^a }).List, (2, 2, 1, 1), 'equal Ints descending');

# not the shape: these still call the block
ck((3, 1, 2).sort(-> $a, $b { $a > $b }).List, (1, 2, 3), 'a Bool-answering comparator');
ck(((1, 2), (0, 5)).sort({ $^a <=> $^b }).map(*.List).List, ((1, 2), (0, 5)), 'lists compared by their length');
{
    my $calls = 0;
    multi infix:«<=>»(Int $a, Int $b where * == 99) { $calls++; Order::Same }
    my @s = (5, 99, 1).sort({ $^a <=> $^b });
    ck($calls > 0, True, 'a user <=> in scope is still called');
}

say $fails ?? "FAIL ($fails)" !! "PASS";
exit $fails ?? 1 !! 0;
