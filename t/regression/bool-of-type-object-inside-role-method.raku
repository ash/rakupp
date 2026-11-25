# Regression: `?self` on a type object, inside a method of a role that does
# another role, with a `multi method Bool(R:D:)` beside it (#269).
#
# `role R does Iterable { multi method Bool(R:D:) { True }; method t { ?self } }`,
# then `C.t` on the class's type object: no candidate of the multi takes a
# type object, so the dispatcher deferred up the redispatch frame it found on
# top of the stack. That frame was `t`'s own, left there by the call that was
# running `t`, so dispatch went looking for `t` in Iterable and died "No such
# method 't' for invocant of type 'Iterable'". A multi now defers only up a
# frame for its own method name, and the type object answers False, Mu's Bool.
#
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) {
    @fail.push("$what: got {$got.raku} want {$want.raku}") unless $got eqv $want
}
role Роль does Iterable {
    multi method Bool(Роль:D:) { True }
    method истина { ?self }
    method so-self { so self }
    method if-self { self ?? 'yes' !! 'no' }
}
class Класс does Роль {}
check Класс.new.истина, True,  'an instance takes the role\'s candidate';
check (try Класс.истина) // 'died', False, '?self on the type object';
check (try Класс.so-self) // 'died', False, 'so self on the type object';
check (try Класс.if-self) // 'died', 'no', 'self ?? !! on the type object';
check Класс.new.if-self, 'yes', 'self ?? !! on an instance';
# without the role doing another role it already worked
role R2 { multi method Bool(R2:D:) { True }; method t { ?self } }
class C2 does R2 {}
check C2.t, False, 'a plain role';
# a class's own Bool still answers for its type object
class B { method Bool { True } }
check so B, True, 'a type object with its own Bool';

if @fail { .say for @fail; say 'FAIL'; exit 1 }
say 'PASS';
