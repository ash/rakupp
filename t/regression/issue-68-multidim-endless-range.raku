# Issue #68: `@a[0..*; 0]` answered the four column values and then 9,996
# trailing (Any)s.
#
#     my @a = [["a", 1, 2], ["b", 1, 2], ["c", 1, 2], ["d", 1, 2]];
#     say @a[0..*; 0];    # (a b c d (Any) (Any) (Any) …)
#
# A multidim subscript evaluates each dimension to a VALUE and flattened it;
# Value::flatten() answers a 10,000-element safety prefix for an endless range,
# so every index past the array's end became a missing element. The single-dim
# `@a[0..*]` resolves `*` against the array syntactically and never saw it.
# Now an endless (or `lazy`) Range dimension stops at the last index of the
# level it selects from — in the plain read, the adverbed forms (:exists :kv :p
# :k :v :delete), multidim slice assignment, and a Range VALUE in a single
# subscript (`my $r = 0..*; @a[$r]`, `@a[0..*]:exists`, `@a[0..*] = …`). A
# finite overrun (`@a[0..10]`) still pads with (Any), as Rakudo's does.
#
# Two neighbours fixed on the way: `*-2..*` curried its bare `*` to 0, so
# `@a[*-2..*; 0]` was empty; and a multidim target was parsed as an ITEM
# assignment, so `@a[0..*; 1] = 7, 8, 9, 10` stored the 7 and sank the rest.
#
# Contract: exit 0 + last line PASS. Every expectation is Rakudo 2026.07's under
# `use v6.e.PREVIEW` (6.d hangs on most of these forms) except where noted:
# Rakudo hangs on `@a[*;0..*]`, `@a[0..*;0..*]` and `@a[*-2..*;0]`, and dies
# with "Cannot convert Inf to Int" on a Range held in a `$` variable; rakupp
# answers the truncated slice in all of those.
my @fail;
sub check($got, $want, $desc) {
    @fail.push("$desc: got {$got.raku}, want {$want.raku}") unless $got eqv $want;
}
sub rows { [["a", 1, 2], ["b", 1, 2], ["c", 1, 2], ["d", 1, 2]] }
my @a = rows;

# the reproducer, and the endless spellings
check @a[0..*;0], <a b c d>, 'issue #68 line';
# On a binary WITHOUT the fix every endless dimension is a 10,000-index walk and
# the two-level forms below multiply that to 10^8: stop here rather than spend
# minutes proving the same thing.
if @fail {
    note "FAILED (the reproducer itself; the rest is skipped):\n" ~ @fail.map({ "  - $_" }).join("\n");
    exit 1;
}
check @a[0..*;1], (1, 1, 1, 1), 'the second column';
check @a[1..*;2], (2, 2, 2), 'from the second row';
check @a[0..Inf;0], <a b c d>, '0..Inf';
check @a[^Inf;0], <a b c d>, '^Inf';
check @a[0..^*;0], <a b c d>, '0..^*';
check @a[0..*;0].elems, 4, 'elems';
check @a[2..*;0].elems, 2, 'elems from the third row';
check @a[5..*;0], (), 'a range that starts past the end';
check @a[0..*;5], (Any, Any, Any, Any), 'a finite miss on the inner level still defaults';
check @a[0..10;0], ('a', 'b', 'c', 'd', |(Any xx 7)), 'a finite overrun keeps one (Any) per missing slot';
check @a[0..*;*], ('a', 1, 2, 'b', 1, 2, 'c', 1, 2, 'd', 1, 2), 'endless outer level under a star';
check @a[*;0..*], ('a', 1, 2, 'b', 1, 2, 'c', 1, 2, 'd', 1, 2), 'endless inner level under a star';
check @a[1..*;1..*], (1, 2, 1, 2, 1, 2), 'both levels endless';
check @a[0..Inf;0..Inf].elems, 12, 'both levels endless: 12 leaves, not 10^8';
check @a[1..*;*-1], (2, 2, 2), 'endless outer, *-1 inner';
check @a[lazy 0..*;0], <a b c d>, 'a lazy endless range';
check @a[lazy 0..10;0], <a b c d>, 'a lazy finite range stops at the end without defaulting';
check @a[(0, 2 ... *);0], <a c>, 'a lazy list dimension';
check @a[0..*][0..*;0], <a b c d>, 'a List base';
check @a[*-2..*;0], <c d>, '*-2..* keeps its open end';
check (*-2..*)(4).gist, '2..Inf', 'the curried range itself';
check (*-2..*-1)(4, 4).gist, '2..3', 'a WhateverCode on both sides takes one argument per star';
check @a[*-2..*-1;0], <c d>, '…and a subscript feeds it the length once per star (POSITIONS)';

# the adverbed forms share the fix
check @a[0..*;0]:v, <a b c d>, ':v';
check @a[0..*;0]:exists, (True, True, True, True), ':exists';
check @a[0..*;0]:k, ((0, 0), (1, 0), (2, 0), (3, 0)), ':k';
check @a[0..*;0]:kv, ((0, 0), 'a', (1, 0), 'b', (2, 0), 'c', (3, 0), 'd'), ':kv';
check (@a[0..*;0]:p).gist, '((0 0) => a (1 0) => b (2 0) => c (3 0) => d)', ':p';

# multidim slice assignment
{
    my @b = rows;
    @b[0..*;0] = <w x y z>;
    check @b, [['w', 1, 2], ['x', 1, 2], ['y', 1, 2], ['z', 1, 2]], 'assignment fills the existing rows only';
}
{
    my @b = rows;
    @b[0..*;1] = 7, 8, 9, 10;
    check @b, [['a', 7, 2], ['b', 8, 2], ['c', 9, 2], ['d', 10, 2]], 'a comma list distributes across the slice';
}
{
    my @b = rows;
    @b[1..*;*] = 7, 8, 9, 10;
    check @b, [['a', 1, 2], [7, 8, 9], [10, Any, Any], [Any, Any, Any]], 'distributes row-major, the rest defaulting';
}
{
    my @b = rows;
    @b[0;1] = 7, 8;
    check @b[0;1], (7, 8), 'a scalar multidim target takes the whole list';
    check @b[0;0], 'a', '…and touches nothing else';
}
{
    my @b = rows;
    @b[0;*] = 7, 8, 9;
    check @b[0], [7, 8, 9], 'a star dimension distributes';
}
{
    my @b = rows;
    @b[0..*;0]:delete;
    check @b, [[Any, 1, 2], [Any, 1, 2], [Any, 1, 2], [Any, 1, 2]], ':delete over an endless dimension';
}

# a Range VALUE in a single subscript (Rakudo dies on the `$r` forms)
my @n = 10, 20, 30;
my $r = 0..*;
check @n[$r], (10, 20, 30), 'an endless Range value';
check @n[$r].elems, 3, '…and its elems';
check @n[0..*]:exists, (True, True, True), ':exists over an endless range';
check @n[0..*]:kv, (0, 10, 1, 20, 2, 30), ':kv over an endless range';
check @n[0..5], (10, 20, 30, Any, Any, Any), 'a finite overrun on a single subscript still defaults';
{
    my @m = 10, 20, 30;
    @m[0..*] = 1, 2, 3, 4;
    check @m, [1, 2, 3], '`@m[0..*] = …` fills the existing slots and does not grow';
}
{
    my @m = 10, 20, 30;
    @m[0..4] = 1, 2, 3, 4;
    check @m, [1, 2, 3, 4, Any], 'a finite range still extends the array';
}
check (do { my $x; $x[0..*] }), (Any,), 'an undefined scalar is a one-item list';
check (do { my $x; $x[0..2] }), (Any, Any, Any), '…that a finite range still pads';

if @fail {
    note "FAILED:\n" ~ @fail.map({ "  - $_" }).join("\n");
    exit 1;
}
say "PASS";
