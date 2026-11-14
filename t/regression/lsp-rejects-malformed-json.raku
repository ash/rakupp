# Regression: `rakupp --lsp` answers a body that is not JSON with a parse error
# (-32700, id null), where its hand-rolled parser read most of them as
# requests (Codex review 2026-11-14, finding 15). A document missing its closing
# brace was an `initialize`, `{"…":"shutdown"}trailing-junk` a shutdown, a
# missing `:` was skipped over, `\q` became `q`, and a bad number became 0. The
# server now reads with JsonLite, the parser --mcp and --jupyter use, so an
# unpaired surrogate escape is U+FFFD here as it is there, and a real pair is
# its character.
#
# `--lsp` is rakupp's own mode (Rakudo has none), so these run here only. The
# server reads a BOUNDED stdin and exits when it closes.
#
# Contract: exit 0 + last line PASS.
my $ok = True;
sub check($got, $want, $label) {
    unless $got eqv $want { note "FAIL: $label — {$got.raku} vs {$want.raku}"; $ok = False }
}

if $*RAKU.compiler.name eq 'Raku++' {
    sub frame(Str $body) { "Content-Length: {$body.encode.bytes}\r\n\r\n$body" }
    my @bodies =
        Q<{"jsonrpc":"2.0","id":1,"method":"initialize","params":{}>,
        Q<{"jsonrpc":"2.0","id":2,"method":"shutdown"}trailing-junk>,
        Q<{"jsonrpc":"2.0","id":3 "method":"shutdown"}>,
        Q<{"jsonrpc":"2.0","id":4,"method":"shutdown","x":"\q"}>,
        Q<{"jsonrpc":"2.0","id":5e+,"method":"shutdown"}>,
        Q< {"jsonrpc":"2.0","id":6,"method":"shutdown"} >,
        Q<{"jsonrpc":"2.0","id":"\ud800A","method":"shutdown"}>,
        Q<{"jsonrpc":"2.0","id":"😀","method":"shutdown"}>,
        ;
    my $p = run($*EXECUTABLE, '--lsp', :in, :out, :err, :bin);
    $p.in.write(@bodies.map(&frame).join.encode);
    $p.in.close;
    my $raw = $p.out.slurp(:close);
    $p.err.slurp(:close);
    my @replies = $raw.decode('utf8-c8').split(/'Content-Length: ' \d+ \r\n \r\n/, :skip-empty);

    check @replies.elems, 8, 'every body gets exactly one reply';
    check @replies[^5].grep(*.contains(Q<"code":-32700>)).elems, 5,
        'a body that is not one JSON value is a parse error';
    check @replies[^5].grep(*.contains(Q<"id":null>)).elems, 5,
        '…answered with a null id, since none could be read';
    check (@replies[5] // "").contains(Q<"id":6,>) && (@replies[5] // "").contains(Q<"result":null>), True,
        'whitespace around the value is still a request';
    check (@replies[6] // "").contains("\"id\":\"\x[FFFD]A\""), True,
        'an unpaired surrogate escape is U+FFFD, and the next character survives';
    check (@replies[7] // "").contains("\"id\":\"\x[1F600]\""), True,
        'a surrogate pair is its character';
    check $raw.list.rotor(2 => -1).first({ .[0] == 0xED && 0xA0 <= .[1] <= 0xBF }).defined, False,
        'no surrogate is written as UTF-8';
}

say $ok ?? 'PASS' !! 'FAIL';
exit $ok ?? 0 !! 1;
