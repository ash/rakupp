# Regression: `.slurp` on an open handle did not leave it at its end.
#
# On a file opened for text, `.slurp` read the file and kept no position, so
# `.eof` stayed False, `.get` started again at the first line and a second
# `.slurp` answered the whole file again; `.comb` (which slurps) the same. On
# `$*IN`, `slurp()` and `words()` the input was consumed but `.eof` stayed
# False, and a captured `Proc` stream answered its output again. Roast reaches
# it only on Linux: S16-io/eof.t slurps a /proc file (skipped elsewhere) and
# `$*IN` on a TTY (todo on darwin), so it showed on a RISC-V board.
#
# `.tell` after a `.seek` into the middle answered from 0, not from where the
# seek landed; it is checked here too. Every expected value is Rakudo's.
# Contract: exit 0 + last line PASS.
my $ok = 0; my $n = 0;
sub ck($got, $want, $desc) {
    $n++;
    if $got eqv $want { $ok++ }
    else { say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

my $p = $*TMPDIR.add("slurp-at-end-$*PID.txt");
LEAVE { $p.unlink if $p.e }
$p.spurt("line1\nline2\n");

{ my $fh = $p.open; $fh.slurp; ck($fh.eof, True, 'file: .eof after .slurp'); $fh.close }
{ my $fh = $p.open; $fh.slurp; ck($fh.get, Nil, 'file: .get after .slurp finds nothing'); $fh.close }
{ my $fh = $p.open; $fh.slurp; ck($fh.slurp, '', 'file: a second .slurp is empty'); $fh.close }
{ my $fh = $p.open; $fh.slurp; ck($fh.lines.List, (), 'file: .lines after .slurp is empty'); $fh.close }
{ my $fh = $p.open; $fh.slurp; ck($fh.getc, Nil, 'file: .getc after .slurp finds nothing'); $fh.close }
{ my $fh = $p.open; $fh.comb.eager; ck($fh.eof, True, 'file: .eof after .comb'); $fh.close }
{ my $fh = $p.open; ck($fh.eof, False, 'file: a fresh handle is not at its end'); $fh.close }
{ my $fh = $p.open; $fh.get; $fh.slurp; ck($fh.eof, True, 'file: .get then .slurp'); $fh.close }
{ my $fh = $p.open; $fh.slurp; ck($fh.tell, 12, 'file: .tell after .slurp is the size'); $fh.close }
{
    my $fh = $p.open; $fh.slurp; $fh.seek(0, SeekFromBeginning);
    ck($fh.eof, False, 'file: .seek(0) after .slurp leaves the end');
    ck($fh.slurp, "line1\nline2\n", 'file: …and the file reads again');
    $fh.close;
}
{
    my $fh = $p.open; $fh.slurp; $fh.seek(6, SeekFromBeginning);
    ck($fh.get, 'line2', 'file: .seek(6) after .slurp lands on line 2');
    $fh.close;
}
{
    my $fh = $p.open; $fh.seek(3, SeekFromBeginning);
    ck($fh.tell, 3, 'file: .tell right after .seek(3)');
    $fh.get;
    ck($fh.tell, 6, 'file: …and after the rest of that line');
    $fh.close;
}

# a captured process stream
{
    my $o = run($*EXECUTABLE, '-e', 'print "a\nb\n"', :out).out;
    ck($o.slurp, "a\nb\n", 'Proc out: .slurp');
    ck($o.eof, True, 'Proc out: .eof after .slurp');
    ck($o.slurp, '', 'Proc out: a second .slurp is empty');
}

# $*IN, through a child: each prints what .eof says after the read
for 'with $*IN { print .eof; .slurp; print .eof }', 'FalseTrue',
    'slurp(); print $*IN.eof',                       'True',
    'words().elems; print $*IN.eof',                 'True',
    'with $*IN { .comb.elems; print .eof }',         'True'
  -> $code, $want {
    my $pr = run($*EXECUTABLE, '-e', $code, :in, :out);
    $pr.in.print("meow\npurr\n"); $pr.in.close;
    ck($pr.out.slurp(:close), $want, "\$*IN: $code");
}

if $ok == $n { say 'PASS' } else { say "FAIL ($ok/$n)" }
