# Regression (issue #109, row D): a `*@l` slurpy bound from plain VALUES —
# literals, operator or call results, nothing to flatten — holds them as they
# came, so writing an element dies. One variable or list among the arguments
# and every element sits in a fresh container: all or nothing, as in Rakudo.
# Contract: exit 0 + last line PASS.
sub w0(*@l) { @l[0] = 9; @l[0] }
sub w1(*@l) { @l[1] = 9; @l[1] }
sub cat(*@l) { @l[0] ~= '!'; @l[0] }
sub f { 3 }
my $x = 1;
my @a = 5, 6;
my @fail;
for ('w0(1,2)', { w0(1, 2) }), ('w1(1,2)', { w1(1, 2) }), ('cat(a)', { cat('a') }),
    ('w0(1+1,2)', { w0(1 + 1, 2) }), ('w0(f())', { w0(f()) }), ('w0(1,:k)', { w0(1, :k) }) -> ($l, &c) {
    try c();
    @fail.push("$l did not die") unless $!;
}
for ('w1($x,2)', { w1($x, 2) }), ('w0(1,$x)', { w0(1, $x) }), ('w0(1,@a)', { w0(1, @a) }),
    ('w0((1,2))', { w0((1, 2)) }), ('w1(1,(2,3))', { w1(1, (2, 3)) }), ('w0(<a b>)', { w0(<a b>) }) -> ($l, &c) {
    my $r = try c();
    @fail.push("$l: {$! // $r}") unless $r == 9;
}
@fail.push('caller variable changed') unless $x == 1;
# the two refusals are Rakudo's two: `=` has no container to write to, while
# an OP= modifies the value in place — "Cannot modify an immutable Str (a)"
try cat('a');
@fail.push("~=: {$!.^name}") unless $! ~~ X::Assignment::RO;
try w0(1);
@fail.push("=: {$!.^name}") unless $! ~~ X::AdHoc && $! !~~ X::Assignment::RO;
sub keep(*@l) { my @b = @l; @b[0] = 7; my $s = @l[1]; $s = 8; @l.push(3); (@b[0], $s, @l.elems) }
@fail.push('copies of the elements are writable') unless keep(1, 2) eqv (7, 8, 3);
sub back(*@l) { @l }
my @r = back(1, 2); @r[0] = 5;
@fail.push('assigned result writable') unless @r[0] == 5;
.say for @fail;
say @fail ?? 'FAIL' !! 'PASS';
