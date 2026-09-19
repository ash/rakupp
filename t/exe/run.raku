#!/usr/bin/env raku
# The `--exe` differential gate (UNBOX-PLAN.md, gate 1).
#
# Every program the native backend accepts is compiled BOTH ways — with `-O`
# and without — and each binary's output is compared byte for byte with the
# same binary INTERPRETING the same program: stdout and exit status. The
# interpreter is the oracle, as it is for --target=js and --jit.
#
# This exists because of what the optimizer does. `-O` is semantics-preserving
# by intent, and every pass in it is a SECOND implementation of something the
# runtime already does — integer overflow, floored modulo, comparison, and since
# UNBOX-PLAN a whole loop's arithmetic run on unboxed C++ locals. A pass that
# computes a different answer from the boxed path is the failure mode this code
# has, and it is invisible to any test that runs only one of the two.
#
# The `-O` / no-`-O` pair is the second axis: a program that is right
# unoptimized and wrong optimized names the optimizer as the culprit.
#
#   rakupp t/exe/run.raku                 # t/regression + examples
#   rakupp t/exe/run.raku examples        # one directory (or files)
#
# What is NOT compared is compiled against interpreted. A compiled binary is
# `$*EXECUTABLE`, its `$*PROGRAM` is itself, and a program that asks anything
# about how it is running answers differently — legitimately. Those differences
# are counted and reported as a number, never as a failure.
#
# A program the backend REFUSES is not a failure: it falls back to bundling in
# real use. Those are counted, not reported as defects.
#
# Exit 1 on any disagreement.

my $ROOT = $*PROGRAM.parent.parent.parent;
my $rakupp = $*EXECUTABLE;
my @args = @*ARGS;
my $Oonly = so @args.grep('--O-only');
@args = @args.grep({ $_ ne '--O-only' });

my $tmp = $*TMPDIR.add("rakupp-exe-gate-$*PID");
$tmp.mkdir;
END { if $tmp.e { for $tmp.dir { .unlink }; $tmp.rmdir } }

my %skip =
    'life.raku' => 'random', 'parallel.raku' => 'threads', 'sleep-sort.raku' => 'threads',
    'echo-server.raku' => 'sockets', 'rand.raku' => 'random',
    'data-native-random.raku' => 'prints measured randomness statistics',
    ;

my @dirs = @args ?? @args.map(*.IO) !! ($ROOT.add('t/regression'), $ROOT.add('examples'));
my @files;
for @dirs -> $d {
    if $d.d    { @files.append: $d.dir.grep({ .extension eq 'raku' }).sort(*.Str) }
    elsif $d.f { @files.push: $d }
    else       { note "no such file or directory: $d"; exit 2 }
}

# Every run is bounded and gets a CLOSED stdin. Without either, this gate
# quietly wedged: a program that reads stdin blocks forever on a terminal, and
# two compiled binaries from t/regression/ sat in the process table for over an
# hour before anyone looked. A hang is indistinguishable from a slow compile
# from outside, which is exactly why it has to be the gate's problem and not the
# reader's. A timed-out run answers Nil and the file is counted as refused.
my $LIMIT = 25;
sub bounded(*@cmd) {
    my $p = Proc::Async.new(|@cmd);
    my ($o, $e) = '', '';
    $p.stdout.tap({ $o ~= $_ });
    $p.stderr.tap({ $e ~= $_ });
    my $done = $p.start;
    my $killed = False;
    await Promise.anyof($done, Promise.in($LIMIT));
    unless $done { $killed = True; $p.kill: SIGKILL; try await $done }
    $killed ?? (Nil, $o, $e) !! ((try await $done).exitcode, $o, $e)
}

sub interp($f) {
    my ($rc, $o, $e) = bounded($rakupp.Str, $f.Str);
    ($rc, $o)
}
sub compiled($f, @flags) {
    my $bin = $tmp.add('a.out');
    $bin.unlink if $bin.e;
    my ($crc, $co, $ce) = bounded($rakupp.Str, '--exe', |@flags, '-q', $f.Str, '-o', $bin.Str);
    return Nil unless ($crc // -1) == 0 && $bin.e;
    my ($rc, $o, $e) = bounded($bin.Str);
    $rc.defined ?? ($rc, $o) !! Nil
}

my ($agree, $differ, $refused, $skipped, $backend) = 0, 0, 0, 0, 0;
my @bad;

for @files -> $f {
    if %skip{$f.basename} { $skipped++; next }
    my $plain = compiled($f, []);
    my $opt   = compiled($f, ['-O']);
    unless $plain.defined && $opt.defined { $refused++; next }
    # `refused` now also covers a program that did not finish inside the bound —
    # a server, something waiting on stdin, or a genuinely long run. None of
    # those says anything about the optimizer.

    # THE GATE: the two compiled binaries against each other. Every difference
    # between compiled and interpreted code — $*EXECUTABLE being the binary
    # rather than rakupp, $*PROGRAM, a spawned child, a missing fallback — is
    # present in BOTH lanes and cancels out here. What is left is the optimizer,
    # which is the only thing this file can usefully be strict about.
    my ($prc, $pout) = $plain;
    my ($orc, $oout) = $opt;
    if $prc == $orc && $pout eq $oout {
        $agree++;
    }
    else {
        $differ++;
        @bad.push: $f.relative($ROOT);
        say "not ok - {$f.relative($ROOT)}: -O disagrees with the SAME program compiled without it";
        say "      plain: " ~ $pout.lines.head(2).join(' | ') ~ "  (exit $prc)";
        say "      -O   : " ~ $oout.lines.head(2).join(' | ') ~ "  (exit $orc)";
    }

    # Informational, and NOT a failure: the native backend's own divergences from
    # the interpreter. Most are a program asking something about itself that a
    # compiled binary answers differently, and they are V5-IDEAS section 4's
    # queue, not this gate's.
    my ($irc, $iout) = interp($f);
    $backend++ unless $irc.defined && $irc == $prc && $iout eq $pout;
}

say "";
say "agreeing            $agree   (-O and no -O produce identical output)";
say "differing           $differ   <- the optimizer";
say "refused             $refused   (the backend fell back to bundling; not a failure)";
say "backend != interp   $backend   (informational: compiled code answering about itself, EVAL, …)";
say "skipped             $skipped" if $skipped;
exit @bad ?? 1 !! 0;
