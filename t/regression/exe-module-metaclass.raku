# --exe: a program using a module that supplies a metaclass (EXPORTHOW)
# bundles with the interpreter. The module's HOW builds the program's classes
# — here it wraps every method at compose, and `is Recorder` is a trait it
# takes rather than inheritance — which a natively registered class never
# goes through: compiled, `Worker.HOW` was the standard ClassHOW and no method
# was wrapped. (The same holds for a slang, whose EXPORT runs under the
# parse's $*LANG.) Found by t/exe/parity.raku (how-legacy-exporthow.raku and
# its three EXPORTHOW siblings). Every line answers the same under Rakudo
# 2026.09.
#
# Contract: exit 0 + last line PASS.
use lib $?FILE.IO.parent.add('lib').Str;
use RakuppLegacyHOW;

my @fail;
sub check($got, $want, $what) { @fail.push("$what: got {$got.raku} want {$want.raku}") unless $got eqv $want }

my @seen;
class Recorder is Tracer { method record($what) { @seen.push($what) } }
class Worker is Recorder { method twice($x) { $x * 2 } }

check Worker.twice(4), 8, 'the wrapped method answers';
check @seen.List, ('in twice',), 'the metaclass wrapped it at compose';

if @fail { .say for @fail; say 'FAIL'; exit 1 }
say 'PASS';
