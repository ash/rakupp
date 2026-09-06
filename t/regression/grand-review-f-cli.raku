# Regression: the Grand Review, batch F — the command line, the FFI switch and
# the language server (docs/dev/findings/REVIEW-GRAND.md). The flag cases are
# rakupp's own (Rakudo has no --lint/--lsp), so they run here only.

my $ok = True;
sub check($got, $want, $label) {
    unless $got eqv $want { note "FAIL: $label — {$got.raku} vs {$want.raku}"; $ok = False }
}
my $pp = $*RAKU.compiler.name eq 'Raku++';

# 1. (`… | rakupp -c` on stdin stays a usage error: rakupp's contract, pinned by t/run.raku —
#     "a mode with no source is a usage error, exit 4". Rakudo reads stdin there. Ledger L12 F13.)

if $pp {
    # 2. The single-dash long-option courtesy works at every option position and with =VALUE.
    my $p = run $*EXECUTABLE, '-I', 'lib', '-lint', '-e', 'say 1', :out, :err;
    check($p.exitcode, 0, '-I lib -lint -e … is --lint (it ran `-e "int"`)');
    check($p.err.slurp(:close).contains("treating '-lint' as '--lint'"), True, '…with the note');

    # 3. The completion table knows --watch.
    my $c = run $*EXECUTABLE, '--completions=bash', :out, :err;
    check($c.out.slurp(:close).contains('--watch'), True, '--completions lists --watch');

    # 4. RAKUPP_FFI=on is the default search, not a library called "on".
    my $f = run $*EXECUTABLE, '--ffi-info', :out, :err, :env(%*ENV, RAKUPP_FFI => 'on');
    check($f.out.slurp(:close).contains('RAKUPP_FFI=on'), False, 'RAKUPP_FFI=on does not disable libffi');

    # 5. --lsp survives hostile JSON: deep nesting, a bad \u escape, a bogus Content-Length.
    sub lsp(Str $body) {
        my $l = run $*EXECUTABLE, '--lsp', :in, :out, :err;
        $l.in.print("Content-Length: {$body.encode.bytes}\r\n\r\n$body"); $l.in.close;
        $l.out.slurp(:close); $l.err.slurp(:close);
        ($l.exitcode, $l.signal)
    }
    check(lsp('[' x 100_000),                                  (0, 0), '100k nested arrays: no stack overflow');
    check(lsp('{"jsonrpc":"2.0","method":"x\uZZZZ","id":1}'), (0, 0), 'a non-hex \u escape: no abort');
    {
        my $l = run $*EXECUTABLE, '--lsp', :in, :out, :err;
        $l.in.print("Content-Length: 4000000000000\r\n\r\n{}"); $l.in.close;
        $l.out.slurp(:close); $l.err.slurp(:close);
        check(($l.exitcode, $l.signal), (0, 0), 'a multi-TB Content-Length: no allocation crash');
    }
}

if $ok { say "PASS" } else { say "FAIL"; exit 1 }
