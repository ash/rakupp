# Regression: `use Slang::X` arms the slang even when a module used before it
# had already pulled it in.
#
# The parser scans each `use`d module for operators and slangs, once per unit;
# a module scanned already returned at once with "not a slang". A module that
# itself says `use Slang::X` gets its uses scanned too (arming nothing, as it
# should: only the module the program names may), so the program's own
# `use Slang::X` after it found the slang scanned, and the rest of the file was
# read without it. Text::CSV's t/91_csv_cb.t says `use Text::CSV; use
# Slang::Tuxic;`, and `csv (in => $file, …), @aoa, "…"` parsed as one call of
# three arguments; `rakupp install Text::CSV` refused the dist. The other order
# always worked.
#
# rakupp only: the fixture slang (t/fixtures/slang-lib, as slang-seams.raku
# uses it) does not load under Rakudo 2026.09, whichever order.
#
# Contract: exit 0 + last line PASS.
my @fail;
sub check($what, $got, $want) {
    @fail.push("$what: got {$got.raku} want {$want.raku}") unless $got eqv $want
}

my $fixtures = $?FILE.IO.parent.add('../fixtures/slang-lib').absolute;
my $dir = $*TMPDIR.add("rakupp-slang-again-{$*PID}");
$dir.mkdir;
LEAVE { .unlink for $dir.dir; $dir.rmdir }
$dir.add('UsesSeams.rakumod').spurt(q:to/END/);
    unit module UsesSeams;
    use Slang::Seams;
    sub helper is export { 'helper' }
    END

sub run-prog($name, $src) {
    my $f = $dir.add("$name.raku");
    $f.spurt($src);
    my $p = run $*EXECUTABLE, '-I', $fixtures, '-I', $dir.absolute, $f.absolute, :out, :err;
    ($p.out.slurp(:close) ~ $p.err.slurp(:close)).trim
}

my $body = q:to/END/;
    sub plain($a, $b) { $a - $b }
    say plain (7, 2), helper;
    END

check 'the slang first',             run-prog('first',  "use Slang::Seams;\nuse UsesSeams;\n$body"), '5helper';
check 'the module that uses it first', run-prog('second', "use UsesSeams;\nuse Slang::Seams;\n$body"), '5helper';
check 'the slang named twice',       run-prog('twice',  "use Slang::Seams;\nuse Slang::Seams;\nsub plain(\$a, \$b) \{ \$a - \$b }\nsay plain (7, 2);\n"), '5';

if @fail {
    .say for @fail;
    say "FAIL";
    exit 1;
}
say "PASS";
