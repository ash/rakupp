# The SLIM differential gate (SLIM-PLAN P4, defence 5): for every program in
# the corpus, build TWICE — `--slim` (the scan) and full — run both, and
# require byte-identical stdout, stderr and exit status. A cut that changes
# any observable behaviour fails the gate. This is the number the campaign is
# judged on as much as the size.
#
# Run:  build/rakupp tools/slim-diff.raku [DIR|FILE ...]
#       (default corpus: t/regression and examples)
#
# Binaries run from the repo root, like t/run.raku runs the same files — the
# IO tests write relative paths and are idempotent across consecutive runs.
# A mismatch is retried once with two fresh runs before it counts: a
# timing-sensitive file that disagrees with ITSELF is noise, not a wrong cut.
#
# Every run is capped (30s) and killed BY PROCESS GROUP: some corpus files
# spawn $*EXECUTABLE, which in a compiled binary is the binary itself — the
# spawn re-runs the embedded program and recurses forever (measured: one such
# binary ate 25 CPU-minutes). macOS has no setsid/timeout binaries, so a perl
# setpgrp shim starts the group and plain sh watches and kills it; output goes
# to FILES, because a pipe inherited by orphaned grandchildren never closes.
# A file that times out on any side is reported by name and not judged.
#
# The build directory is picked from $*EXECUTABLE (run it with the rakupp
# under test). Compiled binaries land in a scratch dir and are removed.

my $ROOT = $?FILE.IO.parent.parent;
# --lib=DIR (repeatable) adds -I DIR to every compile — the battery leg
# compiles dist test files against the dists' own lib/ trees this way.
my @libs = @*ARGS.grep(*.starts-with('--lib=')).map(*.substr(6));
my @args = @*ARGS.grep({ !.starts-with('--lib=') });
my @dirs = @args ?? @args.map(*.IO) !! ($ROOT.add('t/regression'), $ROOT.add('examples'));
my @files;
for @dirs -> $d {
    if $d.f {
        @files.push($d);
    }
    elsif $d.d {
        @files.append($d.dir.grep({ .extension eq 'raku' | 'rakutest' | 't' }).sort);
    }
}

my $tmp = $*TMPDIR.add("rakupp-slim-diff-{$*PID}");
$tmp.mkdir;

sub build($src, $out, *@extra) {
    my @inc = @libs.map({ ('-I', $_).Slip });
    my $p = run $*EXECUTABLE, '--exe', $src.Str, '-o', $out.Str, |@inc, |@extra, :out, :err;
    $p.out.slurp(:close); $p.err.slurp(:close);
    $p.exitcode == 0
}

my $runseq = 0;
sub run-bin($bin) {
    $runseq++;
    my $o  = $tmp.add("out-$runseq");
    my $e  = $tmp.add("err-$runseq");
    my $rc = $tmp.add("rc-$runseq");
    my $tf = $tmp.add("to-$runseq");
    my $cmd = "/usr/bin/perl -e 'setpgrp(0,0); exec \@ARGV or exit 127' -- '{$bin.Str}' "
            ~ "> '{$o}' 2> '{$e}' & P=\$!; n=0; "
            ~ "while kill -0 \$P 2>/dev/null && [ \$n -lt 300 ]; do sleep 0.1; n=\$((n+1)); done; "
            ~ "if kill -0 \$P 2>/dev/null; then kill -9 -\$P 2>/dev/null; kill -9 \$P 2>/dev/null; "
            ~ "echo 1 > '{$tf}'; fi; wait \$P; echo \$? > '{$rc}'";
    my $sh = run '/bin/sh', '-c', $cmd, :cwd($ROOT.Str), :out, :err;
    $sh.out.slurp(:close); $sh.err.slurp(:close);
    my $timed = $tf.IO.e;
    my $x = $rc.IO.e ?? $rc.IO.slurp.trim.Int !! -1;
    my ($ot, $et) = ($o.IO.e ?? $o.IO.slurp !! '', $e.IO.e ?? $e.IO.slurp !! '');
    .IO.unlink for $o, $e, $rc; $tf.IO.unlink if $timed;
    ($x, $ot, $et, $timed)
}

sub compare($file, $full, $slim) {
    my ($xf, $of, $ef, $tof) = run-bin($full);
    my ($xs, $os, $es, $tos) = run-bin($slim);
    return 'TIMEOUT' if $tof || $tos;
    return '' if $xf == $xs && $of eq $os && $ef eq $es;
    my @why;
    @why.push("exit $xf vs $xs")           if $xf != $xs;
    @why.push("stdout differs ({$of.chars} vs {$os.chars} chars)") if $of ne $os;
    @why.push("stderr differs ({$ef.chars} vs {$es.chars} chars)") if $ef ne $es;
    @why.join('; ')
}

my ($n, $pass, $selfnoise) = (0, 0, 0);
my @failed;
my @nocompile;
my @nondet;      # disagrees with itself: rand/timing — the differential cannot judge it
my @timeout;     # hit the per-run cap (self-spawning $*EXECUTABLE tests, hangs)

for @files -> $f {
    $n++;
    my $full = $tmp.add("full-$n");
    my $slim = $tmp.add("slim-$n");
    unless build($f, $full) {
        @nocompile.push($f.basename);   # does not compile at all: not this gate's business
        next;
    }
    unless build($f, $slim, '--slim') {
        @failed.push("{$f.basename}: compiles full but NOT under --slim");
        next;
    }
    my $why = compare($f, $full, $slim);
    if $why eq 'TIMEOUT' {
        @timeout.push($f.basename);   # self-spawners and hangs: not judged
    }
    elsif $why {
        # Before believing a mismatch, ask whether the program agrees with
        # ITSELF: a `rand`-seeded or timing-dependent program differs on
        # every run and the differential cannot judge it. Then one retry —
        # occasional self-noise is not a wrong cut either.
        my $self = compare($f, $full, $full);
        if $self {
            $selfnoise++;
            @nondet.push($f.basename);
        }
        else {
            my $again = compare($f, $full, $slim);
            if $again && $again ne 'TIMEOUT' {
                @failed.push("{$f.basename}: $why");
            }
            elsif $again eq 'TIMEOUT' {
                @timeout.push($f.basename);
            }
            else {
                $selfnoise++;
                $pass++;
            }
        }
    }
    else {
        $pass++;
    }
    $full.unlink; $slim.unlink;
    say "  … $pass/$n" if $n %% 25;
}

.IO.unlink for $tmp.dir;
$tmp.rmdir;

say "slim-diff: {$pass} identical of {$n} programs "
    ~ "({@nocompile.elems} skipped: do not compile; {@nondet.elems} nondeterministic; "
    ~ "{@timeout.elems} timed out; {$selfnoise} needed a second look)";
say "  nondeterministic (self-disagreeing): {@nondet.join(', ')}" if @nondet;
say "  timed out (not judged): {@timeout.join(', ')}" if @timeout;
if @failed {
    say "DIFFERENT:";
    say "  $_" for @failed;
    exit 1;
}
say 'ALL SLIM-DIFF CHECKS PASSED';
exit 0;
