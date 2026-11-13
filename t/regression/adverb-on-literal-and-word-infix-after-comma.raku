# An adverb glued to a literal modifies nothing (rakudo#1476, #3426), and a
# loose word infix right after a comma is a name nothing declares
# (rakudo#4107), as in Rakudo. `42:foo` printed a Pair after 42, and
# `say 1, 2, and 3` was `(say 1, 2) and 3`.

my $fails = 0;
sub check($got, $want, $what) {
    if $got ne $want { say "FAIL: $what: got $got, want $want"; $fails++ }
}
sub refused($code) { try EVAL $code; $! ?? $!.^name.subst(/'+{' .* $/, '') !! 'ok' }

check refused('42:foo'), 'X::Syntax::Adverb', '42:foo (rakudo#1476)';
check refused('False:so'), 'X::Syntax::Adverb', 'False:so (rakudo#3426)';
check refused('say 1, 2, and 3'), 'X::Undeclared::Symbols', 'say 1, 2, and 3 (rakudo#4107)';
check refused('my %h = a => 1; %h<a>:exists'), 'ok', 'an adverb on a subscript, as before';
check refused('my @a = 1, 2,; @a'), 'ok', 'a trailing comma, as before';
try EVAL '42:foo';
check $!.message, "You can't adverb 42", 'the message';

say $fails ?? "FAIL ($fails)" !! 'PASS';
exit $fails ?? 1 !! 0;
