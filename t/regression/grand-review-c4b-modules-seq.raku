# Regression: the Grand Review, batch C4b — exact big-Int sequences, selective
# imports, the `file#` repo spec, CALLER:: (docs/dev/findings/REVIEW-GRAND.md).
# Every case is what Rakudo answers.
use MONKEY-SEE-NO-EVAL;

my $ok = True;
sub check($got, $want, $label) {
    unless $got eqv $want { note "FAIL: $label — {$got.raku} vs {$want.raku}"; $ok = False }
}
sub dies(&code) { my $lived = False; try { code(); $lived = True }; !$lived }

# 1. `...` with Int seeds at or past 2**53 walks exactly (it stepped in doubles:
#    the step deduced as 0 and the seed repeated forever).
check((10**17, 10**17 + 1 ... 10**17 + 5).List, (10**17 .. 10**17 + 5).List, 'a 10**17 progression reaches its end');
check((2**53, 2**53 + 1 ... 2**53 + 3).elems,   4,                          'a 2**53 progression has four elements');
check((2**60, 2**60 + 3 ... 2**60 + 12).List,   (2**60, 2**60 + 3, 2**60 + 6, 2**60 + 9, 2**60 + 12), 'a stepped 2**60 progression');
check((2**53, 2**53 + 1 ... *).head(3).List,    (2**53, 2**53 + 1, 2**53 + 2), 'the lazy twin too');
check((1, 2 ... 5).List,                        (1, 2, 3, 4, 5),            'small progressions unchanged');
check((1, 3 ... 11).List,                       (1, 3, 5, 7, 9, 11),        '…and stepped ones');

# 2. `use Mod :tag` imports that tag only; a plain `is export` is :DEFAULT and
#    is NOT implied (it was imported alongside).
my $dir = $*TMPDIR.add("rakupp-c4b-{$*PID}");
$dir.mkdir;
$dir.add("TagModC4b.rakumod").spurt(q:to/END/);
    unit module TagModC4b;
    sub always is export          { 'always' }
    sub tagged is export(:extra)  { 'tagged' }
    END
check(EVAL("use lib '$dir'; use TagModC4b :extra; tagged()"),          'tagged', 'use Mod :tag imports the tag');
check(dies({ EVAL "use lib '$dir'; use TagModC4b :extra; always()" }), True,     '…and not the default exports');
check(EVAL("use lib '$dir'; use TagModC4b; always()"),                 'always', 'a plain use imports the defaults');
# (a second `use TagModC4b` in the same process still sees `tagged`: imports publish to
#  the global scope here — the known module-`use` leak, not a tag question.)
check(EVAL("use lib '$dir'; use TagModC4b :ALL; always() ~ tagged()"), 'alwaystagged', ':ALL imports everything');

# 3. `-I file#/dir` names a directory (the guard that should skip repo specs
#    never matched one, so the spelling was probed with its prefix attached).
{
    my $p = run $*EXECUTABLE, "-Ifile#$dir", '-e', 'use TagModC4b; print always()', :out, :err;
    check($p.out.slurp(:close), 'always', '-I file#/dir finds the module');
}
unlink $dir.add("TagModC4b.rakumod"); rmdir $dir;

# 4. CALLER::<$x> is the caller's dynamic, not the callee's own.
sub callee     { my $y is dynamic = 'callee'; CALLER::<$y> }
sub caller-sub { my $y is dynamic = 'caller'; callee() }
check(caller-sub(), 'caller', 'CALLER::<$y> reads the caller');
sub own-y { my $y is dynamic = 'mine'; MY::<$y> }
check(own-y(), 'mine', 'MY::<$y> is still our own');

if $ok { say "PASS" } else { say "FAIL"; exit 1 }
