# Regression (RAKUDO-ISSUES-PLAN R5; rakudo#1399, rakudo#1398): `EXPR for
# 1..*` runs for as long as the range does, which is forever, as the block
# form `for 1..* { … }` does.
#
# Before the fix the statement-modifier form walked an ENDLESS range through
# its flattened prefix of 10,000 elements and then stopped: `.sink for 1..*`
# returned at once, and `$n++ for 1..*` left $n at 10000.
#
# Contract: exit 0 + last line PASS.
my @fail;
sub check(Str $what, $got, $want) {
    @fail.push("$what: got {$got.raku}, want {$want.raku}") unless $got eqv $want;
}
my $p = start { .sink for 1..*; 'ended' };
await Promise.anyof($p, Promise.in(1));
check '.sink for 1..* is still running after a second (rakudo#1399)', $p.status, Planned;

my $n = 0;
($n++ == 20000 and last) for 1..*;
check 'past the old 10,000', $n, 20001;
my $m = 0;
$m += $_ for ^10;
check 'a finite range, as before', $m, 45;
check 'a list of results', (.Str for 1..3).List, ('1', '2', '3');

if @fail {
    note "FAILED:\n" ~ @fail.map({ "  - $_" }).join("\n");
    exit 1;
}
say "PASS";
exit 0;   # (the endless loop above is left running in its thread)
