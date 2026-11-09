# `substr`'s start and length as Rakudo reads them, which the JavaScript
# runtime (src/js-rt/20-str.js) read its own way: it added the length to a
# negative start, so `"abc".substr(-2)` answered "bc" and `"abc".substr(10)`
# hinted "use *--7"; it handed a length callable the whole length, so
# `"abcd".substr(1, *-1)` was "bcd"; it read a negative length as none, `*`
# as 0 and refused Inf. t/js/run.raku runs this program under the
# JavaScript host against the interpreter, which already answered so.
# Every line answers the same under Rakudo 2026.09.

sub ck($got, $want, $desc) {
    return if $got eqv $want;
    say "FAIL: $desc — got {$got.raku}, want {$want.raku}";
    exit 1;
}

sub refusal(&code) {
    code();
    return 'no refusal';
    CATCH { default { return .message } }
}

my $start = 'Start argument to substr out of range. Is: ';
my $hint = ' if you want to index relative to the end';
ck refusal({ "abc".substr(10) }), $start ~ '10, should be in 0..3; use *-10' ~ $hint, 'a start past the end';
ck refusal({ "abc".substr(-2) }), $start ~ '-2, should be in 0..3; use *-2' ~ $hint, 'a negative start';
ck refusal({ "abc".substr(-4) }), $start ~ '-4, should be in 0..3; ', 'one further back, with no hint';
ck refusal({ "abc".substr(*-5) }), $start ~ '-2, should be in 0..3; use *-2' ~ $hint, 'a Whatever start before the beginning';
ck refusal({ "abc".substr(1, -1) }),
    'Number of characters argument to substr out of range. Is: -1, should be in 0..^Inf; use *-1' ~ $hint,
    'a negative length';
ck "abcd".substr(1, *-1), 'bc', 'a length callable is handed what is left';
ck "abcd".substr(1, {2}), 'bc', '…and its answer is the length';
ck "abcd".substr(1, *), 'bcd', 'a Whatever length is the rest';
ck "abcd".substr(2, Inf), 'cd', '…and so is Inf';
ck "abc".substr(*-2), 'bc', 'a Whatever start inside';
ck "abc".substr(3), '', 'a start at the end';
ck "abc".substr(1, 1), 'b', 'a start and a length';

say 'PASS';
