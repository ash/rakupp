# Regression: a list function given an ITEMIZED argument — a List or an Array
# held in a `$` variable — takes it as one item, as Rakudo's +args / +values /
# +lists signatures do. rakupp spread it:
#
#   * `max($l)` for `my $l = (5, 7)` answered 7; Rakudo answers the List, the
#     only candidate there is. The same for `min`, and for `max($a)` of an Array.
#   * `unique($l)` and `squish($l)` answered (5 7) where Rakudo has ((5 7)).
#   * `zip($l, $m)` paired the elements, ((5 1) (7 2)); Rakudo pairs the Lists
#     whole, (((5 7) (1 2))), and `cross` the same. The meta-operators `[Z]` and
#     `[X]` DO flatten, and stay as they were.
#   * `flat($l)` kept the List whole, ((5 7)), where the single-argument rule
#     spreads it — but `flat($l, 1)` keeps it.
#
# What spreads an argument is unchanged: `max(@b)`, `max((5, 7))`, `max(|$l)`,
# `sum($l)`, `minmax($l)`, and a Range in a variable. Rakudo passes every check.
#
# Contract: exit 0 + last line PASS.
my @fail;

sub check($got, $want, $desc) {
    @fail.push("$desc: got {$got.raku}, want {$want.raku}") unless $got eqv $want;
}

my $l = (5, 7);
my $m = (1, 2);
my $a = [3, 9];
my @b = 4, 8;
my $r = 1..3;

check max($l), (5, 7), 'max of an itemized List is that List';
check min($l), (5, 7), '…and min';
check max($a), [3, 9], '…and of an itemized Array';
check max($l, :by(*.Str)), (5, 7), '…with :by too';
check unique($l).elems, 1, 'unique takes it as one value';
check unique($l)[0].List, (5, 7), '…the List';
check squish($l).elems, 1, '…and squish';
check zip($l, $m).List, (((5, 7), (1, 2)),), 'zip pairs itemized Lists whole';
check zip($l).List, (((5, 7),),), '…a lone one too';
check cross($l, $m).List, (((5, 7), (1, 2)),), 'cross crosses them whole';
check flat($l).List, (5, 7), 'flat spreads a lone itemized argument';
check flat($l, 1).List, ((5, 7), 1), '…but keeps one among others';

# what spreads is unchanged
check max(@b), 8, 'an array variable is the list';
check max((5, 7)), 7, 'a List literal is the list';
check max(|$l), 7, 'a slipped one is the list';
check max($r), 3, 'a Range in a variable is the list';
my $s = (5, 7).Seq;
check max($s), 7, '…and a Seq in a variable';
check sum($l), 12, 'sum spreads it';
check minmax($l), 5..7, 'minmax spreads it';
check ([Z] $l, $m).List, ((5, 1), (7, 2)), '[Z] flattens';
check ([X] $l, $m).List, ((5, 1), (5, 2), (7, 1), (7, 2)), '[X] flattens';

if @fail { .say for @fail; say "{+@fail} check(s) failed"; exit 1 }
say 'PASS';
