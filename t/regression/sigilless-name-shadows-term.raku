# Regression: a sigilless name the program declares beats the built-in term of
# the same spelling. `sub f(\i) { i }` returned the imaginary unit, because the
# NameTerm path consulted the pi/e/i/tau/now/time/rand constants BEFORE looking
# in the lexical scope — while the codegen runtime's rtNameTerm had always done
# it the other way round. Found by the pre-3.5 review, extracting the shared
# sigilless-parameter parse.
#
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) {
    @fail.push("$what: got {$got.raku} want {$want.raku}") unless $got eq $want
}

sub f1(Int \i)  { i }
sub f2(\e)      { e }
sub f3(Int \pi) { pi }
sub f4(\now)    { now }
check f1(7), 7, 'a sigilless parameter named i is the parameter';
check f2(7), 7, '…and one named e';
check f3(7), 7, '…and one named pi';
check f4(7), 7, '…and one named now';

my \tau = 9;
check tau, 9, 'a sigilless declaration named tau shadows the constant too';

# …and the terms still answer where nothing declares them
check i.^name,    'Complex', 'the i term survives';
check pi.substr(0, 4), '3.14', 'the pi term survives';
check e.substr(0, 4),  '2.71', 'the e term survives';
check (1+2i).im,  2,        'i inside a literal is untouched';

if @fail { note "FAILED: @fail.join('; ')"; say 'FAIL' } else { say 'PASS' }
