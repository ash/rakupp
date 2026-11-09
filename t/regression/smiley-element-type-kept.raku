# A `:D` or `:U` element type stays on the container, as Rakudo's does: `my
# Int:D @d` is an Array[Int:D] (`.of` Int:D, `.^name` Array[Int:D], `.raku`
# Array[Int:D].new(…)) and `my Int:D %h` a Hash[Int:D], where both said Int;
# and `@d[0] = Nil` is refused, "perhaps Nil was assigned to a :D which had
# no default?", where it stored Nil. The default stays the bare type
# (`@d[5]` is `(Int)`), and an `is default` takes the Nil as before.
# Every line answers the same under Rakudo 2026.09.

sub ck($got, $want, $desc) {
    return if $got eqv $want;
    say "FAIL: $desc — got {$got.raku}, want {$want.raku}";
    exit 1;
}
sub msg(&c) { try c(); $!.message }
my $hint = '(perhaps Nil was assigned to a :D which had no default?)';

my Int:D @d = 1;
ck (@d.of.^name, @d.WHAT.^name, @d.raku), ('Int:D', 'Array[Int:D]', 'Array[Int:D].new(1)'), 'an array';
my Int:D %h = a => 1;
ck (%h.of.^name, %h.WHAT.^name, %h.raku), ('Int:D', 'Hash[Int:D]', '(my Int:D % = :a(1))'), 'a hash';
ck (@d.of eqv Int:D, @d[0].VAR.of eqv Int:D, Array[Int:D].new.of eqv Int:D), (True, True, True), 'the type is Int:D';
ck msg({ @d[0] = Nil }), "Type check failed for an element of \@d; expected Int:D but got Int (Int) $hint", 'a Nil element';
ck msg({ %h<a> = Nil }), "Type check failed for an element of \%h; expected Int:D but got Int (Int) $hint", 'a Nil value';
ck @d.List, (1,), 'kept as it was';
@d[3] = 4;
ck (@d[1], @d.default, @d.raku), (Int, Int, 'Array[Int:D].new(1, Int:D, Int:D, 4)'), 'its holes';
ck %h<zz>, Int, "a hash's missing key";
my Int:D @e is default(7) = 1; @e[0] = Nil;
ck @e.List, (7,), 'an `is default` takes the Nil';
my Int:U @u; @u[0] = Int;
ck (@u.raku, @u.of.^name), ('Array[Int:U].new(Int:U)', 'Int:U'), 'a :U array';
@d.push(5);
ck @d.elems, 5, 'a push';

say 'PASS';
