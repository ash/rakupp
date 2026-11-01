# A module that imports no operators: its `eqv`, and its `where` clauses', are CORE's.
use RakuppScopeNode;
unit module RakuppScopeNoOps;
sub in-module($a, $b) is export { $a eqv $b }
sub reduced(*@x) is export { [eqv] @x }
multi pick-same($a, $b where { $a eqv $b }) is export { "same" }
multi pick-same($a, $b) is export { "general" }
role Optimizer is export {
    multi method optimize(:%when!, :$else! where { [eqv] |%when.values, $else }) { "else" }
    multi method optimize(:%when!, :$else) { "general" }
}
sub run-it(&f) is export { f() }
