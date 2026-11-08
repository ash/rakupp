# Issue #229: a program whose mainline ended — by `exit` or by falling off its
# end — while a `start` waited in a read nothing would satisfy (stdin on a
# pipe that never closes, through `$*IN.read`, `$*IN.get` or an `is native`
# read(2)) took 2.2 s to end, where Rakudo ends at once: the worker could not
# see the shutdown flag, so drainWorkers sat out its whole 2 s grace before
# abandoning it all the same. A worker parked in such a call (a native
# routine, a read of stdin) is now counted, and the drain stops waiting once
# every live worker is one. A full-screen TUI that reads keys on a thread
# felt it as a pause after quitting.
#
# A worker reading stdin still gets what arrives while the program runs.
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) { @fail.push("$what: got $got want $want") unless $got eq $want }

# Runs `$code` with a stdin that stays open and silent; the time to its end.
sub ends-promptly($code, $what) {
    my $proc = Proc::Async.new(:w, $*EXECUTABLE, '-e', $code);
    my $out = '';
    $proc.stdout.tap(-> $c { $out ~= $c; Nil });
    $proc.stderr.tap(-> $ { Nil });
    my $t0 = now;
    my $done = $proc.start;
    await Promise.anyof($done, Promise.in(10));
    my $took = now - $t0;
    check($done.status ~~ Kept, True, "$what: it ended");
    check($out.contains('leaving'), True, "$what: the mainline ran to its end");
    check($took < 1.5, True, "$what: exit did not wait out the 2 s grace ({$took.fmt('%.2f')} s)");
    $proc.kill unless $done;
    try $proc.close-stdin;
}

ends-promptly(q:to/END/, '$*IN.read, then exit');
    start { my $b = $*IN.read(4096) }
    sleep 0.2; say "leaving"; exit 0;
    END
ends-promptly(q:to/END/, '$*IN.get, falling off the end');
    start { my $l = $*IN.get }
    sleep 0.2; say "leaving";
    END
ends-promptly(q:to/END/, '$*IN.slurp');
    start { my $s = $*IN.slurp }
    sleep 0.2; say "leaving"; exit 0;
    END
unless $*DISTRO.is-win {   # (no read(2) to call by that name)
    ends-promptly(q:to/END/, 'an `is native` read(2)');
        use NativeCall;
        sub c-read(int32, Buf, size_t --> ssize_t) is native is symbol('read') {*}
        start { my $b = Buf.new(0 xx 16); c-read(0, $b, 16) }
        sleep 0.2; say "leaving"; exit 0;
        END
}

# …and a reader on a worker still reads: a line sent while it waits is its
{
    my $proc = Proc::Async.new(:w, $*EXECUTABLE, '-e', q:to/END/);
        my $p = start { $*IN.get };
        say "got ", await $p;
        END
    my $out = '';
    $proc.stdout.tap(-> $c { $out ~= $c; Nil });
    my $done = $proc.start;
    sleep 0.3;
    await $proc.say('hello');
    $proc.close-stdin;
    await Promise.anyof($done, Promise.in(10));
    check($out.trim, 'got hello', 'a worker waiting on stdin gets the line sent to it');
}

if @fail { note "FAILED: @fail.join('; ')"; say 'FAIL' } else { say 'PASS' }
