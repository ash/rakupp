# A lone Array bound through a `+` slurpy keeps its elements' containers
# (rakudo#1355): `-> +l { l }([1, (2, 3)])` is `(1, $(2, 3))`, where a
# List argument binds as it is.

my $fails = 0;
sub check($got, $want, $what) {
    if $got ne $want { say "FAIL: $what: got $got, want $want"; $fails++ }
}

check (-> +l { l }([1, (2, 3)])).raku, '(1, $(2, 3))', '+l over an Array';
check (-> +@l is raw { @l }([1, (2, 3)])).raku, '(1, $(2, 3))', '+@l is raw over an Array';
check (-> +@l { @l }([1, (2, 3)])).raku, '[1, (2, 3)]', '+@l over an Array';
check (-> +l { l }((1, (2, 3)))).raku, '(1, (2, 3))', '+l over a List';
check (-> +l { l.elems }([1, (2, 3)])), '2', 'the inner list is one element';
check (-> +l { l }([1, [2, 3]])).raku, '(1, $[2, 3])', '+l over an Array of Arrays';
check (-> +l { l }(1, (2, 3))).raku, '(1, (2, 3))', '+l over two arguments';

say $fails ?? "FAIL ($fails)" !! 'PASS';
exit $fails ?? 1 !! 0;
