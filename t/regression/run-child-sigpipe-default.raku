# Regression: a child must start with SIGPIPE at its default, not our SIG_IGN.
#
# The engine ignores SIGPIPE process-wide (Runtime.cpp), so a write to a closed
# socket is an EPIPE it can raise instead of a death. SIG_IGN survives execve,
# and every child of run, shell, qx and Proc::Async inherited it. A writer whose
# reader went away therefore got EPIPE instead of being killed by SIGPIPE, and
# lived on to complain: `shell 'yes | head -n 1'` printed "yes: stdout: Broken
# pipe" on stderr, and a `yes` whose stdout fed `head` exited 1 instead of dying
# of signal 13. The fix is forkForExec (src/ChildSignals.h): SIGPIPE and any
# handled signal go back to SIG_DFL in the child, and its signal mask is cleared,
# before exec. qx used popen(), which can do neither, and now spawns as shell()
# does.
#
# Every row but the last two is checked against Rakudo, whose children (libuv)
# start with default dispositions and an empty mask.

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

if $*DISTRO.is-win {
    say "ok - skipped on Windows: no SIGPIPE";
    say "PASS";
    exit 0;
}

my $pp = $*RAKU.compiler.name eq 'Raku++';
my $pipeline = 'yes | head -n 1';   # `head` exits after one line; `yes` must die quietly

# --- shell(): the writer in the pipeline is a grandchild ---------------------
my $s = shell($pipeline, :out, :err);
ck($s.out.slurp(:close), "y\n", 'shell(): the pipeline produces its line');
ck($s.err.slurp(:close), '',    'shell(): and the cut-off writer says nothing');

# --- run() of sh, the same through the live-pipe spawn ------------------------
my $r = run('sh', '-c', $pipeline, :out, :err);
ck($r.out.slurp(:close), "y\n", 'run(): the pipeline produces its line');
ck($r.err.slurp(:close), '',    'run(): and the cut-off writer says nothing');

# --- run() with :in deferred: the spawn that feeds stdin (spawnWithInput) -----
my $null = open('/dev/null', :w);
my $d = run('sh', '-c', $pipeline, :in, :out($null), :err);
$d.in.close;
ck($d.err.slurp(:close), '', 'run(:in): the cut-off writer says nothing');
$null.close;

# --- Proc::Async ----------------------------------------------------------------
my ($ao, $ae) = '', '';
my $pa = Proc::Async.new('sh', '-c', $pipeline);
$pa.stdout.tap({ $ao ~= $_ });
$pa.stderr.tap({ $ae ~= $_ });
await $pa.start;
ck($ao, "y\n", 'Proc::Async: the pipeline produces its line');
ck($ae, '',    'Proc::Async: and the cut-off writer says nothing');

# --- qx, which went through popen() ----------------------------------------------
ck(qx/sh -c 'yes | head -n 1' 2>&1/, "y\n", 'qx: only the line, no "Broken pipe" after it');

# --- the mask: every signal is blocked across fork, and the child must not keep
#     that. A child that sends itself SIGTERM dies of it only when it is not
#     blocked; a blocked one stays pending and the shell goes on to say so.
#     (`ps -o sigmask` cannot be the probe: macOS's reports 0 regardless.)
my $term = run('sh', '-c', 'kill -TERM $$; echo survived', :out);
ck($term.out.slurp(:close), '', 'the child starts with SIGTERM unblocked…');
ck($term.signal, +SIGTERM, '…and dies of it');

if $pp {
    # --- a DIRECT child dies of SIGPIPE: rakupp hands `yes`'s stdout to `head`
    #     outright, so when `head` exits nobody is left reading. (Rakudo keeps a
    #     reader of its own on that pipe and buffers `yes` until it runs out of
    #     memory, so this row is ours alone.)
    my $yes  = run('yes', :out, :err);
    my $head = run('head', '-n', '1', :in($yes.out), :out);
    ck($head.out.slurp(:close), "y\n", 'yes piped into head: head reads its line');
    ck($yes.err.slurp(:close), '', 'yes says nothing when head goes away');
    ck($yes.signal, +SIGPIPE, 'yes is killed by SIGPIPE');

    # --- only OUR ignore is undone: a SIGHUP ignored when we started (nohup's
    #     shape) still reaches the child ignored. (libuv resets every signal, so
    #     under Rakudo the child dies of the HUP; this is a deliberate choice.)
    my $inner = q[print run('sh', '-c', 'kill -HUP $$; echo alive', :out).out.slurp(:close)];
    my $hup = run('sh', '-c', 'trap "" HUP; exec "$0" -e "$1"', ~$*EXECUTABLE, $inner, :out);
    ck($hup.out.slurp(:close), "alive\n", 'an inherited SIG_IGN is passed on unchanged');
}
else {
    say "ok - the direct-child and inherited-ignore rows are rakupp's own, skipped under {$*RAKU.compiler.name}";
}

say $fails ?? "FAIL ($fails)" !! "PASS";
exit $fails ?? 1 !! 0;
