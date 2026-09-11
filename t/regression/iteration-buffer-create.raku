# Regression: `IterationBuffer.CREATE` is an empty IterationBuffer.
#
# Backtrace::Files gathers its context lines into `IterationBuffer.CREATE`
# and pushes to it. `.new` built the buffer; `.CREATE` fell to the generic
# allocator and made a bare object — "No such method 'push'".
#
# Every expectation was checked against Rakudo.

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

my $b := IterationBuffer.CREATE;
ck($b.elems, 0, 'empty on creation');
$b.push(1); $b.push(2);
ck($b.elems, 2, 'push counts');
ck($b.List, (1, 2), '.List reads it back');
ck($b.^name, 'IterationBuffer', 'and it is an IterationBuffer');

say $fails ?? "FAIL ($fails)" !! "PASS";
exit $fails ?? 1 !! 0;
