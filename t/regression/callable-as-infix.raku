# Regression: `A [&f] B` — a Callable used as an infix operator.
#
# Terminal::UI picks `&infix:<+>` or `&infix:<->` at run time and applies it as
# `$current [&($op)] 1`. The bracketed-infix parser only recognised an
# OPERATOR's own spelling (`[+]`, `[max]`, `[R//]=`), so every `&` form died at
# "expected ) (got '['".
#
# Rakudo gives it additive precedence, left-associative, which is what the two
# mixed-operator checks below pin down.
#
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) {
    @fail.push("$what: got {$got.raku} want {$want.raku}") unless $got eqv $want
}

sub mul($a, $b) { $a * $b }

check (3 [&mul] 4),           12, 'a named sub as an infix';
check (3 [&infix:<+>] 4),      7, 'a built-in operator by its full name';
my $op = &infix:<+>;
check (3 [&($op)] 4),          7, 'a Callable held in a variable';
my $sub = &mul;
check (3 [&($sub)] 4),        12, '…and a user sub the same way';

# precedence: additive, left-associative
check (1 + 2 [&mul] 3),        9, '`1 + 2 [&mul] 3` groups as (1+2) [&mul] 3';
check (2 [&mul] 3 + 1),        7, '`2 [&mul] 3 + 1` groups as (2 [&mul] 3) + 1';

# the forms it sits beside must keep working
check (3 [+] 4),               7, 'the plain bracketed operator';
check (3 [max] 4),             4, 'the word form';
my %h;
%h<x> [R//]= 5;
check %h<x>,                   5, 'the metaop-assignment form';
my @a = 1, 2, 3;
check @a[1],                   2, 'and an ordinary subscript is not an infix';

if @fail {
    .say for @fail;
    say "FAIL";
    exit 1;
}
say "PASS";
