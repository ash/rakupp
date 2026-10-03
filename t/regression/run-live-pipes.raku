# `run` with `:in` / `:out` / `:err` waited for the child before returning.
#
# The child ran to its end and its output came back as one string, so:
#  - `.out.get` / `.out.lines` saw nothing until the child had exited;
#  - with `:in`, the child started only when its output was read, got what had
#    been written so far as its whole input, and a later `.in.say` reached
#    nobody — a conversation (write, read the answer, write again) could not
#    happen. nige123/cli.321.do noted "a Proc's stdin write blocks until the
#    child exits" and drove its child through Proc::Async instead.
# Now the child runs from `run` on over live pipes, as Rakudo's does.
#
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) { @fail.push("$what: got {$got.raku} want {$want.raku}") unless $got eqv $want }

# a conversation
{
    my $p = run 'sh', '-c', 'while read x; do echo "got $x"; done', :in, :out;
    $p.in.say: 'one';
    check($p.out.get, 'got one', 'the answer to the first line, before the second is written');
    $p.in.say: 'two';
    check($p.out.get, 'got two', 'and to the second');
    $p.in.close;
    check($p.out.get, Nil, 'then the end');
    check($p.exitcode, 0, 'exit code after the conversation');
}
# a line arrives while the child is still running
{
    my $t0 = now;
    my $p = run 'sh', '-c', 'echo first; sleep 2; echo second', :out;
    check(now - $t0 < 1.5, True, 'run returns while the child runs');
    my $first = $p.out.get;
    check($first, 'first', 'the first line');
    check(now - $t0 < 1.5, True, '…read before the child ends');
    check($p.out.get, 'second', 'then the second');
}
# what stays the same
{
    my $p = run 'sh', '-c', 'echo a; echo b >&2; exit 3', :out, :err;
    check($p.out.slurp, "a\n", ':out slurps');
    check($p.err.slurp, "b\n", ':err slurps');
    check($p.exitcode, 3, 'exit code');
    check(?$p, False, 'a failed child is False');
    my $q = run 'sh', '-c', 'exit 2', :out;
    my $died = False;
    try { sink $q; CATCH { default { $died = .^name } } }
    check($died, 'X::Proc::Unsuccessful', 'sinking a failed child still dies');
    my $a = run 'printf', 'b\na\nc\n', :out;
    my $b = run 'sort', :in($a.out), :out;
    check($b.out.slurp(:close).lines.List, <a b c>, 'one child\'s .out is the next one\'s :in');
    # A child that fills its stderr pipe while we read its stdout: every read
    # here services both pipes. (Rakudo 2026.09 waits forever on this one, so
    # it is not part of the cross-engine run.)
    if $*RAKU.compiler.name ne 'rakudo' {
        my $s = run 'sh', '-c', 'seq 1 50000; seq 1 50000 >&2', :out, :err;
        check($s.out.lines.elems, 50000, 'a full stderr does not wedge a stdout reader');
        check($s.err.lines.elems, 50000, '…and none of it is lost');
    }
    my $o = run 'printf', 'x\ny\n', :out;
    check($o.out.get, 'x', 'a .get');
    check($o.out.slurp, "y\n", '…then a .slurp takes the rest');
}

if @fail { note "FAILED: @fail.join('; ')"; say 'FAIL' } else { say 'PASS' }
