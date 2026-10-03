# Regression: the REPL under TERM=dumb — Emacs's M-x run-raku and M-x shell.
#
# Emacs runs the REPL on a pty with TERM=dumb. The raw-mode line editor's
# redraws (`\r`, `ESC[nC`, bracketed paste) and the coloured prompt arrived
# there as literal bytes and erased the `> ` that Emacs uses to tell output
# from input. Under TERM=dumb the session is now plain: no escapes at all, a
# bare `> ` prompt, `* ` on an unfinished line, and `\clear` prints nothing.
#
# The same session under TERM=xterm-256color must still be coloured, so this
# test fails if the check stops telling the two apart.
#
# Contract: exit 0 + last line PASS.
my @fail;

sub session(Str $term) {
    my %env = %*ENV;
    %env<TERM> = $term;
    %env<RAKUPP_REPL> = '1';      # a session even with stdin on a pipe
    %env<RAKUPP_HISTORY> = '';
    %env<RAKUPP_COLOR>:delete;
    %env<NO_COLOR>:delete;
    my $p = run($*EXECUTABLE, '-q', :in, :out, :err, :%env);
    $p.in.print("my \$x = 6;\nsay \$x * 7;\nsub f(\$n) \{\n  \$n + 1\n}\nf(41)\n\\clear\n");
    $p.in.close;
    my $out = $p.out.slurp(:close);
    $p.err.slurp(:close);
    $out
}

my $dumb = session('dumb');
@fail.push("TERM=dumb printed an escape: {$dumb.raku}") if $dumb.contains("\e");
@fail.push("TERM=dumb: no `> 42` line: {$dumb.raku}")   unless $dumb.contains("> 42\n");
@fail.push("TERM=dumb: no `* ` continuation prompt")     unless $dumb.contains('* ');
@fail.push("TERM=dumb: no `> 42` from f(41): {$dumb.raku}") unless $dumb.lines.grep(/^ '> ' .* 42 $/) >= 2;

my $xterm = session('xterm-256color');
@fail.push("TERM=xterm-256color printed no escape: {$xterm.raku}") unless $xterm.contains("\e[");

if @fail {
    .say for @fail;
    say "FAIL";
    exit 1;
}
say "PASS";
