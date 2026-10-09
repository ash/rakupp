# Regression: user postfixes stack, and a superscript power follows one.
#
# With only `postfix:<!>` declared, `3!!` was a parse error: the lexer reads
# `!!` as one operator (the ternary's), and only an identifier run was split
# back into the declared postfixes. And `3!²` dropped its power: a superscript
# after an operator lexed as a bare number, a statement of its own (2026-10-09).
#
# Expectations checked against Rakudo 2026.09 (/opt/homebrew/bin/rakudo).
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) { @fail.push("$what: got {$got.raku} want {$want.raku}") unless $got eqv $want }

sub postfix:<!>($n) { [*] 1..$n }

check 3!!,               720, '3!! is (3!)!';
check 3!²,               36,  '3!² is (3!)²';
check 3!¹⁰,              60466176, '…with several superscript digits';
check 3!⁻¹,              1/6, '…and a sign';
check 3!**2,             36,  'an ASCII power after it, as before';
check 3! + 1,            7,   'a postfix, then an infix';
check (True ?? 1 !! 2),  1,   'a spaced ternary is still a ternary';
check 2²³,               8388608, 'a superscript after a number, as before';
my $x = 3;
check $x²,               9,   '…and after a variable';
check ((1, 2)»²).List,   (1, 4), '…and after a hyper';

if @fail { .say for @fail; say "FAIL" } else { say "PASS" }
