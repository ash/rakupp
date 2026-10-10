# Regression: `run(…, :in, :out($fh))` must deliver the child's output to $fh.
#
# A handle given to :out or :err is a SINK: run() copies the child's stream into
# it (procDrainToSink, BuiltinsRegister.cpp). A run with a bare `:in` and a sink
# is deferred — the child starts only when its stdin is closed or its output
# read — and that path recorded which streams were asked for but not the
# handles. The spawn in ProcIn's close (MethodCallPart2.cpp) captured the output
# and kept it, so the file stayed empty: `run 'cat', :in, :out($fh)` wrote
# nothing, and a `:err($fh)` beside an `:in` lost stderr the same way. The
# deferral now carries the handles and that spawn fills them. Rakudo 2026.09
# passes every row.

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

if $*DISTRO.is-win {
    say "ok - skipped on Windows: the rows use cat and sh";
    say "PASS";
    exit 0;
}

my $tmp = $*TMPDIR.add("rakupp-in-out-sink-$*PID");
END { $tmp.unlink if $tmp.e }

# --- :out($fh): what the child writes reaches the handle ---------------------
{
    my $fh = open($tmp, :w);
    my $p = run('cat', :in, :out($fh));
    $p.in.print("b\n");
    $p.in.close;
    ck($tmp.slurp, "b\n", ':in with :out($fh): the output is in the file once stdin closes');
    $fh.close;
    ck($p.exitcode, 0, 'and the child succeeded');
}

# --- after an earlier run's output, in order ---------------------------------
{
    my $fh = open($tmp, :w);
    run('echo', 'a', :out($fh));
    my $p = run('cat', :in, :out($fh));
    $p.in.print("b\n");
    $p.in.close;
    $fh.close;
    ck($tmp.slurp, "a\nb\n", 'two runs into one handle, in order');
}

# --- a child that is never written to still writes its output ----------------
{
    my $fh = open($tmp, :w);
    my $p = run('sh', '-c', 'echo y', :in, :out($fh));
    $p.in.close;
    $fh.close;
    ck($tmp.slurp, "y\n", 'stdin closed unwritten: the output still arrives');
}

# --- :err($fh) beside :in -----------------------------------------------------
{
    my $fh = open($tmp, :w);
    my $p = run('sh', '-c', 'cat >&2', :in, :err($fh), :out);
    $p.in.print("to-err\n");
    $p.in.close;
    $fh.close;
    ck($tmp.slurp, "to-err\n", ':in with :err($fh): stderr is in the file');
    ck($p.out.slurp(:close), '', 'and :out alone stays a pipe, here empty');
}

say $fails ?? "FAIL ($fails)" !! "PASS";
exit $fails ?? 1 !! 0;
