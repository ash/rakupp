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
# Every child runs with stdin CLOSED. A corpus program that reads stdin (the
# `-ne` one-liners) otherwise parks forever waiting for input nobody sends, and
# the cap below did not always reap them: one release run left ~2,200 of them
# alive on the machine, quiet enough that only the process count showed it.
# Every run is capped (30s) and killed BY PROCESS TREE: some corpus files spawn
# $*EXECUTABLE, which in a compiled binary is the binary itself — the spawn
# re-runs the embedded program and recurses forever (measured: one such binary
# ate 25 CPU-minutes). macOS has no setsid/timeout binaries, so a perl setpgrp
# shim starts the group and plain sh watches and kills it; output goes to FILES,
# because a pipe inherited by orphaned grandchildren never closes.
# A file that times out on any side is reported by name and not judged.
#
# A group kill is NOT enough, and believing it was is how gate 4b reported
# `ALL SLIM-DIFF CHECKS PASSED` in the v3.21.0 sitting while leaving 1,253
# processes and load average 450 behind it. Every child rakupp spawns calls
# setpgid(0,0) (src/Builtins.cpp — so that rakupp's OWN run(:timeout) can reap
# grandchildren), which makes each generation its own group leader: `kill -9 -$P`
# reaches generation one and nothing after it. So the reaper below walks
# ps(1) for the whole TREE, kills deepest-first, and repeats — a self-spawning
# chain grows while it is being killed. It must run BEFORE `wait`, while the
# links still exist: an orphan re-parents to launchd and is unfindable from $P.
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

# The tree reaper, written out once and invoked by each watcher at its timeout.
# Perl because the shim already depends on /usr/bin/perl and POSIX sh has no
# sane way to walk a process tree.
my $reaper = $tmp.add('reap.pl');
$reaper.spurt: q:to/PERL/;
    # kill the process TREE rooted at each PID given, deepest-first, repeatedly:
    # the chain spawns while it is being killed, and every generation is its own
    # process-group leader, so one `kill -9 -PGID` misses all but the first.
    my @roots = @ARGV;
    for (1 .. 8) {
        my %kids;
        open(my $ps, '-|', 'ps', '-Ao', 'pid=,ppid=') or last;
        while (<$ps>) { next unless /\s*(\d+)\s+(\d+)/; push @{ $kids{$2} }, $1; }
        close $ps;
        # NOT `my (@order, @q) = ((), @roots)` — a list assignment lets the first
        # array slurp everything, leaving @q empty and the walk never running.
        my @order;
        my @q = @roots;
        while (@q) { my $x = shift @q; for my $k (@{ $kids{$x} || [] }) { push @order, $k; push @q, $k; } }
        last unless @order;
        kill 'KILL', reverse @order;   # deepest first, so none outlives its parent
        select undef, undef, undef, 0.1;
    }
    kill 'KILL', @roots;
    PERL

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
            ~ "< /dev/null > '{$o}' 2> '{$e}' & P=\$!; n=0; "
            ~ "while kill -0 \$P 2>/dev/null && [ \$n -lt 300 ]; do sleep 0.1; n=\$((n+1)); done; "
            ~ "if kill -0 \$P 2>/dev/null; then "
            ~ "/usr/bin/perl '{$reaper}' \$P 2>/dev/null; "
            ~ "kill -9 -\$P 2>/dev/null; kill -9 \$P 2>/dev/null; "
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
    # NOTE on programs that spawn $*EXECUTABLE (49 of this corpus do): under the
    # interpreter that reaches rakupp, but COMPILED it is the binary itself, which
    # carries one program — so the spawn re-runs the program that did the spawning.
    # Two sittings measured where that goes: 1,253 processes at load 450, and
    # 2,633 at load 95. They are still judged here, and correctly: both sides of
    # this comparison are compiled, so both refuse the re-entry identically and the
    # --slim question is still answered. What makes that safe is the guard in the
    # binary (rakuppRefuseInterpreterEval), which bounds the chain at one
    # generation — not this harness noticing the shape.
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

# Nothing this run compiled may still be running. Gate 4b reported
# `ALL SLIM-DIFF CHECKS PASSED` while leaving 1,253 processes and load average
# 450 behind it, and the only reason anybody noticed was the machine's fans; a
# gate that reports green while doing damage is worse than one that fails. Every
# binary this run built lives under $tmp, so its path in a command line is
# unambiguous evidence — no pattern-matching on the word "rakupp" required.
sub survivors() {
    my $p = run 'ps', '-Ao', 'pid=,command=', :out, :err;
    my $t = $p.out.slurp(:close); $p.err.slurp(:close);
    $t.lines.grep(*.contains($tmp.Str)).map({ .trim.words[0].Int }).grep(* != $*PID)
}
my @leaked = survivors();
if @leaked {
    # kill the trees, then look again — what survives THAT is worth reporting
    run '/usr/bin/perl', $reaper.Str, |@leaked>>.Str, :out, :err;
    sleep 0.5;
}

.IO.unlink for $tmp.dir;
$tmp.rmdir;

say "slim-diff: {$pass} identical of {$n} programs "
    ~ "({@nocompile.elems} skipped: do not compile; {@nondet.elems} nondeterministic; "
    ~ "{@timeout.elems} timed out; {$selfnoise} needed a second look)";
# NAME the programs that did not compile. The count alone said "23 skipped: do
# not compile" and stopped there, so a construct the code generator cannot
# compile at all is invisible here — and `--exe` is a second implementation of
# the language, which is precisely where a bug can live and nowhere else.
# Found this way in v3.22.0: `sub f([$b, @c])` — sub-signature destructuring —
# emits C++ referencing an undeclared `v_ac` and fails to build. slim-diff called
# that "not this gate's business" and optbench only ever tries its own kernels,
# so nothing in the release was looking at it.
say "  do not compile ({@nocompile.elems}): {@nocompile.join(', ')}" if @nocompile;
say "  nondeterministic (self-disagreeing): {@nondet.join(', ')}" if @nondet;
say "  timed out (not judged): {@timeout.join(', ')}" if @timeout;
if @failed {
    say "DIFFERENT:";
    say "  $_" for @failed;
    exit 1;
}
if @leaked {
    say "LEAKED PROCESSES: {@leaked.elems} process\{@leaked.elems == 1 ?? '' !! 'es'\} from this run were still";
    say "  alive at the end of it (killed now). A run that leaves a self-spawning chain";
    say "  behind has not proved anything about the programs it was judging.";
    say "  PIDs: {@leaked.sort.join(', ')}";
    exit 1;
}
say 'ALL SLIM-DIFF CHECKS PASSED';
exit 0;
