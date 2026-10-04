# Regression: the array composer's one-arg rule on a Range, in both lanes.
# Under --exe, `[1..3]` was ONE element holding the Range: rtOneArgItem spread a
# lone List but had no case for a Range, so mutsu's bench-index-read.raku —
# `@grid[$_] = [($_ * 8) ..^ ($_ * 8 + 8)] for ^8`, then `@grid[$r][$c]` — read
# Nil and summed to the wrong checksum. And in the interpreter, a `$` variable
# was spread as if it were the list it holds: `my $r = 1..3; [$r]` was three
# elements where Rakudo has one.
#
# Every expectation below was checked against Rakudo 2026.09.

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

ck([1..3].elems, 3, 'a lone Range spreads');
my $n = 2;
ck([$n..4].elems, 3, 'with a variable bound');
ck([($n * 2) ..^ 8].elems, 4, 'with expression bounds, excluding the end');
ck(['a'..'c'].elems, 3, 'a string Range');
ck([1..3,].elems, 1, 'a trailing comma keeps it one element');

my $r = 1..3;
ck([$r].elems, 1, 'a Range held in a $ variable is one item');
ck([$r<>].elems, 3, 'and spreads once taken out of it');
my $l = (1, 2);
ck([$l].elems, 1, 'so is a List held in a $ variable');
ck((1..2).map({ [$_] }).map(*.elems).List, (1, 1), 'and the topic');

my @x = 1, 2;
ck([@x].elems, 2, 'an @ variable still spreads');
ck([(1..3).map(* * 2)].elems, 3, 'and so does a Seq');

# the benchmark's shape
my @grid;
@grid[$_] = [($_ * 8) ..^ ($_ * 8 + 8)] for ^8;
my $g = 0;
loop (my $k = 0; $k < 1000; $k++) { $g = $g + @grid[$k +& 7][($k +> 3) +& 7] }
ck($g, 31440, 'a grid of composed rows reads back every cell');

say $fails ?? "FAIL ($fails)" !! "PASS";
exit $fails ?? 1 !! 0;
