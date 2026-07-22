# BROKE: leg 18 — the new X::Syntax::Perl5Var lexer checks ($^A..$^Z on
# sight) fired INSIDE `< ... >` word lists, where everything is words:
# the `for < $^A $^B ... >` loop in misc2.t killed the whole file at lex
# time (misc2 -> 0, caught immediately in the leg's own probes).
# FIXED: same leg — all Perl5Var checks are angleWords_-guarded.
my @w = < $^A $. %- $" >;
die "angle word list with P5-var lookalikes broken: {@w.raku}" unless @w == 4 && @w[0] eq '$^A';
say 'PASS';
