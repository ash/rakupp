# Guard: a worker whose BODY has returned is not another thread running Raku
# code, even while its thread is still tearing down.
#
# A worker thread gives its liveWorkers_ count back at the thread's exit, after
# its captures and registers are freed (the cycle collector must not walk what
# it is still freeing). Several checks read that count to ask "can another
# thread be running Raku code now?", and right after `await start {…}` the
# answer was yes for as long as the teardown took:
# - `my $db := $d` refused to share $d's cell (promotionSafe), so
#   `$db.VAR.default` died "No such method 'default' for invocant of type 'Int'";
# - a callee could not see a `my Int $*X` declared later in its caller's frame
#   (the dynamic lookup declined to define it), and read the outer `$*X`.
# CI's Linux legs hit it now and then in scalar-alias-keeps-type.raku and
# dynamic-var-memo.raku. Those checks read Interpreter::runningBodies_ now,
# given back when a body returns. RAKUPP_WORKER_EXIT_DELAY holds every worker
# thread at its exit, so the window is wide and the run is deterministic.

sub check($got, $want, $what) {
    die "worker-teardown-is-not-a-running-body: $what: got {$got.raku}, expected {$want.raku}" unless $got eqv $want;
}

# Under Rakudo there is no such delay and nothing to check.
unless $*EXECUTABLE.basename.starts-with('rakupp') { say "PASS"; exit }

my $prog = q:to/END/;
    my $*X = 'top';
    sub g() { $*X // 'none' }
    sub deep($n, &f) { $n ?? deep($n - 1, &f) !! f() }
    my @p = (^4).map(-> $i { start { my $*X = "t$i"; deep(30, &g) } });
    say await(@p).join(',');
    sub gx() { $*X.raku }
    sub late1() { my @r; @r.push: gx(); my Int $*X = 5; @r.push: gx(); @r }
    say late1().join(' ');
    my Int $d is default(7) = 1;
    my $db := $d;
    say $db.VAR.default;
    END

my $p = run $*EXECUTABLE, '-e', $prog, :out, :err, :env(|%*ENV, RAKUPP_WORKER_EXIT_DELAY => 400);
my @lines = $p.out.slurp(:close).lines;
my $err = $p.err.slurp(:close);
check $p.exitcode, 0, "exit code (stderr: {$err.lines.head(3).join(' | ')})";
check @lines[0], 't0,t1,t2,t3', 'the workers ran';
check @lines[1], 'Int 5', 'a later typed declaration, seen by a callee right after the await';
check @lines[2], '7', 'a binding shares the cell right after the await';

say "PASS";
