# `use Telemetry` loads, as Rakudo's core module does (rakudo#1513, #3844,
# #5144): it was "Could not find Telemetry". T<max-rss>, T{"max-rss"}, snaps,
# periods and the report, with Rakudo's columns.

use Telemetry;

my $fails = 0;
sub check($got, $want, $what) {
    if $got ne $want { say "FAIL: $what: got $got, want $want"; $fails++ }
}

check T<max-rss> > 0, True, 'T<max-rss> (rakudo#3844)';
check T{"max-rss"} > 0, True, 'T{"max-rss"} (rakudo#5144)';
my $t = T;
check $t.^name, 'Telemetry', 'T is a Telemetry';
check $t<wallclock> > 0 && $t<cpu> > 0, True, 'wallclock and cpu';
check $t.max-rss == $t<max-rss>, True, 'a column as a method';
check $t<nope>.raku, 'Nil', 'an unknown column';
check $t.sampler.raku, 'Telemetry::Sampler.new(Telemetry::Instrument::Usage,Telemetry::Instrument::ThreadPool)',
      'the default instruments';
my $x = 0; $x += $_ for ^20000;
my $p = T() - $t;
check $p.^name, 'Telemetry::Period', 'a difference is a Period';
check $p<wallclock> >= 0, True, '…of each column';
check Telemetry::Instrument::Usage.new.default-columns.join(','), 'wallclock,util%,max-rss', 'the default columns';
check Telemetry::Instrument::Usage.new.columns.elems, 20, 'all of the Usage columns';

my @s;
snap(@s); $x += $_ for ^20000; snap(@s); snap(@s);
check periods(@s).elems, 2, 'periods of snaps';
my @r = report(@s, :!legend).lines;
check @r[0].starts-with("Telemetry Report of Process #$*PID"), True, 'the report';
check @r[1], 'Number of Snapshots: 3', '…its snaps';
check @r.grep(*.starts-with('wallclock  util%')).elems > 0, True, '…its header';
try { $t.nope; CATCH { default { check .message, "No such method 'nope' for invocant of type 'Telemetry'", 'no such column, as a method' } } }

say $fails ?? "FAIL ($fails)" !! 'PASS';
exit $fails ?? 1 !! 0;
