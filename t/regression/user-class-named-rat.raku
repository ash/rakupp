# Regression (rakudo#5537): a user class may be called Rat. `class Rat is
# Quadruped { has $.head = 1 }; Rat.new` took SIGBUS in Raku++: a class that
# does Rational gets a `.new` that boxes a real Rat, and whether a class does
# Rational was also asked by its NAME, which for a class called Rat answered
# as the core Rat. Its `.new` then built the box with `Rat.new`, which was
# itself, until the stack ran out. Rakudo makes an ordinary object.
#
# Contract: exit 0 + last line PASS.
my @fail;
sub same(Str $what, $got, $want) {
    @fail.push("$what: got {$got.raku}, want {$want.raku}") unless $got eqv $want;
}

class Quadruped { has $.legs = 4 }
{
    my class Rat { has $.head = 1 }
    my $r = Rat.new;
    same 'a class named Rat: its attribute', $r.head, 1;
    same 'a class named Rat: its own type', $r.^name, 'Rat';
}
{
    my class Rat is Quadruped { has $.head = 2 }
    my $r = Rat.new;
    same 'Rat is Quadruped: inherited attribute', $r.legs, 4;
    same 'Rat is Quadruped: own attribute', $r.head, 2;
    same 'Rat is Quadruped: parents', $r.^parents.map(*.^name).List, ('Quadruped',);
}
{
    my class FatRat { has $.x = 3 }
    same 'a class named FatRat: no crash', (try FatRat.new) ~~ FatRat, True;
}
same 'the core Rat is untouched', 1.5.WHAT.^name, 'Rat';
same 'the core Rat still builds', Rat.new(3, 4), 0.75;
my class MyRat does Rational[Int, Int] { }
same 'a class doing Rational still boxes a ratio', MyRat.new(1, 4) + 0, 0.25;

if @fail {
    note "FAILED:\n" ~ @fail.map({ "  - $_" }).join("\n");
    exit 1;
}
say "PASS";
