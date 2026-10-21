# Regression: IO::Socket::INET's text `.recv($n)` counts characters, and reads
# on from what `.get` has buffered.
#
# `.recv($n)` read up to $n BYTES off the socket, so `.recv(3)` of "Å\r\nö…"
# was "Å\r" and the next one began with the LF, and an "e" and its combining
# mark could come out in two reads. Rakudo's decoder hands out $n whole
# characters: from its buffer if it holds them, else after one more read,
# else all it has. And `.get` reads ahead into that same buffer, which the
# old `.recv` and `.read` never looked at, so the rest of what `.get` had
# taken was lost to them. Found while fixing the 2026-10-21 review's O23
# (ledger O35).
#
# Runs clean under Rakudo too.

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

my $srv = IO::Socket::INET.new(:listen, :localhost<127.0.0.1>, :localport(0));
my $port = $srv.localport;
# the peer writes what it is given and closes; the reads run on our side
sub served($payload, &read) {
    my $client = start {
        my $c = IO::Socket::INET.new(:host<127.0.0.1>, :$port);
        $c.write($payload ~~ Blob ?? $payload !! $payload.encode);
        sleep 0.3;
        $c.close;
    }
    my $s = $srv.accept;
    sleep 0.1;
    my @r = read($s);
    $s.close;
    await $client;
    @r.map({ $_ ~~ Blob ?? .list.List !! $_ }).List
}

ck served("Å\r\nöta\nend", { .recv(3), .recv(3), .recv(3) }),
   ("Å\r\nö", "ta\n", "end"), '.recv(3) is three characters, a CR LF one of them';
ck served(Blob.new(0x65, 0xCC, 0x81, 0x78, 0x79), { .recv(2), .recv }),
   ("\x[e9]x", "y"), 'a letter and its mark are one character, and NFC';
ck served("one\ntwo three", { .get, .recv(3), .recv }),
   ("one", "two", " three"), '.recv goes on from what .get read ahead';
ck served("one\ntwo three", { .get, .read(3) }),
   ("one", (116, 119, 111)), '…and so does .read';
ck served("abc", { .recv, .recv }), ("abc", ""), '.recv takes what there is, then "" at the end';
ck served("abc", { .recv(0), .recv }), ("", "abc"), '.recv(0) is ""';
ck served("abc", { .recv(:bin) }), ((97, 98, 99),), '.recv(:bin) is bytes';

$srv.close;
say $fails ?? "FAIL ($fails)" !! 'PASS';
