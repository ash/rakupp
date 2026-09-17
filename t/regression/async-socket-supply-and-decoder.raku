# Regression: four faults between IO::Socket::Async::SSL and green — it is the
# single biggest dependency blocker in the ecosystem, and seven of its eight
# test files pass once these are fixed.
#
#   * `Rakudo::Internals.NORMALIZE_ENCODING` did not exist. It is internal, but
#     any module that decodes a BYTE STREAM calls it, because the canonical
#     spelling is what `Encoding::Registry.find` is keyed by. It is the FIRST
#     call in that module's character Supply, so the whole supply block threw
#     before emitting anything and `.Supply(:enc(…))` on a TLS connection
#     yielded NOTHING while `.Supply(:bin)` worked.
#   * `consume-available-chars` drained the decoder's buffer regardless of what
#     was in it, so a character split across two socket writes became two
#     replacement characters: "ПИВО" arrived as "П\xB8ВО".
#   * `whenever $socket` (no `.Supply`) ran its body ONCE with the socket as
#     the topic instead of tapping it.
#   * `done` inside such a handler did nothing at all: reactStack_ is
#     thread-local and the read worker never carried the enclosing react
#     across, so the react waited for ever.
#
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) {
    @fail.push("$what: got {$got.raku} want {$want.raku}") unless $got eqv $want
}

# ---- NORMALIZE_ENCODING ----------------------------------------------------
my $RI = Rakudo::Internals;
check $RI.NORMALIZE_ENCODING('utf-8'),    'utf8',        'utf-8 is utf8';
check $RI.NORMALIZE_ENCODING('UTF-8'),    'utf8',        'whatever its case';
check $RI.NORMALIZE_ENCODING('utf8'),     'utf8',        'and already-canonical stays';
check $RI.NORMALIZE_ENCODING('UTF-16'),   'utf16',       'utf16 likewise';
check $RI.NORMALIZE_ENCODING('UTF-16LE'), 'utf16le',     'with its endianness';
check $RI.NORMALIZE_ENCODING('latin-1'),  'iso-8859-1',  'latin-1 is iso-8859-1';
check $RI.NORMALIZE_ENCODING('latin1'),   'iso-8859-1',  'in either spelling';
check $RI.NORMALIZE_ENCODING('ASCII'),    'ascii',       'ascii just lowercases';
check $RI.NORMALIZE_ENCODING('UTF-8-c8'), 'utf8-c8',     'and utf8-c8 is its own name';
# An unknown name comes back lowercased and otherwise untouched — `utf32` is a
# real name but `UTF-32` is not, which is the giveaway that this is a TABLE.
check $RI.NORMALIZE_ENCODING('UTF32'),    'utf32',       'utf32 is in the table';
check $RI.NORMALIZE_ENCODING('UTF-32'),   'utf-32',      'utf-32 is not, so it only lowercases';
check $RI.NORMALIZE_ENCODING('Utf_8'),    'utf_8',       'nor is utf_8';
check $RI.NORMALIZE_ENCODING('nonesuch'), 'nonesuch',    'and neither is nonsense';

# ---- the streaming decoder -------------------------------------------------
sub feed(@chunks) {
    my $d = Encoding::Registry.find('utf8').decoder();
    my @out;
    for @chunks -> $c { $d.add-bytes($c); @out.push: $d.consume-available-chars }
    @out.push: $d.consume-all-chars;
    @out
}
my $bytes = "пиво\n".encode('utf-8');
check $bytes.elems, 9, 'four two-byte characters and a newline';

# Split so the SECOND character straddles the boundary. Nothing may be emitted
# until it is whole again.
check feed([$bytes.subbuf(0, 3), $bytes.subbuf(3)]).List, ("", "пиво\n", ""),
      'a character split across chunks survives whole';
check feed([$bytes,]).List, ("пиво\n", ""),
      'and a whole string ending in a newline comes out at once';

# The hold-back is about GRAPHEMES, not bytes: the next chunk could open with a
# combining mark, so the final one waits…
check feed(["abc".encode('utf-8'),]).List, ("ab", "c"), 'the last character waits';
check feed(["e\x[301]".encode('utf-8'),]).List, ("", "é"), 'a whole cluster waits together';
# …unless nothing can extend or join it. A newline, tab or NUL is safe; a CR is
# not, because CR LF is one cluster, and a space is not, because a mark can
# attach to it.
check feed(["ab\n".encode('utf-8'),]).List, ("ab\n", ""), 'a newline is safe to emit';
check feed(["ab\t".encode('utf-8'),]).List, ("ab\t", ""), 'so is a tab';
check feed(["ab\r".encode('utf-8'),]).List, ("ab", "\r"), 'a carriage return is not';
check feed(["ab ".encode('utf-8'),]).List,  ("ab", " "),  'and neither is a space';

# ---- a socket's Supply, and `done` from its handler -------------------------
my $port = 41000 + ($*PID % 9000);
my $ready = Promise.new;
my $server = start react {
    whenever IO::Socket::Async.listen('localhost', $port) -> $c {
        whenever $c -> $m { $c.print($m.uc) }   # the BARE socket, not .Supply
    }
    $ready.keep;
}
await Promise.anyof($ready, Promise.in(10));
check $ready.status, Kept, 'the listener comes up';

my $conn = await IO::Socket::Async.connect('localhost', $port);
# Split a multi-byte character across two writes, as the module's own test does.
# The pause is what makes this a real test rather than a lucky one: written
# back to back the two halves coalesce into a single read on the loopback and
# the boundary never materialises. With it, the server's tap MUST hold the half
# character back — and `.uc` above is what proves it did, because our Str is
# its own bytes, so a broken character survives a plain `~` and only a real
# string operation destroys it.
my $out = "привет\n".encode('utf-8');
await $conn.write($out.subbuf(0, 5));
sleep 0.4;
await $conn.write($out.subbuf(5));

# The react is bounded so a `done` that does NOT end it fails the file instead
# of hanging the whole suite — which is how this reads on a CI runner, where a
# wall-clock assertion is not something to hang the result on.
my $got = '';
my $ended = start react {
    whenever $conn -> $msg {      # again the bare socket
        $got ~= $msg;
        done if $got.contains("\n");
    }
}
await Promise.anyof($ended, Promise.in(30));
check $ended.status, Kept, '`done` in the handler ends the react';
check $got, "ПРИВЕТ\n", 'and a bare socket taps as a character stream, split and all';

if @fail {
    .say for @fail;
    say "FAIL";
    exit 1;
}
say "PASS";
