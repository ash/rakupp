# A type argument's smiley stays on it: `Positional[Int:D].^name` is
# Positional[Int:D], where the parser read the `:D` as an argument of its own
# and the name said Positional[Int,D]; `Hash[Int:D, Str:D]` is
# Hash[Int:D,Str:D]. A role parameterized with one asks each argument as
# Rakudo does, `want.ACCEPTS(have)`: `R[Int:D] ~~ R[Int]` is True and
# `R[Int] ~~ R[Int:D]` False. `Array[Str:D].new` checks the smiley too, where
# the comma kept it from checking anything.
# Every line answers the same under Rakudo 2026.09.

sub ck($got, $want, $desc) {
    return if $got eqv $want;
    say "FAIL: $desc — got {$got.raku}, want {$want.raku}";
    exit 1;
}

ck Positional[Int:D].^name, 'Positional[Int:D]', 'Positional[Int:D]';
ck Positional[Int:D].raku, 'Positional[Int:D]', '.raku';
ck Positional[Int:D].gist, '(Positional[Int:D])', '.gist';
ck Array[Int:D].^name, 'Array[Int:D]', 'Array[Int:D]';
ck Hash[Int:D, Str:D].^name, 'Hash[Int:D,Str:D]', 'two arguments';
ck Array[Array[Int:D]].^name, 'Array[Array[Int:D]]', 'nested';
role R[::T] { }
ck R[Int:D].^name, 'R[Int:D]', 'a role';
ck R[Int:U].^name, 'R[Int:U]', ':U';
ck Positional[Int:D] ~~ Positional[Int], True, 'Positional[Int:D] ~~ Positional[Int]';
ck R[Int:D] ~~ R[Int], True, 'R[Int:D] ~~ R[Int]';
ck R[Int] ~~ R[Int:D], False, 'R[Int] !~~ R[Int:D]';
ck R[Int:D] ~~ R[Int:D], True, 'the same';
ck R[Int:D] ~~ R[Cool], True, 'a wider type';
ck R[Int:U] ~~ R[Int], True, ':U';
ck Array[Str:D].new('A', 'B').elems, 2, 'Array[Str:D].new takes defined Strs';
ck (try Array[Str:D].new(Str)) // $!.^name, 'X::TypeCheck::Assignment', '…and refuses a type object';
ck (try Array[Str:U].new('a')) // $!.^name, 'X::TypeCheck::Assignment', '…as :U refuses a value';

say 'PASS';
