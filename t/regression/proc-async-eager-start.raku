# Regression: Proc::Async.start spawns the process THERE AND THEN (issue #29).
# The old model ran the child only when the promise was realized, so a
# fire-and-forget `.start` with no await never launched anything — Sparky's
# detached workers simply did not exist. Rakudo's `.start` means "the process
# is running from this moment on", and a never-awaited child is an ordinary
# orphan the OS keeps after we exit. Same change made bind-stdin possible
# (both ends of the pipe run concurrently) and gave `.kill` a live pid.

my $ok = True;
sub ck($got, $want, $l) { unless $got eqv $want { note "FAIL: $l — {$got.raku} vs {$want.raku}"; $ok = False } }

# 1. The child runs from .start on — while we are merely sleeping, no await yet.
{
    my $m = $*TMPDIR.add("rakupp-eager-{$*PID}-1");
    $m.unlink;
    my $pr = Proc::Async.new('sh', '-c', "echo x > $m").start;
    my $seen = False;
    for ^100 { if $m.e { $seen = True; last }; sleep 0.1 }
    ck($seen, True, 'the process runs before any await');
    await $pr;
    $m.unlink;
}

# 2. Fire-and-forget: the parent exits at once, the child outlives it (#29's
#    `sleep 1000` shape, with a marker file instead of ps).
{
    my $m = $*TMPDIR.add("rakupp-eager-{$*PID}-2");
    $m.unlink;
    my $t0 = now;
    # the daemon sheds its inherited stdout/stderr first: run() captures the
    # inner rakupp's output to EOF, and a grandchild holding the pipe's write
    # end would hold run() open with it — under Rakudo just the same
    my $p = run($*EXECUTABLE, '-e',
        "Proc::Async.new('sh', '-c', 'exec >/dev/null 2>&1; sleep 2; echo alive > $m').start;");
    my $parent-took = now - $t0;
    ck($p.exitcode, 0, 'the fire-and-forget parent exits cleanly');
    ck($parent-took < 2, True, 'the parent does not wait for the child');
    my $seen = False;
    for ^150 { if $m.e { $seen = True; last }; sleep 0.1 }
    ck($seen, True, 'the child survives its parent and finishes its work');
    $m.unlink;
}

# 3. bind-stdin: one proc's stdout is the other's stdin, over a real pipe —
#    the docs' echo | cat -n pipeline, tapped on the far end.
{
    my $echo = Proc::Async.new: 'echo', 'Hello, world';
    my $cat  = Proc::Async.new: 'cat', '-n';
    my $out = '';
    $cat.stdout.tap({ $out ~= $_ });
    $cat.bind-stdin: $echo.stdout;
    await $echo.start, $cat.start;
    ck($out.trim.subst(/\s+/, ' ', :g), '1 Hello, world', 'bind-stdin pipes stdout to stdin');
}

# 4. .kill sends a real signal now that a real process exists to receive it.
{
    my $t0 = now;
    my $p = Proc::Async.new('sleep', '60');
    my $pr = $p.start;
    sleep 0.2;
    $p.kill;
    await $pr;
    ck(now - $t0 < 30, True, '.kill ends the process (await returns early)');
}

# 5. A second .start throws, as in Rakudo — the alternative is spawning twice.
{
    my $p = Proc::Async.new('true');
    await $p.start;
    my $died = False;
    { $p.start; CATCH { default { $died = True } } }
    ck($died, True, 'a second .start dies');
}

# 6. An untapped, unbound stream is INHERITED, like Rakudo — not swallowed.
{
    my $p = run($*EXECUTABLE, '-e',
        'await Proc::Async.new("echo", "inherited-out").start;', :out);
    ck($p.out.slurp(:close).trim, 'inherited-out', 'untapped stdout reaches ours');
}

say 'PASS' if $ok;
