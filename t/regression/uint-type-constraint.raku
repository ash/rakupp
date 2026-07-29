# Reported: github.com/ash/rakupp/issues/11 — a `UInt` parameter rejected every
# Int it was handed ("expected UInt but got Int (15)"), so a MAIN signature like
# `UInt :$pipeline = 15` could not be called at all. The DEFAULT bound fine, which
# is what made it confusing: only an explicitly-passed value went through the
# type check.
#
# UInt is `subset UInt of Int where * >= 0`, so it is not a plain name match: a
# non-negative Int satisfies it, a negative Int does not, and a Rat never does.
# typeMatchesArg owns that rule; the smartmatch path asks it rather than
# restating it.
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) { @fail.push("$what: got $got want $want") unless $got eq $want }

sub named(UInt :$p = 15) { "p=$p" }
check(named(),      'p=15', 'a UInt named param takes its default');
check(named(:p(3)), 'p=3',  'and an explicitly passed value');
check(named(:p(0)), 'p=0',  'zero included');

sub positional(UInt $x) { "g=$x" }
check(positional(7), 'g=7', 'a UInt positional binds');

check((15 ~~ UInt).gist,   'True',  'smartmatch: a positive Int');
check((0 ~~ UInt).gist,    'True',  'smartmatch: zero');
check((-1 ~~ UInt).gist,   'False', 'smartmatch: a negative Int does NOT match');
check((2.5 ~~ UInt).gist,  'False', 'smartmatch: a Rat does not match');
check((1000000000000000000000 ~~ UInt).gist, 'True',  'a bigint does');
check((-1000000000000000000000 ~~ UInt).gist,'False', 'and a negative bigint does not');

# the constraint is enforced, not merely accepted
my $threw = False;
try { positional(-1); CATCH { default { $threw = True } } }
@fail.push('a negative argument was not rejected') unless $threw;

my UInt $v = 5;
check($v.gist, '5', 'a UInt container holds a value');

if @fail { note "FAILED: @fail.join('; ')"; say 'FAIL' } else { say 'PASS' }
