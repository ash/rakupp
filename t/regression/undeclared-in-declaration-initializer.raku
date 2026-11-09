# An undeclared variable in a declaration's initializer is refused at
# compile time, as Rakudo refuses it: `my $y = "a $zqx b"` ran the statements
# before it and died when it was reached, and so did `my $y = $zqx`, `my $t
# := $zqx` and `my %h = a => $zqx` — the check took the `my` before the name
# for its declaration. The name a declarator declares, and a routine's
# signature, are as before.
# Every line answers the same under Rakudo 2026.09.

sub ck($got, $want, $desc) {
    return if $got eqv $want;
    say "FAIL: $desc — got {$got.raku}, want {$want.raku}";
    exit 1;
}
my $ran;
sub compiled($code) { $ran = 0; try EVAL '$ran = 1; ' ~ $code; ($ran, $!.^name) }

ck compiled(q[my $y = "a $zqx b"]), (0, 'X::Undeclared'), 'in a string';
ck compiled(q[my $y = $zqx]), (0, 'X::Undeclared'), 'alone';
ck compiled(q[my $t := $zqx]), (0, 'X::Undeclared'), 'bound';
ck compiled(q[my %h = a => $zqx]), (0, 'X::Undeclared'), 'in a Pair';
ck compiled(q[my ($p, $q) = 1, $zqx]), (0, 'X::Undeclared'), 'beside a list of them';
ck (try { EVAL q[my $y = "a $zqx b"]; 1 }) // $!.message,
   "Variable '\$zqx' is not declared. Perhaps you forgot a 'sub' if this was\nintended to be part of a signature?", 'in these words';

ck EVAL(q[my $x = 1; my $y = $x; my ($a, $b) = 1, $y; sub f($m, $n = $m) { $n }; my Int $w where * >= 1 = 5; $a + $b + f(3) + $w]),
   10, 'what is declared, as before';

say 'PASS';
