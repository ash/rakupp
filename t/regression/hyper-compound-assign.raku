# Regression: hyper metaop over a compound assignment — `@a <<+=>> n` applies
# the base op elementwise and mutates the array in place. Previously died with
# "Unsupported operator '+='" (the evalBinary hyper path handed the inner op
# straight to applyBinOp/applyArith, which know no assignment ops).
# Reported 2026-07-22 (user snippet: @data <<+=>> 2019).
#
# Also: `($t, $y) »+=« (a, b)` must write through the scalars. The pair
# evaluates to a fresh List of copies, so mutating that List is a no-op
# on $t/$y (runge-kutta.raku would hang at t=0).

my $ok = True;

my @data = 1, 2, 3;
@data <<+=>> 2019;
unless @data eqv [2020, 2021, 2022] {
    say "FAIL: <<+=>> scalar rhs: {@data.raku}";
    $ok = False;
}

my @a = 1, 2, 3;
@a «+=» (10, 20, 30);
unless @a eqv [11, 22, 33] {
    say "FAIL: «+=» list rhs: {@a.raku}";
    $ok = False;
}

my @s = <a b>;
@s <<~=>> '!';
unless @s eqv ['a!', 'b!'] {
    say "FAIL: <<~=>> string append: {@s.raku}";
    $ok = False;
}

# plain (non-assign) hyper must NOT mutate its left operand
my @b = 4, 5, 6;
my @r = @b <<+>> 1;
unless @r eqv [5, 6, 7] && @b eqv [4, 5, 6] {
    say "FAIL: plain <<+>> mutated or miscomputed: {@b.raku} / {@r.raku}";
    $ok = False;
}

# parenthesized scalars: the List is a fresh copy, so mutation must write
# through each item's container (runge-kutta.raku: `($t, $y) »+=« …`)
my ($t, $y) = (0, 1);
($t, $y) »+=« (0.1, 0.2);
unless $t == 0.1 && $y == 1.2 {
    say "FAIL: list-of-scalars »+=«: $t $y";
    $ok = False;
}

say 'PASS' if $ok;
