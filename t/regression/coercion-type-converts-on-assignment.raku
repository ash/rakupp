# A coercion-typed slot CONVERTS what it is handed rather than refusing it.
# `my Int() $x = "7"` already did; `my Int() $x; $x = "7"` threw, so the two
# spellings of the same declaration disagreed. Git::Blame::File's porcelain
# chunk counter is `my Int() $todo` assigned later, and the mismatch made an
# ordinary `git blame` die with "weird end".
use Test;
plan 8;

my Int() $a; $a = "7";
is $a, 7,          'a later assignment coerces';
is $a.^name, 'Int', '…to the declared type';

my Int() $b = "1";
is $b, 1,           'a declaration initialiser still coerces';
$b = "42";
is $b, 42,          '…and so does the assignment after it';
is $b.^name, 'Int', '…to the declared type';

my Str() $c; $c = 99;
is $c, '99',        'the other direction too';
is $c.^name, 'Str', '…to the declared type';

# …and a plain typed slot still REFUSES rather than coercing
my Int $d;
dies-ok { $d = "7" }, 'a plain typed slot is unchanged';
