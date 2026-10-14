# Regression: `$?FILE` in a file EVALFILE'd from a ROUTINE in a module named
# the module, not the file.
#
# `$?FILE` follows the routine whose frame is running when it was entered
# after the current file's top level began (fileConstNow, curDeclDepth_).
# EVALFILE set the file but not that depth, so when a module's `sub load($f) {
# EVALFILE $f }` read a file, the file's top level — running in load's frame —
# answered "Loader.rakumod (Loader)". Rakudo answers the file's own path.
# Found by RakuPress, whose app module EVALFILEs a Webraku page that locates
# its own sibling module with `use lib $?FILE.IO.parent`.
#
# Contract: exit 0 + last line PASS.

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — got {$got.raku}, want {$want.raku}" }
}

my $dir = $*TMPDIR.add("rakupp-evalfile-{$*PID}");
$dir.add('lib').mkdir;
$dir.add('lib/Loader.rakumod').spurt: q:to/END/;
    unit module Loader;
    sub load(Str $f) is export { EVALFILE $f }
    END
$dir.add('target.raku').spurt: q:to/END/;
    sub inner() { $?FILE.IO.basename }
    ($?FILE.IO.basename, inner(), $?LINE)
    END
$dir.add('main.raku').spurt: q:to/END/;
    use lib $?FILE.IO.parent.add('lib').Str;
    use Loader;
    say load($?FILE.IO.parent.add('target.raku').Str).join(' ');
    END

my $p = run $*EXECUTABLE, $dir.add('main.raku').Str, :out, :err;
my $out = $p.out.slurp(:close).trim;
$p.err.slurp(:close);
ck $out, 'target.raku target.raku 2', 'the EVALFILEd file, its routine, and its line';

# (a precompiling engine leaves a .precomp beside the module)
sub rm-tree(IO::Path $d) { for $d.dir { .d && !.l ?? rm-tree($_) !! .unlink }; $d.rmdir }
rm-tree($dir);

say $fails ?? "FAIL: $fails" !! 'PASS';
exit $fails ?? 1 !! 0;
