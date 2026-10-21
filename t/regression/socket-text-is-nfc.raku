# Regression: text read from a socket is NFC.
#
# Text decoded from a file, $*IN or a pipe became NFC in O2 of the 2026-10-21
# review, as Rakudo's decoders make it; a socket's did not. IO::Socket::Async's
# Supply emitted the bytes it held as they came, and IO::Socket::INET's text
# `.get`, `.lines` and `.recv` did too, so "e\x[301]x" off the wire had 3
# codes and was not `eq "\x[e9]x"`. A `:bin` read, and utf8-c8, keep their
# bytes. Found while fixing O2 (ledger O23).
#
# Runs clean under Rakudo too.

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

# "e" + COMBINING ACUTE + "x" and a newline; its NFC form is "\x[e9]x"
my $bytes = Blob.new(0x65, 0xCC, 0x81, 0x78, 0x0A);
sub shape($s) { ($s.codes, $s eq "\x[e9]x") }

# ---- IO::Socket::INET ----
my $srv = IO::Socket::INET.new(:listen, :localhost<127.0.0.1>, :localport(0));
my $port = $srv.localport;
sub served(&read) {
    my $client = start {
        my $c = IO::Socket::INET.new(:host<127.0.0.1>, :$port);
        $c.write($bytes);
        sleep 0.2;
        $c.close;
    }
    my $s = $srv.accept;
    sleep 0.1;
    my $r = read($s);
    $s.close;
    await $client;
    $r
}
ck shape(served({ .get })),            (2, True), 'INET .get';
ck shape(served({ .lines[0] })),       (2, True), 'INET .lines';
ck shape(served({ .recv.chomp })),     (2, True), 'INET .recv';
ck served({ .recv(:bin) }).list.List,  $bytes.list.List, 'INET .recv(:bin) keeps the bytes';
$srv.close;

# ---- IO::Socket::Async ----
sub async-read(*%how) {
    my @chunks;
    my $end = Promise.new;
    my $tap = IO::Socket::Async.listen('127.0.0.1', 0).tap: -> $conn {
        $conn.Supply(|%how).tap: -> $m { @chunks.push: $m }, done => { $end.keep };
    }
    my $c = await IO::Socket::Async.connect('127.0.0.1', $tap.socket-port.result);
    await $c.write($bytes);
    $c.close;
    await Promise.anyof($end, Promise.in(10));
    $tap.close;
    @chunks
}
{
    my @c = async-read;
    # each chunk is NFC by itself (a `~` of them would normalize on its own)
    ck @c.grep({ .codes != .chars }).elems, 0, 'Async Supply: every chunk is NFC';
    ck shape(([~] @c).chomp), (2, True), '…and the text is "\x[e9]x"';
}
ck ([~] async-read(:bin)).list.List, $bytes.list.List, 'Async Supply(:bin) keeps the bytes';
ck ([~] async-read(:enc<utf8-c8>)).encode('utf8-c8').bytes, 5, 'Async utf8-c8 keeps the bytes';

say $fails ?? "FAIL ($fails)" !! 'PASS';
