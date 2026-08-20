# Regression: three unrelated divergences found sweeping The Weekly Challenge.
#
# 1. A plain list is SETTY in a set operation, so a repeat is still one
#    element: `(1,2,2,3) (-) (2,)` is Set(1 3), and `(1,1,2) (==) (1,2)` is
#    True. rakupp counted occurrences even at Set tier, which made every
#    duplicate-bearing list behave like a Bag — `$s (-) $s.repeated`, the
#    idiomatic "words that appear once", returned everything.
#
# 2. The reverse metaop turns the whole REDUCTION around, not each step:
#    `[R-] 1,2,3` is 3-2-1 (0), not R-(R-(1,2),3) (2). And `[R,]` — the way a
#    list gets reversed inside an index — did not parse at all, because the
#    comma arrives as its own token kind.
#
# 3. `:k`/`:v`/`:kv`/`:p` on an ASSOCIATIVE invocant answer as a mapping, not
#    as a numbered list: `%h.max(:v)` is the largest value and `:k` its key,
#    where a Positional's `:k` is the index. On an EMPTY collection they answer
#    an empty List (there is no winning position), not ±Inf.
#
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) {
    @fail.push("$what: got {$got.raku} want {$want.raku}") unless $got eq $want
}

# --- 1. set tier ---
check ((1,2,2,3) (-) (2,)).raku,      'Set.new(1,3)',   'a repeat is one element in (-)';
check ((1,1,2) (^) (1,)).raku,        'Set.new(2)',     '…and in (^)';
check ((1,1,2) (<=) (1,2)),           True,             '…and a duplicate does not break (<=)';
check ((1,1,2) (==) (1,2)),           True,             '…nor (==)';
my $s = ("apple banana apple".words, "banana orange".words).flat.List;
check ($s (-) $s.repeated).raku,      'Set.new("orange")', 'the words-appearing-once idiom';
# a genuinely baggy operand still lifts the whole operation to Bag tier
check ((1,1,2) (-) bag(1)).pairs.sort.join(' '), '1	1 2	1', 'a Bag operand keeps the counts';
check ((1,1,2) (+) (1,)).pairs.sort.join(' '),   '1	3 2	1', '(+) is baggy by definition';

# --- 2. the R metaop under a reduction ---
check ([R-] 1,2,3),        0,        '[R-] reduces right to left';
check ([R-] 1,2,3,4),     -2,        '…for any length';
check ([R/] 1,2,4),        2,        '…and for division';
check ([R~] <a b c>),      'cba',    '…and concatenation';
check ([R,] 1,2,3).join(' '), '3 2 1', '[R,] reverses a list';
my @a = 1, 2, 3;
check @a[ [R,] 0,1 ].join(' '), '2 1', '…including inside an index';
check ([\R-] 1,2,3).join(' '), '3 1 0', 'the triangle form scans the same way';
check ([\R~] <a b c>).join(' '), 'c cb cba', '…for concatenation';
check ([\R,] 1,2,3).map(*.join('')).join(' '), '1 21 321', '…and [\R,] gives reversed prefixes';
check ([\,] 1,2,3).map(*.join('')).join(' '), '1 12 123', 'the plain [\,] is unchanged';
check (1 R- 2),            1,        'the standalone R- still swaps its operands';

# --- 3. min/max adverbs ---
my %h = a => 3, b => 9;
check %h.max(:v).raku,  '(9,)',   'a Hash maxes by value';
check %h.max(:k).raku,  '("b",)', '…and :k names the key';
check %h.min(:k).raku,  '("a",)', '…min too';
check %h.max.raku,      ':b(9)',  'the bare .max still compares whole pairs';
my %tie = a => 9, b => 9;
check %tie.max(:k).sort.join(' '), 'a b', 'every key attaining the maximum is answered';
check (1,1,2).Bag.max(:k).raku,  '(1,)',     'a Bag maxes by count';
check (1,1,2).Bag.max(:v).raku,  '(2,)',     '…and :v is that count';
check (1,1,2).Bag.max(:kv).raku, '(1, 2)',   '…:kv interleaves them';
check (1,1,2).Bag.max(:p).raku,  '(1 => 2,)', '…and :p pairs them';
# a Positional keeps the index/element reading
check (3,9,1).max(:v).raku, '(9,)', 'a list still answers :v with the element';
check (3,9,1).min(:k).raku, '(2,)', '…and :k with the index';
# empty in, empty out — the ±Inf answer is for the adverb-free form only
check ().max(:v).raku, '()',    'an empty list has no winning position';
check ().min(:k).raku, '()',    '…for min as well';
check Empty.Bag.max(:v).raku, '()', '…and an empty Bag';
check ().max, -Inf,             'the bare .max of nothing is still -Inf';
check ().min,  Inf,             '…and .min is Inf';
check (sum flat (1,).Bag.max(:v), Empty.Bag.max(:v)), 1, 'so summing over a possibly-empty bag works';

if @fail { note "FAILED: " ~ @fail.join('; '); say 'FAIL' } else { say 'PASS' }
