# `fail` in a compiled ROUTINE returns its Failure from that routine, as it does
# interpreted. The builtin decides between returning and dying by the
# interpreter's routine frame — with none, `fail` is `die` (Rakudo's rule for
# the mainline) — and compiled code runs in no frame of the interpreter's, so
# every compiled sub that failed DIED instead: `q(-1) // "failed"` printed the
# exception and exited 1, with or without -O. A compiled routine now calls
# rtFailRoutine, which returns the Failure (or throws it under `use fatal`).
#
# Contract: exit 0 + last line PASS.
my @fail;
my $bin = $*EXECUTABLE.absolute;
my $dir = $*TMPDIR.add("rakupp-fail-routine-{$*PID}");
$dir.mkdir;
LEAVE { try { .unlink for $dir.dir; $dir.rmdir } }

my $src = q:to/END/;
    sub q($x) { fail "bad" if $x < 0; $x }
    say (q(-1) // "failed") ~ q(2);
    my $r = q(-1); say $r.defined; say $r.exception.message;
    sub fl() { my sub q2($x) { fail "bad2" }; (q2(1) // "failed2") }
    say fl();
    class K { method m($x) { fail "meth" if $x; 5 } }
    say K.new.m(1) // "mfailed"; say K.new.m(0);
    sub inblock($n) { for ^$n -> $i { fail "at $i" if $i == 2 }; "done" }
    say inblock(5) // "blockfail"; say inblock(2);
    sub bare() { fail }
    say bare().exception.message;
    sub sink-it() { q(-5); "after" }
    say (try sink-it()) // "sunk: $!";
    sub fatal-one() { use fatal; q(-1); "not here" }
    say (try fatal-one()) // "fatal: {$!.message}";
    try { fail "top" }; say "top: ", $!.message;
    END
# Rakudo 2026.09's output
my $want = "failed2\nFalse\nbad\nfailed2\nmfailed\n5\nblockfail\ndone\nFailed\nsunk: bad\nfatal: bad\ntop: top";

my $f = $dir.add("fail.raku");
$f.spurt($src);
my $interp = run($bin, $f.absolute, :out, :err);
my $iout = $interp.out.slurp(:close).trim; $interp.err.slurp(:close);
@fail.push("interpreted: {$iout.raku}") unless $iout eq $want;
for ('-O', ''), ('', 'plain') -> ($opt, $tag) {
    my $exe = $dir.add("fail{$opt}.bin");
    my @cmd = $bin, '--exe', |($opt ?? $opt !! ()), $f.absolute, '-o', $exe.absolute;
    my $c = run(|@cmd, :out, :err);
    $c.out.slurp(:close); $c.err.slurp(:close);
    if !$exe.e { @fail.push("--exe $opt: did not compile"); next }
    my $r = run($exe.absolute, :out, :err);
    my $cout = $r.out.slurp(:close).trim; $r.err.slurp(:close);
    @fail.push("--exe $opt: {$cout.raku}") unless $cout eq $want;
}

if @fail { .say for @fail; say "FAIL" } else { say "PASS" }
