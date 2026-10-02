# Regression: a `<…>` word list nests its angles by CHARACTER, as Rakudo
# counts them — `<a <=> b>` holds the word `<=>` — and the first `>` with
# nothing open closes it, after a space too: `< a >= b >` is a syntax error.
# Also `».^name`: the meta-method of the list itself, once (`List`).
# Contract: exit 0 + last line PASS.
use MONKEY-SEE-NO-EVAL;
my @fail;
@fail.push('<a <=> b>')     unless <a <=> b> eqv ('a', '<=>', 'b');
@fail.push('<a <b> c>')     unless <a <b> c> eqv ('a', '<b>', 'c');
@fail.push('<a <<b>> c>')   unless <a <<b>> c> eqv ('a', '<<b>>', 'c');
@fail.push('<< <=> >>')     unless << <=> >> eq '<=>';
@fail.push('%h<a>=9')       unless (my %h = a => 1; %h<a>=9; %h<a> == 9);
for q[<a >= b>], q[<a <b c>], q[<a > b>], q[<a>>] -> $c {
    try EVAL $c;
    @fail.push("$c compiled") unless $!;
}
@fail.push('».^name') unless (1, 'a')».^name eq 'List' && [1, 'a']».^name eq 'Array';
.say for @fail;
say @fail ?? 'FAIL' !! 'PASS';
