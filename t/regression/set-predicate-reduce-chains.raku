# Regression (RAKUDO-ISSUES-PLAN R5, found in R2): a set relation reduces as
# a chain, as a comparison does.
#
# Before the fix `[⊆] ()` was Any and `[∈] (1,)` was 1: the reduction folded
# them like a set operator. Rakudo chains them, so nothing or one operand is
# vacuously True and three operands test each adjacent pair.
#
# Contract: exit 0 + last line PASS.
my @fail;
sub check(Str $what, $got, $want) {
    @fail.push("$what: got {$got.raku}, want {$want.raku}") unless $got eqv $want;
}
check 'nothing', ([⊆](), [(<=)](), [≡](), [∈]()), (True, True, True, True);
check 'one operand', ([⊆](set(1)), [∈](1), [⊃](set(1))), (True, True, True);
check 'three that hold', ([⊆] set(1,2), set(1,2,3), set(1,2,3,4)), True;
check 'three where one pair fails', ([⊆] set(1,2), set(1), set(1,2,3)), False;
check '∈ chains', ([∈] 1, (1, 2)), True;
check '⊂ is strict', ([⊂] set(1), set(1,2), set(1,2)), False;
check '≡', ([≡] set(1), set(1), set(1)), True;

if @fail {
    note "FAILED:\n" ~ @fail.map({ "  - $_" }).join("\n");
    exit 1;
}
say "PASS";
