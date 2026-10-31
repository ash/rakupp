# Regression: a module does not see the operators the main program imports or
# declares (issue #77, the empty WHEN in Red's CASE).
#
# The program's top scope is global_, and every module's scope hung from it,
# so `use Red;` in a program put Red::Operators' `multi infix:<eqv>(Red::AST,
# Red::AST)` in front of Red's own Case optimizer, a file that imports no
# operators. Its `where { [eqv] |%when.values, $else }` built an AST — truthy —
# instead of asking CORE's eqv, the candidate for "every branch is the else"
# took a one-branch map, and `.grep(…).map(*.id)` sent SQLite
# `CASE WHEN  THEN "bla".id END` ("near THEN: syntax error"), about twenty of
# Red's test files with it. In Rakudo a module sees its own imports and CORE's.

use lib $?FILE.IO.parent.add('lib').Str;
use RakuppScopeNode;
use RakuppScopeOps;        # the PROGRAM imports the custom eqv; RakuppScopeNoOps does not
use RakuppScopeNoOps;

sub ck($got, $want, $desc) {
    return if $got eqv $want;
    say "FAIL: $desc — got {$got.raku}, want {$want.raku}";
    exit 1;
}

class Leaf does RakuppScopeNode { }
class Thing does Optimizer { }

ck in-module(Leaf.new, RakuppScopeNode), False, 'a module\'s own eqv is CORE\'s';
ck reduced(Leaf.new, RakuppScopeNode), False, '…and its [eqv]';
ck pick-same(Leaf.new, RakuppScopeNode), 'general', 'a where clause of the module\'s';
my %when = a => Leaf.new;
ck Thing.optimize(:%when, :else(RakuppScopeNode)), 'general', 'Red\'s Case optimizer shape';

# the program still has what it imported and declared
ck (Leaf.new eqv RakuppScopeNode), 'custom', 'the program\'s eqv is the import';
sub mine { Leaf.new eqv RakuppScopeNode }
ck mine(), 'custom', '…in its own subs';
ck run-it({ Leaf.new eqv RakuppScopeNode }), 'custom', '…and in a closure a module runs';
multi infix:<~~~>($a, $b) { "tilde" }
ck (1 ~~~ 2), 'tilde', 'an operator the program declares';

say 'PASS';
