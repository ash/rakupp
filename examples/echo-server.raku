#!/usr/bin/env raku
# A TCP echo server and its client, in one self-contained program.
#
# Everything runs over the loopback interface (127.0.0.1), so no internet
# connection is needed and the output is deterministic. The server runs on
# its own thread; the main thread is the client. This is real networking —
# IO::Socket::INET is the same socket API you would point at a remote host.

constant HOST = '127.0.0.1';
constant PORT = 8099;

# Create the listening socket up front. `:listen` binds the port here, in
# the main thread, so the client below can connect without any race — only
# `.accept` (waiting for a connection) is deferred to the server thread.
my $listener = IO::Socket::INET.new(:localhost(HOST), :localport(PORT), :listen);

my $server = start {
    loop {
        my $conn = $listener.accept;
        my $request = $conn.recv;
        last if $request eq 'BYE';
        $conn.print($request.uc);          # shout the message back
        $conn.close;
    }
    $listener.close;
}

# The client: send a few messages, print what the server echoes back.
sub ask($message) {
    my $sock = IO::Socket::INET.new(:host(HOST), :port(PORT));
    $sock.print($message);
    my $reply = $sock.recv;
    $sock.close;
    $reply;
}

say 'Client -> server -> client, over TCP on 127.0.0.1:';
for 'hello', 'raku sockets', 'goodbye' -> $msg {
    say sprintf('  sent %-16s got back %s', "'$msg'", ask($msg));
}

# Tell the server to shut down, then wait for its thread to finish.
my $bye = IO::Socket::INET.new(:host(HOST), :port(PORT));
$bye.print('BYE');
$bye.close;
await $server;
say 'Server stopped cleanly.';
