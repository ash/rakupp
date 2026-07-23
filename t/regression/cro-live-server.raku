# Regression: the LIVE Cro hello-world server (batch 10) — a real
# Cro::HTTP::Server with router, served over real sockets/threads, fetched by
# a plain IO::Socket::INET client in the same process. Exercises: on-demand
# supply/whenever wiring, IO::Socket::Async listen/read/write workers,
# method-frame state vars, subset-junction typechecks, .?, EVAL'd route-matcher
# regex, proto-token actions, coercion-multi dispatch, stub-vs-accessor, and
# Mu.return.
#
# Needs RAKULIB pointing at the module battery's Cro dists
# (/Users/ash/raku-module-battery). When Cro is not on RAKULIB (e.g. CI),
# passes vacuously — the t/run.raku contract is exit 0 + last line PASS.
use Cro::HTTP::Router;
use Cro::HTTP::Server;

if ::('Cro::HTTP::Server') ~~ Failure {
    say 'PASS';     # Cro dists not vendored here — nothing to exercise
    exit 0;
}

my $app = route {
    get -> {
        content 'text/plain', 'Hello from Cro on rakupp!';
    }
    get -> 'greet', $name {
        content 'text/plain', "Hello, $name!";
    }
}
my $server = Cro::HTTP::Server.new(:host<127.0.0.1>, :port<10203>, application => $app);
$server.start;
# fetch with plain sockets (no Cro::HTTP::Client -> no TLS chain at all)
my $conn = IO::Socket::INET.new(:host<127.0.0.1>, :port(10203));
$conn.print("GET /greet/Andrew HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n");
my $resp = '';
while $conn.recv -> $chunk { $resp ~= $chunk }
$conn.close;
$server.stop;
my $status-ok = ($resp.lines[0] // '').trim eq 'HTTP/1.1 200 OK'; # .lines keeps the \r
my $body-ok   = ($resp.split("\r\n\r\n")[1] // '').contains('Hello, Andrew!');
unless $status-ok { note "FAIL: status line was: ", $resp.lines[0] // '(empty)' }
unless $body-ok   { note "FAIL: body was: ", $resp.split("\r\n\r\n")[1] // '(none)' }
say ($status-ok && $body-ok) ?? 'PASS' !! 'FAIL';
