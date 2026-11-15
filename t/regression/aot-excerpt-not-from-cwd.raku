# An --aot binary's error trace showed the source line of whatever file in
# the WORKING DIRECTORY had the script's name — GitHub issue #265.
#
# The binary carries the program's tree, not its text, and the excerpt under
# the origin frame was read by opening the script's name relative to the
# current directory. Run from a directory that held a different file of that
# name, the trace printed that file's line as the program's; the reporter's
# was an executable, and the "line" was kilobytes of machine code.
#
# The binary now shows no excerpt (as Rakudo shows none). Needs a C++
# toolchain, as the other compiling cases do.

my $rakupp = $*RAKU.compiler.name eq 'Raku++';
unless $rakupp {
    note 'aot-excerpt-not-from-cwd: not rakupp, nothing to compile';
    say 'PASS';
    exit 0;
}

my $work = $*TMPDIR.add("aot-excerpt-not-from-cwd-$*PID");
my $elsewhere = $work.add('elsewhere');
mkdir $elsewhere;

my $src = $work.add('sink.raku');
$src.spurt: "sub bye() \{ die 'boom' }\nbye();\n";
$elsewhere.add('sink.raku').spurt: "IMPOSTOR LINE ONE\nIMPOSTOR LINE TWO\n";

my $bin = $work.add('sink.bin');
my $c = run $*EXECUTABLE, '--aot', '-o', $bin.Str, $src.Str, :out, :err;
$c.out.slurp(:close);
my $cerr = $c.err.slurp(:close);
my $fails = 0;
if $c.exitcode != 0 || !$bin.e {
    note "compile failed: $cerr";
    $fails++;
}
else {
    my $p = run $bin.Str, :cwd($elsewhere.Str), :out, :err;
    $p.out.slurp(:close);
    my $err = $p.err.slurp(:close);
    if $err.contains('IMPOSTOR') {
        $fails++;
        say 'not ok - the trace printed a line of the cwd file of the same name';
        note $err;
    }
    else { say 'ok - no line from the cwd file' }
    if $err.contains('boom') && $err.contains('in sub bye at sink.raku line 1') {
        say 'ok - the trace still names the frame';
    }
    else {
        $fails++;
        say 'not ok - the trace lost its frame';
        note $err;
    }
}

for $elsewhere.add('sink.raku'), $elsewhere, $bin, $src, $work { .unlink if .f; .rmdir if .d }
say $fails ?? 'FAIL' !! 'PASS';
exit $fails ?? 1 !! 0;
