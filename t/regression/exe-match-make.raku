# --exe: an action method's `.make: …` / `$/.make(…)` sets the match's
# made value compiled as interpreted. The interpreter's method-call arm
# attaches the value to the match itself (and to the node the parse is
# building); compiled, the call looked for a `make` METHOD on the grammar's
# match and died "No such method 'make' for invocant of type 'G'", in a
# program compiled whole and in a bundled program's native method alike. A
# routine or program holding one now stays interpreted. Found by
# t/exe/parity.raku (top100-chunk-d-grammar-made-2026-09-03.raku). Every line
# answers the same under Rakudo 2026.09.
#
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) { @fail.push("$what: got {$got.raku} want {$want.raku}") unless $got eqv $want }

grammar G {
    token TOP  { <pair>+ % ',' }
    token pair { <name> '=' <val> }
    token name { \w+ }
    token val  { \w+ }
}
class A {
    method name($_) { .make: .Str.uc }
    method val($/)  { $/.make(+$/) }
    method pair($/) { make $<name>.made => $<val>.made }
    method TOP($/)  { make $<pair>.map(*.made).List }
}
check G.parse('a=1,b=2', :actions(A)).made, (A => 1, B => 2), '.make, $/.make and make';

if @fail { .say for @fail; say 'FAIL'; exit 1 }
say 'PASS';
