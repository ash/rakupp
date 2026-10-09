# Regression: which unimplemented role stub a class is told about (#35).
#
# A class missing several of its roles' stub methods hears of ONE. It was the
# alphabetically first, because the requirements were kept in a std::set. Rakudo
# names a role's own stubs in the order they are declared, then those of the
# roles it composes, and checks the class's roles last-`does` first. zef met it
# (2026-10-07); fixed 2026-10-09.
#
# Expectations checked against Rakudo 2026.09 (/opt/homebrew/bin/rakudo).
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) { @fail.push("$what: got {$got.raku} want {$want.raku}") unless $got eqv $want }

role R { method zeta {...}; method alpha {...} }
role S { method mid {...}; method beta {...} }
role T does S { method omega {...} }

sub missing(Str $code) {
    my $r = (try EVAL $code) // $!;
    $r ~~ Exception ?? ($r.message ~~ / "Method '" (<-[']>+) "'" .* "roles: " (\w+) /).list.join(' ') !! 'lived';
}
check missing('class K1 does R {}'),                     'zeta R',  'the first stub declared';
check missing('class K2 does S does R {}'),              'zeta R',  'the role named last is checked first';
check missing('class K3 does R does S {}'),              'mid S',   '…either way round';
check missing('class K4 does T {}'),                     'omega T', 'a role\'s own stubs before the ones it composes';
check missing('class K5 does R { method zeta {} }'),     'alpha R', 'the next one once the first is written';
check missing('class K6 does T { method omega {} }'),    'mid T',   '…the composed role\'s, in its order';
check missing('class K7 does T { method omega {}; method mid {}; method beta {} }'), 'lived', 'all written';

if @fail { .say for @fail; say "FAIL" } else { say "PASS" }
