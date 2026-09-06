# Regression: `$?FILE` inside a module answered the MAIN program's file.
#
# $?FILE is the file the code was WRITTEN in — a compile-time fact — so a
# module's top level and its subs name the module, and a block written in the
# program still names the program when a module's sub calls it. Rakudo spells a
# module's answer as the absolute source path followed by the name the module
# was loaded as, in parens ("…/QFile.rakumod (QFile)"); this engine matches it.
# Two neighbours fixed with it: -e code answered its cwd-prefixed name, and a
# program on stdin was called -e rather than -.
# Found by --trace during the CLI campaign (2026-09-06). Runs under both engines.
#
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) {
    @fail.push("$what: got {$got.raku} want {$want.raku}") unless $got eq $want
}
# paths compared resolved, so a symlinked temp dir (/var → /private/var on
# macOS) cannot fail the comparison on either engine's spelling
sub same-file($got, $want) { $got.IO.resolve.Str eq $want.IO.resolve.Str }

my $dir = $*TMPDIR.add("qfile-{$*PID}");
$dir.mkdir;
my $mod = $dir.add('QFile.rakumod');
$mod.spurt: q:to/END/;
    unit module QFile;
    our $top = $?FILE;
    our $line = $?LINE;
    sub in-sub() is export { $?FILE }
    sub run-cb(&cb) is export { cb() }
    END
my $prog = $dir.add('prog.raku');
$prog.spurt: q:to/END/;
    use QFile;
    say $?FILE;
    say $QFile::top;
    say $QFile::line;
    say in-sub();
    say run-cb({ $?FILE });
    END
my $p = run($*EXECUTABLE, '-I', ~$dir, ~$prog, :out, :err);
my @l = $p.out.slurp(:close).lines;
my $err = $p.err.slurp(:close);
check $err, '', 'no diagnostics';
check @l.elems, 5, 'five answers';
check same-file(@l[0] // '', $prog), True, 'the program names itself';
for 1, 3 -> $i {
    my $where = $i == 1 ?? 'a module top level' !! 'a module sub';
    my $got = @l[$i] // '';
    check $got.ends-with(' (QFile)'), True, "$where: the module name in parens, as Rakudo spells it";
    check same-file($got.subst(/' (QFile)' $/, ''), $mod), True, "$where names the module file";
}
check @l[2] // '', '3', '$?LINE in a module counts the module';
check same-file(@l[4] // '', $prog), True, 'a block written in the program says the program, even called from the module';

# a program with no file answers its plain name: -e for a one-liner, - for stdin
my $e = run($*EXECUTABLE, '-e', 'say $?FILE; sub f { $?FILE }; say f()', :out, :!err);
check $e.out.slurp(:close), "-e\n-e\n", '-e code says -e, from its mainline and from a sub';
my $s = run($*EXECUTABLE, :in, :out, :!err);
$s.in.print("say \$?FILE; say \$*PROGRAM-NAME\n"); $s.in.close;
check $s.out.slurp(:close), "-\n-\n", 'a program on stdin says -, as does $*PROGRAM-NAME';

# recursive: Rakudo leaves its .precomp store beside the module
sub rm-rf(IO::Path $d) { for $d.dir { .d ?? rm-rf($_) !! .unlink }; $d.rmdir }
rm-rf($dir);
if @fail { note "FAILED: @fail.join('; ')"; say 'FAIL' } else { say 'PASS' }
