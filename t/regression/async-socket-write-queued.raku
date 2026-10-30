# Regression: IO::Socket::Async's write sent its bytes on the caller's thread,
# so it blocked until the peer read them. A client that wrote more than the
# socket buffers hold before it tapped the echo never got to tap it: the
# server's echo stalled on the unread reply, its reader stopped reading, and
# the client's send waited for ever. In Rakudo the write is queued for the
# event loop and returns its Promise at once, kept once the bytes are out.
# Also Cro::RPC::JSON's t/060-websocket.rakutest: Cro's WebSocket handler
# emits its Close reply before it keeps the close promise, and the reply,
# sent inline, reached the client first now and then.
#
# Expectations checked against Rakudo 2026.09
# (~/.rakubrew/versions/moar-2026.09/install/bin/rakudo). Green on both.
# Contract: exit 0 + last line PASS.
my @fail;
sub check($ok, $what) { @fail.push($what) unless $ok }

my $N = 8_000_000;
my $echo = IO::Socket::Async.listen('127.0.0.1', 0).tap: -> $conn {
    $conn.Supply(:bin).tap: -> $buf { $conn.write($buf) };
};
my $port = $echo.socket-port.result;
my $c = await IO::Socket::Async.connect('127.0.0.1', $port);

# the write goes out on a thread of its own, so a stuck one can be reported
my $wrote = start $c.write(Blob.allocate($N, 7));
await Promise.anyof($wrote, Promise.in(5));
check $wrote.status ~~ Kept, "write of $N bytes returned before the echo was read";
my $wp = $wrote.status ~~ Kept ?? $wrote.result !! Nil;
check ($wp andthen .status ~~ Planned), "its promise is Planned until the bytes are out";

my $total = 0; my $back = Promise.new; my $bv = $back.vow;
$c.Supply(:bin).tap: -> $b { $total += $b.elems; $bv.keep(True) if $total >= $N && $back.status ~~ Planned };
await Promise.anyof($back, Promise.in(20));
check $total == $N, "echoed back $total of $N";
await Promise.anyof($wrote, Promise.in(5));
$wp = $wrote.result if $wrote.status ~~ Kept;
check ($wp andthen .status ~~ Kept && .result == $N), "the write promise is kept with its byte count";
$c.close;
$echo.close;

# writes made before a close all go out first, in order, then the end
my $got = ''; my $eof = Promise.new; my $ev = $eof.vow;
my $sink = IO::Socket::Async.listen('127.0.0.1', 0).tap: -> $conn {
    $conn.Supply.tap: -> $s { $got ~= $s }, done => { $ev.keep(True) };
};
my $d = await IO::Socket::Async.connect('127.0.0.1', $sink.socket-port.result);
$d.print("first ");
$d.write(("x" x 65536).encode);
$d.print(" last");
$d.close;
await Promise.anyof($eof, Promise.in(5));
check $eof.status ~~ Kept, "the server saw the end";
check $got eq "first " ~ ("x" x 65536) ~ " last", "the server got every write in order ({$got.chars} chars)";
$sink.close;

if @fail { note "FAILED: $_" for @fail; say 'FAIL' } else { say 'PASS' }
