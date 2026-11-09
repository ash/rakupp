# An element type with a smiley is checked whole, as Rakudo checks it: `my
# Int:D @d = 1; @d[0] = "s"` says "expected Int:D", where it said "expected
# Int"; `.push`, `.append` and `.unshift` of a type object into a `:D`
# array are refused, where they stored it; and an `Array[Str:D]`'s element
# refuses a type object or a Nil, where only its nominal type was asked.
# An `is default` still takes a Nil.
# Every line answers the same under Rakudo 2026.09.

sub ck($got, $want, $desc) {
    return if $got eqv $want;
    say "FAIL: $desc — got {$got.raku}, want {$want.raku}";
    exit 1;
}
sub msg(&c) { try c(); $!.message }
sub refused(&c) { (try { c(); 1 }) // ($!.^name, $!.expected.^name) }

my Int:D @d = 1;
ck msg({ @d[0] = "s" }), 'Type check failed for an element of @d; expected Int:D but got Str ("s")', 'an element';
ck (try { @d[0] = Int; 1 }) // ($!.symbol, $!.got.^name), ('@d', 'Int'), 'its symbol';
my Int:D %h = a => 1;
ck msg({ %h<a> = "s" }), 'Type check failed for an element of %h; expected Int:D but got Str ("s")', 'a value';
ck refused({ @d.push(Int) }), ('X::TypeCheck::Assignment', 'Int:D'), '.push of a type object';
ck refused({ @d.append(2, Int) }), ('X::TypeCheck::Assignment', 'Int:D'), '.append';
ck refused({ @d.unshift(Int) }), ('X::TypeCheck::Assignment', 'Int:D'), '.unshift';
ck @d.List, (1, 2), 'only what came before the refusal landed';

my @a := Array[Str:D].new("a");
ck refused({ @a[0] = Str }), ('X::TypeCheck::Assignment', 'Str:D'), "an Array[Str:D]'s element";
ck (try { @a[0] = Nil; 1 }) // $!.^name, 'X::TypeCheck::Assignment', 'and a Nil';
ck refused({ @a.push(Str) }), ('X::TypeCheck::Assignment', 'Str:D'), 'and a push';
ck @a.List, ("a",), 'kept';

my Int:U @u;
ck msg({ @u[0] = 5 }), 'Type check failed for an element of @u; expected Int:U but got Int (5)', 'a :U element, as before';
my Int:D @e is default(7) = 1; @e[0] = Nil;
@d[0] //= 5; @d[0] += 1; @d.push(3);
ck (@e.List, @d.List, @a.push("b").List), ((7,), (2, 2, 3), ("a", "b")), 'what fits, as before';

say 'PASS';
