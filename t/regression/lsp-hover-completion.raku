# Regression: `rakupp --lsp` answers hover, completion and go-to-definition,
# and underlines the token a diagnostic is about rather than its whole line.
#
# Until 2026-10-03 the server advertised one capability, full-document sync:
# every other request got "method not found", and every squiggle covered a
# whole line. The answers now come from src/LspIndex.cpp (the token stream and
# the REFERENCE.md baked into the CLI).
#
# `--lsp` is rakupp's own mode (Rakudo has none), so these run here only. Every
# case drives a real server over a BOUNDED stdin — the messages go down the
# pipe, stdin closes, the server exits — so nothing is left running.
#
# Contract: exit 0 + last line PASS.
my $ok = True;
sub check($got, $want, $label) {
    unless $got eqv $want { note "FAIL: $label — {$got.raku} vs {$want.raku}"; $ok = False }
}
my $pp = $*RAKU.compiler.name eq 'Raku++';

if $pp {
    sub frame(Str $body) { "Content-Length: {$body.encode.bytes}\r\n\r\n$body" }
    sub esc(Str $s) { $s.subst('\\', '\\\\', :g).subst('"', '\\"', :g).subst("\n", '\\n', :g) }

    # One session: open `$text`, send each request (method, line, character —
    # 0-based, as on the wire) with ids 10, 11, …, and answer every line the
    # server wrote, keyed by id ("diag" for the diagnostics, "init" for the
    # initialize reply).
    sub session(Str $text, **@reqs) {
        my @msgs =
            Q<{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"capabilities":{"textDocument":{"hover":{"contentFormat":["plaintext"]}}}}}>,
            '{"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file:///probe.raku","languageId":"raku","version":1,"text":"' ~ esc($text) ~ '"}}}';
        my $id = 10;
        for @reqs -> ($m, $l, $c) {
            @msgs.push: '{"jsonrpc":"2.0","id":' ~ $id++ ~ ',"method":"textDocument/' ~ $m
                ~ '","params":{"textDocument":{"uri":"file:///probe.raku"},"position":{"line":'
                ~ $l ~ ',"character":' ~ $c ~ '}}}';
        }
        @msgs.push: Q<{"jsonrpc":"2.0","id":2,"method":"shutdown"}>, Q<{"jsonrpc":"2.0","method":"exit"}>;
        my $p = run($*EXECUTABLE, '--lsp', :in, :out, :err);
        $p.in.print(@msgs.map(&frame).join); $p.in.close;
        my $out = $p.out.slurp(:close);
        $p.err.slurp(:close);
        my %by;
        # "\r\n" is ONE grapheme, which \n matches; `\r \n` never would.
        for $out.split(/'Content-Length: ' \d+ \n \n/).grep(*.chars) -> $msg {
            if $msg ~~ / '"id":' (\d+) ',' / { %by{~$0} = $msg }
            elsif $msg.contains('publishDiagnostics') { %by<diag> = $msg }
        }
        %by<exit> = $p.exitcode;
        %by
    }

    my $src = q:to/END/;
    #| A point on the plane.
    class Point {
        has $.x;
        method norm() { $!x }
    }
    #| Greets someone.
    sub greet(Str $who, :$loud = False) { say $loud ?? $who.uc !! $who }
    my $count = 1;
    {
        my $inner = 2;
    }
    greet('ann');
    say Point.new(x => 3).norm;
    $co
    END

    my %r = session($src,
        <hover 6 5>,         # 10: `greet` in its own declaration
        <hover 6 23>,        # 11: the `:$loud` parameter
        <hover 6 39>,        # 12: `say`, a built-in
        <hover 3 21>,        # 13: `$!x` inside the method
        <completion 13 3>,   # 14: `$co|`
        <completion 12 24>,  # 15: `….no|` — methods, this file's first
        <definition 11 1>,   # 16: `greet('ann')` → line 7
        <hover 10 0>,        # 17: `}` — nothing to say
        <completion 13 1>,   # 18: `$|` — `$inner` is out of scope here
    );

    check(%r<exit>, 0, 'the server exits 0 when its client closes stdin');
    check(%r<1>.contains('"hoverProvider":true') && %r<1>.contains('"definitionProvider":true')
          && %r<1>.contains('"completionProvider"'), True,
          'initialize advertises hover, completion and definition');
    check(%r<1>.contains('diagnosticProvider'), False,
          '…and still no diagnosticProvider (lsp-mode would pull with it)');

    check(%r<10>.contains('sub greet(Str $who, :$loud = False)') && %r<10>.contains('Greets someone.'),
          True, 'hover on a sub: its header and its #| comment');
    check(%r<11>.contains(':$loud = False') && %r<11>.contains('Parameter of sub greet'), True,
          'hover on a parameter: its own segment of the signature');
    check(%r<11>.contains('Greets someone'), False, '…without the sub\'s #| comment');
    check(%r<12>.contains('.gist + newline'), True,
          'hover on a built-in reads the baked REFERENCE.md');
    check(%r<13>.contains('has $.x;') && %r<13>.contains('Attribute of Point'), True,
          'hover on $!x finds the attribute of the enclosing class');
    check(%r<17>.contains('"result":null'), True, 'hover on punctuation answers null');

    check(%r<14>.contains('"label":"$count"'), True, 'completion offers a declared variable');
    check(%r<14>.contains('"range":{"end":{"character":3,"line":13},"start":{"character":0,"line":13}}'),
          True, '…replacing the typed prefix, sigil included');
    check(%r<15>.contains('"label":"norm"') && %r<15>.contains('"sortText":"0norm"'), True,
          'method completion lists this file\'s method first');
    check(%r<18>.contains('$inner'), False, 'a variable of a closed block is not offered');
    check(%r<18>.contains('"label":"$count"'), True, '…but one in scope is');

    check(%r<16>.contains('"range":{"end":{"character":9,"line":6},"start":{"character":4,"line":6}}'),
          True, 'definition jumps to the sub\'s name');

    # Diagnostics underline the token: `$y` in `my $x = 1; say $y;`, and the
    # non-ASCII `é` before it counts one UTF-16 unit.
    my %d = session("my \$é = 1; say \$y;\n");
    check(%d<diag>.contains('"range":{"end":{"character":17,"line":0},"start":{"character":15,"line":0}}'),
          True, 'the undeclared variable is underlined, not its line');
}

say $ok ?? 'PASS' !! 'FAIL';
exit $ok ?? 0 !! 1;
