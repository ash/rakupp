# Regression: hyper metaop over a compound assignment — `@a <<+=>> n` applies
# the base op elementwise and mutates the array in place. Previously died with
# "Unsupported operator '+='" (the evalBinary hyper path handed the inner op
# straight to applyBinOp/applyArith, which know no assignment ops).
# Reported 2026-07-22 (user snippet: @data <<+=>> 2019).

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

say 'PASS' if $ok;
