# Regression: `.minmax` over Rats or Nums answered a List of the two ends,
# not a Range, so `.minmax.bounds` died ("No such method 'bounds' for
# invocant of type 'List'"). Only an all-Int or all-Str list got a Range.
# webraku's examples/wavelet/Analysis.rakumod calls `.minmax.bounds`.
# A lone Num or Rat answered an Int Range (2.5.minmax was 2..2).
#
# Contract: exit 0 + last line PASS. Runs under Rakudo unchanged; every
# expectation was checked against Rakudo 2026.08 on 2026-10-19.
my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

ck((3e0, 1e0).minmax.raku, '1e0..3e0', 'Nums make a Range');
ck((1, 2.5).minmax.raku, '1..2.5', 'an Int and a Rat make a Range');
ck((1, 1/3).minmax.raku, '<1/3>..1', 'a Rat end keeps its type');
ck((3e0, 1e0).minmax.bounds, (1e0, 3e0), '.bounds works on it');
ck(minmax(3e0, 1e0).raku, '1e0..3e0', 'the sub form too');
ck((3e0, 1e0).minmax ~~ Range, True, 'it is a Range');
ck(2.5.minmax.raku, '2.5..2.5', 'a lone Rat spans itself');
ck(1e0.minmax.raku, '1e0..1e0', 'a lone Num spans itself');
ck((1, 2, 3).minmax.raku, '1..3', 'Ints still make an Int Range');
ck(<b a c>.minmax.raku, '"a".."c"', 'Strs still make a Str Range');
ck(().minmax.raku, 'Inf..-Inf', 'the empty minmax is unchanged');
ck((3, 1).minmax(-*).raku, '3..1', 'a mapper still orders the ends');
ck((2.5, -1e0, 7).minmax(*.abs).raku, '-1e0..7', 'and they keep their own types');

say $fails ?? "FAIL ($fails)" !! "PASS";
exit $fails ?? 1 !! 0;
