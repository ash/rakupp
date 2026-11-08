# `constant @L = (2, 4)` is a List compiled, as interpreted and on Rakudo:
# `--exe` assigned it as `my @L` is, so it said Array and printed [2 4]. A
# Positional keeps its kind (an Array, a Range), a Seq is cached into a List
# and anything else is the one-element List.
# Every line answers the same under Rakudo 2026.09, and as --exe.

sub ck($got, $want, $desc) {
    return if $got eqv $want;
    say "FAIL: $desc — got {$got.raku}, want {$want.raku}";
    exit 1;
}

constant @L = (2, 4);
ck @L.^name, 'List', 'a List';
ck @L.gist, '(2 4)', '…gists as one';
constant @A = [1, 2];
ck @A.^name, 'Array', 'an Array';
constant @R = 1..3;
ck @R.^name, 'Range', 'a Range';
constant @S = <a b>;
ck @S.^name, 'List', 'a word list';
constant @M = (1, 2).map(* * 2);
ck (@M.^name, @M.List), ('List', (2, 4)), 'a Seq, cached';
constant @O = 5;
ck (@O.^name, @O.List), ('List', (5,)), 'a lone value';
sub f { @L.elems + @R.elems }
ck f(), 5, 'from a routine';

say 'PASS';
