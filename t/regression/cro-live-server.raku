# Regression: the LIVE Cro hello-world server (batch 10) — a real
# Cro::HTTP::Server with router, served over real sockets/threads, fetched by
# a plain IO::Socket::INET client in the same process. Exercises: on-demand
# supply/whenever wiring, IO::Socket::Async listen/read/write workers,
# method-frame state vars, subset-junction typechecks, .?, EVAL'd route-matcher
# regex, proto-token actions, coercion-multi dispatch, stub-vs-accessor, and
# Mu.return. Needs RAKULIB pointing at the module battery's Cro dists.
# Expected output:
#   server started
#   status-line: HTTP/1.1 200 OK
#   body: Hello, Andrew!
#   done
# The canonical Cro hello-world: router + http server, no TLS anywhere
use Cro::HTTP::Router;
use Cro::HTTP::Server;
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
say "server started";
# fetch with plain sockets (no Cro::HTTP::Client -> no TLS chain at all)
my $conn = IO::Socket::INET.new(:host<127.0.0.1>, :port(10203));
$conn.print("GET /greet/Andrew HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n");
my $resp = '';
while $conn.recv -> $chunk { $resp ~= $chunk }
$conn.close;
say "status-line: ", $resp.lines[0];
say "body: ", $resp.split("\r\n\r\n")[1] // '(none)';
$server.stop;
say "done";
