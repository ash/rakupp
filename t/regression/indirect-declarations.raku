# Regression: indirect DECLARATIONS did nothing — `class ::(name)` parsed
# without error but registered no type (the name expression was never
# evaluated), `sub ::(name)` likewise, and indirect method names were lost.
# Before the ::() lookup strictness landed, the class case even half-passed
# by accident: the LOOKUP of the undeclared name minted a stub whose .^name
# echoed the name back.
#
# Now ClassDecl/SubDecl carry a nameExpr, evaluated when the declaration
# RUNS (declarations are run-time here, so an indirect name is only an
# expression slot). Hoisting never pre-registers an indirect sub — the sub
# exists from its statement on, which is also the semantic constraint: the
# name's inputs must already have values. Native codegen refuses these
# (--exe falls back to AOT); the AOT path evaluates them like the
# interpreter. Roast: S02-names/indirect.t 0/10 -> 10/10.
#
# Every check verified against Rakudo (this file runs on both engines).
#
# Contract: exit 0 + last line PASS.
my @fail;

sub check($got, $want, $desc) {
    @fail.push("$desc: got {$got.raku}, want {$want.raku}") unless $got eqv $want;
}

my constant type-name = 'IndirectType';
class ::(type-name) {
    method f() { 42 }
}
check IndirectType.f, 42, 'an indirectly declared class works by its computed name';
check ::(type-name).^name, 'IndirectType', 'and the runtime lookup agrees on the name';

my constant sub-name = 'indirect-sub';
sub ::(sub-name) ($x) { $x + 40 }
check indirect-sub(2), 42, 'an indirectly declared sub is callable by its computed name';
check &indirect-sub.name, 'indirect-sub', 'and it knows its own name';

class IndirectMethods {
    method ::('plain-indirect') { 'pi' }
    method ::('with space') { 'ws' }
}
check IndirectMethods.plain-indirect, 'pi', 'an indirect method name dispatches normally';
check IndirectMethods."with space"(), 'ws', 'a spaced method name dispatches via the quoted call';

if @fail {
    .say for @fail;
    say "FAIL";
    exit 1;
}
say "PASS";
