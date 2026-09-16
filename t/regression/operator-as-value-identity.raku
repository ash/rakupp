# Regression: `&[R[~~]]` parses, and two mentions of one operator are one
# routine.
#
# Test::Run flips its comparison operator with
#
#     $op_std = &[R[~~]] if !$test.defined and $op_std eqv &[~~] | '~~';
#
# and needed both halves. The `&[…]` parser read tokens up to the FIRST `]`, so
# the nested metaop spelling left a stray bracket behind ("Confused"). And a
# built-in operator named as a value is synthesised fresh at every mention, so
# comparing two of them by pointer said False where Rakudo says True — the
# condition never matched and the operator was never flipped.
#
# Rakudo prints the nested spelling back as the name (`infix:<R[~~]>`); the
# behaviour is the same operator, and this engine normalises it to the flat
# name it uses internally.
#
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) {
    @fail.push("$what: got {$got.raku} want {$want.raku}") unless $got eqv $want
}

# the nested metaop spelling parses and runs
check &[R[~~]](3, 3),    True,  '&[R[~~]] applies the reversed smartmatch';
check &[R-](7, 4),         -3,  'the flat metaop spelling still works';
check &[+](3, 4),           7,  'and a plain operator';
check &[~~](3, 3),       True,  'and a plain smartmatch';
check &[Z+]((1,2),(3,4)).List, (4, 6), 'a Z metaop as a value';

# two mentions of one built-in operator are one routine
check (&[~~] === &[~~]), True,  'two mentions are identical';
check (&[+]  eqv &[+]),  True,  '…under eqv as well';
check (&[+]  === &[-]),  False, 'different operators are not';
check (&[+]  eqv &[-]),  False, '…under eqv either';

# a USER routine keeps pointer identity — this must not make everything equal
sub one() { 1 }
sub two() { 2 }
check (&one === &one), True,  'a user sub is itself';
check (&one === &two), False, '…and not another one';
check (&one eqv &two), False, 'eqv agrees';

# …and a Callable is still not its name
check (&[~~] eqv '~~'), False, 'an operator is not the string that spells it';

# the condition Test::Run actually writes
my $op = &[~~];
check ($op eqv &[~~] | '~~').Bool, True, 'the junction form the dist uses';

if @fail {
    .say for @fail;
    say "FAIL";
    exit 1;
}
say "PASS";
