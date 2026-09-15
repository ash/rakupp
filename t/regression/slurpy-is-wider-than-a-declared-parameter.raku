# A `*@slurpy` candidate is WIDER than one that declares the parameter, whatever
# the declaration order. Scored as a tie, declaration order decided instead —
# and Text::CSV, whose `multi method combine(*@f) { self.combine(@f) }` sits
# above `multi method combine(@f)`, called itself 18,036 times and died of
# recursion on its very first test.
use Test;
plan 5;

multi slurpy-first(*@f) { 'slurpy' }
multi slurpy-first(@f)  { 'array'  }
my @a = 1, 2, 3;
is slurpy-first(@a), 'array',   'a single Positional prefers the declared parameter';

multi array-first(@f)  { 'array'  }
multi array-first(*@f) { 'slurpy' }
is array-first(@a), 'array',    '…in the other declaration order as well';

is slurpy-first(1, 2), 'slurpy', 'and the slurpy still takes what nothing else can';

class K {
    multi method combine(*@f) { 'slurpy' }
    multi method combine(@f)  { 'array'  }
}
is K.combine(@a), 'array',      'the same on a method';

multi two-slots($a, $b) { 'pair'   }
multi two-slots(*@f)    { 'slurpy' }
is two-slots(1, 2), 'pair',     'two declared positionals beat a slurpy that fits';
