# `CALLERS::('$x')` looks the name up as `CALLERS::<$x>` does, as in Rakudo
# (rakudo#2639): it called a routine named `CALLERS::`. So do `CALLER::(…)`
# and a package's `M::(…)`. A `given` or `for` topic is no dynamic `$_`:
# CALLERS reaches a routine's own, or the mainline's.

my $fails = 0;
sub check($got, $want, $what) {
    if $got ne $want { say "FAIL: $what: got $got, want $want"; $fails++ }
}

my $y is dynamic = 7;
sub f1 { CALLER::('$y') }
sub f2 { CALLERS::('$y') }
check f1(), 7, 'CALLER::(…)';
check f2(), 7, 'CALLERS::(…)';
sub h { CALLERS::('$*d') }
sub g { my $*d = 9; h() }
check g(), 9, 'a dynamic of a caller further up';
module M { our $q = 5 }
check M::('$q'), 5, 'a package\'s';

sub test-run() { CALLERS::('$_') }
given 'foo' { check test-run().raku, 'Any', 'a given topic is not reached (rakudo#2639)' }
for 'q' { check test-run().raku, 'Any', '…nor a for topic' }
sub in { $_ = 'inner'; test-run() }
check in(), 'inner', 'a routine\'s own $_ is';
sub ang { CALLERS::<$_> }
given 'bar' { check ang().raku, 'Any', 'the <…> form alike' }

say $fails ?? "FAIL ($fails)" !! 'PASS';
exit $fails ?? 1 !! 0;
