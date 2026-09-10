# Regression: closing a Proc's pipes must be able to report a child that failed.
#
# `$proc.out` / `$proc.err` answered a plain captured FileHandle whose `.close`
# returned Bool::True, so a script that CLOSED its pipes instead of sinking the
# Proc never learned the child had failed. Rakudo's IO::Pipe.close answers the
# PROC; a bare `$p.err.close;` statement then SINKS that Proc, and the sink is
# what raises X::Proc::Unsuccessful. Closing was the one path that stayed quiet.
#
# Found via `rakupp test Gnome::N`: its Build.rakumod runs `ldconfig`, which no
# macOS has, and then closes both pipes. Under Rakudo the close stopped the
# build; under rakupp it returned True, so the build ran on and OVERWROTE the
# distribution's shipped NativeLib.rakumod with a header and no subs at all —
# a failed spawn turned into a silently truncated module and a failing suite.
#
# The pair that has to hold together: a PROC pipe answers the Proc (so the sink
# can fire), while a plain file handle still answers Bool — a change that makes
# every close return a Proc breaks `if $fh.close` everywhere.
#
# Every expectation was checked against Rakudo.

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

my $exe = $*EXECUTABLE.absolute;

# --- what .close ANSWERS ---------------------------------------------------
{
    my $p = run $exe, '-e', 'exit 0', :out, :err;
    ck($p.out.close.^name, 'Proc', '$proc.out.close answers the Proc');
    ck($p.err.close.^name, 'Proc', '$proc.err.close answers the Proc');
}

# …and a plain file handle is untouched: still Bool, so `if $fh.close` holds
{
    my $f = $*TMPDIR.add("rakupp-proc-close-{$*PID}.txt");
    my $fh = $f.open(:w);
    $fh.print('x');
    my $r = $fh.close;
    ck($r.^name, 'Bool', 'a plain file handle still answers Bool from .close');
    ck($r, True, '…and it is True');
    $f.unlink;
}

# --- a child that SUCCEEDED closes quietly ---------------------------------
{
    my $caught = '';
    {
        my $p = run $exe, '-e', 'exit 0', :out, :err;
        $p.err.close;
        $p.out.close;
        CATCH { default { $caught = .^name } }
    }
    ck($caught, '', 'closing the pipes of a child that succeeded throws nothing');
}

# --- a child that FAILED is reported at the close --------------------------
{
    my $caught = '';
    {
        my $p = run $exe, '-e', 'exit 3', :out, :err;
        $p.err.close;
        CATCH { default { $caught = .^name } }
    }
    ck($caught, 'X::Proc::Unsuccessful', 'closing the pipe of a child that failed reports it');
}

# …but only when the value is SUNK — assigning it asks no question
{
    my $caught = '';
    {
        my $p = run $exe, '-e', 'exit 3', :out, :err;
        my $r = $p.err.close;
        ck($r.^name, 'Proc', 'an assigned close answers the Proc');
        CATCH { default { $caught = .^name } }
    }
    ck($caught, '', '…and does not throw, because nothing sank it');
}

# --- the Build.rakumod shape: the script must STOP at the close ------------
{
    my $prog = q:to/PROG/;
        my $p = run $*EXECUTABLE.absolute, '-e', 'exit 3', :out, :err;
        for $p.out.lines -> $l { }
        $p.err.close;
        $p.out.close;
        say 'REACHED-PAST-CLOSE';
        PROG
    my $c   = run $exe, '-e', $prog, :out, :err;
    my $out = $c.out.slurp;
    my $err = $c.err.slurp;
    ck($out.contains('REACHED-PAST-CLOSE'), False,
       'a failed child stops the script at the close, as Rakudo does');
    ck($err.contains('exited unsuccessfully'), True,
       '…saying the spawned command exited unsuccessfully');
    ck($c.exitcode != 0, True, '…and the script exits non-zero');
}

say $fails ?? "FAIL ($fails)" !! "PASS";
exit $fails ?? 1 !! 0;
