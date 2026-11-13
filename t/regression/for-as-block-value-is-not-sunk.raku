# A `for` loop that is its block's value (the block's last statement) does
# not sink its iterations, as in Rakudo (rakudo#1360): `{ Failure.new for ^5 }()`
# threw the first Failure, where Rakudo returns Nil and nothing explodes.
# Anywhere else a loop sinks each iteration, as before.

my $fails = 0;
sub check($got, $want, $what) {
    if $got ne $want { say "FAIL: $what: got $got, want $want"; $fails++ }
}

{ Failure.new for ^5 }();
check 'reached', 'reached', 'the modifier form as a block\'s value (rakudo#1360)';
sub g { Failure.new for ^2 }
check (try { g(); 'ok' }) // 'threw', 'ok', '…as a routine\'s last statement';
sub k { for ^2 { Failure.new } }
check (try { k(); 'ok' }) // 'threw', 'ok', 'the block form too';
sub f { Failure.new for ^2; 1 }
check (try { f(); 'ok' }) // 'threw', 'threw', 'not the last statement: sunk';
check (try { for ^2 { Failure.new }; 'ok' }) // 'threw', 'threw', 'a loop statement in the middle: sunk';
sub v { 42 for ^2 }
check v().raku, 'Nil', 'the block\'s value is Nil';
my @a = (Failure.new for ^2);
check @a.elems, '2', 'a loop in an expression keeps its values';

say $fails ?? "FAIL ($fails)" !! 'PASS';
exit $fails ?? 1 !! 0;
