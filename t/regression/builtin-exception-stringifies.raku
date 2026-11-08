# `~` and `sprintf('%s')` of a built-in exception made with `.new` are its
# message, as `.Str` already was: `~X::AdHoc.new(:payload<boom>)` was "" with
# a "Use of uninitialized value" warning, and its sprintf `X::AdHoc<4377956096>`.
# An exception with no message says "Something went wrong in (E)", as
# Rakudo's Exception.Str does.
# Every line answers the same under Rakudo 2026.09.

sub ck($got, $want, $desc) {
    return if $got eqv $want;
    say "FAIL: $desc — got {$got.raku}, want {$want.raku}";
    exit 1;
}
sub all-four($e) { (~$e, $e.Str, sprintf('%s', $e), "$e") }

ck all-four(X::AdHoc.new(:payload<boom>)), ('boom' xx 4).List, 'X::AdHoc with a payload';
ck all-four(X::AdHoc.new), ('Unexplained error' xx 4).List, 'X::AdHoc with none';
ck all-four(Exception.new), ('Something went wrong in (Exception)' xx 4).List, 'Exception.new';
ck all-four(X::TypeCheck::Assignment.new(:symbol<$x>, :got(1), :expected(Str))),
   ('Type check failed in assignment to $x; expected Str but got Int (1)' xx 4).List,
   'a built-in with a computed message';
ck all-four(X::NYI.new(:feature<f>)), ('f not yet implemented. Sorry.' xx 4).List, 'X::NYI';

class MyE is Exception { method message { 'mine' } }
ck ~MyE.new, 'mine', 'a user message method (as before)';
class MyA is X::AdHoc { }
ck ~MyA.new(:payload<pp>), 'pp', 'a user class deriving X::AdHoc';
class E2 is Exception { }
ck ~E2.new, 'Something went wrong in (E2)', 'a user class with no message';
ck E2.new.Str, 'Something went wrong in (E2)', '…and its .Str';
class E3 is Exception { has $.message }
ck ~E3.new(:message<m3>), 'm3', 'a message attribute';
ck ~E3.new, 'Something went wrong in (E3)', 'an empty message attribute';
ck sprintf('%s|%s', E2.new, E3.new), 'Something went wrong in (E2)|Something went wrong in (E3)', 'sprintf';
try die 'x';
ck ~$!, 'x', 'a die (as before)';

say 'PASS';
