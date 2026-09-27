# Regression: a `start` waiting on a file lock does not hold up the program's exit.
#   * A worker blocked in a blocking `.lock` — in F_SETLKW, or in the poll that
#     waits out a conflicting lock before a wrong-mode request fails — could not
#     see the shutdown flag, so a program whose mainline ended while one waited
#     sat out drainWorkers' 2 s grace before it exited; Rakudo exits at once.
#     S32-io/lock.t ends eighteen children that way: 57 s here, 25 s on Rakudo.
#   * Unwound, the worker runs nothing after the call: no FAILED line.
#   * A waiting worker still gets the lock once it is released, and so does a
#     waiting mainline (which keeps the kernel's blocking wait).
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) { @fail.push("$what: got $got want $want") unless $got eq $want }

unless $*DISTRO.is-win {   # no POSIX record locks there: .lock succeeds at once, keeps nobody out
    my $file = $*TMPDIR.add("rakupp-lock-exit-$*PID.txt");
    $file.spurt("test\n");
    my $held = $file.open(:r);
    $held.lock(:shared);

    # The worker says it is about to lock, the mainline waits for that, and ends.
    sub ends-promptly($mode, $what) {
        my $code = qq:to/END/;
            my \$ready = Promise.new;
            start try \{ my \$fh = '$file'.IO.open($mode); \$ready.keep; \$fh.lock; say "FAILED" }
            await \$ready; sleep 0.1; say "DONE"
            END
        my $t0   = now;
        my $p    = run($*EXECUTABLE, '-e', $code, :out, :err);
        my $out  = $p.out.slurp(:close); $p.err.slurp(:close);
        my $took = now - $t0;
        check($out.contains('DONE'),   True,  "$what: the mainline ran to its end");
        check($out.contains('FAILED'), False, "$what: the worker ran nothing after the lock");
        check($took < 1.5, True, "$what: exit did not wait out the 2 s grace ({$took.fmt('%.2f')} s)");
    }
    ends-promptly(':w', 'exclusive lock on a write handle');
    ends-promptly(':r', 'exclusive lock on a read handle (the wrong-mode wait)');

    # A waiter gets the lock once the holder lets go — from a worker, then from the mainline.
    sub granted($code, $want, $what) {
        my $proc = Proc::Async.new($*EXECUTABLE, '-e', $code);
        my $out  = '';
        $proc.stdout.tap(-> $c { $out ~= $c; Nil });
        my $done = $proc.start;
        sleep 0.5;
        $held.unlock;
        await Promise.anyof($done, Promise.in(10));
        check($out, $want, $what);
    }
    granted(qq:to/END/, "GOT\nDONE\n", 'a waiting worker gets the lock once it is released');
        await start \{ my \$fh = '$file'.IO.open(:w); \$fh.lock; say "GOT"; \$fh.close }
        say "DONE"
        END
    $held.lock(:shared);
    granted(qq:to/END/, "GOT\n", 'so does a waiting mainline');
        my \$fh = '$file'.IO.open(:w); \$fh.lock; say "GOT"; \$fh.close
        END

    $held.close;
    $file.unlink;
}

if @fail { note "FAILED: @fail.join('; ')"; say 'FAIL' } else { say 'PASS' }
