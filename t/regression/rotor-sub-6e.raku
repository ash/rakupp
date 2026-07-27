use v6.e.PREVIEW;
# Regression: the 6.e `rotor` SUB takes the cycle FIRST and the iterable LAST —
# the opposite way round from the method. Every positional was being swept into
# the list, so `rotor(3, 'a'..'h')` rotored the cycle along with the data. The
# iterable is the last POSITIONAL, not the last argument, because `:partial` may
# come after it.
#
# This lives in its own file because the sub form only exists under 6.e, and the
# pragma has to be at file scope — Rakudo does not honour it inside an EVAL.
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) { @fail.push("$what: got $got want $want") unless $got eq $want }

check(rotor(3, 'a'..'h').join('|'),       'a b c|d e f',   'the cycle comes first');
check(rotor(2, 1..7).join('|'),           '1 2|3 4|5 6',   'and the iterable last');
check(rotor(2, 1..7, :partial).join('|'), '1 2|3 4|5 6|7', ':partial may come after it');
check(rotor(2 => 1, 1..6).join('|'),      '1 2|4 5',       'a Pair cycle works too');
check((1..7).rotor(2).join('|'),          '1 2|3 4|5 6',   'the method is unchanged');

if @fail { note "FAILED: @fail.join('; ')"; say 'FAIL' } else { say 'PASS' }
