# Regression: quotes inside a chained `<…>` subscript are part of the key (#285).
#
# `%a<a><'b-c'>` read the key `b-c`: after a first subscript the lexer hands
# `'b-c'` over as a string with its quotes taken off. `<…>` is a word list
# and quotes are not syntax in it — Rakudo reads the key `'b-c'`, quotes and
# all, in every position, as the first subscript here already did.
#
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) {
    @fail.push("$what: got {$got.raku} want {$want.raku}") unless $got eqv $want
}
my %a; %a<a> = %( 'b-c' => 22, "'q'" => 1, '"x"' => 5 );
check %a<a><'b-c'>, Any, q[%a<a><'b-c'> is the key 'b-c' with quotes];
check %a<a><b-c>, 22, '%a<a><b-c>';
check %a<a><'q'>, 1, q[%a<a><'q'> finds the quoted key];
check %a<a><"x">, 5, '%a<a><"x"> finds the double-quoted key';
check %a<a>.<'b-c'>, Any, q[%a<a>.<'b-c'>];
my %w; %w<x><'q-z'> = 44;
check %w<x>.keys.List, ("'q-z'",), 'a write keeps the quotes';
my @r = %( 'b-c' => 33 );
check @r[0]<'b-c'>, Nil, q[@r[0]<'b-c'>];
check <a 'b c' d>.List, ("a", "'b", "c'", "d"), 'a word list term is unchanged';

if @fail { .say for @fail; say 'FAIL'; exit 1 }
say 'PASS';
