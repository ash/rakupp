# Regression: the VS Code extension could never start the language server.
# vscode-languageclient appends `--stdio` to the server command whenever the
# transport is TransportKind.stdio, so VS Code launches `rakupp --lsp --stdio`
# — and rakupp answered "Illegal option --stdio" and exited 0 before reading a
# byte. The client retried five times and gave up ("Server initialization
# failed"); no editor ever saw a diagnostic.
#
# Stdio is the only transport the server has, so under --lsp the flag is a
# no-op. Everywhere else it stays an illegal option, as it always was.
#
# `--lsp` is rakupp's own mode (Rakudo has none), so these run here only. The
# server reads a BOUNDED stdin — the messages go down the pipe, stdin closes,
# the server exits — so nothing is left running.
#
# Contract: exit 0 + last line PASS.
my $ok = True;
sub check($got, $want, $label) {
    unless $got eqv $want { note "FAIL: $label — {$got.raku} vs {$want.raku}"; $ok = False }
}
my $pp = $*RAKU.compiler.name eq 'Raku++';

if $pp {
    sub frame(Str $body) { "Content-Length: {$body.encode.bytes}\r\n\r\n$body" }
    my $session = (
        Q<{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"processId":null,"rootUri":null,"capabilities":{}}}>,
        Q<{"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file:///probe.raku","languageId":"raku","version":1,"text":"my $x = 1;\n"}}}>,
        Q<{"jsonrpc":"2.0","id":2,"method":"shutdown"}>,
        Q<{"jsonrpc":"2.0","method":"exit"}>,
    ).map(&frame).join;

    sub serve(*@args) {
        my $p = run($*EXECUTABLE, |@args, :in, :out, :err);
        $p.in.print($session); $p.in.close;
        my $out = $p.out.slurp(:close);
        my $err = $p.err.slurp(:close);
        ($p.exitcode, $out, $err)
    }

    # The exact command line VS Code runs, and the other order.
    for <--lsp --stdio>, <--stdio --lsp> -> @argv {
        my ($ec, $out, $err) = serve(|@argv);
        check($ec, 0, "@argv[]: the server exits 0");
        check($err.contains('Illegal option'), False, "@argv[]: --stdio is not refused");
        check($out.contains(Q<"name":"rakupp-lsp">), True, "@argv[]: initialize is answered");
        check($out.contains(Q<"code":"unused-variable">), True, "@argv[]: diagnostics are published");
    }

    # Outside --lsp the flag means nothing, so it stays illegal: the program
    # does not run.
    my $r = run($*EXECUTABLE, '--stdio', '-e', 'say "ran-ac0f"', :out, :err);
    my $rout = $r.out.slurp(:close);
    my $rerr = $r.err.slurp(:close);
    check($rerr.contains('Illegal option --stdio'), True, 'a plain run still refuses --stdio');
    check($rout.contains('ran-ac0f'), False, '…and does not run the program');

    my ($mec, $mout, $merr) = serve('--mcp', '--stdio');
    check($merr.contains('Illegal option --stdio'), True, '--mcp does not take --stdio either');
}

if $ok { say "PASS" } else { say "FAIL"; exit 1 }
