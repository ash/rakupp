# Regression: language behaviour from the third batch of audited mutsu gap
# files (2026-10-04).
#
# - `.contains($needle, $pos)` counts $pos in graphemes.
# - The key of `$item => …` is decontainerized.
# - `.nodemap` keeps a Slip it is handed as one element.
# - A native array's `.WHAT` is `array[int]`.
# - A Complex limit for `IO::Path.lines` is its value.
#
# Every expectation below was checked against Rakudo.

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

my $s = "q\x[301]ab";
ck(($s.contains('a', 1), $s.contains('a', 2), $s.contains('A', 2, :i)), (True, False, False),
   'contains counts its position in graphemes');
my $item = $(1, 2);
ck(($item => 'x').raku, '(1, 2) => "x"', 'a Pair key is decontainerized');
ck((1, 2).nodemap({ slip }).raku, '(Empty, Empty)', 'nodemap keeps a Slip as one element');
ck([[2, 3], [[4, 5], 6, 7], 7].nodemap({ .elems == 1 ?? $_ !! slip }).gist, '(() () 7)', '... the documented example');
my int @n = 1, 2, 3;
@n.pop;
ck(@n.WHAT.^name, 'array[int]', "a native array's WHAT");
ck(@n.WHAT === array[int], True, '... is array[int] itself');
my $f = $*TMPDIR.add("rakupp-lines-limit-$*PID.txt");
$f.spurt: "a\nb\nc\nd\ne\n";
ck($f.lines(3+0i).elems, 3, 'a Complex lines limit');
$f.unlink;

say $fails ?? "FAIL ($fails)" !! "PASS";
exit $fails ?? 1 !! 0;
