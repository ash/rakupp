# The multi-dispatch cache covers candidates with a positional `where`: per
# argument shape it keeps which candidates can match at all, and a table from
# the outcomes of their `where`s to the winner. A call evaluates the `where`s
# and looks the winner up; an outcome not seen yet runs the full dispatch with
# those outcomes given, so no `where` runs twice. Every answer here must be the
# full dispatch's, whatever order the outcomes arrive in.
use Test;
plan 12;

class K {
    proto method m(|) {*}
    multi method m(Int $x where * > 0) { 'pos' }
    multi method m(Int $x)             { 'int' }
    multi method m(Str $x)             { 'str' }
}
my $k = K.new;
is (0, 1, -1, 2, 0).map({ $k.m($_) }).join(','), 'int,pos,int,pos,int', 'a method: both outcomes, in turn';
is $k.m('a'), 'str',                        '…another shape beside it';

multi f(Int $a where * %% 2, Int $b where * %% 3) { 'both' }
multi f(Int $a where * %% 2, Int $b)              { 'even' }
multi f(Int $a, Int $b where * %% 3)              { 'three' }
multi f(Int $a, Int $b)                           { 'none' }
is ((2, 3), (2, 1), (1, 3), (1, 1), (4, 9), (5, 5)).map({ f(|$_) }).join(','),
   'both,even,three,none,both,none',        'a sub with two where-parameters per candidate';

# a `where` runs once per candidate per call, as the full dispatch runs it
my @log;
multi g(Int $n where { @log.push("a$n"); $n > 5 }) { 'big' }
multi g(Int $n)                                     { 'small' }
my @r = (1, 9, 2, 9).map({ g($_) });
is @r.join(','), 'small,big,small,big',      'a where with a side effect';
is @log.join(','), 'a1,a9,a2,a9',            '…which runs once per call';

# an earlier parameter is in scope in a later `where`
multi h(Int $l, Int $n where * > $l) { 'up' }
multi h(Int $l, Int $n)              { 'down' }
is (h(1, 2), h(2, 1), h(5, 9), h(9, 5)).join(','), 'up,down,up,down', 'a where that reads an earlier parameter';

# a method's `where` sees the invocant
class W {
    has $.limit;
    multi method t(Int $n where * < $!limit) { 'under' }
    multi method t(Int $n)                   { 'over' }
}
my $w1 = W.new(limit => 3);
my $w2 = W.new(limit => 10);
is ($w1.t(5), $w2.t(5), $w1.t(1), $w2.t(50)).join(','), 'over,under,under,over', 'a where that reads the invocant';

# no candidate takes it
multi only-pos(Int $n where * > 0) { $n }
is only-pos(3), 3,                           'one candidate with a where';
throws-like { only-pos(-3) }, X::Multi::NoMatch, '…and a value it refuses';
is only-pos(4), 4,                           '…then a value it takes again';

# two equally narrow candidates whose `where`s both pass: the first declared wins
multi q(Int $n where * > 0)  { 'first' }
multi q(Int $n where * > -5) { 'second' }
multi q(Int $n)              { 'plain' }
is (q(3), q(-3), q(-9), q(3)).join(','), 'first,second,plain,first', 'tied where-candidates';

# a smiley next to a where
multi d(Int:D $n where * > 0) { 'D' }
multi d(Int $n)               { 'other' }
is (d(1), d(Int), d(-1)).join(','), 'D,other,other', 'a smiley beside a where';
