# Regression (RAKUDO-ISSUES-PLAN R5; rakudo#6128, rakudo#1616): the R
# metaoperator composes over Z and X.
#
# Before the fix `(1,2) RZ (3,4)` was "Undefined routine 'RZ'", `[RZ] (1,2),
# (3,4)` was an array holding the name RZ, and `&[RZ,]` was "Unsupported
# operator". Rakudo reverses the operands of the zip or cross. `RZ+` lexed
# the `+` as a prefix of the right operand, and `ZR-` was an undeclared name.
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
# …with a symbolic inner operator, and Z or X over R
check 'RZ+', ((1,2) RZ+ (10,20)), '(11 22)';
check 'RZ~', ((1,2) RZ~ (3,4)), '(31 42)';
check 'RX~', ((1,2) RX~ (3,4)), '(31 32 41 42)';
check 'RZ=>', (<a b> RZ=> (1,2)), '(1 => a 2 => b)';
check '[RZ+]', ([RZ+] (1,2),(10,20)), '(11 22)';
check 'ZR-', ((1,2) ZR- (10,20)), '(9 18)';
check 'XR~', ((1,2) XR~ (3,4)), '(31 41 32 42)';
check 'plain Z unchanged', ([Z] (1,2),(3,4)), '((1 3) (2 4))';
check 'plain Z+ unchanged', ((1,2) Z+ (3,4)), '(4 6)';
check 'XZ+ unchanged', ((1,2) XZ+ (3,4)), '((4) (5) (5) (6))';
check 'plain X unchanged', ([X] (1,2),(3,4)), '((1 3) (1 4) (2 3) (2 4))';
role R { }
check '[R] is still an array of a type', [R].elems, '1';

if @fail {
    note "FAILED:\n" ~ @fail.map({ "  - $_" }).join("\n");
    exit 1;
}
say "PASS";
