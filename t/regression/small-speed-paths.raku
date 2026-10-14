# The answers the V6 P1 shortcuts must keep (2026-10-14): `.sum`/`.min`/`.max`
# reading a plain Int array in place, `STMT for ^N` walking the range by its
# ends, a block's placeholders bound into its pad, and the three Order values
# built once. Each case is something the shortcut must still do, or must
# step aside for.
use Test;
plan 26;

# the borrowed fold: Ints, overflow into a BigInt, Nums, and what is not plain
my @i = 3, -7, 3, 9223372036854775807, 5;
is @i.sum, 9223372036854775811,                   'an Int sum leaves int64 exactly';
is [2**70, 1].sum, 1180591620717411303425,        'a BigInt element takes the general fold';
is @i.min, -7,                                    '.min of Ints';
is @i.max, 9223372036854775807,                   '.max of Ints';
is [0.5e0, 1e0, 2.25e0].sum, 3.75e0,              'a Num sum';
is (1, 2.5, 3).sum, 6.5,                          'a Rat among Ints takes the general fold';
is [<3>, 3].max.^name, 'IntStr',                  'an allomorph keeps its own arm (the first on a tie)';
is [1, "x"].max, 'x',                             'a Str among Ints compares as the general arm does';
is ().sum, 0,                                     'the empty sum';
is [].max, -Inf,                                  'the empty max';
my @h; @h[2] = 5;
is (quietly @h.sum), 5,                           'a hole is the general fold\'s business';
enum E <Ea Eb Ec>;
is [Eb, Ec, Ea].max, Ec,                          'enum members are not plain Ints';

# `STMT for RANGE` walks the range without building it
my @o; @o.push($_) for ^5;
is @o, [0, 1, 2, 3, 4],                           'STMT for ^N';
@o = (); @o.push($_) for 2^..^6;
is @o, [3, 4, 5],                                 'excluded ends';
@o = (); @o.push($_) for 3..1;
is @o, [],                                        'an empty range';
@o = (); { @o.push($_); last if $_ == 2 } for ^10;
is @o, [0, 1, 2],                                 '`last` ends the walk';
@o = (); @o.push($_) for (2**65)..(2**65+1);
is @o, [36893488147419103232, 36893488147419103233], 'BigInt ends take the flatten';
$_ = 'kept'; my $n = 0; $n += $_ for ^4;
is "$n $_", '6 kept',                             'the outer topic comes back';
throws-like { $_ = 1 for ^3 }, Exception,         'the topic is not a container';

# placeholders in the pad
is { $^b ~ $^a }('x', 'y'), 'yx',                 'placeholders bind in name order';
is { $^a; $a * 10 }(4), 40,                       '$^a and a later $a are one variable';
my @c = (^3).map({ my $j = $^i; -> { $j + $i } });
is @c».(), (0, 2, 4),                             'a closure keeps each call\'s placeholder';
is <c a b>.sort({ $^b cmp $^a }), <c b a>,        'a sort comparator';
throws-like { { $^p + $^q }(1) }, Exception,      'too few arguments still dies';

# the Order values
is (1 <=> 2).^name, 'Order',                      'an Order';
is "{3 <=> 1} {1 cmp 1} {'a' leg 'b'}", 'More Same Less', 'each of the three';
