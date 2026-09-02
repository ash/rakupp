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

# The subset is `where { not .defined or $_ >= 0 }` on Rakudo: Nil is no Int at
# all, so it fails; the Int TYPE OBJECT is admitted by the where clause; and a
# Bool is a non-negative Int. Nil passing let Mathematica::Serializer's
# `when UInt` arm take a Nil Pair value and answer `Nil.Str` — an empty string
# where `NULL` was due (Rule["condo",] for Rule["condo",NULL]).
check((Nil ~~ UInt).gist,  'False', 'smartmatch: Nil is not a UInt');
check((Any ~~ UInt).gist,  'False', 'smartmatch: nor is Any');
check((Int ~~ UInt).gist,  'True',  'smartmatch: the Int type object is (undefined is admitted)');
check((Bool ~~ Int).gist,  'True',  'smartmatch: the Bool type object is an Int (its MRO is Bool, Int, Cool)');
check((Bool ~~ UInt).gist, 'True',  'smartmatch: so it is a UInt too');
check((True ~~ UInt).gist, 'True',  'smartmatch: and True, a non-negative Int');
check((Cool ~~ UInt).gist, 'False', 'smartmatch: Cool sits above Int and is not');
check(positional(True), 'g=True', 'a UInt positional takes a Bool');
sub typed(UInt $x) { $x.gist }
check(typed(Int), '(Int)', 'and the Int type object');
my $where = do given Nil { when UInt { 'UInt' }; when Int { 'Int' }; default { 'default' } };
check($where, 'default', 'given Nil: the UInt arm is passed over');

if @fail { note "FAILED: @fail.join('; ')"; say 'FAIL' } else { say 'PASS' }
