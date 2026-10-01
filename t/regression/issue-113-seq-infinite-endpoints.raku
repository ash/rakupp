# Issue #113: the sequence operator against an unbounded endpoint.
#
#   * `-Inf` is unbounded DOWNWARDS: a lone seed steps by .pred
#     (`3 ... -Inf` is 3 2 1 0 …), where `*` and `Inf` step by .succ. It was
#     lumped in with `*`, so every lone seed climbed. Seeds already climbing
#     never come down to it, so `1, 3 ... -Inf` is empty, as `5, 3 ... 10` is.
#   * `^...` dropped the seed by erasing it from the result's array — but a
#     lazy sequence's array is its generator's cache, the value it steps from,
#     so `3 ^... *` came out empty and `1, *+1 ^... *` called the generator
#     with no arguments. The seed is now skipped by a view over the sequence.
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) { @fail.push("$what: got {$got.raku}, want {$want.raku}") unless $got eqv $want }

check (3 ...  100).head(4).List, (3, 4, 5, 6),    '3 ... 100';
check (3 ...    *).head(4).List, (3, 4, 5, 6),    '3 ... *';
check (3 ...  Inf).head(4).List, (3, 4, 5, 6),    '3 ... Inf';
check (3 ... -Inf).head(4).List, (3, 2, 1, 0),    '3 ... -Inf';
check (3 ... -∞).head(3).List,   (3, 2, 1),       '3 ... -∞';
check (1 ... -Inf)[^5].List,     (1, 0, -1, -2, -3), '(1 ... -Inf)[^5]';
check (3 ...^ -Inf).head(3).List, (3, 2, 1),      '3 ...^ -Inf';
check (1/2 ... -Inf).head(3).List, (0.5, -0.5, -1.5), 'a Rat seed steps down';
check (2, 1 ... -Inf).head(4).List, (2, 1, 0, -1), 'descending seeds continue';
check (1, 3 ... -Inf).List,      (),               'ascending seeds never reach -Inf';
check (1, 2, 4 ... -Inf).List,   (),               'nor do growing geometric ones';
check (16, 8, 4 ... -Inf).head(4).List, (16, 8.0, 4.0, 2.0), 'shrinking geometric ones run on';
check (5, 3 ... Inf).head(4).List, (5, 3, 1, -1),  'Inf is a Whatever: the seeds lead';

check (3 ^...  100).head(4).List, (4, 5, 6, 7),   '3 ^... 100';
check (3 ^...    *).head(4).List, (4, 5, 6, 7),   '3 ^... *';
check (3 ^...  Inf).head(4).List, (4, 5, 6, 7),   '3 ^... Inf';
check (3 ^... -Inf).head(4).List, (2, 1, 0, -1),  '3 ^... -Inf';
check (3 ^...^ Inf).head(3).List, (4, 5, 6),      '3 ^...^ Inf';
check (3 ^...^ -Inf).head(3).List, (2, 1, 0),     '3 ^...^ -Inf';
check (3 ^... *)[^3].List,        (4, 5, 6),      'indexing a ^... sequence';
my @a = (3 ^... *); check @a[^3].List, (4, 5, 6), 'assigned to an array';
check (1, *+1 ^... *).head(3).List, (2, 3, 4),    'a generator still sees the seed';
check (1, *-1 ^... -Inf).head(3).List, (0, -1, -2), '…towards -Inf too';
check (2, 4 ^... *).head(3).List, (4, 6, 8),      'two seeds, the first dropped';
check (3 ^... 3).List,            (),             'eager: nothing left';
check (3 ^... 5).List,            (4, 5),         'eager: the seed dropped';

if @fail { note "FAILED:\n" ~ @fail.join("\n"); say 'FAIL' } else { say 'PASS' }
