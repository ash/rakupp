# An assignment through a list literal's item answers to the variable's
# type, as `$a = v` does and as Rakudo's does: `my Int $a; ($a, 2)[0] =
# "l"` stored the Str, where Rakudo says "Type check failed in assignment
# to $a"; a `:D` and a coercion the same; and a Nil reset the variable to
# Any, where Rakudo resets it to its default, `(Int)` or an `is default`.
# Every line answers the same under Rakudo 2026.09.

sub ck($got, $want, $desc) {
    return if $got eqv $want;
    say "FAIL: $desc — got {$got.raku}, want {$want.raku}";
    exit 1;
}
sub msg(&c) { try c(); $!.message }

my Int $a = 1;
ck msg({ ($a, 2)[0] = "l" }), 'Type check failed in assignment to $a; expected Int but got Str ("l")', 'a type';
ck $a, 1, 'kept';
my Int:D $d = 1;
ck msg({ ($d, 2)[0] = Int }),
   'Type check failed in assignment to $d; expected Int:D but got Int (Int) (perhaps Nil was assigned to a :D which had no default?)', 'a :D';
my Int() $c; ($c, 2)[0] = "7";
ck $c, 7, 'a coercion';
($a, 2)[0] = Nil;
my $b is default(7) = 1; ($b, 2)[0] = Nil;
my $u = 1; ($u, 2)[0] = Nil;
ck ($a, $b, $u), (Int, 7, Any), 'a Nil resets to the default';
($a, 2)[0] = 5; my $q = 1; ($q, 2)[0] = "q";
ck ($a, $q), (5, "q"), 'what fits, as before';
ck (try { ($a, 2)[1] = 3; 1 }) // $!.^name, 'X::Assignment::RO', 'a value item, as before';

say 'PASS';
