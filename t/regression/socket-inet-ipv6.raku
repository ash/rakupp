# IO::Socket::INET over IPv6, as in Rakudo (rakudo#1879): `:family(PF_INET6)`
# died "Socket family 30 is not supported: only PF_INET". A machine with no
# IPv6 loopback skips.

my $fails = 0;
sub check($got, $want, $what) {
    if $got ne $want { say "FAIL: $what: got $got, want $want"; $fails++ }
}

my $server = try IO::Socket::INET.new(:localhost('::1'), :localport(0), :listen, :family(PF_INET6));
if $server {
    check $server.localhost, '::1', 'a listener on ::1 (rakudo#1879)';
    check $server.family, 'PF_INET6', '…of the family asked for';
    my $port = $server.localport;
    check $port > 0, True, '…on the port the OS chose';
    my $client = IO::Socket::INET.new(:host('::1'), :$port, :family(PF_INET6));
    my $conn = $server.accept;
    $conn.print("sup?\n");
    check $client.recv, "sup?\n", 'a client reads what the server wrote';
    check $client.host, '::1', '…and knows where it is connected';
    $conn.close; $client.close; $server.close;

    # the async sockets, as before
    my $l = IO::Socket::Async.listen('::1', 0);
    my $got = Promise.new; my $v = $got.vow;
    my $tap = $l.tap(-> $c { $c.Supply.tap(-> $d { $v.keep($d); $c.close }) });
    my $c = await IO::Socket::Async.connect('::1', await $tap.socket-port);
    await $c.print("not much\n");
    check (await $got), "not much\n", 'IO::Socket::Async over ::1';
    $c.close; $tap.close;
}
else { say "# skipped: no IPv6 loopback here" }

say $fails ?? "FAIL ($fails)" !! 'PASS';
exit $fails ?? 1 !! 0;
