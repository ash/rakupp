# `andthen`, `orelse` and `notandthen` hand a typed variable's own container
# to their `$_`, as `with` and `given` do and as Rakudo's do: `my Int $x =
# 1; $x andthen $_ = "s"` stored the Str, where Rakudo refuses it, and so
# did a block on the right and a `:D`. What fits lands as before, down a
# chain too.
# Every line answers the same under Rakudo 2026.09.

sub ck($got, $want, $desc) {
    return if $got eqv $want;
    say "FAIL: $desc — got {$got.raku}, want {$want.raku}";
    exit 1;
}
sub msg(&c) { try c(); $!.message }

my Int $x = 1;
ck msg({ $x andthen $_ = "s" }), 'Type check failed in assignment to $x; expected Int but got Str ("s")', 'andthen';
ck msg({ $x andthen { $_ = "s" } }), 'Type check failed in assignment to $x; expected Int but got Str ("s")', 'a block';
ck msg({ $x andthen $_ = 5 andthen $_ = "s" }), 'Type check failed in assignment to $x; expected Int but got Str ("s")', 'a chain';
ck $x, 5, 'what fit landed';
my Int $u;
ck msg({ $u orelse $_ = "s" }), 'Type check failed in assignment to $u; expected Int but got Str ("s")', 'orelse';
my Int:D $d = 1;
ck msg({ $d andthen $_ = Int }),
   'Type check failed in assignment to $d; expected Int:D but got Int (Int) (perhaps Nil was assigned to a :D which had no default?)', 'a :D';

$x andthen $_++; $x andthen { $_ = 9 };
my Int $v; $v notandthen $_ = 4;
my Int $w; $w orelse .=new andthen .=new: 43;
my $y = 1; $y andthen $_ = "s";
ck ($x, $v, $w, $y), (9, 4, 43, "s"), 'as before';

say 'PASS';
