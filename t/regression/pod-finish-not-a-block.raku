# What follows `=finish` is `$=finish`, no block of `$=pod`, as Rakudo has
# it: a program with one pod block and a `=finish` section had two blocks in
# `$=pod`, the second a Pod::Block::Named "finish".
# Every line answers the same under Rakudo 2026.09.

sub ck($got, $want, $desc) {
    return if $got eqv $want;
    say "FAIL: $desc — got {$got.raku}, want {$want.raku}";
    exit 1;
}

=begin pod
A
=end pod

ck $=pod.elems, 1, 'one block';
ck $=pod[0].name, 'pod', 'the pod block';
ck $=finish, "xyz\n", '$=finish, as before';

say 'PASS';

=finish
xyz
