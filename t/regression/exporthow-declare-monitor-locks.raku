# Regression: issue #78 (2026-11-10).
#
#   OO::Monitors stopped excluding: a `monitor` declared through
#   EXPORTHOW::DECLARE ran its methods without the lock its metaclass's
#   add_method wraps them in, so four threads bumping a counter 1,000 times
#   each ended near 3,931, not 4,000. lib/AuditMonitorHOW.rakumod is a
#   cut-down OO::Monitors.
#
# Expected answers are Rakudo 2026.09's (2026-11-10).

use lib $?FILE.IO.parent.add('lib').Str;
use AuditMonitorHOW;

my $fails = 0;
sub check($got, $want, $what) {
    return if $got eqv $want;
    $fails++;
    say "not ok - $what\n  got:  {$got.raku}\n  want: {$want.raku}";
}

monitor Counter {
    has $.n;
    # Read, yield, write: without the lock, the threads' updates are lost.
    method bump() { my $v = $!n // 0; $!n = $v + 1 }
}
class Plain {
    has $.n;
    method bump() { my $v = $!n // 0; $!n = $v + 1 }
}

check Counter.HOW.^name, 'MetamodelX::AuditMonitorHOW', 'monitor is built by the declared metaclass';
check Counter.^attributes.map(*.name).grep('$!MONITOR-lock').elems, 1, 'new_type added the lock attribute';
check Plain.HOW.^name, 'Perl6::Metamodel::ClassHOW', 'a class keeps the standard metaclass';

my $c = Counter.new;
await (^4).map: { start { $c.bump for ^1000 } };
check $c.n, 4000, 'four threads, 1,000 locked bumps each: no update lost';

my $d = Counter.new;
await (^4).map: { start { $d.bump for ^1000 } };
check $d.n, 4000, '…and again on a second instance';

if $fails { say "$fails failure(s)"; exit 1 }
say 'PASS';
