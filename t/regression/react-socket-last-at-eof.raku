# Regression, 2026-10-10: a `whenever $conn.Supply` in a REACT runs its
# block's LAST phaser when the peer closes the connection, and a `done` in
# it ends the react — the shape a client uses to notice the server hung up.
# The react's done callback for a socket stream only counted the source
# down; LAST never ran, so a react with another live source (a timeout
# `whenever Promise.in(…)`) waited it out. Log::Timeline's socket test
# (t/output-socket.rakutest: the server answers a bad handshake and closes)
# timed out twice on this, and `rakupp install cro` stopped there.
# As Rakudo 2026.09 answers. Contract: exit 0 + last line PASS.
my @fail;
my $port = 22100 + $*PID % 400;

# the server answers one line from a sub that `whenever`s the print, then closes
my $server = supply {
    whenever IO::Socket::Async.listen('127.0.0.1', $port) -> $conn {
        whenever $conn.Supply.lines -> $message {
            answer($conn, "bad: $message");
        }
    }
    sub answer($conn, $text) {
        whenever $conn.print("$text\n") { $conn.close }
    }
}
my $tap = $server.tap;
sleep 0.3;

my $conn = await IO::Socket::Async.connect('127.0.0.1', $port);
my @got;
my $timed-out = False;
my $last-ran = False;
react {
    whenever $conn.Supply.lines {
        push @got, $_;
        LAST { $last-ran = True; done }
    }
    whenever Promise.in(10) { $timed-out = True; done }
    await $conn.print("hello\n");
}
@fail.push("got {@got.raku}") unless @got eqv ['bad: hello'];
@fail.push("LAST did not run at EOF") unless $last-ran;
@fail.push("the react waited for its timeout") if $timed-out;
$tap.close;

if @fail { .note for @fail; say "FAIL" } else { say "PASS" }
