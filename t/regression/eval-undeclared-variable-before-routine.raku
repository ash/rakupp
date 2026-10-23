# BROKE: an EVAL'd snippet with an undeclared routine AND an undeclared
# variable was refused for the routine, where Rakudo refuses it for the
# variable (found 2026-10-23 by FatRoast's strings-v1, whose mangled `say`
# turned integration/advent2010-day07.t's `{ my $var = 42 }; say $var` into
# `òàù $var`):
#
#   EVAL '{ my $var = 42 }; nope $var';   # X::Undeclared::Symbols, Rakudo X::Undeclared
#
# Rakudo throws X::Undeclared the moment it parses the variable and collects
# undeclared routines for the end of the compilation, so the variable always
# wins. rakupp judged the calls first, and its variable pass pardons a name the
# text declares anywhere (`my $var` in the block before), so nothing was left
# to say about `$var`.
#
# FIXED: EVAL judges the variables first; and when the snippet is refused for a
# routine, a scoped variable pass (findUndeclaredVars's `scoped`, without the
# text pardon) names an undeclared variable instead if there is one.
#
# Contract: exit 0 + last line PASS.

my @fail;
sub ck($got, $want, $what) { @fail.push("$what: got {$got.raku} want {$want.raku}") unless $got eqv $want }
sub refusal(Str $code) { try { EVAL $code; return 'lived' }; $!.^name }

ck(refusal('{ my $var = 42 }; nope $var'), 'X::Undeclared', 'a variable declared only in another scope');
ck(refusal('nope $var'), 'X::Undeclared', 'a variable declared nowhere');
ck(refusal('$bad = 1; nope()'), 'X::Undeclared', 'the variable before the call');
ck(refusal('nope(); $bad = 1'), 'X::Undeclared', 'the variable after the call');
ck(refusal('nope(1)'), 'X::Undeclared::Symbols', 'a routine alone is still the routine');
{ my $v = 1; try EVAL '{ my $w = 1 }; nope($v)'; ck($!.^name, 'X::Undeclared::Symbols', 'a variable of the calling scope is declared') }
ck(refusal('{ my $var = 42 }; say $var'), 'X::Undeclared', 'a variable alone, as before');

if @fail { note $_ for @fail; say 'FAIL' }
else { say 'PASS' }
