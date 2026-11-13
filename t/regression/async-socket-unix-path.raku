# IO::Socket::Async.listen-path and .connect-path, UNIX-domain sockets, as in
# Rakudo (rakudo#5358, #5359): both were "No such method". The tap names the
# path; a connection's ends have no port; a missing path breaks the Promise
# with libuv's words. Windows has none of it.

my $fails = 0;
sub check($got, $want, $what) {
    if $got ne $want { say "FAIL: $what: got $got, want $want"; $fails++ }
}

if $*DISTRO.is-win { say "# skipped: no UNIX-domain sockets on Windows" }
else {
    my $path = $*TMPDIR.add("rakupp-r9-{$*PID}.sock").Str;
    unlink $path;
    my $got = Promise.new; my $v = $got.vow;
    my $server-peer;
    my $tap = IO::Socket::Async.listen-path($path).tap(-> $conn {
        $server-peer = $conn.peer-host;
        $conn.Supply.lines.tap(-> $line { $v.keep($line); $conn.print("echo $line\n") });
    });
    check (await $tap.socket-host), $path, 'the listener is at the path (rakudo#5358)';
    check (await $tap.socket-port), 0, '…with no port';
    check $path.IO.e, True, '…which exists';
    my $c = await IO::Socket::Async.connect-path($path);
    check $c.peer-host, $path, 'connect-path connects to it (rakudo#5359)';
    check $c.peer-port, 0, '…with no port';
    my $reply = Promise.new; my $rv = $reply.vow;
    $c.Supply.lines.tap(-> $l { $rv.keep($l) });
    await $c.print("hi\n");
    check (await $got), 'hi', 'the server reads the client';
    check (await $reply), 'echo hi', '…and the client the server';
    check $server-peer, '', 'a server end names no peer';
    $c.close;
    unlink $path;
    my $p = IO::Socket::Async.connect-path($*TMPDIR.add("rakupp-r9-none-{$*PID}.sock").Str);
    try { await $p; CATCH { default { check .message, 'no such file or directory', 'no such path' } } }
}

say $fails ?? "FAIL ($fails)" !! 'PASS';
exit $fails ?? 1 !! 0;
