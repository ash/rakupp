# Two places `--target=js` answered differently from the interpreter, which
# is this file's oracle under t/js/run.raku (the interpreter always passed):
#  - a NUL in a string literal was written `\0`, so a digit after it made a
#    legacy octal escape — `"a\x[0]1b".ords` was (97 1 98);
#  - a Num prints as an integer below 1e16, as Rakudo's does and the C++
#    runtime's; the JS runtime's boundary was 1e15, so `say 1e15` was 1e+15.
# Found by the review of src/ (docs/dev/findings/src-review-2026-11-09.md,
# H2 and H3). Every line answers the same under Rakudo 2026.09.

sub ck($got, $want, $desc) {
    return if $got eqv $want;
    say "FAIL: $desc — got {$got.raku}, want {$want.raku}";
    exit 1;
}

ck "a\x[0]1b".ords.List, (97, 0, 49, 98), 'a NUL before a digit';
ck "\x[0]7".chars, 2, '…and before a 7';
ck "\x[0]".ords.List, (0,), 'a NUL alone';
ck "a\x[0]b".ords.List, (97, 0, 98), 'a NUL before a letter';

ck 1e15.Str, '1000000000000000', '1e15';
ck 1.5e15.Str, '1500000000000000', '1.5e15';
ck (-1e15).Str, '-1000000000000000', '-1e15';
ck 999999999999999e0.Str, '999999999999999', 'just under 1e15';
ck 1e16.Str, '1e+16', '1e16 is written with its exponent';
ck 1e14.Str, '100000000000000', '1e14, as before';

say 'PASS';
