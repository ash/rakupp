# BROKE: `my Int $x = ∞` was refused as `my Int $x = Inf` is, but its
# X::Syntax::Number::LiteralType carried `.value` Any and said "(∞)" where
# Rakudo carries Inf and says "(Inf)" (found 2026-10-23 by FatRoast's ops-v1,
# which spells Roast's S02-types/infinity.t `Inf` as `∞`; its `throws-like …,
# :value(∞)` failed).
#
# FIXED: the parser's literal-type check spells the `∞` literal `Inf`, so the
# value numifies (src/Parser.cpp, near X::Syntax::Number::LiteralType).
#
# Contract: exit 0 + last line PASS.

my @fail;
sub ck($got, $want, $what) { @fail.push("$what: got {$got.raku} want {$want.raku}") unless $got eqv $want }

for ｢my Int $x = ∞｣, ｢my Int $x = Inf｣ -> $code {
    try EVAL $code;
    ck($!.^name, 'X::Syntax::Number::LiteralType', "$code is refused");
    ck($!.value, Inf, "…with .value Inf");
    ck($!.vartype, Int, "…and .vartype Int");
    ck($!.message.contains('(Inf)'), True, "…and says (Inf)");
}
{ my Num $x = ∞; ck($x, Inf, 'my Num $x = ∞ is fine') }

if @fail { note $_ for @fail; say 'FAIL' }
else { say 'PASS' }
