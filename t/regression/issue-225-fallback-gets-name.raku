# Issue #225: a class's FALLBACK never saw `.name`. `class Wrapper { has $.o;
# method FALLBACK($n, |a) { $!o."$n"(|a) } }` answered every other name it
# lacked through FALLBACK, but `.name` was "No such method 'name' for invocant
# of type 'Wrapper'" — on 5.0.0 it worked. `.name` is no method of Mu or Any:
# the built-in arm that says so for `42.name` threw before the unknown-name
# path, which is where FALLBACK has run since that path moved out of the
# dispatcher (0cc5b2bd). On a user class the arm now hands the name to that
# path: a `handles *` delegation, a FALLBACK or a `.^add_fallback` takes it,
# and without one it is X::Method::NotFound as before.
#
# Contract: exit 0 + last line PASS. Every answer is Rakudo 2026.09's — run
# this file under `rakudo` too.
my @fail;
sub check($what, Mu $got, Mu $want) { @fail.push("$what: got {$got.raku}, want {$want.raku}") unless $got eqv $want }

# the issue's program
class Wrapper { has $.o; method FALLBACK($n, |a) { $!o."$n"(|a) } }
check 'the issue', Wrapper.new(o => class { has $.name = 1 }.new).name, 1;

class W { method FALLBACK($n, |a) { "fb:$n" ~ a.list.join(',') } }
check 'an instance', W.new.name, 'fb:name';
check 'the type object', W.name, 'fb:name';
check 'with arguments', W.new.name(1, 2), 'fb:name1,2';

# without a FALLBACK it is still no method
class Plain { }
check 'a plain instance', (try Plain.new.name) // $!.^name, 'X::Method::NotFound';
check 'a plain class', (try Plain.name) // $!.^name, 'X::Method::NotFound';
check '.? answers Nil', Plain.new.?name, Nil;
role R { }
check 'a role', (try R.name) // $!.^name, 'X::Method::NotFound';
check 'an Int', (try 42.name) // $!.^name, 'X::Method::NotFound';

# what answers before FALLBACK still does
class HasName { has $.name = 'mine'; method FALLBACK($n) { 'fb' } }
check 'an accessor', HasName.new.name, 'mine';
check 'a routine', (sub foo { }).name, 'foo';
check 'an attribute', HasName.^attributes[0].name, '$!name';
sub bar($x) { }
check 'a parameter', &bar.signature.params[0].name, '$x';

# …and the other ways a class takes an unknown name
class Target { method name { 'target' } }
class Del { has $.t handles * = Target.new }
check 'handles *', Del.new.name, 'target';
class Late { }
Late.^add_fallback(-> $, $n { $n eq 'name' }, -> $, $n { method { 'added' } });
check '.^add_fallback', Late.new.name, 'added';

.say for @fail;
say @fail ?? "FAIL ({+@fail})" !! 'PASS';
exit @fail ?? 1 !! 0;
