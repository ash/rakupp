# Regression: currying is SYNTACTIC — only a `*` WRITTEN in an expression
# composes. A Whatever, or an already-curried WhateverCode, that ARRIVES as a
# value (out of a variable, a return, an element) is an ordinary object.
#
# We curried on the VALUE instead, in every engine, so `my $c = * > 100; $c eqv
# True` answered a WhateverCode where Rakudo answers False — and a WhateverCode
# is truthy, so every test written that way passed no matter what it asked.
# evalBinary now says whether a star is written at the site, and native codegen
# emits applyArithValue where its own walk finds none.
#
# Three more from the same sitting, all of them about what COMPOSES:
#
#   * a chained comparison does: `1 < * < 5` is one arity-one closure, the band
#     predicate a `when` or a `.grep` is handed. `--target=js` ran the chain
#     eagerly against the Whatever itself and answered a Bool, so `when 1 < * <
#     5` took the same arm for every topic.
#   * `=:=` does, like every other operator over a written star. It needs the
#     AST (container identity is about SLOTS), so it ran ahead of the curry and
#     answered a Bool.
#   * a metamodel call on a COMPOSED WhateverCode does: `(* < 1).^name` is a
#     WhateverCode, where we answered the Str "WhateverCode".
#
# Every expectation was checked against Rakudo. Must stay natively compilable.

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}
sub isWC($x) { $x ~~ Callable && $x ~~ WhateverCode }

# --- a curry that ARRIVED as a value composes no further -------------------
{
    my $c = * > 100;
    ck(($c eqv True),  False, 'a WhateverCode in a variable is not equivalent to True');
    ck((True eqv $c),  False, '…from either side');
    ck(($c eqv $c),    True,  '…and is equivalent to itself');
    ck(($c ~~ Callable), True, 'it is still a Callable');
    ck($c.^name, 'WhateverCode', 'and .^name on it is a Str, not another curry');
    ck($c(200), True,  'it still computes');
    ck($c(3),   False, '…both ways');
}
{
    my $w = *;
    ck(($w eqv True), False, 'a bare `*` in a variable is not equivalent to True');
    ck(($w ~~ Whatever), True, '…it is just a Whatever');
}

# --- but a star WRITTEN at the site does compose --------------------------
ck(isWC(* eqv 1),  True, 'a written `*` curries `eqv`');
ck(isWC(* =:= 1),  True, '…and `=:=`, which needs the AST and ran ahead of it');
ck(isWC(* > 100),  True, '…and a comparison, as ever');
{
    my $w = *;
    ck(isWC($w eqv *), True, 'a written `*` on either side is enough');
}
ck((* eqv 1)(1), True,  'and the curry computes');
ck((* eqv 1)(2), False, '…both ways');

# --- a chained comparison is one arity-one closure ------------------------
sub band($t) { given $t { when 1 < * < 5 { 'in' }; default { 'out' } } }
ck(band(3), 'in',  'a chained comparison in a `when` tests the topic');
ck(band(9), 'out', '…and falls through outside the band');
ck(band(1), 'out', '…exclusive at the bottom');
ck(isWC(1 < * < 5), True,  'it composes rather than running eagerly');
ck((1 < * < 5)(3),  True,  'and it computes');
ck((1 < * < 5)(9),  False, '…both ways');

# --- each SIDE composes on its own ----------------------------------------
{
    my $both = * > 2 && *.abs > 10;
    ck($both(20),  True,  'a method curry under `&&` is a closure of its own');
    ck($both(3),   False, '…and the `&&` hands back the right side');
    ck($both(-20), True,  '…which is what an .abs band means');
}

# --- a metamodel call on a composed curry composes too --------------------
ck(isWC((* < 1).^name), True, 'a `.^` call on a curried expression composes');
ck((* < 1).WHAT.^name,  'WhateverCode', 'while `.WHAT` answers about it');
ck((*).WHAT.^name,      'Whatever',     '…and about a bare star');

say $fails ?? "FAIL ($fails)" !! "PASS";
exit $fails ?? 1 !! 0;
