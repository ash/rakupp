# Regression (RAKUDO-ISSUES-PLAN R5; rakudo#4757): the R metaoperator reverses
# an operator's associativity as well as its operands.
#
# Before the fix `2 R- 3 R- 4` grouped from the left, (2 R- 3) R- 4 = 3,
# where Rakudo groups `R-` from the right: 2 R- (3 R- 4) = (4 - 3) - 2 = -1,
# which is also what `[R-] 2, 3, 4` gives. A right-associative operator
# turns left (`R**`), and a comparison is not regrouped.
#
# Contract: exit 0 + last line PASS.
my @fail;
sub check(Str $what, $got, $want) {
    @fail.push("$what: got {$got.raku}, want {$want.raku}") unless $got eqv $want;
}
check '2 R- 3 R- 4 (rakudo#4757)', 2 R- 3 R- 4, -1;
check '…as [R-] does', [R-](2, 3, 4), -1;
check 'R~ from the right', 2 R~ 3 R~ 4, '432';
check 'R/ from the right', 10 R/ 2 R/ 40, 2.0;
check 'R** from the LEFT', 2 R** 3 R** 2, 512;
check 'R< is not regrouped', (1 R< 2 R< 3, 3 R< 2 R< 1), (False, False);
check 'tighter operators still bind first', (2 R- 3 + 1, 2 R- 3 * 2), (2, 4);

if @fail {
    note "FAILED:\n" ~ @fail.map({ "  - $_" }).join("\n");
    exit 1;
}
say "PASS";
