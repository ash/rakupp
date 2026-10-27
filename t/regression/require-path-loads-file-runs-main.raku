# Regression: `require "/abs/path/Build.pm6"` loaded NOTHING, silently, and a
# file required by its path never ran its `sub MAIN`.
#
# The path was turned into a module name (`::abs::path::Build`) and looked
# up quietly on the search path, where nothing answers to it. zef's legacy
# builder runs `require '<dist>/Build.pm6'; ::('Build').new.build(…)`, so a
# Build.pm6 that is only a `sub MAIN` script (Timezones::ZoneInfo's) never ran
# here and the class lookup failed. Rakudo loads that very file, compiles it
# from source as a mainline, and runs its MAIN on @*ARGS at the end; a `use`
# of the same file does not run MAIN. Both `require "path"` and
# `require Stub:file(path)` take the file; a missing absolute path throws.

my $fails = 0;
sub ok($cond, $what) { $fails++ unless $cond; say "not ok - $what" unless $cond }

my $dir = $*TMPDIR.add("rakupp-require-path-{$*PID}");
$dir.mkdir;
LEAVE { run 'rm', '-rf', $dir.Str }
$dir.add('Script.pm6').spurt: q:to/MOD/;
    say "mainline";
    sub MAIN { say "MAIN ran" }
    MOD
$dir.add('Both.pm6').spurt: q:to/MOD/;
    sub MAIN { say "both MAIN" }
    class Build { method build($d) { say "build $d"; True } }
    MOD
$dir.add('Args.pm6').spurt: q:to/MOD/;
    sub MAIN($x) { say "got $x" }
    MOD

sub probe(*@cmd) {
    my $p = run $*EXECUTABLE, |@cmd, :out, :err, :cwd($dir);
    my $out = $p.out.slurp(:close);
    my $err = $p.err.slurp(:close);
    ($out.lines.join('|'), $err, $p.exitcode)
}
my $s = $dir.add('Script.pm6').absolute;
my $b = $dir.add('Both.pm6').absolute;

my ($o, $e, $x) = probe '-e', "require '$s'; say 'after'";
ok $o eq 'mainline|MAIN ran|after', "an absolute path loads the file and runs its MAIN (got '$o')";

($o, $e, $x) = probe '-I.', '-e', 'require "Script.pm6"; say "after"';
ok $o eq 'mainline|MAIN ran|after', "a relative path under -I does too (got '$o')";

($o, $e, $x) = probe '-e', "require Stub:file('$b'); say 'after'";
ok $o eq 'both MAIN|after', "require Stub:file(path) runs MAIN (got '$o')";

# zef's legacy-build command, a hook with both a MAIN and a Build class
($o, $e, $x) = probe '-e', "require '$b'; ::('Build').new.build('d') ?? exit(0) !! exit(1)";
ok $o eq 'both MAIN|build d' && $x == 0, "zef's command runs MAIN, then Build.build (got '$o', exit $x)";

($o, $e, $x) = probe '-e', "require '$s'; require '$s'; say 'after'";
ok $o eq 'mainline|MAIN ran|after', "a file required twice loads and runs MAIN once (got '$o')";

($o, $e, $x) = probe '-e', "sub MAIN \{ say 'program MAIN' }; require '$b'; say 'after'";
ok $o eq 'both MAIN|after|program MAIN', "the program's own MAIN still runs, last (got '$o')";

($o, $e, $x) = probe '-e', "require '{$dir.add('Args.pm6').absolute}'", 'zz';
ok $o eq 'got zz', "the required MAIN binds the program's @*ARGS (got '$o')";
($o, $e, $x) = probe '-e', "require '{$dir.add('Args.pm6').absolute}'; say 'after'";
ok $x == 2 && $e.contains('Usage'), "…and a failed bind is the usage path, exit 2 (exit $x)";

($o, $e, $x) = probe '-I.', '-e', 'use Script; say "after"';
ok $o eq 'mainline|after', "a `use` of the same file does not run MAIN (got '$o')";

($o, $e, $x) = probe '-e', "try require '{$dir.add('Nope.pm6').absolute}'; say \$!.^name";
ok $o eq 'X::NotFoundInRepository', "a missing absolute path throws, as in Rakudo (got '$o')";

say $fails == 0 ?? 'PASS' !! "FAIL ($fails)";
exit $fails == 0 ?? 0 !! 1;
