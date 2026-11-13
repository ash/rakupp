# Regression (RAKUDO-ISSUES-PLAN R5; rakudo#5951): a hyper operator reduced
# over ONE operand is its inner operator's one-argument form, and the
# one-argument `~` joins a list.
#
# Before the fix `[»+«] [[1,2],]` handed back [1 2], where Rakudo numifies
# the list (2), as `[+]` of one Array does. `infix:<~>(<a b>)` and
# `[~] $(<a b>)` stringified the list as "a b", where Rakudo joins it, "ab".
#
# Contract: exit 0 + last line PASS.
my @fail;
sub check(Str $what, $got, $want) {
    @fail.push("$what: got {$got.raku}, want {$want.raku}") unless $got eqv $want;
}
check '[»+«] over one (rakudo#5951)', ([»+«] [[1,2],]), 2;
check '[»*«] over one', ([»*«] [[2,3],]), 2;
check '[»~«] over one', ([»~«] [<a b>,]), 'ab';
check 'two operands, as before', ([»+«] [[1,2],[3,4]]), [4, 6];
my @a = [1,2,3], [4,5,6];
check 'the issue\'s shape', ([»+«] @a), [5, 7, 9];
check 'infix:<~>(<a b>)', infix:<~>(<a b>), 'ab';
check '[~] $(<a b>)', ([~] $(<a b>)), 'ab';
check 'one scalar stays', (infix:<~>(5), [~] 1), ('5', '1');

if @fail {
    note "FAILED:\n" ~ @fail.map({ "  - $_" }).join("\n");
    exit 1;
}
say "PASS";
