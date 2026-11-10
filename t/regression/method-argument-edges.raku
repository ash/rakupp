# Three built-in methods at the edges of their arguments, each found by the
# review of src/ (docs/dev/findings/src-review-2026-11-09.md):
#  - D3: `.log(0)` and `.log(1)` divide by the base's logarithm. A base of 0
#    was read as "no base given", so `5.log(0)` was ln 5 where Rakudo has -0,
#    and `5.log(1)` was Inf where Rakudo refuses the division.
#  - D4: `.words(-1)` and `.lines(-1)` are empty. -1 was the "no limit"
#    mark, so a negative limit returned everything.
#  - D1: `.substr($from, $len)` with a length near the int64 maximum takes
#    the rest of the string. `$from + $len` wrapped, and it came back empty.
#    (Rakudo 2026.09 dies there, on the same overflow: "Substring end
#    (-9223372036854775808) cannot be less than 0".)
# Every other line answers the same under Rakudo 2026.09.

sub ck($got, $want, $desc) {
    return if $got eqv $want;
    say "FAIL: $desc — got {$got.raku}, want {$want.raku}";
    exit 1;
}

sub refusal(&code) {
    code();
    return 'no refusal';
    CATCH { default { return .message.lines[0] } }
}

ck 5.log(0).raku, '-0e0', '5.log(0)';
ck 0.log(0).raku, 'NaN', '0.log(0)';
ck refusal({ 5.log(1) }), 'Attempt to divide 1.6094379124341003 by zero using /', '5.log(1)';
ck 8.log(2), 3e0, 'a base of 2, as before';
ck 100.log(10), 2e0, '…and of 10';
ck 5.log, 1.6094379124341003e0, '…and none';

ck "a b c".words(-1).elems, 0, '.words(-1)';
ck "a\nb".lines(-1).elems, 0, '.lines(-1)';
ck "a b c".words(2).List, ("a", "b"), '.words(2), as before';
ck "a\nb\nc".lines(2).List, ("a", "b"), '.lines(2)';
ck "a b".words(*).elems, 2, '.words(*)';
ck "a\nb".lines(0).elems, 0, '.lines(0)';

# D1 is checked only where rakupp and Rakudo agree, and by rakupp's own answer
ck "abc".substr(1, 1), 'b', '.substr(1, 1)';
ck "abc".substr(3, 5), '', '.substr at the end';
if $*RAKU.compiler.name eq 'Raku++' {
    ck "abc".substr(1, 9223372036854775807), 'bc', '.substr with a length near the int64 maximum';
}

say 'PASS';
