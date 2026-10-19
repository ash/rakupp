# Regression: `for` over an array or hash slice looped over COPIES, so a
# write through the topic was lost: `$_ *= 10 for @a[^2]` left @a alone,
# and `for @a[^2] -> $x is rw { $x = 0 }` too. A slice hands out the
# element containers. The loop now walks those positions in the array's
# own storage (the view `@a.grep(…)` already had), and a hash slice's
# entries directly. `@a[^2]».++` took copies the same way: the dotted
# `».++` was a `.map`, where `»++` was already the hyper postfix.
# Rosetta Code's "100 doors" (docs/dev/findings/ROSETTACODE.md) is written so.
#
# Not covered, and copied as before: a subscript with a call in it (it must
# not run twice), a position past the end or a missing key (Rakudo vivifies
# them on the write).
#
# Contract: exit 0 + last line PASS. Runs under Rakudo unchanged; every
# expectation was checked against Rakudo 2026.08 on 2026-10-19.
my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

my @a = 1..5;
$_ *= 10 for @a[^2];
ck(@a, [10, 20, 3, 4, 5], '$_ *= 10 for @a[^2]');
.=not for @a[3, 4];
ck(@a, [10, 20, 3, False, False], '.=not for @a[3, 4]');
@a[^2]».++;
ck(@a, [11, 21, 3, False, False], '@a[^2]».++');
@a[0, 1]»--;
ck(@a, [10, 20, 3, False, False], '@a[0, 1]»--');
for @a[^2] -> $x is rw { $x = 0 }
ck(@a, [0, 0, 3, False, False], 'for @a[^2] -> $x is rw');
for @a[1, 2] { $_ = 7 }
ck(@a, [0, 7, 7, False, False], 'for @a[1, 2] { $_ = 7 }');
for @a[3, 4] <-> $x { $x = 1 }
ck(@a, [0, 7, 7, 1, 1], 'for @a[3, 4] <-> $x');
my @i = 0, 4;
$_ = 9 for @a[@i];
ck(@a, [9, 7, 7, 1, 9], 'an @-variable subscript');
my ($lo, $hi) = 1, 2;
$_ = 5 for @a[$lo..$hi];
ck(@a, [9, 5, 5, 1, 9], 'a range of variables');
my @c = 1, 2, 3, 4;
$_ = 0 for @c[1..*];
ck(@c, [1, 0, 0, 0], 'an open-ended range runs to the end');
my @b = 1, 2, 3;
for @b[*] { $_ *= 2 }
ck(@b, [2, 4, 6], 'the whole slice still aliases');
for @b[0, 0] { $_++ }
ck(@b, [4, 4, 6], 'a position named twice is written twice');
my @r = 1, 2, 3;
ck((for @r[0, 1] { $_ * 10 }), (10, 20), 'a loop used as a value');
my @q = 1, 2, 3;
for @q[^2] -> $x { }   # a readonly parameter takes no container, and dies on a write
ck((try { for @q[^2] -> $x { $x = 1 } } // $!.message), 'Cannot assign to a readonly variable or a value', 'a readonly parameter still refuses');

# Rosetta Code's "100 doors", as written there
my @doors = False xx 101;
(.=not for @doors[0, $_ ... 100]) for 1..100;
ck((1..100).grep({ @doors[$_] }).List, (1, 4, 9, 16, 25, 36, 49, 64, 81, 100), '100 doors: the squares are open');
my @s = 1..5; my $i = 1;
$_ = 0 for @s[$i + 1, $i * 3];
ck(@s, [1, 2, 0, 0, 5], 'a subscript of arithmetic on variables');

my %h = a => 1, b => 2;
$_++ for %h<a b>;
ck(%h<a b>, (2, 3), '$_++ for %h<a b>');
for %h<a b> <-> $v { $v = 9 }
ck(%h, {a => 9, b => 9}, 'for %h<a b> <-> $v');
my @k = <a b>;
$_ = 0 for %h{@k};
ck(%h, {a => 0, b => 0}, 'a hash slice by an @-variable');
for %h<a c> { }
ck(%h.keys.sort.List, ('a', 'b'), 'reading a missing key does not vivify it');

say $fails ?? "FAIL ($fails)" !! "PASS";
exit $fails ?? 1 !! 0;
