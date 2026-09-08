# Regression: `rakupp --lsp` published strictly FEWER diagnostics than
# `rakupp --lint` for the same source — it never reported an undeclared
# variable, which is the one finding that means the program will not run.
#
# src/Lsp.cpp included Lint.h and not DeclCheck.h, so the declaration check
# simply never ran there. docs/book/ch/38-tooling.md states the rule it broke:
# whatever else a linter does, it must never say less than running the program
# would. An editor showing a clean file that will not run is that failure one
# layer further out.
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

    # Drive one document through initialize / didOpen / shutdown / exit and
    # answer the publishDiagnostics payload.
    sub diagnose(Str $docJson, *%env) {
        my @msgs =
            Q<{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"processId":null,"rootUri":null,"capabilities":{}}}>,
            Q<{"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file:///probe.raku","languageId":"raku","version":1,"text":"> ~ $docJson ~ Q<"}}}>,
            Q<{"jsonrpc":"2.0","id":2,"method":"shutdown"}>,
            Q<{"jsonrpc":"2.0","method":"exit"}>;
        # A properly MERGED environment. `:env(%*ENV, K => v)` builds a LIST,
        # not a hash, so the override is silently dropped and the child runs
        # with the ambient environment — which would make the switched-off
        # rows below pass for the wrong reason.
        my %e = %*ENV;
        %e{.key} = .value for %env;
        my $l = run($*EXECUTABLE, '--lsp', :in, :out, :err, :env(%e));
        $l.in.print(@msgs.map(&frame).join); $l.in.close;
        my $out = $l.out.slurp(:close);
        $l.err.slurp(:close);
        my $line = $out.lines.first(*.contains('publishDiagnostics')) // '';
        ($l.exitcode, $l.signal, $line)
    }

    # `my $x = 1;` on line 1, `say $y;` on line 2 — what --lint reports as one
    # warning and one error. LSP lines are 0-based, so line 2 is "line":1.
    my $doc = Q<my $x = 1;\nsay $y;\n>;
    my ($ec, $sig, $d) = diagnose($doc);

    check(($ec, $sig), (0, 0), 'the server exits 0 when its client closes stdin');
    check($d.contains(Q<"code":"undeclared-variable">), True,
          'the undeclared variable is published at all');
    check($d.contains(Q<"message":"'$y' is not declared">), True,
          '…naming the variable, as --lint does');
    check($d.contains(Q<"severity":1>), True,
          '…with severity Error, not a warning the editor can shrug off');
    check($d.contains(Q<"code":"unused-variable">) && $d.contains(Q<"severity":2>), True,
          'and the lint warning is still there: one was not traded for the other');

    # The error is on the second line of the document, not wherever the first
    # finding happened to be: a diagnostic on the wrong line is its own bug.
    my $err = ($d ~~ / '{"code":"undeclared-variable"' <-[}]>* '}' <-[}]>* '}' <-[}]>* '}' /) // '';
    check(so(~$err ~~ / '"line":1' /), True, 'the error is reported on the line it is on');

    # The same switch --lint honours: RAKUPP_NO_DECLCHECK=1 drops the error and
    # keeps the warning, so the two tools cannot disagree about being switched off.
    my $off = diagnose($doc, RAKUPP_NO_DECLCHECK => '1')[2];
    check($off.contains('undeclared-variable'), False,
          'RAKUPP_NO_DECLCHECK=1 turns the check off here too');
    check($off.contains('unused-variable'), True, '…and leaves the linter alone');

    # A file that cannot be parsed publishes a parse diagnostic and the server
    # lives: the declaration check must not be able to take it down.
    my ($pec, $psig, $bad) = diagnose(Q<my $x = ;\n>);
    check(($pec, $psig), (0, 0), 'an unparseable document does not kill the server');
    check($bad.contains(Q<"code":"parse-error">), True, '…it publishes a parse diagnostic');
}

if $ok { say "PASS" } else { say "FAIL"; exit 1 }
