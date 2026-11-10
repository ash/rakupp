# `%%` with a width pads its percent sign, as any directive pads what it
# prints: `sprintf("%5%|%-5%|")` was "%|%|", where Rakudo has "    %|%    |".
# Found by the review of src/ (docs/dev/findings/src-review-2026-11-09.md,
# E8). Every line answers the same under Rakudo 2026.09.

sub ck($got, $want, $desc) {
    return if $got eqv $want;
    say "FAIL: $desc — got {$got.raku}, want {$want.raku}";
    exit 1;
}

ck sprintf("%5%|"), '    %|', 'a width';
ck sprintf("%-5%|"), '%    |', 'left-justified';
ck sprintf("%05%|"), '0000%|', 'zero-padded';
ck sprintf("%1%|"), '%|', 'a width of 1';
ck sprintf("100%%"), '100%', 'none, as before';
ck sprintf("%%|%5s|", "a"), '%|    a|', '…beside another directive';

say 'PASS';
