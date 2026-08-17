# Regression: two socket defects, both between OpenSSL and IO::Socket::SSL.
#
# 1. IO::Socket is the ROLE a synchronous socket does, and every wrapper
#    declares its parameter as that — OpenSSL.set-socket(IO::Socket $s),
#    IO::Socket::SSL's own `my IO::Socket $ssl`. A socket did not do it, so
#    IO::Socket::SSL failed on its first line. It is a role, not a parent: .isa
#    is False and .^mro does not list it, on both engines. And it is not an IO.
#
# 2. `recv(Inf)` means "whatever has arrived", and it is a real argument, not a
#    mistake: OpenSSL's bio-read calls `$.net-read.()`, whose closure defaults
#    to `-> $n = Inf { $s.recv($n, :bin) }`. Sizing the buffer from it threw
#    std::bad_alloc before the socket was ever read, so every SSL handshake died
#    inside the first WANT_READ.
# Contract: exit 0 + last line PASS.
my @fail;
sub ok($desc, $got, $want = True) { @fail.push("$desc: got {$got.raku}, want {$want.raku}") unless $got eqv $want }

# A loopback pair, so the test needs no network and no fixed port.
# Rakudo rejects :port(0), so take the first port in a range that binds.
my ($listener, $port);
for 15000 .. 15040 -> $p {
    $listener = try IO::Socket::INET.new(:listen, :localhost<127.0.0.1>, :localport($p));
    if $listener { $port = $p; last }
}
die 'no free port in 15000..15040' unless $listener;

my $server = start {
    my $conn = $listener.accept;
    $conn.print('hello from the other side');
    $conn.close;
}
my $client = IO::Socket::INET.new(:host<127.0.0.1>, :port($port));

# ---- 1. the socket does the IO::Socket role --------------------------------
ok('does the role',      so $client ~~ IO::Socket);
ok('.does agrees',       $client.does(IO::Socket));
ok('still its own type', so $client ~~ IO::Socket::INET);
# …but it is a ROLE: not an ancestor, and not an IO
ok('not isa',            $client.isa(IO::Socket), False);
ok('not an IO',          so $client ~~ IO, False);
ok('mro omits the role', IO::Socket::INET.^mro.map(*.^name).join(' '), 'IO::Socket::INET Any Mu');
# a parameter typed as the role binds
sub takes-socket(IO::Socket $s --> Str) { 'bound' }
ok('binds as IO::Socket', takes-socket($client), 'bound');

# ---- 2. recv(Inf) reads what has arrived ------------------------------------
my $got = $client.recv(Inf, :bin);
ok('recv(Inf) answers a Buf', so ($got ~~ Blob));
ok('recv(Inf) got the bytes', $got.decode, 'hello from the other side');

$client.close;
await $server;
$listener.close;

if @fail { note "FAILED:\n" ~ @fail.join("\n"); say 'FAIL' } else { say 'PASS' }
