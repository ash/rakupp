# A variable's `where` clause is asked of a store through an alias of it,
# as Rakudo asks it: `my $wh where Int = 1; my $w := $wh; $w = "q"` stored
# the Str, and so did `with $x { $_ = -1 }` and an `is rw` parameter, where
# Rakudo says "Type check failed in assignment to $x; expected <anon>". The
# clause runs in the scope that declared it; what it takes lands as before.
# Every line answers the same under Rakudo 2026.09.

sub ck($got, $want, $desc) {
    return if $got eqv $want;
    say "FAIL: $desc — got {$got.raku}, want {$want.raku}";
    exit 1;
}
sub msg(&c) { try c(); $!.message.lines[0] }

my $wh where Int = 1;
my $w := $wh;
ck msg({ $w = "q" }), 'Type check failed in assignment to $wh; expected <anon> but got Str ("q")', 'a bind';
my $x where * > 0 = 1;
ck msg({ with $x { $_ = -1 } }), 'Type check failed in assignment to $x; expected <anon> but got Int (-1)', "with's topic";
sub f($p is rw) { $p = -2 }
ck msg({ f($x) }), 'Type check failed in assignment to $x; expected <anon> but got Int (-2)', 'an is rw parameter';
my $lim = 10; my $y where * < $lim = 1; my $yy := $y;
ck msg({ $yy = 11 }), 'Type check failed in assignment to $y; expected <anon> but got Int (11)', 'in the scope that declared it';
with $x { $_ = 5 }; my $xa := $x; $xa = 7; $yy = 9;
ck ($x, $y, $wh), (7, 9, 1), 'what it takes, as before';

say 'PASS';
