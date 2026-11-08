# A plain `constant` in a block or an EVAL does not take the place of a CORE
# term outside it, as on Rakudo, where a bare name finds the lexical scopes
# and CORE before the package the constant is installed in: after
# `{ constant Less = 42 }`, `Less` was 42 everywhere, a sub's `Less` too, and
# so after `EVAL q[constant Less = 42; Less]`. A name CORE has no term for
# is still found through the package (`{ constant FOO = 1 }; FOO` is 1), and
# by its sigil'd name.
# Every line answers the same under Rakudo 2026.09.

use MONKEY-SEE-NO-EVAL;
sub ck($got, $want, $desc) {
    return if $got eqv $want;
    say "FAIL: $desc — got {$got.raku}, want {$want.raku}";
    exit 1;
}

sub mention { Less }
{ constant Less = 42; ck Less, 42, 'inside its block' }
ck Less, Order::Less, 'outside it';
ck mention(), Order::Less, "a sub's";
ck EVAL(q[constant Less = 42; Less]), 42, 'inside an EVAL';
ck Less, Order::Less, 'after it';
{ constant pi = 3; ck pi, 3, 'pi inside' }
ck pi, 3.141592653589793e0, 'pi outside';
{ constant e = 5 }
ck e, 2.718281828459045e0, 'e';
{ constant SIGINT = 5 }
ck SIGINT, Signal::SIGINT, 'an enum member';
{ constant FOO = 1 }
ck FOO, 1, 'a name CORE has not';
{ constant $c = 1 }
ck ::('$c'), 1, 'a sigiled one';

say 'PASS';
