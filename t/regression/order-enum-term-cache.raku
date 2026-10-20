# Regression: the Order path's caches must not change what an Order value is,
# what `cmp` answers, or what a shadowed `Less` means.
#
# Three caches made `($a cmp $b) == Less` cheap (BinaryHeap 2.0.3's comparators
# are `* cmp * == More`, and a sort or heap asks this per compare):
#
#   · a bare `Less` (any CORE enum member) answers the member kept on its
#     NameTerm, skipping the scope walk, while g_coreEnumNameBound is clear —
#     armed by every way a name enters a scope (EnvVars' inserts,
#     PadLayout::add) when the name is one coreEnumValue answers to;
#   · `cmp` / `<=>` on two plain Ints answer in applyArith's hot path;
#   · an enum or object operand asks its scope for a user `&infix:<op>` only
#     when g_boundInfixMask says such a spelling was bound somewhere, and two
#     plain scalars skip evalBinary's general-path walk.
#
# A cache that missed an arming would answer Order::Less where a `my \Less`,
# a parameter, a lexical constant or a user enum should win; a fast path that
# took the wrong operands would lose an allomorph's string order or a user
# operator. The shadows below are made by EVAL, at run time, AFTER the caches
# have been filled, so the arming happens mid-program.
#
# Runs clean under Rakudo too.

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

# ---- before anything is shadowed: the members and their renderings --------
sub mention-less { Less }
ck mention-less().raku, 'Order::Less', 'a routine mentioning Less (fills its cache)';
ck (Less.raku, Same.raku, More.raku), ('Order::Less', 'Order::Same', 'Order::More'), '.raku';
ck (Less.WHAT.raku, More.^name, Same.gist, Less.Str), ('Order', 'Order', 'Same', 'Less'), '.WHAT / .^name / .gist / .Str';
ck (Less.Int, Same.Int, More.Int, +Less), (-1, 0, 1, -1), 'the values';
ck (Order::Less === Less, Order::More.raku), (True, 'Order::More'), 'the qualified spelling';

# ---- cmp and <=> on Ints: the fast path's operands, and its neighbours -----
ck ((3 cmp 5), (5 cmp 3), (4 cmp 4)), (Less, More, Same), 'Int cmp Int';
ck ((5 <=> 3), (-7 <=> -2), (0 <=> 0)), (More, Less, Same), 'Int <=> Int';
ck ((3 cmp 5).WHAT.raku, (3 cmp 5).raku), ('Order', 'Order::Less'), 'cmp answers an Order';
ck ((2**70 cmp 3), (-1 cmp 2**70), (2**70 <=> 2**70)), (More, Less, Same), 'big Ints';
{
    my int $ni = 5; my uint8 $nu = 200;
    ck (($ni cmp $nu), ($nu <=> $ni)), (Less, More), 'natives';
}
ck (IntStr.new(3, "3") cmp IntStr.new(3, "03")), More, 'allomorphs of one value order by their strings';
ck ((Less cmp More), (More <=> Less), (Same cmp Same)), (Less, More, Same), 'Order cmp Order';
{
    enum Color <Red Green Blue>;
    ck ((Red cmp Blue), (Blue <=> Red), Red == 0, Blue == 2), (Less, More, True, True), 'a user enum';
}

# ---- == and friends on Order values ---------------------------------------
{
    my ($x, $y) = 3, 5;
    my @a = ^10;
    my $o = $x cmp $y;
    ck (($x cmp $y) == Less, ($y cmp $x) == More, ($x cmp $x) == Same), (True, True, True), '(… cmp …) == member';
    ck ((@a[3] cmp @a[5]) == Less, (@a[5] cmp @a[3]) == Less), (True, False), 'element operands';
    ck ($o == Less, Less == $o, $o != More, $o == Same), (True, True, True, False), 'a stored Order';
    ck (Less == -1, -1 == Less, More != 1, Same == 0), (True, True, False, True), 'against plain Ints';
    ck (Less === Less, Less === More, Less eqv Less, $o === Less), (True, False, True, True), '=== and eqv';
    ck ((Less < More), (More > Same), (Less <= Less)), (True, True, True), 'ordered comparisons';
    my &more = * cmp * == More;
    ck (more(3, 2), more(2, 3)), (True, False), 'a WhateverCode comparator';
    ck (5, 3, 9, 1).sort({ $^a cmp $^b }).List, (1, 3, 5, 9), 'a cmp sort';
    ck (5, 3, 9, 1).sort(-> $p, $q { ($q cmp $p) == Less ?? Less !! ($q cmp $p) == More ?? More !! Same }).List,
       (9, 5, 3, 1), 'a comparator built from == Less';
}

# ---- user operators an enum or object operand must still find -------------
ck EVAL(q:to/END/), ('user', True, True), 'a user multi infix:<==> over a user enum';
    enum Hue <Cyan Magenta>;
    multi infix:<==>(Hue $a, Hue $b) { 'user' }
    (Cyan == Magenta, 1 == 1, Less == Less)
    END
ck EVAL(q:to/END/), (True, False), 'a user multi infix:<==> over objects';
    class P { has $.v }
    multi infix:<==>(P $a, P $b) { $a.v == $b.v }
    (P.new(v => 1) == P.new(v => 1), P.new(v => 1) == P.new(v => 2))
    END

# ---- shadows made at run time, after the caches were filled ---------------
ck EVAL('my \Same = "s"; Same'), 's', 'a sigilless my \Same';
ck Same.raku, 'Order::Same', '…which stays inside its EVAL';
ck EVAL('sub g(\Less) { Less }; g(7)'), 7, 'a parameter named Less';
ck EVAL('my constant Less = 42; Less'), 42, 'a lexical constant Less';
ck EVAL('enum Weird <More Fewer>; (More.raku, More.^name)'), ('Weird::More', 'Weird'), 'a user enum member More';
ck EVAL('sub h { my \More = "m"; More }; h()'), 'm', 'a sigilless shadow inside a routine';
ck (Less.raku, Same.raku, More.raku), ('Order::Less', 'Order::Same', 'Order::More'), 'the members, after all that';
ck mention-less().raku, 'Order::Less', 'the routine whose cache was filled first';
ck ((3 cmp 5) == Less, (@(1, 2)[0] cmp 2) == Less), (True, True), '== Less, after all that';

die "$fails failure(s)" if $fails;
say 'PASS';
