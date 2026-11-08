# A native return type unboxes what comes back, as a native parameter binds
# it, interpreted and compiled: `sub rn(--> num) { "s" }` answered "s", where
# Rakudo says "This type cannot unbox to a native number: P6opaque, Str".
# `--> num` takes a Num or a NumStr (answering the Num), `--> int` an Int (a
# Bool is 1), `--> str` a Str or an allomorph; a type object cannot unbox, and Nil
# passes, as through any return type.
# Every line answers the same under Rakudo 2026.09.

sub ck($got, $want, $desc) {
    return if $got eqv $want;
    say "FAIL: $desc — got {$got.raku}, want {$want.raku}";
    exit 1;
}
sub msg(&c) { try { c(); return 'lived' }; $!.message }

sub rn(--> num) { "s" }
ck msg(&rn), 'This type cannot unbox to a native number: P6opaque, Str', '--> num refuses a Str';
sub ri(--> int) { "s" }
ck msg(&ri), 'This type cannot unbox to a native integer: P6opaque, Str', '--> int refuses a Str';
sub rs(--> str) { 5 }
ck msg(&rs), 'This type cannot unbox to a native string: P6opaque, Int', '--> str refuses an Int';
sub f(--> num) { 5 }
ck msg(&f), 'This type cannot unbox to a native number: P6opaque, Int', '--> num refuses an Int';
sub g(--> int) { 5e0 }
ck msg(&g), 'This type cannot unbox to a native integer: P6opaque, Num', '--> int refuses a Num';
sub h(--> int) { Int }
ck msg(&h), 'Cannot unbox a type object (Int) to an int.', '--> int refuses a type object';
sub k(--> int) { Nil }
ck k(), Nil, 'Nil passes';
sub m(--> num) { <1.5e0> }
ck m(), 1.5e0, 'a NumStr is its Num';
sub n(--> str) { <42> }
ck n(), <42>, 'an IntStr stays one';
sub o(--> int) { True }
ck o(), 1, 'a Bool is 1';
sub ok1(--> int) { 5 }
ck ok1(), 5, '--> int takes an Int';
sub ok2(--> num) { 1e0 }
ck ok2(), 1e0, '--> num takes a Num';
sub ok3(--> str) { 'a' }
ck ok3(), 'a', '--> str takes a Str';
sub q(--> int) { return 3 }
ck q(), 3, 'through return';
class X { method m(--> int) { 7 } }
ck X.new.m, 7, 'a method';
sub fib(int $n --> int) { $n < 2 ?? $n !! fib($n - 1) + fib($n - 2) }
ck fib(15), 610, 'a native routine (as before)';

say 'PASS';
