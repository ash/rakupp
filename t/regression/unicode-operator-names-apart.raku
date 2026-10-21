# Regression: `×` `÷` `−` `≤` `≥` `≠` are operators of their own names.
#
# Raku's `&infix:<×>` can be bound, shadowed and given candidates apart from
# `&infix:<*>`; only the BUILT-IN behind the two names is the same. The lexer
# reads `×` as `*` (and `×=` as `*=`), keeping the written spelling only on
# the token, so the evaluator saw one operator. A "twin" lookup (a `*` node
# also asked for `&infix:<×>`) made a `×` shadow work and broke `*`:
#
#   · `my &infix:<×> = …` captured every `*` in its scope (`6 * 7` was "t");
#   · `my &infix:<×> = -> $a, $b { $a * $b }` called itself until the stack
#     ran out, the body's `*` being the shadow again;
#   · `$x ×= 3` never asked for the shadow, nor did `@n[0] ×= 5`;
#   · a declared `multi infix:<×>(V, V)` was never consulted: `V × V` and
#     `$v ×= V` ran the built-in `*` over the objects' .Numeric;
#   · a `my &infix:<*>` captured `×` too, and `[*]` `Z*` `»*»` found a `×` one.
#
# The parser now marks a node written so (Binary::uniSpelled,
# Assign::uniSpelled); user operators are looked up under the written name
# and the built-in runs under the ASCII one. A metaop carries the written
# name in its op string (`[×]`, `Z×`, `>>×>>`). `@a »×=» 3`, which died with
# "Unsupported operator '×'", works as a side effect.
#
# Runs clean under Rakudo 2026.09.

my $fails = 0;
sub ck(Mu $got, Mu $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

class V { has $.x; method Numeric { $!x } }

# ---- a shadow of one name leaves the other alone ------------------------
{
    my &infix:<×> = -> $a, $b { "t" };
    ck 6 * 7, 42, 'my &infix:<×> leaves `*` the built-in';
    ck 2 × 3, 't', '…and shadows `×`';
    my $x = 2; $x ×= 3;
    ck $x, 't', '`$x ×= 3` applies the `×` shadow';
    my $y = 2; $y *= 3;
    ck $y, 6, '…and `$y *= 3` does not';
    my @n = 1, 2; @n[0] *= 5;
    ck @n, [5, 2], '`@n[0] *= 5` is the built-in';
    ck &infix:<*>(2, 3), 6, '`&infix:<*>(2, 3)` is the built-in';
    ck [*](1, 2, 3), 6, '`[*]` is the built-in';
    ck ((1, 2) Z* (3, 4)).List, (3, 8), '`Z*` is the built-in';
    ck ((1, 2) »*» (3, 4)), (3, 8), '`»*»` is the built-in';
    ck 2 R* 3, 6, '`R*` is the built-in';
    my &g = * * 2;
    ck g(5), 10, '`* * 2` curries the built-in';
}
{
    my &infix:<×> = -> $a, $b { $a * $b };
    ck (try 2 × 3), 6, 'a `×` shadow whose body says `*` means the built-in (no recursion)';
}
{
    my &infix:<*> = -> $a, $b { "star" };
    ck 2 × 3, 6, 'my &infix:<*> leaves `×` the built-in';
    ck 2 * 3, 'star', '…and shadows `*`';
    my $x = 2; $x ×= 3;
    ck $x, 6, '`$x ×= 3` is the built-in';
    my @n = 1, 2; @n[0] ×= 5;
    ck @n, [5, 2], '`@n[0] ×= 5` is the built-in';
    ck [×](2, 3), 6, '`[×]` is the built-in';
    ck 2 R× 3, 6, '`R×` is the built-in';
    ck ((2,) Z× (3,)).List, (6,), '`Z×` is the built-in';
    my &f = * × 2;
    ck f(5), 10, '`* × 2` curries the built-in';
}

# ---- the metaops find a shadow by the name written ----------------------
{
    my &infix:<×> = -> $a, $b { "t" };
    ck [×](1, 2, 3), 't', '`[×]` reduces with the `×` shadow';
    ck ((1, 2) Z× (3, 4)).List, <t t>, '`Z×` zips with it';
    ck ((1, 2) »×» (3, 4)), <t t>, '`»×»` hypers with it';
    ck 2 R× 3, 't', '`R×` reverses it';
    ck ((1, 2) X× (3, 4)).List, <t t t t>, '`X×` crosses with it';
    ck [\×](1, 2, 3).List, (1, 't', 't'), '`[\×]` scans with it';
    my @c = 1, 2; @c »×=» 3;
    ck @c, [<t t>], '`»×=»` assigns through it';
    my &f = * × 2;
    ck f(5), 't', '`* × 2` curries it';
    ck (* × *)(2, 3), 't', '`* × *` curries it';
}
{
    my &infix:<≤> = -> $a, $b { "le" };
    ck [≤](1, 2, 3), 'le', '`[≤]` folds with a `≤` shadow (it chains no more)';
    ck [<=](1, 2, 3), True, '…and `[<=]` still chains the built-in';
    ck ((1, 2) »≤« (2, 1)), <le le>, '`»≤«` hypers with the shadow';
    ck ((1, 2) »<=« (2, 1)), (True, False), '…and `»<=«` is the built-in';
}

# ---- every name in the family -------------------------------------------
{
    sub infix:<−>($a, $b) { "minus" }
    ck (5 − 3, 5 - 3), ('minus', 2), 'sub infix:<−> shadows `−`, not `-`';
}
{
    sub infix:<≤>($a, $b) { "le" }
    ck (5 ≤ 3, 5 <= 3), ('le', False), 'sub infix:<≤> shadows `≤`, not `<=`';
}
{
    my &infix:<÷> = -> $a, $b { "div" };
    my $q = 6; $q ÷= 2;
    my $r = 6; $r /= 2;
    ck (6 ÷ 3, 6 / 3 == 2, $q, $r == 3), ('div', True, 'div', True), 'my &infix:<÷> shadows `÷` and `÷=`, not `/` and `/=`';
}
{
    my &infix:<≠> = -> $a, $b { "ne" };
    ck (1 ≠ 2, 1 != 2), ('ne', True), 'my &infix:<≠> shadows `≠`, not `!=`';
}
{
    my &infix:<≥> = -> $a, $b { "ge" };
    ck (1 ≥ 2, 1 >= 2), ('ge', False), 'my &infix:<≥> shadows `≥`, not `>=`';
}
{
    my &infix:<−> = -> $a, $b { "m" };
    my $x = 5; $x −= 1;
    my $y = 5; $y -= 1;
    ck ($x, $y), ('m', 4), '`−=` applies a `−` shadow, `-=` does not';
}
{
    my &infix:<-> = -> $a, $b { "m" };
    my $x = 5; $x −= 1;
    ck ($x, 5 − 1), (4, 4), 'a `-` shadow leaves `−` and `−=` the built-in';
}
{
    sub infix:<×>($a, $b) { $a * $b + 1 }
    ck (2 × 3, 2 * 3, [×](1, 2, 3)), (7, 6, 10), 'sub infix:<×> answers `×` and `[×]`; its body and `*` are the built-in';
}

# ---- a declared multi of the name is consulted --------------------------
{
    multi sub infix:<×>(V $a, V $b) { "cross" }
    ck V.new(x => 2) × V.new(x => 3), 'cross', 'multi infix:<×>(V, V) takes `V × V`';
    my $v = V.new(x => 2); $v ×= V.new(x => 5);
    ck $v, 'cross', '…and `$v ×= V`';
    ck (2 × 3, V.new(x => 2) * V.new(x => 3)), (6, 6), '…and leaves `2 × 3` and `V * V` the built-in';
    ck (&infix:<×>(2, 3), &infix:<×>(V.new(x => 1), V.new(x => 1))), (6, 'cross'),
       '`&infix:<×>` is the multi, extending the built-in';
    ck [×](V.new(x => 2), V.new(x => 3)), 'cross', '`[×]` over V objects meets the multi';
    ck ((V.new(x => 1),) »×» (V.new(x => 1),)), ('cross',), '`»×»` over V objects meets it';
    ck ((1, 2) »×» (3, 4), [×](1, 2, 3)), ((3, 8), 6), '…and over numbers the built-in';
    {
        multi sub infix:<*>(V $a, V $b) { "star" }
        ck (V.new(x => 2) × V.new(x => 3), V.new(x => 2) * V.new(x => 3)), ('cross', 'star'),
           'a multi infix:<*> beside it takes `*` only';
    }
}
{
    multi sub infix:<*>(V $a, V $b) { "star" }
    ck V.new(x => 2) × V.new(x => 3), 6, 'a multi infix:<*> alone leaves `V × V` the built-in';
}
{
    multi sub infix:<≤>(V $a, V $b) { "le" }
    ck ([≤](1, 2, 3), [≤](3, 2, 1)), (True, False), 'a multi infix:<≤> leaves `[≤]` chaining';
}

# ---- the hyper compound form of a Unicode name --------------------------
my @a = 1, 2; @a »×=» 3;
ck @a, [3, 6], '`@a »×=» 3` multiplies in place';
my @b = 1, 2; @b »×=« (3, 4);
ck @b, [3, 8], '`@b »×=« (3, 4)` too';

say $fails ?? "FAIL ($fails)" !! 'PASS';
