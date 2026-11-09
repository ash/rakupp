# `flatmap` is `map` then `.flat`, as lazy as the map, and a Hash a list holds
# bare spreads into its pairs under `.flat`. The JavaScript runtime
# (src/js-rt) had no `flatmap` at all, "No such method 'flatmap' for invocant
# of type 'List'", and its `.flat` kept such a Hash whole; t/js/run.raku runs
# this program under the JavaScript host against the interpreter, which
# already answered so.
# Every line answers the same under Rakudo 2026.09.

sub ck($got, $want, $desc) {
    return if $got eqv $want;
    say "FAIL: $desc — got {$got.raku}, want {$want.raku}";
    exit 1;
}

ck (1, 2).flatmap({ ($_, ($_, ($_,))) }).raku, '(1, 1, 1, 2, 2, 2).Seq', 'a nested list flattens all the way';
ck (1, 2).flatmap({ [$_, $_] }).raku, '(1, 1, 2, 2).Seq', 'an Array spreads';
ck (1, 2).flatmap({ 1..$_ }).raku, '(1, 1, 2).Seq', 'a Range';
ck (1, 2).flatmap({ |($_, $_) }).raku, '(1, 1, 2, 2).Seq', 'a Slip';
ck (1, 2).flatmap({ Empty }).raku, '().Seq', 'Empty';
ck (1, 2).flatmap({ $_ => 1 }).raku, '(1 => 1, 2 => 1).Seq', 'a Pair';
ck (1, 2).flatmap({ %(a => $_) }).raku, '(:a(1), :a(2)).Seq', 'a Hash spreads into its pairs';
ck (1..3).flatmap({ $_ xx 2 }).raku, '(1, 1, 2, 2, 3, 3).Seq', 'over a Range';
ck "ab".flatmap({ .comb }).raku, '("a", "b").Seq', 'over a Str';
ck %(a => 1).flatmap({ .key, .value }).raku, '("a", 1).Seq', 'over a Hash';
ck ({ 5 }).flatmap({ $_() }).raku, '(5,).Seq', 'over a Code';
ck (1, 2).flatmap(* + 1).raku, '(2, 3).Seq', 'a WhateverCode';
ck (1, 2).flatmap({ gather { take $_; take $_ * 10 } }).List, (1, 10, 2, 20), 'a gather';
ck (1, 2).flatmap({ gather { take $_ for 1..* } })[^3].List, (1, 2, 3), 'an endless gather is read only as far as asked';
ck (1..*).flatmap({ $_, $_ })[^5].List, (1, 1, 2, 2, 3), 'over an endless Range';
ck (%(a => 1), 2).flat.raku, '(:a(1), 2).Seq', '.flat spreads a bare Hash';
ck [%(a => 1), 2].flat.raku, '(${:a(1)}, 2).Seq', '…but not an Array\'s';

say 'PASS';
