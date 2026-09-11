# Regression: a coercion type on ONE slot of a `my (…)` list declaration.
#
# `my ($before, Int() $linenr) = $line.split(/ ":" <( \d+ /, :v, 2)` is how
# Backtrace::Files reads a line number out of a backtrace line. The list form
# accepted `Int $x` in a slot but not `Int() $x` — Ident-then-LParen read as
# "not a type", and the declaration died "expected variable in declaration
# (got 'Int')". And once parsed, the slot's coercion has to APPLY: list
# assignment stored whatever it was handed.
#
# Every expectation was checked against Rakudo.

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

{
    my ($a, Int() $b) = "3", "42";
    ck($a, "3", 'an untyped slot keeps the Str');
    ck($b, 42,  'an Int() slot coerces the Str to Int');
}
{
    my (Int() $e, $f) = "7", 8;
    ck($e, 7, 'the coercion slot may come first');
    ck($f, 8, 'and the plain slot after it is untouched');
}
{
    my (Int $c, Str $d) = 1, "x";
    ck(($c, $d), (1, "x"), 'plain typed slots still work');
}
{
    my (Str() $s, Int(Str) $n) = 5, "9";
    ck($s, "5", 'Str() coerces an Int');
    ck($n, 9,   'the Int(Str) spelling coerces too');
}
{
    my ($before, Int() $linenr) = 'file.raku:148 in sub foo'.split(/ ":" <( \d+ /, :v, 2);
    ck($before, 'file.raku:', 'the reported shape: text before the number');
    ck($linenr, 148,          'and the number as an Int');
}

say $fails ?? "FAIL ($fails)" !! "PASS";
exit $fails ?? 1 !! 0;
