#!/usr/bin/env rakupp
# The --cpp build check: does every program `--cpp` accepts also compile?
#
#   rakupp tools/cpp-build-check.raku                        # the rakupp Gate picks
#   RAKUPP=/path/to/rakupp rakupp tools/cpp-build-check.raku # a named binary
#
# Options: --only=TEXT (programs whose path contains TEXT), --no-roast,
# --roast-list=FILE (default: the newest docs/status/roast-lists/vX.Y.Z.list),
# --out=FILE (every program's result as a TSV).
#
# The programs are every `.raku` under examples/ and t/, and every fully-passing
# Roast file on the roast list, read from the Roast checkout (ROAST=/path, or
# ~/roast). Each is translated with `rakupp --cpp`. A program the code generator
# refuses (exit 5) is outside the native subset: `--exe` falls back to bundling
# it, which is correct, and it is only counted here. A program it ACCEPTS is
# compiled with the C++ compiler `--exe` would use ($CXX, else c++), under
# `-std=c++17 -fsyntax-only` and the runtime's headers. It has to compile: a
# program --cpp accepts and the compiler rejects is an `--exe` that fails to
# build, with no fallback (V5-PLAN B0; the `Inf`/`NaN` literals were ten such
# programs at the 2026-09-26 survey).
#
# -fsyntax-only skips code generation and the link, so this checks what the
# generator writes, not the runtime archive. It runs sequentially, about a
# second a program, and is a release gate rather than a per-batch one.
#
# Exit 0: every accepted program compiles. Exit 1: some do not, listed. Exit 2:
# could not judge — no binary, no headers, no compiler — never a pass.

use lib $?FILE.IO.parent.add('lib').Str;
use Gate;
use Battery;

my $TOOL = 'cpp-build-check';
my $REPO = $?FILE.IO.parent.parent.absolute.IO;
my ($only, $out-file, $roast-list) = Str, Str, Str;
my $with-roast = True;
for @*ARGS -> $a {
    if    $a ~~ / ^ '--only=' (.+) $ /       { $only = ~$0 }
    elsif $a ~~ / ^ '--out=' (.+) $ /        { $out-file = ~$0 }
    elsif $a ~~ / ^ '--roast-list=' (.+) $ / { $roast-list = ~$0 }
    elsif $a eq '--no-roast'                 { $with-roast = False }
    else { note "$TOOL: unknown argument: $a"; exit 2 }
}

my %PICK = pick-rakupp($REPO);
require-native(%PICK, :tool($TOOL), :verdict<INCONCLUSIVE>);
my $ENGINE = %PICK<path>.IO.absolute;

# The headers --exe compiles against, found the way `findRuntime` in
# src/main.cpp finds them: RAKUPP_HOME first, then the build tree's src/, then
# an installed prefix.
my $bin-dir = $ENGINE.IO.parent;
my @inc-candidates = |(%*ENV<RAKUPP_HOME> ?? %*ENV<RAKUPP_HOME>.IO.add('include').add('rakupp') !! ()),
    $bin-dir.parent.add('src'), $bin-dir.parent.add('include').add('rakupp'),
    $bin-dir.add('include').add('rakupp');
my $INC = @inc-candidates.first(*.add('Interpreter.h').f);
without $INC {
    note "$TOOL: no runtime headers (Interpreter.h) beside $ENGINE. Looked in:";
    note "  $_" for @inc-candidates;
    exit 2;
}
my $CXX = %*ENV<CXX> // 'c++';
{
    my $v = run($CXX, '--version', :out, :err);
    $v.out.slurp(:close); $v.err.slurp(:close);
    unless $v.exitcode == 0 { note "$TOOL: no C++ compiler '$CXX' (set CXX)"; exit 2 }
}

sub raku-files(IO::Path $dir --> List) {
    my @out;
    my @todo = $dir;
    while @todo {
        my $d = @todo.shift;
        next unless $d.d;
        for $d.dir -> $e {
            if $e.d { @todo.push: $e }
            elsif $e.basename.ends-with('.raku') { @out.push: $e }
        }
    }
    @out.sort(*.Str).List
}

# %( path => the file, label => what the report calls it, cwd => where it runs )
my @progs = <examples t>.map(-> $d {
    |raku-files($REPO.add($d)).map({ %( path => $_, label => .relative($REPO), cwd => .parent ) })
});

my $ROAST = (%*ENV<ROAST> // ((%*ENV<HOME> // '.') ~ '/roast')).IO;
my $roast-note = 'Roast not included (--no-roast)';
if $with-roast {
    without $roast-list {
        my $dir = $REPO.add('docs').add('status').add('roast-lists');
        $roast-list = $dir.dir.grep({ .basename ~~ / ^ 'v' \d+ '.' \d+ '.' \d+ '.list' $ / })
            .sort({ $^a.basename.substr(1).split('.')[^3]».Int cmp $^b.basename.substr(1).split('.')[^3]».Int })
            .tail.?absolute;
    }
    unless $roast-list && $roast-list.IO.f && $ROAST.d {
        note "$TOOL: need a roast list and a Roast checkout (ROAST=/path), or pass --no-roast.";
        note "  list: {$roast-list // 'none found'}; checkout: $ROAST";
        exit 2;
    }
    my @listed = $roast-list.IO.lines.grep(*.trim.chars).map({ $ROAST.add(.trim) });
    my @missing = @listed.grep(!*.f);
    @progs.append: @listed.grep(*.f).map({ %( path => $_, label => 'roast/' ~ .relative($ROAST), cwd => $ROAST ) });
    $roast-note = "Roast {$ROAST} with {@listed.elems} files from {$roast-list.IO.basename}"
                ~ (@missing ?? ", {@missing.elems} of them missing from the checkout" !! '');
}
@progs = @progs.grep(*<label>.contains($only)) if $only;
unless @progs { note "$TOOL: no programs to check"; exit 2 }

say provenance-line($TOOL, %PICK);
say "$TOOL: headers $INC, compiler $CXX";
say "$TOOL: {@progs.elems} programs; $roast-note";

my $WORK = $*TMPDIR.add("cpp-build-check-{$*PID}");
$WORK.mkdir;
my $GEN = $WORK.add('gen.cpp');

my @rows;
my %count = compiles => 0, refused => 0, FAILS => 0, other => 0;
my $t0 = now;
for @progs.kv -> $i, %p {
    $GEN.unlink if $GEN.e;
    my %t = run-capped([$ENGINE, '--cpp', '-o', $GEN.absolute, %p<path>.absolute],
                       :cap(60), :work(%p<cwd>), :env(TZ => 'UTC'));
    my ($verdict, $note);
    if %t<rc> == 5 && !%t<signal> {
        ($verdict, $note) = 'refused', first-line(%t);
    }
    elsif %t<rc> != 0 || %t<signal> || !$GEN.f {
        # a parse error, a crash or a hang in the translator: not this gate's
        # question (Roast and t/ judge those), but never silently dropped
        ($verdict, $note) = 'other', %t<signal> ?? "--cpp: {signal-name(%t<signal>)}"
                                                !! "--cpp exit {%t<rc>}: {first-line(%t)}";
    }
    else {
        my %c = run-capped([$CXX, '-std=c++17', '-w', '-fsyntax-only', '-I', $INC.absolute, $GEN.absolute],
                           :cap(300), :work($WORK));
        if %c<rc> == 0 && !%c<signal> {
            ($verdict, $note) = 'compiles', '';
        }
        else {
            my $err = %c<err>.lines.first(* ~~ / 'error' /) // first-line(%c);
            ($verdict, $note) = 'FAILS', $err.subst(/ ^ .*? 'gen.cpp:' /, 'gen.cpp:').subst(/\t/, ' ', :g).substr(0, 200);
            say "FAILS  {%p<label>}";
            say "       $note";
        }
    }
    %count{$verdict}++;
    @rows.push: [%p<label>, $verdict, $note];
    note "$TOOL: {$i + 1}/{@progs.elems} programs, {%count<FAILS>} fail to compile, "
       ~ "{((now - $t0) / 60).round(0.1)} min"
        if ($i + 1) %% 250;
}

if $out-file {
    my $fh = open $out-file, :w;
    $fh.say: "program\tverdict\tnote";
    $fh.say: .join("\t") for @rows;
    $fh.close;
    say "$TOOL: every program's result in $out-file";
}
run 'rm', '-rf', $WORK.absolute;

my $accepted = %count<compiles> + %count<FAILS>;
say "$TOOL: {@progs.elems} programs in {((now - $t0) / 60).round(0.1)} min — "
  ~ "--cpp accepts {$accepted}, refuses {%count<refused>}, fails otherwise on {%count<other>}; "
  ~ "of the accepted, {%count<compiles>} compile and {%count<FAILS>} do not";
say %count<FAILS> ?? "$TOOL: RED — {%count<FAILS>} programs --cpp accepts do not compile"
                  !! "$TOOL: GREEN — every program --cpp accepts compiles";
exit %count<FAILS> ?? 1 !! 0;
