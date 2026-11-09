# A `:D` or `:U` refusal is worded as Rakudo's: the value it got with its
# `.raku`, and for an undefined one under `:D` "(perhaps Nil was assigned to
# a :D which had no default?)", a Nil being the type it resets to. `my Int:D
# $x = 1; $x = Nil` said "expected Int:D but got Nil", `$x = Int` "… but got
# Int", an element's "… but got Int", and a defined Str was shown bare.
# Every line answers the same under Rakudo 2026.09.

sub ck($got, $want, $desc) {
    return if $got eqv $want;
    say "FAIL: $desc — got {$got.raku}, want {$want.raku}";
    exit 1;
}
sub msg(&c) { try c(); $!.message }
my $hint = '(perhaps Nil was assigned to a :D which had no default?)';

my Int:D $x = 1;
ck msg({ $x = Nil }), "Type check failed in assignment to \$x; expected Int:D but got Int (Int) $hint", 'a scalar given Nil';
ck msg({ $x = Int }), "Type check failed in assignment to \$x; expected Int:D but got Int (Int) $hint", 'and a type object';
ck (try { $x = Nil; 1 }) // ($!.got.^name, $!.expected.^name), ('Int', 'Int:D'), 'its attributes';
my Str:U $u;
ck msg({ $u = "s" }), 'Type check failed in assignment to $u; expected Str:U but got Str ("s")', 'a :U given a value';
my Int:D @d = 1;
ck msg({ @d[0] = Int }), "Type check failed for an element of \@d; expected Int:D but got Int (Int) $hint", 'an element';
my Int:U @e;
ck msg({ @e[0] = 5 }), 'Type check failed for an element of @e; expected Int:U but got Int (5)', 'a :U element';
my Int:D $y is default(3) = 1; $y = Nil;
ck $y, 3, 'a default takes the Nil, as before';

say 'PASS';
