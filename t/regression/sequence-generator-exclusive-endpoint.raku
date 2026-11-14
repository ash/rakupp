# A generator sequence stops at an excluded endpoint however it is read
# (rakudo#3881): `{ $++ } ...^ 5` dropped the 5 and walked on, so printing it,
# `.join` and an array assignment ran past it, while `.elems` and indexing
# stopped. And a Range on the right is a list whose first element is the
# endpoint, the rest following, for a lone generator too: `{ $++ } ... ^5` is
# 0 then 1..4.

my $fails = 0;
sub check($got, $want, $what) {
    if $got ne $want { say "FAIL: $what: got $got, want $want"; $fails++ }
}

check ({ $++ } ...^ 5).gist, '(0 1 2 3 4)', 'printed whole';
check ({ $++ } ...^ 5).join(','), '0,1,2,3,4', '.join';
my @x = ({ $++ } ...^ 5);
check @x.raku, '[0, 1, 2, 3, 4]', 'assigned to an array';
check ({ $++ } ...^ 5).elems, '5', '.elems';
check ({ $++ * ($ *= -1) } ...^ 21).tail, '-20', 'the issue\'s alternating walk';
check ({ $++ } ... 5).gist, '(0 1 2 3 4 5)', 'the inclusive form';
check (1, { $_ + 2 } ...^ 9).gist, '(1 3 5 7)', 'with a seed';
check ({ $++ } ... ^5).gist, '(0 1 2 3 4)', 'a Range endpoint is a list';
check ({ $++ * ($ *= -1) } ... ^21).elems, '21', '…the issue\'s fourth line';
check (1, { $_ * 2 } ... (8, 20, 30)).gist, '(1 2 4 8 20 30)', 'a List endpoint';
check ({ $++ } ... *)[^3].gist, '(0 1 2)', 'an endless one stays lazy';

say $fails ?? "FAIL ($fails)" !! 'PASS';
exit $fails ?? 1 !! 0;
