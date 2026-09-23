# Regression: an operator glued to the `>` that closes an angle subscript.
#
# `%h<a>==0` was a parse error ("unexpected operator in term position (got
# '=')"): the lexer took `>=` as one token and then `=`, and the parser, which
# split the leading `>` off as the closer, was left with `=` `=` — two
# assignments, never `==`. The same went for `===`, `=~=`, `=:=`, `==>` and the
# fat arrow `=>`. `%h<a>=5`, `%h<a>>=0` and `%h<a>!=0` happened to survive the
# split. The lexer now emits the outermost closer alone when an operator is
# glued to it, and lexes the rest afresh.
#
# Every expectation was checked against Rakudo 2026.08.

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

my %h = a => 1, b => 2;
my $h = { a => 1 };

ck(%h<a>==0, False, '%h<a>==0');
ck(%h<a>==1, True, '%h<a>==1');
ck($h<a>==1, True, '$h<a>==1 on a Hash in a scalar');
ck(%h<a b>==2, True, 'a two-word slice, then ==');
ck(%h<a>===1, True, '===');
ck(%h<a>!==1, False, '!==');
ck(%h<a>!=0, True, '!=');
ck(%h<a>=~=1, True, '=~=');
ck(%h<a>=:=%h<a>, True, '=:=');
ck(%h<a>>=1, True, '>= (the closer, then >=)');
ck(%h<a>>1, False, '> (the closer, then >)');
ck(%h<a>=>0, (1 => 0), '=> makes a Pair');
ck(%h<a>== 1, True, '== with a space only after it');

my @fed;
%h<a>==>push(@fed);
ck(@fed, [1], '==> feeds the value');

my %w;
%w<a>=5;
ck(%w<a>, 5, '= assigns');
%w<a>+=1;
ck(%w<a>, 6, '+= assigns');
%w<b>//=7;
ck(%w<b>, 7, '//= assigns');
$h<a>=9;
ck($h<a>, 9, '= assigns through $h<a>');

ck(%h<a b>>>.succ, (2, 3), 'the >> hyper after a slice still re-glues');
ck(<a b>==2, True, 'a bare word list, then ==');

say $fails ?? "FAIL ($fails)" !! "PASS";
exit $fails ?? 1 !! 0;
