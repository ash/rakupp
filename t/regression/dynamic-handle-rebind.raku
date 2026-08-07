# Rebinding the dynamic output handles — `my $*OUT = $handle;` must reroute
# say/print/put to the handle (and `my $*ERR = …` must reroute note), exactly
# as in Rakudo; the rebinding ends with the block. Found building -i in-place
# editing (v3 CLI step 5): ioEmit routed a rebound $*OUT only when it held a
# user OBJECT — a real FileHandle fell through to stdout, so the whole edit
# leaked to the terminal and the file came out empty. The original probe was
# read wrong because a leak and a success print identically — this file keeps
# the two apart by capturing the live stream through a subprocess.
# Passes under both rakupp and Rakudo.

my $work = $*TMPDIR.add("dyn-rebind-$*PID");
mkdir $work;

my $fails = 0;
sub check(Str $desc, $got, $want) {
    if $got eq $want {
        say "ok - $desc";
    }
    else {
        $fails++;
        say "not ok - $desc";
        note "GOT [{$got}] WANT [{$want}]";
    }
}

my $target = $work.add('target.txt');
my $prog   = $work.add('prog.raku');
$prog.spurt(qq:to/END/);
    my \$h = open("{$target}", :w);
    \{
        my \$*OUT = \$h;
        say "s";
        print "p";
        put "u";
        "m".say;
    \}
    \$h.close;
    say "live-out";
    my \$e = open("{$target}.err", :w);
    \{
        my \$*ERR = \$e;
        note "n";
    \}
    \$e.close;
    note "live-err";
    END

my $p = run($*EXECUTABLE, $prog.Str, :out, :err);
my ($out, $err) = $p.out.slurp(:close), $p.err.slurp(:close);

check('say/print/put/.say all follow a rebound $*OUT', $target.IO.slurp, "s\npu\nm\n");
check('the rebinding ends with the block', $out, "live-out\n");
check('note follows a rebound $*ERR', ($target ~ '.err').IO.slurp, "n\n");
check('$*ERR rebinding ends with the block too', $err, "live-err\n");

unlink $prog; unlink $target; unlink $target ~ '.err';
say $fails == 0 ?? 'PASS' !! 'FAIL';
exit($fails ?? 1 !! 0);
