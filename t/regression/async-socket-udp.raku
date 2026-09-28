# Regression: UDP on IO::Socket::Async (S32-io/IO-Socket-Async-UDP.t). Each
# line was checked under Rakudo 2026.08 as well.
#   * bind-udp binds at once, so a second bind of a taken port dies, and a
#     close frees the port for an immediate rebind even with a live tap
#   * udp makes a client; print-to / write-to send one datagram each and keep
#     their Promise with the byte count; write-to takes a Blob only
#   * the Supply emits once per datagram, decoded whole: a lone combining mark
#     is its own emit, an empty datagram is "", and :bin gives Buf[uint8]
#   * :datagram wraps each one with its sender; :enc sets the socket's encoding
#   * closing the socket ends its taps with `done`; whenever works in a react
# Contract: exit 0 + last line PASS.
my @fail;
my $port = 47400 + $*PID % 400;
sub wait-for($p) { await Promise.anyof($p, Promise.in(5)); $p }

{
    my $srv = IO::Socket::Async.bind-udp('127.0.0.1', $port);
    @fail.push('a second bind-udp of a taken port dies')
        if try IO::Socket::Async.bind-udp('127.0.0.1', $port);
    my @got; my $p = Promise.new;
    $srv.Supply.tap: -> $s { @got.push($s); $p.keep if @got == 4 };
    my $c = IO::Socket::Async.udp;
    my @n;
    @n.push(await $c.print-to('127.0.0.1', $port, $_)) for "e", "\x[301]", "", "Unusually Dubious Protocol";
    wait-for($p);
    @fail.push("print-to byte counts, got {@n.raku}") unless @n eqv [1, 2, 0, 26];
    @fail.push("one emit per datagram, got {@got.raku}")
        unless @got eqv ["e", "\x[301]", "", "Unusually Dubious Protocol"];
    @fail.push('write-to refuses a Str') if try { $c.write-to('127.0.0.1', $port, "x"); True };
    $c.close; $srv.close;
}

{
    my $srv = IO::Socket::Async.bind-udp('127.0.0.1', $port + 1);
    my $srv2 = IO::Socket::Async.bind-udp('127.0.0.1', $port + 2);
    my $p = Promise.new; my $q = Promise.new;
    $srv.Supply(:bin).tap: -> $b { $p.keep($b) unless $p };
    $srv2.Supply(:datagram).tap: -> $d { $q.keep($d) unless $q };
    my $c = IO::Socket::Async.udp;
    my $n = await $c.write-to('127.0.0.1', $port + 1, Buf.new(90, 113));
    await $c.print-to('127.0.0.1', $port + 2, "hé");
    wait-for($p); wait-for($q);
    @fail.push("write-to keeps 2, got $n") unless $n == 2;
    @fail.push(':bin is a Buf[uint8] of the bytes')
        unless $p && $p.result ~~ Buf[uint8] && $p.result.list eqv (90, 113);
    my $d = $q ?? $q.result !! Nil;
    @fail.push(':datagram carries data and sender')
        unless $d && $d.^name eq 'IO::Socket::Async::Datagram' && $d.data eq 'hé'
            && $d.hostname eq '127.0.0.1' && $d.port ~~ Int;
    $c.close; $srv.close; $srv2.close;
}

{
    my $srv = IO::Socket::Async.bind-udp('127.0.0.1', $port + 3);
    my $done = Promise.new;
    $srv.Supply.tap: -> $ { }, done => { $done.keep };
    sleep 0.05;
    $srv.close;
    wait-for($done);
    @fail.push('closing the socket sends done to its tap') unless $done;
    my $again = try IO::Socket::Async.bind-udp('127.0.0.1', $port + 3);
    @fail.push('the port is free right after close') unless $again;
    $again.close if $again;
}

{
    my $srv = IO::Socket::Async.bind-udp('127.0.0.1', $port + 4);
    my $p = Promise.new;
    $srv.Supply(:bin).tap: -> $b { $p.keep($b) unless $p };
    my $c = IO::Socket::Async.udp(:enc<latin-1>);
    my $n = await $c.print-to('127.0.0.1', $port + 4, "é");
    wait-for($p);
    @fail.push(":enc<latin-1> sends one byte, got $n") unless $n == 1 && $p && $p.result.list eqv (233,);
    @fail.push("enc reads back iso-8859-1, got {$c.enc}") unless $c.enc eq 'iso-8859-1';
    $c.close; $srv.close;
}

{
    my $srv = IO::Socket::Async.bind-udp('127.0.0.1', $port + 5);
    my $c = IO::Socket::Async.udp;
    my @r;
    react {
        whenever $srv.Supply -> $s { @r.push($s); done if @r == 2 }
        whenever Promise.in(0.05) { $c.print-to('127.0.0.1', $port + 5, $_) for <a b> }
        whenever Promise.in(5) { done }
    }
    @fail.push("whenever over a UDP Supply, got {@r.raku}") unless @r eqv [<a b>];
    $srv.close; $c.close;
}

if @fail { say "FAILED:"; .say for @fail; say "FAIL"; exit 1 }
say "PASS";
