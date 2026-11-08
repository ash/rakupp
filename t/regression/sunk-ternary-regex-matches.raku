# A ternary statement sinks the branch it takes, so a regex literal there
# matches `$_` and sets `$/`, as a bare regex statement does: `$_ = 'abc';
# False ?? /q/ !! /c/;` left `$/` untouched, where Rakudo and `--exe` match
# /c/. A parenthesized inner ternary is a value, and is not sunk.
# Every line answers the same under Rakudo 2026.09.

sub ck($got, $want, $desc) {
    return if $got eqv $want;
    say "FAIL: $desc — got {$got.raku}, want {$want.raku}";
    exit 1;
}

$_ = 'abc';
False ?? /q/ !! /c/;
ck ~$/, 'c', 'the else branch matches';
True ?? /a/ !! /c/;
ck ~$/, 'a', 'the then branch matches';
0 ?? /x/ !! 1 ?? /b/ !! /z/;
ck ~$/, 'b', 'a chained ternary';
1 ?? (0 ?? /x/ !! /c/) !! /z/;
ck ~$/, 'b', 'a parenthesized ternary is a value';
my $n = 0;
True ?? $n++ !! 0;
ck $n, 1, 'another branch runs as before';
my $r = False ?? /q/ !! /c/;
ck $r ~~ Regex, True, 'in value position it is the Regex (as before)';

say 'PASS';
