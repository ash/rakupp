# `nqp::time_n`, the seconds since the epoch as a Num, which GLib::Timeout
# reads for its intervals. Raku++ had `nqp::time` (nanoseconds) and not this
# one, so the module died "Undefined routine 'nqp::time_n'" when it ticked.
use nqp;

my $fails = 0;
sub check($got, $want, $what) {
    if $got ne $want { say "FAIL: $what: got $got, want $want"; $fails++ }
}

my $t = nqp::time_n();
check $t.^name, 'Num', 'nqp::time_n answers a Num';
check abs($t - time) < 2, True, '…the POSIX seconds, as `time` counts them';
check $t != $t.Int || nqp::time_n() != $t, True, '…with the fraction';
check nqp::time_n() >= $t, True, 'and it does not go back';

say $fails ?? "FAIL ($fails)" !! 'PASS';
exit $fails ?? 1 !! 0;
