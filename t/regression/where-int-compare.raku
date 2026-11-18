# Regression guard: `where * > 0` of a plain Int, compared without the call.
#
# A `where` that is a WhateverCode comparing its argument with an Int literal
# (`* > 0`, `* == 1`, `0 <= *`) was called as a Code for every argument it
# judged: a frame, the curry's own argument list, applyArith. That call was a
# ninth of tools/bench/multiwhere-steady.raku. Interpreter::whereQuick
# (InterpreterCalls.cpp) answers it as a comparison of two machine integers,
# for a plain Int argument only. What must stay true: every other argument
# still asks the Code — a big Int, a Num, a Rat, a Str, an enum value, a Bool,
# a native — and so does a `where` under an operator the program has
# redefined; a `where` on an only method or sub still refuses an argument it
# fails, with the same error; and the multi candidates are chosen as before.

my @got;
sub o(**@a) { @got.push: @a.map(*.gist).join }
enum Level <low mid high>;
class K {
    multi method m(Int $x where * > 0)  { 'pos' }
    multi method m(Int $x where * == 0) { 'zero' }
    multi method m(Int $x)              { 'neg' }
    multi method n($x where 5 <= *)     { 'big' }
    multi method n($x where * != 3)     { 'not3' }
    multi method n($x)                  { 'three' }
    multi method r($x where 10 > *)     { 'under' }
    multi method r($x where * >= 10)    { 'from10' }
    method only(Int $x where * > 0)     { $x * 2 }
    method two(Int $a where * >= 1, Int $b where * < 5) { "$a,$b" }
}
my $k = K.new;
for ^3 {
    o (-1, 0, 1, 2 ** 70, -(2 ** 70)).map({ $k.m($_) }).join(' ');
    o (3, 4, 5, 6, 2.5, '7', 5e0, 2 ** 70, 11/2).map({ $k.n($_) }).join(' ');
    o (9, 10, 11, 9.99, '10').map({ $k.r($_) }).join(' ');
    o $k.only(3), ' ', (try $k.only(-3)) // "refused: {$!.^name}", ' ', (try $k.only(0)) // "refused: {$!.^name}";
    o $k.two(1, 4), ' ', (try $k.two(0, 4)) // 'first refused', ' ', (try $k.two(1, 5)) // 'second refused';
    o $k.m(mid), $k.m(low), ' ', $k.m(True), $k.m(False);                  # an enum value and a Bool are Ints too
    my int $n = 5;  my int $z = 0;  o $k.m($n), $k.m($z), $k.m(-$n), ' ', $k.only($n);
    my Int $typed = 3;  o $k.m($typed), ' ', $k.only($typed);
}
multi sub f(Int $x where * >= 10) { 'ge10' }
multi sub f($x where * < 10)      { 'lt10' }
o (5, 10, 15, 9.5, '3').map(&f).join(' ');
sub g(Int $x where * > 0) { $x }
o g(3), ' ', (try g(-3)) // "refused: {$!.^name}";
# an operator of the program's own decides, where one is in scope
{
    my &infix:«>» = -> $a, $b { True };
    multi sub h(Int $x where * > 100) { 'the lexical operator' }
    multi sub h($x)                   { 'the built-in' }
    o h(1);
}
class L { method only(Int $x where * > 100) { 'passed' } }
o (try L.new.only(1)) // 'refused', ' ', L.new.only(101), ' ', (try L.new.only(100)) // 'refused';

my @want = q:to/WANT-END/.lines;
neg zero pos pos neg
three not3 big big not3 big big big big
under from10 from10 under from10
6 refused: X::TypeCheck::Binding::Parameter refused: X::TypeCheck::Binding::Parameter
1,4 first refused second refused
poszero poszero
poszeroneg 10
pos 6
neg zero pos pos neg
three not3 big big not3 big big big big
under from10 from10 under from10
6 refused: X::TypeCheck::Binding::Parameter refused: X::TypeCheck::Binding::Parameter
1,4 first refused second refused
poszero poszero
poszeroneg 10
pos 6
neg zero pos pos neg
three not3 big big not3 big big big big
under from10 from10 under from10
6 refused: X::TypeCheck::Binding::Parameter refused: X::TypeCheck::Binding::Parameter
1,4 first refused second refused
poszero poszero
poszeroneg 10
pos 6
lt10 ge10 ge10 lt10 lt10
3 refused: X::TypeCheck::Binding::Parameter
the lexical operator
refused passed refused
WANT-END
for ^max(+@got, +@want) -> $i {
    next if (@got[$i] // '(nothing)') eq (@want[$i] // '(nothing)');
    say "FAIL: line {$i + 1} — got {(@got[$i] // '(nothing)').raku}, want {(@want[$i] // '(nothing)').raku}";
    exit 1;
}
say 'PASS';
