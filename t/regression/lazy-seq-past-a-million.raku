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
my $n = 1_050_000;
# Each eager read holds more than a million elements, so each runs in a child
# of its own: one at a time, the peak is one read's, not all of theirs.
sub child(Str $code) {
    my $p = run $*EXECUTABLE, '-e', "my \$n = $n; sub g() \{ gather \{ take \$_ for ^\$n } }; say $code", :out;
    $p.out.slurp(:close).trim
}
ck child('g().elems'), ~$n, '(gather …).elems';
ck child('g().map(* + 1).sum'), ~($n * ($n + 1) div 2), '….map(…).sum';
ck child('g().grep(* %% 3).elems'), ~($n div 3), '….grep(…).elems';
ck child('g()[1_040_000]'), '1040000', 'an element past a million';
ck child('(my @a = g()).elems'), ~$n, 'assigned to an Array';
ck child('(1 ... $n).elems'), ~$n, '1 ... N';
ck child('(1, { $_ + 1 } ... $n).elems'), ~$n, 'a closure to an endpoint';
my $s = 0; for 1, { $_ + 1 } ... * > $n { $s++ }; ck $s, $n + 1, '…and one to an endpoint block, walked by a for';
ck child('(^Inf).map(* * 2)[$n]'), ~(2 * $n), 'an element of an endless map';
ck child('(1..Inf).grep(* %% 2)[$n]'), ~(2 * $n + 2), '…and of an endless grep';
$s = 0; for (^Inf).map(* + 1).grep(* %% 2) { last if $_ > 2 * $n; $s++ }; ck $s, $n, 'a for over them, ended by last';

# ALL of an endless one is refused, as Rakudo refuses it
# (each reads a million before it knows: in a child too)
ck child('(try { my $r = (^Inf).map(* + 1).grep(* %% 2).sum; $r.Int }) // $! ~~ X::Cannot::Lazy'), 'True',
   '.sum of an endless grep';
ck child('(try { my $r = (1 xx *).map(* + 1).grep(*.so).elems; $r.Int }) // $! ~~ X::Cannot::Lazy'), 'True',
   '.elems of one';
ck (^Inf).grep({ last if $_ > 5; True }).eager, (0, 1, 2, 3, 4, 5), '…but .eager runs it to its last';
ck (^Inf).grep(* %% 2).head(3).List, (0, 2, 4), '…and .head takes what it needs';

say $fails ?? "FAIL ($fails)" !! 'PASS';
exit $fails ?? 1 !! 0;
