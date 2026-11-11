# Regression: issue #30 (2026-11-10).
#
#   `xx` ignored a Slip on its left, so `(|(1,2) xx 2)` was ((1 2) (1 2)),
#   and `xx *` did not loop: an iterator over `1 xx *` answered 1 and then
#   IterationEnd. A colour cycle `(|@c xx *).map(...).iterator` broke on both.
#
# Expected answers are Rakudo 2026.09's (2026-11-10).

my $fails = 0;
sub check($got, $want, $what) {
    return if $got eqv $want;
    $fails++;
    say "not ok - $what\n  got:  {$got.raku}\n  want: {$want.raku}";
}

my $x = (|(1, 2) xx *).iterator;
check (gather for ^3 { take $x.pull-one }).List, (1, 2, 1), 'a Slip repeated for ever, pulled by hand';

my $y = (1 xx *).iterator;
check (gather for ^3 { take $y.pull-one }).List, (1, 1, 1), '1 xx * keeps answering';

check (|(1, 2) xx 2).head(4).List, (1, 2, 1, 2), 'a Slip repeated twice is spread';
check (|(1, 2) xx 2).List,         (1, 2, 1, 2), '…as a whole list too';
check ((1, 2) xx 2).List,          ((1, 2), (1, 2)), 'a List repeated stays nested';
check (|(1, 2) xx *).head(5).List, (1, 2, 1, 2, 1), 'head of an endless Slip repeat';
check ('a' xx *).head(3).List,     ('a', 'a', 'a'), "a string repeated for ever ('…')";
check ("a" xx *).head(3).List,     ('a', 'a', 'a'), 'a string repeated for ever ("…")';

my @bg = <232 235>;
my $gen = (|@bg xx *).map({ "[" ~ $_ ~ "]" }).iterator;
check (^5).map({ $gen.pull-one }).List, ('[232]', '[235]', '[232]', '[235]', '[232]'),
    'the colour cycle of the report: (|@bg xx *).map(...).iterator';
my $gen2 = (|@bg xx *).map({ '[' ~ $_ ~ ']' }).iterator;
check $gen2.pull-one, '[232]', '…with a single-quoted wrapper';

if $fails { say "$fails failure(s)"; exit 1 }
say 'PASS';
