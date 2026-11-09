# An OP= that leaves a typed element something it refuses names the
# container, as Rakudo's does: `my Int @a = 1; @a[0] ~= "x"` says "for an
# element of @a", where it said "of @a[0]", and a Hash's `%h<a> ~= "x"` "of
# %h", where it said "of %h{'a'}". A `//=`, `||=` or `&&=` still names the
# element, as Rakudo's does.
# Every line answers the same under Rakudo 2026.09.

sub ck($got, $want, $desc) {
    return if $got eqv $want;
    say "FAIL: $desc — got {$got.raku}, want {$want.raku}";
    exit 1;
}
sub msg(&c) { try c(); $!.message }

my Int @a = 1;
ck msg({ @a[0] ~= "x" }), 'Type check failed for an element of @a; expected Int but got Str ("1x")', '~=';
ck msg({ @a[0] += 1.5 }), 'Type check failed for an element of @a; expected Int but got Rat (2.5)', '+=';
my $i = 0;
ck msg({ @a[$i] x= 2 }), 'Type check failed for an element of @a; expected Int but got Str ("11")', 'x= by a variable';
my Int %h = a => 1;
ck msg({ %h<a> ~= "x" }), 'Type check failed for an element of %h; expected Int but got Str ("1x")', "a hash's";
my Int:D @d = 1;
ck msg({ @d[0] ~= "x" }), 'Type check failed for an element of @d; expected Int:D but got Str ("1x")', 'a :D one';
ck (@a.List, %h<a>, @d.List), ((1,), 1, (1,)), 'each kept';

my Int @b; my Int %g;
ck msg({ @b[0] //= "q" }), 'Type check failed for an element of @b[0]; expected Int but got Str ("q")', '//=, as before';
ck msg({ @b[0] ||= "q" }), 'Type check failed for an element of @b[0]; expected Int but got Str ("q")', '||=';
ck msg({ %g<a> //= "q" }), q[Type check failed for an element of %g{'a'}; expected Int but got Str ("q")], "a hash's //=";

say 'PASS';
