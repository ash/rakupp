# An `eqv` for RakuppScopeNode, as Red::Operators exports one for Red::AST.
use RakuppScopeNode;
unit module RakuppScopeOps;
multi infix:<eqv>(RakuppScopeNode $a, RakuppScopeNode $b) is export { "custom" }
