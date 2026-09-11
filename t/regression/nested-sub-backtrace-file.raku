# Regression: a `my sub` declared inside a module's routine was attributed to
# the MAIN PROGRAM in a backtrace, with the MODULE's line number — so the frame
# named a line that need not exist in the file it named, and the source excerpt
# under it came from the wrong file (or from nowhere).
#
# The cause: a routine records its declaring file at declaration time, and that
# was `curDeclFile()` — the file whose TOP LEVEL is running. Right for anything
# declared while a module loads, wrong for anything declared inside a routine
# called later, when the program's top level is what is running. declFileNow()
# asks the question $?FILE already answers: which file was the code that is
# executing WRITTEN in.
#
# Found 2026-09-10 debugging GUI::Wings on Windows, where a frame read
# `in sub put-ptr at examples\counter.raku line 170` for a sub written in
# lib/GUI/Wings/Backend/Win32.rakumod.
# Contract: exit 0 + last line PASS.
my @fail;

my $dir = $*TMPDIR.add("rk-btfile-{$*PID}");
$dir.add('lib').mkdir(:p);
$dir.add('lib/BtProbe.rakumod').spurt(q:to/MOD/);
    unit class BtProbe;
    method go() {
        my sub inner($x) { die "boom from a nested sub" }
        inner(1);
    }
    MOD
$dir.add('main.raku').spurt(q:to/MAIN/);
    use BtProbe;
    BtProbe.new.go;
    MAIN

my $p = run($*EXECUTABLE, '-I', $dir.add('lib').absolute, $dir.add('main.raku').absolute,
            :out, :err);
$p.out.slurp(:close);
my @lines = $p.err.slurp(:close).lines;

# The frame for the nested sub, and the one for the method that declared it.
my $inner  = @lines.first(*.contains('in sub inner'))    // '<no frame for inner>';
my $method = @lines.first(*.contains('BtProbe::go'))     // '<no frame for go>';

@fail.push("the nested sub names the wrong file: $inner")
    unless $inner.contains('BtProbe.rakumod');
@fail.push("the nested sub still names the program: $inner")
    if $inner.contains('main.raku');
# line 3 of the module is where `inner` is written; a frame that names a file
# must name a line that is IN it.
@fail.push("the nested sub names the wrong line: $inner")
    unless $inner.contains('line 3');
# the method's own frame was always right — it must stay right
@fail.push("the method frame changed: $method")
    unless $method.contains('BtProbe.rakumod') && $method.contains('line 4');

note @fail.join("\n") if @fail;
say @fail ?? "FAIL" !! "PASS";
exit @fail ?? 1 !! 0;
