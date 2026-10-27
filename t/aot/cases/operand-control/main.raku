# `next`, `last` and `return` as operands in native module routines (t/aot/run.raku).
use Aot::OperandCtl;

say field({a => 1}, 'a');
say field({a => 1}, 'b');
say first-word('hello world');
say first-word('');
say tail-default('set');
say tail-default(Nil);
say defined-values([1, Any, 3, Nil]);
say until-empty(['a', 'b', '', 'c']);
say pick-branch(4);
say pick-branch(3);
say from-block([1, 2]);
say from-block([1, Any]);
