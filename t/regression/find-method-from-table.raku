# `.^find_method` and `.can` on the built-in types no sentinel instance can
# stand for (Promise, Channel, Supply, Lock, Semaphore, Thread and the code
# types) answer from a table of Rakudo's methods for each, and Any's and Mu's:
# `Promise.^find_method('nope')` was a method object for any name. A role
# answers `.can` from its own methods and Any's: `Cool.can('chars')` was
# empty, and `Numeric.can('abs')` stays empty, as on Rakudo.
# Every line answers the same under Rakudo 2026.09.

sub ck($got, $want, $desc) {
    return if $got eqv $want;
    say "FAIL: $desc — got {$got.raku}, want {$want.raku}";
    exit 1;
}

for Promise, Channel, Supply, Lock, Semaphore, Thread, Sub, Block, Code, Method -> \T {
    ck (so(T.^find_method('nope')), so(T.^find_method('WHAT')), so(T.^find_method('say'))),
       (False, True, True), T.^name;
}
ck (so(Promise.^find_method('then')), so(Promise.^find_method('result')), so(Thread.^find_method('run'))),
   (True, True, True), 'their own methods';
ck Promise.^find_method('then').name, 'then', 'a method object';
ck (so(Promise.can('then')), so(Channel.can('receive')), so(Lock.can('protect')), so(Sub.can('signature'))),
   (True, True, True, True), '.can';
ck so(Promise.can('nope')), False, '.can no';
ck (so(Cool.can('chars')), so(Cool.can('nope')), so(Numeric.can('abs')), so(Real.can('floor'))),
   (True, False, False, True), 'roles';
ck (so(Positional.can('elems')), so(Stringy.can('chars')), so(Iterable.can('map'))), (True, False, True),
   'roles and Any';
ck Cool.can('chars')[0].name, 'chars', "a role's method";

say 'PASS';
