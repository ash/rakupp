# Regression: `$*TMPDIR` read the TMPDIR environment variable and nothing else,
# so on Windows — which sets TEMP and TMP and never TMPDIR — it was the literal
# string "/tmp". Drive-relative, almost certainly not a directory, and not where
# anything belongs.
#
# It mattered because things UNPACK there and then run a child in it:
# `rakupp install` builds its staging directory under $*TMPDIR, so every
# distribution's tests ran from a path that was wrong before the first test
# file was read.
#
# Four copies of the same one-liner had drifted apart (IOSpec, Interpreter
# twice, Builtins); they are one function now. The cheapest assertion that
# would have caught it is not "which variable does it read" but "is the answer
# a directory that exists", which is true on every platform and was false on
# one.
#
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) {
    @fail.push("$what: got {$got.raku} want {$want.raku}") unless $got eq $want
}

check $*TMPDIR.defined,        True, '$*TMPDIR is defined';
check ($*TMPDIR.Str.chars > 0), True, '…and not empty';
check $*TMPDIR.d,              True, "…and is a directory that exists ($*TMPDIR)";

# It is also writable, which is the whole point of asking for it.
my $probe = $*TMPDIR.add("rakupp-tmpdir-probe-{$*PID}");
$probe.spurt("ok");
check $probe.slurp, 'ok', '…and writable';
$probe.unlink;

# One implementation, so the spec classes cannot drift from the dynamic
# variable again.
check IO::Spec::Unix.tmpdir.Str,  $*TMPDIR.Str, 'IO::Spec::Unix.tmpdir agrees';
check IO::Spec::Win32.tmpdir.Str, $*TMPDIR.Str, 'IO::Spec::Win32.tmpdir agrees';

if @fail { die "FAIL:\n" ~ @fail.join("\n") }
say 'PASS';
