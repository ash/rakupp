# Test's failure diagnostic ("# Failed test '…' at FILE line N") named the last
# line the ARGUMENTS ran, not the line of the `is`: evaluating `is f(), 3`
# runs f() first, and every statement f() executes advances the interpreter's
# current line into f's body — and into f's FILE when f lives in a module, so
# a 115-line test file once reported a failure "at t/02-write.t line 420",
# which was a line inside JSON::Fast's to-json that the test's oracle had
# called. Now a routine call puts the caller's line back when its frame pops
# (Interpreter::callCallableRaw / invokeMethod), so the diagnostic names the
# statement that made the assertion — the line Rakudo reports.
#
# The program under test is written to a temp dir with a module beside it whose
# sub sits past line 300, so a wrong answer cannot be mistaken for a right one.
# Contract: exit 0 + last line PASS.
my @fail;
my $rakupp = $*EXECUTABLE.absolute;
my $dir = $*TMPDIR.add("rakupp-testline-{$*PID}");
$dir.add('lib').mkdir;
$dir.add('lib/Far.rakumod').spurt(
    "unit module Far;\n" ~ ("\n" x 300)
    ~ "our sub far() is export \{ my \$x = 1;\n    \$x + 1\n}\n"
    ~ "class Ob is export \{ method m() \{ my \$z = 3;\n    \$z * 2\n} }\n"
    ~ "our sub boom() is export \{ my \$q = 0;\n    die 'boom'\n}\n");
my $prog = $dir.add('t.raku');
$prog.spurt(q:to/END/);
    use lib $*PROGRAM.parent.add('lib').Str;
    use Test;
    use Far;
    sub near() { my $y = 5;
        $y * 2
    }
    is far(), 3, 'a sub from another file';
    is near(), 11, 'a sub defined below the assertion';
    is far(),
       3,
       'a multi-line assertion names its first line';
    ok near() == 11 && far() == 3, 'ok after two calls';
    is Ob.new.m(), 7, 'a method from another file';
    { boom(); CATCH { default { is .message, 'bang', 'after a throw caught here' } } }
    is-deeply [far()], [3], 'is-deeply after a call';
    done-testing;
    END

my $p = run($rakupp, $prog.absolute, :out, :err);
my $out = $p.out.slurp(:close);
my $err = $p.err.slurp(:close);

# the assertion lines of t.raku, in order — the multi-line `is` names line 9,
# where the statement starts, as Rakudo does
my @want = 7, 8, 9, 12, 13, 14, 15;
my @got = $err.lines.grep(*.starts-with('# Failed test')).map({ /'line' \s+ (\d+)/ ?? +$0 !! -1 });
@fail.push("expected 7 failures, got {@got.elems}:\n$err") unless @got.elems == @want.elems;
for @want Z @got -> ($w, $g) {
    @fail.push("reported line $g, wanted $w") unless $g == $w;
}
@fail.push("every diagnostic names t.raku:\n$err")
    unless $err.lines.grep(*.starts-with('# Failed test')).all.contains(' at ' ~ $prog.absolute ~ ' line ');

unlink($prog, $dir.add('lib/Far.rakumod'));
rmdir($dir.add('lib')); rmdir($dir);

if @fail { note "FAILED:\n" ~ @fail.join("\n"); say 'FAIL' }
else     { say 'PASS' }
