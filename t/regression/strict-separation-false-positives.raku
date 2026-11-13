# Code EVAL's strict statement separation refused though it is valid Raku,
# found by giving whole files that strictness (R10's re-measure): `return not
# …`, which also returned Nil in a file (an R7 regression), `GLOBAL::<x>:delete`,
# whose `:delete` was a Pair of its own, `!!! ::?CLASS.^name ~ '…'`, and
# `… or last LABEL`, whose label was dropped.

my $fails = 0;
sub check($got, $want, $what) {
    if $got ne $want { say "FAIL: $what: got $got, want $want"; $fails++ }
}

sub f { return not 1 }
check f(), False, 'return not 1';
sub g { return not 1, 2 }
check g().raku, '(Bool::False, 2)', 'return not 1, 2';
check (EVAL q[sub h { return not (1 < 2) }; h()]), False, '…in EVAL';
class Gone {}
GLOBAL::<Gone>:delete;
check (GLOBAL::<Gone>:exists), False, 'GLOBAL::<x>:delete';
check (try EVAL q[GLOBAL::<Zz>:delete; 'ok']) // 'refused', 'ok', '…in EVAL';
class A { method m { !!! ::?CLASS.^name ~ ' must' } }
try A.m;
check $!.message, 'A must', '!!! ::?CLASS.^name ~ …';
check (EVAL q[my $n = 0; LOOP: loop { my $b = $n++ < 2 or last LOOP; }; $n]), 3, '… or last LABEL in EVAL';
my @out;
L1: for ^3 -> $i { for ^3 -> $j { ($j < 1) or next L1; @out.push("$i$j") } }
check @out.join(','), '00,10,20', '… or next LABEL leaves the labelled loop';

say $fails ?? "FAIL ($fails)" !! 'PASS';
exit $fails ?? 1 !! 0;
