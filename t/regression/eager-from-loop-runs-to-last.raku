# Regression (RAKUDO-ISSUES-PLAN R5; rakudo#1692): `eager` runs an
# unconditioned `Seq.from-loop` until its body says `last`, and a body whose
# value is Empty (a false `last if …`) adds nothing.
#
# Before the fix `eager Seq.from-loop: { $i++; last if $i > 5; $i }` died
# "Cannot eager a lazy list", and `eager Seq.from-loop: {$i++; last if $i >
# 5}, {1}` answered five empty lists, where Rakudo answers ().
#
# Contract: exit 0 + last line PASS.
my @fail;
sub check(Str $what, $got, $want) {
    @fail.push("$what: got {$got.raku}, want {$want.raku}") unless $got eqv $want;
}
my $i = 0;
check 'eager runs to the last (rakudo#1692)', (eager Seq.from-loop: { $i++; last if $i > 5; $i }).List, (1, 2, 3, 4, 5);
$i = 0;
check '.eager', Seq.from-loop({ $i++; last if $i > 5; $i }).eager.List, (1, 2, 3, 4, 5);
$i = 0;
check 'still lazy before', Seq.from-loop({ $i++; last if $i > 5; $i }).is-lazy, True;
my int $j;
check 'an Empty body adds nothing', (eager Seq.from-loop: {$j++; last if $j > 5}, {1}).List, ();
$i = 0;
check 'a condition, as before', Seq.from-loop({ $i++ }, { $i < 3 }).List, (0, 1, 2);

if @fail {
    note "FAILED:\n" ~ @fail.map({ "  - $_" }).join("\n");
    exit 1;
}
say "PASS";
