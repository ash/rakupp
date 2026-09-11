# Regression: `shell(CMD, :out($fh))` — the adverb is a SINK, not a flag. run()
# had learned that; shell() read it as a mere boolean, captured the child's
# output and dropped it, so the file stayed empty.
#
# …and the copy has to be FLUSHED. Rakudo hands the child the handle's own
# descriptor, so the bytes are in the file the moment the child exits; a
# buffered `print` here is not on disk until the handle closes. App::RaCoCo
# slurps the file with the handle still open and read an empty one.
#
# Every expectation was checked against Rakudo.

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

my $dir = $*TMPDIR.add("rakupp-shell-sink-$*PID");
$dir.mkdir;
LEAVE { try { .unlink for $dir.dir; $dir.rmdir } }

# --- the reported shape: read the file with the handle still open ---------
{
    my $f = $dir.add('a.txt');
    my $fh = $f.open(:w);
    my $p = shell(q{echo SHELL-SINK-A}, :out($fh));
    ck($f.slurp.trim, 'SHELL-SINK-A', 'shell(:out($fh)) writes the file');
    ck($p.exitcode, 0, '…and the Proc still reports the exit code');
    $fh.close;
}

# --- :err takes the same route -------------------------------------------
{
    my $f = $dir.add('b.txt');
    my $fh = $f.open(:w);
    shell(q{echo SHELL-SINK-B 1>&2}, :err($fh), :!out);
    ck($f.slurp.trim, 'SHELL-SINK-B', 'shell(:err($fh)) writes the file');
    $fh.close;
}

# --- run() must keep working, which is where the rule came from -----------
{
    my $f = $dir.add('c.txt');
    my $fh = $f.open(:w);
    run('echo', 'RUN-SINK-C', :out($fh));
    ck($f.slurp.trim, 'RUN-SINK-C', 'run(:out($fh)) writes the file');
    $fh.close;
}

# --- a BOOLEAN :out still captures into the Proc --------------------------
{
    my $p = shell(q{echo SHELL-CAPTURED}, :out);
    ck($p.out.slurp(:close).trim, 'SHELL-CAPTURED', 'a boolean :out still captures');
}

say $fails ?? "FAIL ($fails)" !! "PASS";
exit $fails ?? 1 !! 0;
