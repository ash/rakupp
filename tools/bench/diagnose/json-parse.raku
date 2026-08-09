# Times JSON::Fast's from-json over one or more documents, best-of-N.
#
# Best-of, not mean: a stray scheduler hiccup can only make a run look SLOWER,
# so the minimum is the closest thing to the machine's real capability, and it
# is the number that does not drift when something else starts up mid-run.
#
# Give it the whole size ladder rather than one file. The improvement factor on
# a single document tells you almost nothing; the SCALING COLUMN tells you
# whether a fix is real — x2 per doubling is linear and fine, x4 is the
# per-operation-O(length) bug documented in
# docs/dev/findings/STRING-SCAN-QUADRATICS.md.
#
#     L=…/JSON--Fast-0.19/lib
#     rakupp -I$L json-parse.raku --reps=3 d200.json d400.json d800.json d1600.json
#     raku   -I$L json-parse.raku --reps=5 d200.json d400.json d800.json d1600.json
#
# Runs unchanged under both engines — the comparison IS the measurement.
use JSON::Fast;
sub MAIN(*@files, Int :$reps = 1) {
    for @files -> $f {
        my $text = $f.IO.slurp;
        my @t;
        for ^$reps {
            my $t0 = now;
            my $d = from-json($text);
            @t.push(((now - $t0) * 1000).Int);
            die "bad parse of $f" unless $d.elems;   # never time a no-op
        }
        say "$f\t{ $text.chars } chars\t{ @t.min } ms  (runs: { @t.join(',') })";
    }
}
