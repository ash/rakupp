# An `is rw`, raw or `\x` parameter of the block or routine `.map` calls is
# bound to the Array's element, as Rakudo binds it: `sub inc($x is rw) { $x++ };
# @a.map(&inc)` and `@a.map(-> $x is rw { $x++ })` left @a as it was (only
# `$_` was aliased), and so did `@a.map(&postfix:<++>)`. Each parameter of a
# chunk is bound to its own element; a plain or `is copy` one stays a copy.
# Every line answers the same under Rakudo 2026.09.

sub ck($got, $want, $desc) {
    return if $got eqv $want;
    say "FAIL: $desc — got {$got.raku}, want {$want.raku}";
    exit 1;
}

my @a = 1, 2; @a.map(-> $x is rw { $x++ }).eager;
ck @a, [2, 3], 'a pointy block';
sub inc($x is rw) { $x++ }
my @b = 1, 2; @b.map(&inc).eager;
ck @b, [2, 3], 'a sub';
my @c = 1, 2; @c.map(&inc);
ck @c, [2, 3], 'sunk';
my @d = 1, 2; @d.map(&postfix:<++>);
ck @d, [2, 3], '&postfix:<++>';
my @e = 1, 2; @e.map(-> \x { x = x * 3 }).eager;
ck @e, [3, 6], 'a raw parameter';
my @f = 1, 2, 3, 4; @f.map(-> $x is rw, $y { $x++ }).eager;
ck @f, [2, 2, 4, 4], 'two at a time';
my @g = 1, 2, 3; @g.map(-> $x is rw, $y? { $x++ }).eager;
ck @g, [2, 2, 4], 'and a short chunk';
my @h = 1, 2; my @r = @h.map(-> $x { $x * 2 });
ck (@h, @r), ([1, 2], [2, 4]), 'a plain one copies';
my @i = 1, 2; @i.map(-> $x is copy { $x++ }).eager;
ck @i, [1, 2], 'and so does `is copy`';
my @j = 1, 2; @j.map({ $_++ }).eager;
ck @j, [2, 3], '$_, as before';

say 'PASS';
