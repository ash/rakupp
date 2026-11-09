# A list assignment into a variable declared before checks it as `$x = v`
# does, as Rakudo's does: `my Int $a = 1; ($a, $z) = "la", 2` stored "la", and
# so did a `:D`, a `where` and a coercion type (`my Int() $c` kept "5"). A
# declaring target checked its own type already.
# Every line answers the same under Rakudo 2026.09.

sub ck($got, $want, $desc) {
    return if $got eqv $want;
    say "FAIL: $desc — got {$got.raku}, want {$want.raku}";
    exit 1;
}
sub msg(&c) { try c(); $!.message }

my Int $a = 1; my $z;
ck msg({ ($a, $z) = "la", 2 }), 'Type check failed in assignment to $a; expected Int but got Str ("la")', 'a type';
ck $a, 1, 'kept';
ck msg({ ($a, my $y) = "lb", 2 }), 'Type check failed in assignment to $a; expected Int but got Str ("lb")', 'beside a declaration';
my Int:D $d = 1;
ck msg({ ($d, $z) = Int, 2 }),
   'Type check failed in assignment to $d; expected Int:D but got Int (Int) (perhaps Nil was assigned to a :D which had no default?)',
   'a smiley';
my $w where Int = 1;
ck msg({ ($w, $z) = "lc", 2 }).lines[0], 'Type check failed in assignment to $w; expected <anon> but got Str ("lc")', 'a where';
my Int() $c; ($c, my $q) = "5", 1;
ck $c, 5, 'a coercion';
my Int ($e, $f);
ck msg({ ($e, $f) = 1, "x" }), 'Type check failed in assignment to $f; expected Int but got Str ("x")', 'declared together, as before';
($a, my $r) = Nil, 2;
ck $a, Int, 'a Nil resets';
my ($s, $t) = 1, 2; ($s, $t) = $t, $s;
ck ($s, $t), (2, 1), 'a swap';

say 'PASS';
