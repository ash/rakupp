# A typed variable with a `where` clause is an anonymous subset of its type,
# as Rakudo has it: what the type refuses is refused as that subset,
# "expected <anon>", and the clause is never run on it. `my Int $x where *
# > 0; $x = "s"` died "Cannot convert string to number", the clause run on
# the Str, and `my Str $s where *.chars < 3; $s = 12` said "expected Str";
# so did a store through an alias. What the clause refuses, and what both
# take, are as before.
# Every line answers the same under Rakudo 2026.09.

sub ck($got, $want, $desc) {
    return if $got eqv $want;
    say "FAIL: $desc — got {$got.raku}, want {$want.raku}";
    exit 1;
}
sub msg(&c) { (try { c(); 1 }) // "{$!.^name}: {$!.message.lines[0]}" }

my Int $x where * > 0 = 1;
ck msg({ $x = "s" }), 'X::TypeCheck::Assignment: Type check failed in assignment to $x; expected <anon> but got Str ("s")', 'the type refuses';
my Str $s where *.chars < 3 = "a";
ck msg({ $s = 12 }), 'X::TypeCheck::Assignment: Type check failed in assignment to $s; expected <anon> but got Int (12)', 'another';
my $w := $x;
ck msg({ $w = "u" }), 'X::TypeCheck::Assignment: Type check failed in assignment to $x; expected <anon> but got Str ("u")', 'through an alias';
ck msg({ with $x { $_ = "t" } }), 'X::TypeCheck::Assignment: Type check failed in assignment to $x; expected <anon> but got Str ("t")', "with's topic";
ck msg({ $x = -1 }), 'X::TypeCheck::Assignment: Type check failed in assignment to $x; expected <anon> but got Int (-1)', 'the clause, as before';
$x = 5; $w = 6; $s = "ab";
ck ($x, $s), (6, "ab"), 'what both take';

say 'PASS';
