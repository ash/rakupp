# Regression: `print "Hello"` at the REPL prompt showed nothing (#277).
#
# On a terminal the line editor draws its prompt with a leading `\r`, so the
# first redraw wrote `> ` over the "Hello" that `print` had left without a
# newline; `say` and `put` were fine. The REPL now remembers whether stdout's
# last byte ended a line and, if not, starts the prompt on a new one.
#
# The editor only runs on a real terminal, so the session goes through
# script(1), which gives it a pty (BSD and util-linux spell the call
# differently). Where there is no script(1), or on Windows, the check is
# skipped.
#
# Contract: exit 0 + last line PASS.
my @fail;

sub session(*@lines) {
    my %env = %*ENV;
    %env<TERM> = 'xterm-256color';
    %env<RAKUPP_HISTORY> = '';
    %env<NO_COLOR> = '1';
    my $exe = $*EXECUTABLE.Str;
    my @cmd = $*KERNEL.name eq 'linux'
        ?? ('script', '-qec', "'$exe' -q", '/dev/null')
        !! ('script', '-q', '/dev/null', $exe, '-q');
    my $p = run(|@cmd, :in, :out, :err, :%env);
    # One line at a time, each once the prompt is up: written all at once, the
    # lines reach the pty while it is still in cooked mode and the session
    # reads the ^D before them.
    for |@lines, "\x04" -> $l {
        sleep 0.5;
        $p.in.print($l eq "\x04" ?? $l !! "$l\r");
    }
    sleep 0.5;
    $p.in.close;
    my $out = $p.out.slurp(:close, :bin).decode('utf8-c8');
    $p.err.slurp(:close);
    $out.subst(/ \e '[' <[?0..9;]>* <[A..Za..z]> /, '', :g)
}

if $*DISTRO.is-win || !(run('sh', '-c', 'command -v script', :out, :err).so) {
    say "skipped: no script(1) here";
    say "PASS";
    exit 0;
}

my $out = session('print "Hello";', 'say 42;', 'print "";');
@fail.push("print's text was overwritten by the prompt: {$out.raku}")
    unless $out.contains("Hello\r\n");
@fail.push("say got an extra blank line: {$out.raku}")
    if $out.contains("42\r\n\r\n");
@fail.push("print \"\" did not echo True: {$out.raku}")
    unless $out.contains("True\r\n");

if @fail {
    .say for @fail;
    say "FAIL";
    exit 1;
}
say "PASS";
