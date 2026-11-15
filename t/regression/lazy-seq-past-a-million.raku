# Regression: a lazy Seq is read past its millionth element. Every read of
# one stopped at 1,000,000 without a word, finite or not: `(gather …).elems`,
# `.sum`, `.grep`, `[1_400_000]`, `my @a = gather …`, `1 ... 1_500_000`,
# `(^Inf).map(…)[1_500_000]`, and a `for` over `(^Inf).map(…).grep(…)` that
# ends with `last`. Now element N is read however far it is, a source that
# ends is read to its end, and only a request for ALL of one that may not
# end stops at a million — where these refuse, as Rakudo's do.
#
# Runs clean under Rakudo too.

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}
my $n = 1_500_000;
sub g() { gather { take $_ for ^$n } }
ck g().elems, $n, '(gather …).elems';
ck g().map(* + 1).sum, $n * ($n + 1) div 2, '….map(…).sum';
ck g().grep(* %% 3).elems, $n div 3, '….grep(…).elems';
ck g()[1_400_000], 1_400_000, 'an element past a million';
ck (my @a = g()).elems, $n, 'assigned to an Array';
ck g().join(",").chars, 10888889, '.join';
ck g().sort.tail, $n - 1, '.sort';
ck (1 ... $n).elems, $n, '1 ... N';
ck (1, 3 ... 2 * $n - 1).elems, $n, '1, 3 ... N';
ck (1, { $_ + 1 } ... $n).elems, $n, 'a closure to an endpoint';
ck (1, { $_ + 1 } ... * > $n).elems, $n + 1, '…and to an endpoint block';
my $s = 0; for 1, { $_ + 1 } ... * > $n { $s++ }; ck $s, $n + 1, '…walked by a for';
ck (^Inf).map(* * 2)[$n], 2 * $n, 'an element of an endless map';
ck (1..Inf).grep(* %% 2)[1_200_000], 2_400_002, '…and of an endless grep';
$s = 0; for (^Inf).map(* + 1).grep(* %% 2) { last if $_ > 2 * $n; $s++ }; ck $s, $n, 'a for over them, ended by last';

# ALL of an endless one is refused, as Rakudo refuses it
sub refused(&c) { try { my $r = c(); $r.Int }; $! ~~ X::Cannot::Lazy }
ck refused({ (^Inf).map(* + 1).grep(* %% 2).sum }), True, '.sum of an endless grep';
ck refused({ (1 xx *).map(* + 1).grep(*.so).elems }), True, '.elems of one';
ck (^Inf).grep({ last if $_ > 5; True }).eager, (0, 1, 2, 3, 4, 5), '…but .eager runs it to its last';
ck (^Inf).grep(* %% 2).head(3).List, (0, 2, 4), '…and .head takes what it needs';

say $fails ?? "FAIL ($fails)" !! 'PASS';
exit $fails ?? 1 !! 0;
