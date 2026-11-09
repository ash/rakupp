# An EVAL's undeclared routines are reported in Rakudo's words, as the main
# program's are: "Undeclared routine:\n    prnt used at line 1. Did you mean
# 'print'?\n", where it said "Undefined routine 'prnt'. Did you mean any of
# these: 'print'?", and every such name at once, "Undeclared routines:". It
# is still refused before anything in the snippet runs, and a variable the
# snippet does not declare is still reported first.
# Every line answers the same under Rakudo 2026.09.

sub ck($got, $want, $desc) {
    return if $got eqv $want;
    say "FAIL: $desc — got {$got.raku}, want {$want.raku}";
    exit 1;
}
sub msg($code) { try EVAL $code; "{$!.^name}: {$!.message}" }

ck msg(q[prnt("x")]), "X::Undeclared::Symbols: Undeclared routine:\n    prnt used at line 1. Did you mean 'print'?\n", 'one';
ck msg(q[say &prnt]), "X::Undeclared::Symbols: Undeclared routine:\n    prnt used at line 1. Did you mean 'print'?\n", 'by its &';
ck msg(qq[say "ran";\nfrob(2);\nlength("x")]),
   "X::Undeclared::Symbols: Undeclared routines:\n    frob used at line 2\n    length used at line 3. Did you mean 'elems', 'chars', 'codes'?\n",
   'every one, with its line';
my $ran = 0;
try EVAL q[$ran = 1; prnt()];
ck $ran, 0, 'before anything runs';
ck (try { EVAL q[{ my $var = 42 }; nope $var]; 1 }) // $!.^name, 'X::Undeclared', 'its variable first, as before';

say 'PASS';
