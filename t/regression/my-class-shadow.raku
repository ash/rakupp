# Regression guard: a `my class` in a routine or block shadows a global class
# of the same name only while that scope runs.
#
# The type registry holds one type per name, the latest declared. A routine
# that declared `my class P` took the name for good: once it had run, `P` named
# its class everywhere, and the program's own `class P` was gone.
# registerDeclaredType (InterpreterModules.cpp) now puts the shadowed type back
# when the declaring scope leaves, with the scope's `temp`s. What must stay
# true: inside the scope its own class answers; after it, the outer one does,
# whether the scope returned, died or ran again; a recursive call and a nested
# scope declaring the name again give it back in order; an object of the local
# class keeps its class; a role is the same; and a `my class` nothing else is
# named after stays visible as before. Each case runs three times.

my @got;
sub o(**@a) { @got.push: @a.map(*.Str).join(' ') }
class P { method k { 'G' } }
role R { method r { 'GR' } }

sub f() { my class P { method k { 'F' } }; P.k }
sub thrower() { my class P { method k { 'T' } }; die 'out' }
sub rec($n) { my class P { method k { "L$n" } }; $n ?? rec($n - 1) ~ P.k !! P.k }
sub inner() { my class P { method k { 'I' } }; P.k }
sub outer() { my class P { method k { 'O' } }; inner() ~ P.k }
sub with-role() { my role R { method r { 'LR' } }; (class :: does R {}).r }
sub escapes() { my class P { has $.v; method k { "E$!v" } }; P.new(v => 7) }
sub only-one() { my class Q { method k { 'Q' } }; Q.k }

for ^3 {
    o f(), P.k;
    try thrower();
    o 'after a die', P.k;
    o rec(2), P.k;
    o outer(), P.k;
    { my class P { method k { 'B' } }; o 'block', P.k }
    o 'after the block', P.k;
    for ^2 { my class P { method k { 'loop' } }; @got.push: P.k }
    o 'after the loop', P.k;
    o with-role(), (class :: does R {}).r;
    my $e = escapes();
    o $e.k, $e.^name, P.k, P.new.k;
    o only-one(), only-one();
}

my @want = flat (q:to/WANT-END/.lines) xx 3;
F G
after a die G
L0L0L0 G
IO G
block B
after the block G
loop
loop
after the loop G
LR GR
E7 P G G
Q Q
WANT-END
for ^max(+@got, +@want) -> $i {
    next if (@got[$i] // '(nothing)') eq (@want[$i] // '(nothing)');
    say "FAIL: line {$i + 1} — got {(@got[$i] // '(nothing)').raku}, want {(@want[$i] // '(nothing)').raku}";
    exit 1;
}
say 'PASS';
