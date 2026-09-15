# A bare type inside a list declaration is an anonymous slot: the position is
# skipped and nothing is declared for it. `my ($q1, Any, $q2) = quartiles(@x)`
# is how Stats reads the first and third quartile out of three, and the
# declaration parser insisted on a variable in every slot — "expected variable
# in declaration (got 'Any')" — so the module would not compile at all.
# (Rakudo also TYPE-CHECKS what lands in such a slot; here the slot simply
# skips the position, so only the skipping is asserted below.)
use Test;
plan 4;

my ($a, Any, $b) = 1, 2, 3;
is $a, 1,                       'the slot before the placeholder binds';
is $b, 3,                       '…and the one after it skips a position';

my ($first, Int, Str, $last) = 'w', 42, 'y', 'z';
is $first, 'w',                 'several placeholders in a row';
is $last,  'z',                 '…each consume exactly one position';
