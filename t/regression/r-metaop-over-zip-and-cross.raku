# Regression (RAKUDO-ISSUES-PLAN R5; rakudo#6128, rakudo#1616): the R
# metaoperator composes over Z and X.
#
# Before the fix `(1,2) RZ (3,4)` was "Undefined routine 'RZ'", `[RZ] (1,2),
# (3,4)` was an array holding the name RZ, and `&[RZ,]` was "Unsupported
# operator". Rakudo reverses the operands of the zip or cross.
#
# Contract: exit 0 + last line PASS.
my @fail;
sub check(Str $what, $got, Str $want) {
    @fail.push("$what: got {$got.gist}, want $want") unless $got.gist eq $want;
}
check '(1,2) RZ (3,4) (rakudo#6128)', ((1,2) RZ (3,4)), '((3 1) (4 2))';
check '[RZ] (1,2),(3,4)', ([RZ] (1,2),(3,4)), '((3 1) (4 2))';
check '&[RZ,] (rakudo#1616)', &[RZ,]((1,2),(3,4)), '((3 1) (4 2))';
check '[RX]', ([RX] (1,2),(3,4)), '((3 1) (3 2) (4 1) (4 2))';
check 'RZcmp', ((1,2) RZcmp (2,1)), '(More Less)';
check 'plain Z unchanged', ([Z] (1,2),(3,4)), '((1 3) (2 4))';
check 'plain X unchanged', ([X] (1,2),(3,4)), '((1 3) (1 4) (2 3) (2 4))';
role R { }
check '[R] is still an array of a type', [R].elems, '1';

if @fail {
    note "FAILED:\n" ~ @fail.map({ "  - $_" }).join("\n");
    exit 1;
}
say "PASS";
