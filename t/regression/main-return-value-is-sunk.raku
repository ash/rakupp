# Regression: `sub MAIN`'s return value is SUNK.
#
# A program whose last act is `run @cmd` must exit non-zero when the command
# failed: the Proc MAIN hands back is sunk, and Proc.sink throws
# X::Proc::Unsuccessful (Rakudo). sparrowdo is written exactly that way — its
# MAIN ends in an if/elsif chain whose branch calls a sub ending in `run @cmd`
# — and reported an exit code of 0 for a failed container run (issue #73).
#
# Up to v3.25.0 this worked BY ACCIDENT: the last statement of ANY sub ran in
# sink context, so the Proc detonated one frame too early. That was its own
# bug — `sub run-it { run |@cmd }; my $p = run-it; $p.exitcode` died on a
# value the caller had asked for — and removing it (the ExprStmt sink, "only
# when SUNK") left MAIN's own value unsunk, because the auto-invoke discarded
# what callCallable returned. Now it goes through the sink everywhere MAIN is
# dispatched: the interpreter, --exe, --repl-after and --target=js (where the
# value doubled as the process exit code, so `sub MAIN() { 42 }` exited 42).
#
# Every row runs a CHILD program and reads its exit code, which is the whole
# observable — a message on stderr with no exit code would not have fixed the
# reporter's build. Verified against Rakudo.
# Contract: exit 0 + last line PASS.

my @fail;
sub check($what, $got, $want) {
    @fail.push("$what: got {$got.raku} want {$want.raku}") unless $got eq $want;
}
sub like-check($what, $got, $want) {
    @fail.push("$what: got {$got.raku} want /$want/") unless $got.contains($want);
}

# The child's exit code AND everything it said. A distinct exit code per row
# (7, 9, 5) keeps one row's answer from standing in for another's.
sub child(Str $code) {
    my $p = run($*EXECUTABLE, '-e', $code, :out, :err);
    my $said = ($p.out.slurp(:close) ~ $p.err.slurp(:close)).chomp;
    ($p.exitcode, $said)
}

my ($rc, $out);

# --- the reported shape: MAIN ends in a call that ends in `run` -------------
($rc, $out) = child q:to/CODE/;
    sub inner { run $*EXECUTABLE, '-e', 'exit 7' }
    sub MAIN() { if True { inner() } }
    CODE
check      'a failed run at the end of MAIN exits non-zero', $rc, 1;
like-check '…naming the exit code the command gave', $out, 'exit code: 7, signal: 0';

# an explicit `return` of the Proc is the same value in the same place
($rc, $out) = child q:to/CODE/;
    sub MAIN() { return run $*EXECUTABLE, '-e', 'exit 9' }
    CODE
check      '`return run …` from MAIN exits non-zero', $rc, 1;
like-check '…with that command\'s exit code', $out, 'exit code: 9, signal: 0';

# --- the other half of the sink rule: a Failure MAIN returns detonates ------
($rc, $out) = child q:to/CODE/;
    sub MAIN() { my $x = +"not-a-number-73"; $x }
    CODE
check      'a Failure returned from MAIN detonates', $rc, 1;
like-check '…saying what could not be converted', $out, 'not-a-number-73';

# --- …and a user `sink` method runs, as it does for a mainline statement ----
($rc, $out) = child q:to/CODE/;
    class C { method sink { note "SUNK-73-marker" } }
    sub MAIN() { C.new }
    CODE
check      'an object returned from MAIN is sunk', $rc, 0;
like-check '…through its own sink method', $out, 'SUNK-73-marker';

# --- what must NOT come back: the over-eager sink v3.26.0 removed -----------
# The caller ASKED for this Proc; sinking a sub's last statement killed it.
($rc, $out) = child q:to/CODE/;
    sub run-it { run $*EXECUTABLE, '-e', 'exit 5' }
    my $p = run-it;
    say "exitcode-{$p.exitcode}";
    CODE
check 'a Proc the caller keeps is not sunk', $rc, 0;
check '…and still carries its exit code',   $out, 'exitcode-5';

# a command that SUCCEEDED says nothing and changes nothing
($rc, $out) = child q:to/CODE/;
    sub MAIN() { run $*EXECUTABLE, '-e', 'exit 0' }
    CODE
check 'a successful run at the end of MAIN is quiet', $rc, 0;
check '…and says nothing',                            $out, '';

# MAIN's ordinary value is SUNK, not adopted as the exit status
($rc, $out) = child q:to/CODE/;
    sub MAIN() { 42 }
    CODE
check 'an Int from MAIN is not the exit code', $rc, 0;
check '…and is not printed either',            $out, '';

# --- --exe: the compiled binary dispatches MAIN through its own entry point -
my $v = run($*EXECUTABLE, '--version', :out, :err);
my $banner = $v.out.slurp(:close); $v.err.slurp(:close);
if $banner.contains('rakupp') {
    my $work = $*TMPDIR.add("main-sink-$*PID");
    mkdir $work;
    my $src = $work.add('m.raku');
    # the interpreter's path comes in as an argument: inside a compiled binary
    # $*EXECUTABLE is that binary, which refuses -e (the fork-bomb guard)
    $src.spurt: q:to/CODE/;
        sub inner($exe) { run $exe, '-e', 'exit 7' }
        sub MAIN($exe) { if True { inner($exe) } }
        CODE
    my $exe = $work.add('m');
    my $c = run($*EXECUTABLE, '--exe', ~$src, '-o', ~$exe, :out, :err);
    my $cout = $c.out.slurp(:close) ~ $c.err.slurp(:close);
    if $c.exitcode == 0 && $exe.e {
        my $p = run(~$exe, ~$*EXECUTABLE, :out, :err);
        my $said = ($p.out.slurp(:close) ~ $p.err.slurp(:close)).chomp;
        check      'a compiled MAIN sinks its value too', $p.exitcode, 1;
        like-check '…with the same message',              $said, 'exit code: 7, signal: 0';
    }
    else {
        note "main-return-value-is-sunk: --exe unavailable, skipping that row ($cout)";
    }

    # --- --target=js: the third lane MAIN is dispatched through --------------
    # Skipped where there is no node. `run` under the JS host spawns for real,
    # so this is the same program, judged the same way.
    my $jssrc = $work.add('j.raku');
    $jssrc.spurt: q:to/CODE/;
        sub inner($exe) { run $exe, '-e', 'exit 7' }
        sub MAIN($exe) { if True { inner($exe) } }
        CODE
    my $jsout = $work.add('j.js');
    my $jc = run($*EXECUTABLE, '--target=js', '--standalone', ~$jssrc, '-o', ~$jsout, :out, :err);
    $jc.out.slurp(:close); $jc.err.slurp(:close);
    my $node = do { my $n = try run('node', '--version', :out, :err);
                    $n andthen do { .out.slurp(:close); .err.slurp(:close) };
                    $n.defined && $n.exitcode == 0 };
    if $jc.exitcode == 0 && $jsout.e && $node {
        my $p = run('node', ~$jsout, ~$*EXECUTABLE, :out, :err);
        my $said = ($p.out.slurp(:close) ~ $p.err.slurp(:close)).chomp;
        check      'a transpiled MAIN sinks its value too', $p.exitcode, 1;
        like-check '…with the same message',                $said, 'exit code: 7, signal: 0';

        # …and an Int MAIN returns is SUNK, not adopted as the exit status
        $jssrc.spurt: "sub MAIN() \{ 42 }\n";
        my $j2 = run($*EXECUTABLE, '--target=js', '--standalone', ~$jssrc, '-o', ~$jsout, :out, :err);
        $j2.out.slurp(:close); $j2.err.slurp(:close);
        my $q = run('node', ~$jsout, :out, :err);
        $q.out.slurp(:close); $q.err.slurp(:close);
        check 'an Int from a transpiled MAIN is not the exit code', $q.exitcode, 0;
    }
    else {
        note 'main-return-value-is-sunk: no node or no js backend, skipping the --target=js rows';
    }
    unlink $jsout if $jsout.e;
    unlink $jssrc if $jssrc.e;

    unlink $exe if $exe.e;
    unlink $src if $src.e;
    rmdir $work if $work.e;
}
else {
    note 'main-return-value-is-sunk: not rakupp, skipping the --exe row';
}

if @fail { note "FAILED: @fail.join('; ')"; say 'FAIL' } else { say 'PASS' }
