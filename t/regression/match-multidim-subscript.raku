# Regression: a multidim subscript and `{*}` on a Match.
#
# LLM::Prompts' own test suite (t/02-prompt-parsing.rakutest) asks a parse
# result `so $m{*;'prompt-function-cell-spec'}`, so `raku install LLM::Prompts`
# refused to install it. The multidim walker knew Arrays, Ranges and Hashes,
# not a Match, and answered `()` — False — where Rakudo walks the named
# captures and answers one Nil per capture (a non-empty list, so True).
# `$m{*}` alone answered Nil instead of the named captures' values, and
# `$m{'a';'c'}` answered `(Any)` instead of the inner capture.
#
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) {
    @fail.push("$what: got {$got.raku} want {$want.raku}") unless $got eqv $want
}
grammar G { token TOP { <a> <b> }; token a { <c> }; token b { y }; token c { x } }
my $m = G.parse('xy');

check $m{*}.elems, 2, '$m{*} is the named captures';
check $m{*}.map(~*).sort.List, <x y>, '$m{*} values';
check $m{*;'zz'}.elems, 2, '$m{*;missing}: one entry per capture';
check $m{*;'zz'}.grep(Nil).elems, 2, '$m{*;missing} entries are Nil';
check (so $m{*;'zz'}), True, 'so $m{*;missing} (LLM::Prompts)';
check $m{*;'c'}.grep(*.defined).map(~*).List, ('x',), '$m{*;c} finds the inner capture';
check $m{'a';'c'}.map(~*).List, ('x',), q[$m{'a';'c'}];
my $p = 'ab' ~~ /(.)((.))/;
check $p[*;0].elems, 2, 'positional [*;0] on a Match walks the captures';
check $p[*;0].map({ .defined ?? ~$_ !! 'Nil' }).List, <Nil b>, 'positional [*;0] values';

if @fail { .say for @fail; say 'FAIL'; exit 1 }
say 'PASS';
