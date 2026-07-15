#!/usr/bin/env raku
# A concurrent multi-client chat server on raw TCP. Every accepted connection
# gets its own `start` thread; a shared roster of clients is guarded by a Lock,
# and each message is broadcast to everyone else. Unlike the pastebin (one
# request, one response), this shows Raku's concurrency: many long-lived
# connections interleaving, blocking reads releasing the interpreter lock so
# other clients keep flowing.
#
#   build/rakupp showcase/chat/chat.raku            # listens on 127.0.0.1:6667
#   build/rakupp --exe -o chat showcase/chat/chat.raku && ./chat
#   PORT=7000 ./chat
#
# Connect with any line-oriented TCP client, one per terminal:
#   nc 127.0.0.1 6667
#   telnet 127.0.0.1 6667
#
# First line you send is your nick. Then anything you type is broadcast.
# Commands: /who lists who's online, /quit disconnects.

constant HOST = '0.0.0.0';

# ---------- shared roster (guarded by $lock) ---------------------------
my $lock = Lock.new;
my @clients;          # each: { id => Int, nick => Str, sock => IO::Socket::INET }
my $next-id = 0;

sub add-client($sock, Str $nick --> Int) {
    $lock.protect: {
        my $id = $next-id++;
        @clients.push: { :$id, :$nick, :$sock };
        $id;
    }
}

sub remove-client(Int $id) {
    $lock.protect: {
        @clients = @clients.grep({ $_<id> != $id }).Array;
    }
}

sub roster-nicks(--> List) {
    $lock.protect: { @clients.map(*<nick>).sort.List }
}

# Send a line to everyone (optionally skipping one id). We snapshot the socket
# list under the lock, then write outside it so a slow client can't stall others.
sub broadcast(Str $line, Int :$except) {
    my @targets = $lock.protect: {
        @clients.grep({ !$except.defined || $_<id> != $except }).map(*<sock>).Array;
    };
    for @targets -> $s {
        try { $s.print($line ~ "\r\n"); }
    }
}

# ---------- per-connection handler (runs on its own thread) ------------
sub serve($sock) {
    # A client can vanish at any moment (half-open connection, port scan, our own
    # readiness probe). Guard every socket write so a broken pipe drops just this
    # client and never takes the server down.
    sub put(Str $s --> Bool) { (try { $sock.print($s); True }) // False }

    return unless put("Welcome to rakupp-chat! Pick a nick: ");
    my $raw-nick = $sock.recv;
    # A probe/scan connects and drops before sending anything: recv comes back
    # empty or undefined. Don't register or announce it — just walk away.
    return unless $raw-nick.defined && $raw-nick ne '';
    my $nick = ($raw-nick.lines.head // '').trim || "anon{$next-id}";

    my $id = add-client($sock, $nick);
    put("Hi $nick — you're online. /who to list, /quit to leave.\r\n");
    broadcast("* $nick joined", :except($id));
    note "  + $nick (id $id), {roster-nicks().elems} online";

    loop {
        my $raw = $sock.recv;
        last unless $raw.defined && $raw ne '';
        my $quit = False;
        for $raw.lines -> $line {
            my $msg = $line.trim;
            next unless $msg;
            if    $msg eq '/quit' { $quit = True }
            elsif $msg eq '/who'  { put("* online: {roster-nicks().join(', ')}\r\n") }
            else                  { broadcast("$nick: $msg", :except($id)) }
        }
        last if $quit;
    }

    remove-client($id);
    try { $sock.close; }
    broadcast("* $nick left");
    note "  - $nick (id $id), {roster-nicks().elems} online";
}

# ---------- accept loop -------------------------------------------------
sub MAIN($port-arg?) {
    my $port = ($port-arg // %*ENV<PORT> // 6667).Int;
    my $listener = IO::Socket::INET.new(:localhost(HOST), :localport($port), :listen);
    note "rakupp-chat listening on 127.0.0.1:$port  (Ctrl-C to stop)";
    loop {
        my $conn = $listener.accept;
        start serve($conn);
    }
}
