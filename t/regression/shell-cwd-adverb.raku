# Regression: `shell` accepted `:cwd` and ignored it. The adverb was never
# parsed in shell's option loop — `run` has parsed it since it was first
# reported — so the command ran in the CALLING process's directory, and a
# `:cwd` naming a directory that does not exist ran it anyway and exited 0,
# which is the answer a caller is least able to notice. Fixed alongside the
# same-shaped `:env` gap (see run-env-adverb-shapes.raku); spawnCapture already
# took the directory, shell passed the empty string.
#
# Every expectation below IS Rakudo v2026.08's output, except where a row says
# otherwise in so many words.

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eq $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — got {$got.raku}, want {$want.raku}" }
}

# A directory of our own, resolved: $*TMPDIR is a symlink on macOS and `pwd` in
# the child reports the real path, so the expectation has to be resolved too.
my $dir = $*TMPDIR.add("shell-cwd-$*PID");
$dir.mkdir;
LEAVE { $dir.rmdir }
my $want = $dir.resolve.absolute;
my $here = $*CWD.resolve.absolute;

ck(shell('pwd', :out, :cwd($dir.Str)).out.slurp(:close).trim, $want, 'shell :cwd runs the command there');
ck(shell('pwd', :out).out.slurp(:close).trim,                 $here, 'shell without :cwd is unchanged');

# Relative, against the process's directory — not against the previous :cwd.
ck(shell('pwd', :out, :cwd('tools')).out.slurp(:close).trim,
   $*CWD.add('tools').resolve.absolute, 'a relative :cwd resolves against the caller');

# A :cwd that does not exist must stop the command, not relocate it. The
# EXITCODE is deliberately not asserted: rakupp reports the child's 126 and
# Rakudo reports -1 for a spawn that never happened, so this pins the part both
# engines agree on and the part that actually matters — the command did not run.
{
    my $p = shell('pwd', :out, :cwd('/no/such/dir/here'));
    my $out = $p.out.slurp(:close).trim;
    ck($out,                          '',    'a missing :cwd produces no output');
    ck(($p.exitcode != 0).Str,        'True', '…and a non-zero exit');
}

# The two adverbs shell learned together compose.
{
    my $p = shell(q{printf %s "$PROBE:$(pwd)"}, :out, :cwd($dir.Str), :env(%(PROBE => 'M')));
    ck($p.out.slurp(:close).trim, "M:$want", ':cwd and :env compose');
}

say $fails == 0 ?? 'PASS' !! "FAIL ($fails)";
exit $fails == 0 ?? 0 !! 1;
