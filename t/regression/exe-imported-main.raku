# #112: a MAIN a program imports from a module (`multi sub MAIN(...) is
# export` there, `use M;` here) runs under the interpreter and --aot, but a
# NATIVE --exe binary only dispatched a MAIN declared in the script itself —
# the imported one never ran, and the binary printed nothing and exited 0,
# even with no arguments (no usage either). Expectations are Rakudo's output
# and exit codes for the same module and script.
#
# Each compile asserts "(native)": if it ever falls back to the bundled
# interpreter, this file stops guarding native codegen and says so.

my $rakupp = $*RAKU.compiler.name eq 'Raku++';
unless $rakupp {
    note 'exe-imported-main: not rakupp, nothing to compile';
    say 'PASS';
    exit 0;
}

my $work = $*TMPDIR.add("exe-imported-main-$*PID");
mkdir $work;
mkdir $work.add('lib');

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

my $mod = $work.add('lib').add('ImpMain112.rakumod');
$mod.spurt(q:to/END/);
    unit module ImpMain112;
    multi sub MAIN("version") is export { say "imp112 version" }
    END
my $src = $work.add('p.raku');
$src.spurt("use ImpMain112;\n");
my $bin = $work.add('p');

my $p = run($*EXECUTABLE, '-I', $work.add('lib').Str, '--exe', '-o', $bin.Str, $src.Str, :out, :err);
my $built = $p.out.slurp(:close) ~ $p.err.slurp(:close);
if $p.exitcode != 0 || !$built.contains('(native)') {
    $fails++;
    note "compile did not produce a native binary:\n$built";
}
else {
    # stdout, whether stderr said Usage, and the exit code
    sub probe(*@args) {
        my $r = run($bin.Str, |@args, :out, :err);
        my $out = $r.out.slurp(:close).trim;
        my $err = $r.err.slurp(:close);
        "$out|{$err.contains('Usage') ?? 'usage' !! ''}|{$r.exitcode}"
    }
    check('the imported MAIN runs',              probe('version'), 'imp112 version||0');
    check('no arguments: usage, exit 2',         probe(), '|usage|2');
    check('a non-matching argument: usage, exit 2', probe('bogus'), '|usage|2');
    my $h = run($bin.Str, '--help', :out, :err);
    my $hout = $h.out.slurp(:close);
    $h.err.slurp(:close);
    check('--help prints usage to stdout, exit 0', "{$hout.contains('version')}|{$h.exitcode}", 'True|0');
}

unlink $_ for $bin, $src, $mod;
try rmdir $work.add('lib');
try rmdir $work;
say $fails == 0 ?? 'PASS' !! 'FAIL';
exit($fails ?? 1 !! 0);
