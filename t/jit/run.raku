#!/usr/bin/env raku
# The --jit differential gate (docs/dev/plans/JIT-PLAN.md, gate 1).
#
# Every program is run TWICE by the same binary — once plainly, once with the
# JIT forced on — and the two runs are compared byte for byte: stdout, stderr
# and exit status. The interpreter is the oracle, not a golden file, which is
# the same rule the --target=js gate follows: a case cannot rot into agreeing
# with a wrong expectation, and a new case needs no expected output written by
# hand.
#
# The JIT lane runs `--jit=sync,threshold=0,nocache`:
#
#   sync         the compile finishes before the loop continues, so a case
#                cannot pass merely by being too short to tier up
#   threshold=0  every eligible loop tiers up on its first iteration, so a
#                two-iteration loop in a test is exercised as hard as a
#                two-million-iteration one in a benchmark
#   nocache      each run compiles what it needs, so the gate never reports on
#                a kernel some earlier run left on disk
#
# With `--cnp` the SECOND lane is the copy-and-patch backend instead
# (docs/dev/plans/CNP-PLAN.md), run as `--cnp=threshold=0`. The two backends
# share this harness, this corpus and this oracle, which is the point: they are
# two ways of compiling the same loops and neither may disagree with the
# interpreter. There is no `sync` or `nocache` in that lane because there is no
# background compile and no cache to bypass.
#
#   rakupp t/jit/run.raku                    # t/jit/cases, then t/regression + examples
#   rakupp t/jit/run.raku t/jit/cases        # one directory (or a list of files)
#   rakupp t/jit/run.raku --quick            # the JIT's own cases only
#   rakupp t/jit/run.raku --cnp              # the copy-and-patch backend
#   rakupp t/jit/run.raku --cnp t/cnp/cases  # and its own cases
#
# Exit 1 on any disagreement.

my $ROOT = $*PROGRAM.parent.parent.parent;
my $rakupp = $*EXECUTABLE;

my @args = @*ARGS;
my $quick = so @args.grep('--quick');
my $cnp   = so @args.grep('--cnp');
@args = @args.grep({ $_ ne '--quick' && $_ ne '--cnp' });
# What the second lane is, and what to call it in a message.
my @lane  = $cnp ?? ('--cnp=threshold=0',) !! ('--jit=sync,threshold=0,nocache',);
my $LANE  = $cnp ?? '--cnp' !! '--jit';

# The copy-and-patch lane needs a binary that HAS that backend, and three
# supported configurations do not have it: a build with no stencil table (a
# cross-compile, or the universal macOS build, which has no single instruction
# set to extract for), an instruction set with no patcher, and x86-64, whose
# patcher is written but unverified and gated off until it is not
# (CNP-PLAN.md P1). Each prints ONE line to stderr and runs interpreted, so
# every case in the corpus would be reported as a stderr disagreement and the
# run would prove nothing about a backend that never ran. Nothing to compare is
# not a failure and it is not a pass either, so say which it is and stop.
if $cnp {
    my $line = run($rakupp.Str, '-V', :out).out.slurp(:close).lines.first(*.starts-with('Cnp')) // '';
    unless $line.contains('copy-and-patch stencils for') {
        my $why = $line.contains('—') ?? $line.split('—', 2)[1].trim !! 'no copy-and-patch backend';
        say "--cnp is not live in this binary, so both lanes would be the interpreter";
        say "and there is nothing to compare: $why";
        exit 0;
    }
}

# Programs whose output is not a function of their source alone, so two runs of
# the SAME binary need not agree with each other and the comparison says nothing.
my %skip =
    'life.raku'         => 'random seed',
    'parallel.raku'     => 'threads',
    'sleep-sort.raku'   => 'threads',
    'echo-server.raku'  => 'sockets',
    'rand.raku'         => 'random',
    # A CSPRNG test that PRINTS its measured statistics: two plain runs of it
    # disagree with each other, so the comparison says nothing about the JIT.
    'data-native-random.raku' => 'prints measured randomness statistics',
    # Hard-links a fixture into $TMPDIR and reports a SKIP line when the link
    # cannot be made — which depends on which filesystem $TMPDIR happens to be
    # on and varies between consecutive runs of the same binary. Measured: the
    # line appears in one plain interpreter run out of five.
    'rakupp-upgrade.raku' => 'a filesystem-dependent skip line, nondeterministic run to run',
    ;

# The copy-and-patch backend runs the JIT's cases too — the two lower the same
# whitelist — plus the cases that are about its own machinery.
my @caseDirs = $cnp ?? ($ROOT.add('t/jit/cases'), $ROOT.add('t/cnp/cases'))
                    !! ($ROOT.add('t/jit/cases'),);
my @dirs = @args ?? @args.map(*.IO)
                 !! ($quick ?? @caseDirs
                            !! (|@caseDirs, $ROOT.add('t/regression'), $ROOT.add('examples')));
my @files;
for @dirs -> $d {
    if $d.d    { @files.append: $d.dir.grep({ .extension eq 'raku' }).sort(*.Str) }
    elsif $d.f { @files.push: $d }
    else       { note "no such file or directory: $d"; exit 2 }
}

sub capture($file, *@flags) {
    my $p = run $rakupp.Str, |@flags, $file.Str, :out, :err;
    my $o = $p.out.slurp(:close);
    my $e = $p.err.slurp(:close);
    ($p.exitcode, $o, $e)
}

my ($agree, $differ, $skipped) = 0, 0, 0;
my @bad;

for @files -> $f {
    if %skip{$f.basename} -> $why { $skipped++; next }
    my ($rc0, $out0, $err0) = capture($f);
    my ($rc1, $out1, $err1) = capture($f, |@lane);
    if $rc0 == $rc1 && $out0 eq $out1 && $err0 eq $err1 {
        $agree++;
    }
    else {
        $differ++;
        my $what = do given True {
            when $out0 ne $out1 { "stdout" }
            when $err0 ne $err1 { "stderr" }
            default             { "exit status ($rc0 vs $rc1)" }
        };
        @bad.push: "$($f.relative($ROOT)) — $what";
        say "not ok - $($f.relative($ROOT)): $what differs under $LANE";
        if $out0 ne $out1 {
            say "      plain: " ~ $out0.lines.head(3).join('\n');
            say "      $LANE: " ~ $out1.lines.head(3).join('\n');
        }
        elsif $err0 ne $err1 {
            say "      plain: " ~ $err0.lines.head(3).join('\n');
            say "      $LANE: " ~ $err1.lines.head(3).join('\n');
        }
    }
}

# The backends' own cases carry a second requirement the corpus cannot: they
# exist to be tiered up, so a case that agrees only because nothing compiled is
# not evidence. `stats` says how many kernels ran.
my $tiered = 0;
if !@args {
    my @want = @caseDirs.map({ .dir.grep({ .extension eq 'raku' }) }).flat
                        .grep({ !.lines[0].starts-with('# JIT: refused' | '# CNP: refused') }).sort(*.Str);
    for @want -> $f {
        my $p = run $rakupp.Str, |@lane.map({ $_ ~ ',stats' }), $f.Str, :out, :err;
        $p.out.slurp(:close);
        my $e = $p.err.slurp(:close);
        $tiered++ if $e ~~ / 'kernels entered ' (\d+) / && +$0 > 0;
    }
    # A case whose first line is `# JIT: refused` or `# CNP: refused` exists to
    # be TURNED DOWN, and
    # its answer is the proof the guard fired. Every other case must actually
    # enter a kernel: one that agrees with the interpreter only because nothing
    # compiled is not evidence of anything, and that is the way this gate would
    # rot without noticing.
    if $tiered < @want.elems {
        say "not ok - only $tiered of the {@want.elems} tier-up cases actually entered a kernel";
        @bad.push: "tier-up coverage";
    }
    else { say "ok - $tiered of the {@want.elems} tier-up cases entered a kernel" }
}

say "";
say "agreeing  $agree";
say "differing $differ";
say "skipped   $skipped" if $skipped;
exit @bad ?? 1 !! 0;
