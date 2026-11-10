# A string's `:N<…>` radix that no int holds is no number: the radix was
# read with atoi, which wrapped, so `+":4294967298<11>"` was read in base 2
# and answered 3, where Rakudo has a Failure ("Cannot convert radix of
# 4294967298 (max 36)"). Found by the review of src/
# (docs/dev/findings/src-review-2026-11-09.md, B2). Every line answers the
# same under Rakudo 2026.09.

sub ck($got, $want, $desc) {
    return if $got eqv $want;
    say "FAIL: $desc — got {$got.raku}, want {$want.raku}";
    exit 1;
}

ck (+":4294967298<11>").^name, 'Failure', 'a radix that wraps to 2';
ck (+":4294967312<ff>").^name, 'Failure', '…and one that wraps to 16';
ck (+":37<1>").^name, 'Failure', 'a radix past 36, as before';
ck +":16<ff>", 255, 'base 16';
ck +":2<101>", 5, 'base 2';
ck +":36<z>", 35, 'base 36';

say 'PASS';
