# Regression: `IO::Socket::INET.new(:host<name:port>)` — the port may ride
# along in the host string.
#
# A request's `.host` in HTTP::UserAgent is its Host HEADER, which by the HTTP
# spec carries the port whenever it is not the scheme's default. So a plain
# `$ua.get("http://localhost:3137/one")` handed us the host "localhost:3137",
# which we tried to RESOLVE as a name — "Cannot connect to
# localhost:3137:3137", the port printed twice because the message appends it
# to a host that already had it.
#
# An explicit `:port` wins over the one in the string; a non-numeric tail is
# dropped; and an IPv6 literal (several colons) is a host in its own right and
# must not be split.
#
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) {
    @fail.push("$what: got {$got.raku} want {$want.raku}") unless $got eqv $want
}

# A listener on the loopback, and a client that reaches it by each spelling.
my $port   = 30000 + ($*PID % 20000);
my $server = IO::Socket::INET.new(:localhost<127.0.0.1>, :localport($port), :listen);
check $server.defined, True, 'the listener comes up';

my $acceptor = start {
    for ^4 {
        my $conn = $server.accept;
        last without $conn;
        $conn.print("ok");
        $conn.close;
    }
}

sub reach(*%args) {
    my $s = IO::Socket::INET.new(|%args);
    my $got = $s.recv;
    $s.close;
    $got
}

check reach(:host<127.0.0.1>, :port($port)), 'ok',
      'a plain host and port connect';
check reach(:host("127.0.0.1:$port"), :port($port)), 'ok',
      'the port may also ride in the host string';
check reach(:host("127.0.0.1:$port")), 'ok',
      'and is used when no explicit port is given';
check reach(:host("127.0.0.1:not-a-port"), :port($port)), 'ok',
      'a non-numeric tail is dropped, the explicit port wins';

$server.close;
await Promise.anyof($acceptor, Promise.in(5));

# An IPv6 literal has several colons and is NOT split — the rule is a SINGLE
# colon, so `::1` and `fe80::1` stay whole. That is not asserted here: every
# observable form of it needs a connection, and a bogus IPv6 address blocks
# until the OS gives up rather than failing fast.

# An out-of-range port is refused before the OS is touched, whichever way it
# arrives.
check (try { IO::Socket::INET.new(:host<127.0.0.1>, :port(99999)); 'no throw' } // 'threw'),
      'threw', 'an explicit out-of-range port is refused';
check (try { IO::Socket::INET.new(:host<127.0.0.1:99999>); 'no throw' } // 'threw'),
      'threw', 'and one that came from the host string';

if @fail {
    .say for @fail;
    say "FAIL";
    exit 1;
}
say "PASS";
