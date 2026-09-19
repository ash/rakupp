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
#   rakupp t/jit/run.raku                    # t/jit/cases, then t/regression + examples
#   rakupp t/jit/run.raku t/jit/cases        # one directory (or a list of files)
#   rakupp t/jit/run.raku --quick            # the JIT's own cases only
#
# Exit 1 on any disagreement.

my $ROOT = $*PROGRAM.parent.parent.parent;
my $rakupp = $*EXECUTABLE;

my @args = @*ARGS;
my $quick = so @args.grep('--quick');
@args = @args.grep({ $_ ne '--quick' });

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

my @dirs = @args ?? @args.map(*.IO)
                 !! ($quick ?? ($ROOT.add('t/jit/cases'),)
                            !! ($ROOT.add('t/jit/cases'), $ROOT.add('t/regression'), $ROOT.add('examples')));
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
    my ($rc1, $out1, $err1) = capture($f, '--jit=sync,threshold=0,nocache');
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
        say "not ok - $($f.relative($ROOT)): $what differs under --jit";
        if $out0 ne $out1 {
            say "      plain: " ~ $out0.lines.head(3).join('\n');
            say "      --jit: " ~ $out1.lines.head(3).join('\n');
        }
        elsif $err0 ne $err1 {
            say "      plain: " ~ $err0.lines.head(3).join('\n');
            say "      --jit: " ~ $err1.lines.head(3).join('\n');
        }
    }
}

# The JIT's own cases carry a second requirement the corpus cannot: they exist
# to be tiered up, so a case that agrees only because nothing compiled is not
# evidence. `--jit=stats` says how many kernels ran.
my $tiered = 0;
if !@args {
    for $ROOT.add('t/jit/cases').dir.grep({ .extension eq 'raku' })
             .grep({ !.lines[0].starts-with('# JIT: refused') }).sort(*.Str) -> $f {
        my $p = run $rakupp.Str, '--jit=sync,threshold=0,nocache,stats', $f.Str, :out, :err;
        $p.out.slurp(:close);
        my $e = $p.err.slurp(:close);
        $tiered++ if $e ~~ / 'kernels entered ' (\d+) / && +$0 > 0;
    }
    # A case whose first line is `# JIT: refused` exists to be TURNED DOWN, and
    # its answer is the proof the guard fired. Every other case must actually
    # enter a kernel: one that agrees with the interpreter only because nothing
    # compiled is not evidence of anything, and that is the way this gate would
    # rot without noticing.
    my $want = $ROOT.add('t/jit/cases').dir.grep({ .extension eq 'raku' })
                    .grep({ !.lines[0].starts-with('# JIT: refused') }).elems;
    if $tiered < $want {
        say "not ok - only $tiered of the $want tier-up cases actually entered a kernel";
        @bad.push: "tier-up coverage";
    }
    else { say "ok - $tiered of the $want tier-up cases entered a kernel" }
}

say "";
say "agreeing  $agree";
say "differing $differ";
say "skipped   $skipped" if $skipped;
exit @bad ?? 1 !! 0;
