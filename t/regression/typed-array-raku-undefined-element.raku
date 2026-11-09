# A typed Array's `.raku` writes an undefined element as the array's type,
# as Rakudo's does: `my Cool @a = Int, 1` is `Array[Cool].new(Cool, 1)`,
# where it was `Array[Cool].new(Int, 1)`. A hole was already its type; a
# defined element and an untyped Array are as before, and a native array
# keeps its zeroes.
# Every line answers the same under Rakudo 2026.09.

sub ck($got, $want, $desc) {
    return if $got eqv $want;
    say "FAIL: $desc — got {$got.raku}, want {$want.raku}";
    exit 1;
}

my Cool @a = Int, 1;
ck @a.raku, 'Array[Cool].new(Cool, 1)', 'a type object';
my Numeric @n = Int;
ck @n.raku, 'Array[Numeric].new(Numeric)', 'another';
my Int @h; @h[2] = 1;
ck @h.raku, 'Array[Int].new(Int, Int, 1)', 'holes, as before';
my Int @d is default(7); @d[2] = 1;
ck @d.raku, 'Array[Int].new(7, 7, 1)', 'a default fills them';
ck Array[Int].new(Int, 3).raku, 'Array[Int].new(Int, 3)', 'the type itself';
my @u = Int, 1;
ck @u.raku, '[Int, 1]', 'an untyped Array';
my int @i = 1, 2;
ck @i.raku, 'array[int].new(1, 2)', 'a native one';

say 'PASS';
