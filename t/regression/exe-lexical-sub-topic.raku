# Regression: a `sub` declared inside a block, compiled with --exe, has a `$_`
# of its own, as every routine does. The binary's lexical sub read and wrote
# the `$_` around it: `for 1, 2 -> $x { sub in-loop { $_ // 'U' }; in-loop() }`
# with the mainline's `$_` = 'outer' answered 'outer', where the interpreter
# and Rakudo answer 'U'.
# Every line answers the same under Rakudo 2026.09.
#
# Contract: exit 0 + last line PASS.
my @fail;
sub ck($got, $want, $what) { @fail.push("$what: got {$got.raku}") unless $got eqv $want }

$_ = 'outer';
for 1, 2 -> $x {
    sub in-loop { $_ // 'U' }
    ck in-loop(), 'U', 'a sub declared in a loop reads its own $_';
    last;
}
{
    my sub writes { $_ = 'w'; $_ }
    ck writes(), 'w', 'a lexical sub writes its own $_';
    ck $_, 'outer', "…and the block's is left alone";
    my sub topic-param($_) { $_ }
    ck topic-param('p'), 'p', 'a $_ parameter is the topic';
}

if @fail { .say for @fail; say 'FAIL'; exit 1 }
say 'PASS';
