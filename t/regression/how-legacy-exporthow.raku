# Regression: `EXPORTHOW.WHO.<class> = TheHOW`, the spelling from before the
# EXPORTHOW directives, replaces the HOW of every `class` declared after the
# `use` — and a class-level `is` trait narrower than inheritance decides what
# `is Parent` means (roast integration/advent2011-day14.t: its aspect class is
# handed to the metaclass, not inherited from). rakupp ignored the spelling, and
# `is Parent` never reached a user `trait_mod:<is>`.
#
# Every expectation below was checked against Rakudo 2026.08.

use lib $?FILE.IO.parent.add('lib').Str;
use RakuppLegacyHOW;

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

my @seen;
class Recorder is Tracer { method record($what) { @seen.push($what) } }
class Worker is Recorder { method twice($x) { $x * 2 } }

ck(Worker.HOW.^name, 'TracingHOW', 'the legacy spelling installs the metaclass');
ck(Recorder.^parents(:local).map(*.^name).List, ('Tracer',), '`is Tracer` still inherits');
ck(Worker.^parents(:local).map(*.^name).List, ('Any',), '`is Recorder` is taken by the trait instead');
ck(Worker.twice(4), 8, 'the wrapped method still answers');
ck(@seen.List, ('in twice',), 'the metaclass wrapped it at compose');

say $fails == 0 ?? 'PASS' !! 'FAIL';
exit($fails ?? 1 !! 0);
