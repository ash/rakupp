# Regression: a `for` compiled with --exe pulls a lazy source as it goes, as
# the interpreter's `for` does. The generated loop walked only the elements
# already in the buffer, so a source nothing had pulled yet ran NO iterations:
# `for (1..*).map(* * 2) { …; last if … }` printed nothing, and so did a loop
# over `lazy 1..3`. (A plain gather was spared only because it fills a probe
# of its first elements when it is made.) t/exe/run.raku compiles this file
# and compares it with the interpreter.
#
# Every expectation below was checked against Rakudo 2026.09.

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

my $s = '';
for (1..*).map(* * 2) { $s ~= $_; last if $_ > 4 }
ck($s, '246', 'a loop over an endless map runs until last');

$s = '';
my @e = lazy 1..3;
for @e { $s ~= $_ }
ck($s, '123', 'a loop over a lazy Array sees every element');
ck(@e.is-lazy, False, 'and reads it out');

$s = '';
for (lazy 1..4) -> $a, $b { $s ~= "$a$b|" }
ck($s, '12|34|', 'two at a time');

$s = '';
for (lazy (1, 2), (3, 4)) -> ($a, $b) { $s ~= $a + $b }
ck($s, '37', 'destructuring');

$s = '';
for gather { take $_ for 1..100 } { $s ~= $_ if $_ %% 25 }
ck($s, '255075100', 'a gather longer than its probe');

$s = '';
for 1..3 { $s ~= $_ }
ck($s, '123', 'and an ordinary range');

say $fails ?? "FAIL ($fails)" !! "PASS";
exit $fails ?? 1 !! 0;
