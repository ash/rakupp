# Regression (RAKUDO-ISSUES-PLAN R5; rakudo#1447): `roundrobin` follows the
# single-argument rule.
#
# Before the fix `roundrobin ((1, 2, 3), (4, 5), (6, 7, 8, 9))` took the one
# argument as one list to take turns from, and answered nine rounds of one.
# Rakudo spreads a lone list (or Range) into its elements, which are the
# lists it takes turns between. An Array's elements are containers, each one
# item.
#
# Contract: exit 0 + last line PASS.
my @fail;
sub check(Str $what, $got, Str $want) {
    @fail.push("$what: got {$got.gist}, want $want") unless $got.gist eq $want;
}
check 'one list of lists (rakudo#1447)', (roundrobin ((1, 2, 3), (4, 5), (6, 7, 8, 9))), '((1 4 6) (2 5 7) (3 8) (9))';
check 'several lists, as before', (roundrobin (1,2,3), (4,5)), '((1 4) (2 5) (3))';
check 'one flat list', (roundrobin (1,2,3)), '((1 2 3))';
check 'a Range', (roundrobin 1..3), '((1 2 3))';
my @a = (1,2), (3,4);
check 'an Array: its elements are items', (roundrobin @a), '(((1 2) (3 4)))';
check ':slip', (roundrobin ((1,2),(3,4)), :slip), '(1 3 2 4)';
check 'an itemized list is one item', (roundrobin $((1,2),(3,4))), '((((1 2) (3 4))))';

if @fail {
    note "FAILED:\n" ~ @fail.map({ "  - $_" }).join("\n");
    exit 1;
}
say "PASS";
