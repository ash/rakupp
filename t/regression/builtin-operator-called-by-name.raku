# A built-in operator called by its name is that operator applied, as Rakudo's
# `&prefix:<->` and friends are: `prefix:<->(3)`, `prefix:<so>(1)`,
# `postfix:<++>(my $y = 1)` and `postcircumfix:<[ ]>((1,2,3), 1)` all died
# "Undefined routine". A postfix keeps its operand's container, so the
# variable steps; the guillemet and double-angle spellings of a name,
# `infix:«<=»(1, 2)` and `infix:<<∈>>(…)`, call too (they parsed as a bare
# `infix`). An operator the program declares is still called as its routine.
# Every line answers the same under Rakudo 2026.09.

sub ck($got, $want, $desc) {
    return if $got eqv $want;
    say "FAIL: $desc — got {$got.raku}, want {$want.raku}";
    exit 1;
}

ck prefix:<->(3), -3, 'prefix:<->';
ck prefix:<so>(1), True, 'prefix:<so>';
ck (prefix:<!>(0), prefix:<not>(1), prefix:<~>(5), prefix:<+>("3"), prefix:<?>(0), prefix:<+^>(5)),
   (True, False, "5", 3, False, -6), 'the other prefixes';
ck prefix:<^>(3), ^3, 'prefix:<^>';
my $y = 1;
ck postfix:<++>($y), 1, 'postfix:<++> answers the old value';
ck $y, 2, '…and steps the variable';
ck postfix:<++>(my $z = 1), 1, 'on a declaration';
my $w = 5;
ck (prefix:<++>($w), postfix:<-->($w), $w), (6, 6, 5), 'prefix ++ and postfix --';
ck postcircumfix:<[ ]>((1, 2, 3), 1), 2, 'postcircumfix:<[ ]>';
ck postcircumfix:<[ ]>([1, 2, 3], 0..1), (1, 2), 'a slice';
my %h = a => 1;
ck postcircumfix:<{ }>(%h, 'a'), 1, 'postcircumfix:<{ }>';
my @a = 1, 2;
postcircumfix:<[ ]>(@a, 0) = 9;
ck @a, [9, 2], 'an element to assign to';
ck infix:«<=»(1, 2), True, 'infix:«<=»';
ck infix:<<∈>>(1, (1, 2)), True, 'infix:<<∈>>';
ck infix:«+»(1, 2), 3, 'infix:«+»';
ck EVAL('sub prefix:<->($x) { "mine" }; prefix:<->(3)'), 'mine', 'a declared operator';

say 'PASS';
