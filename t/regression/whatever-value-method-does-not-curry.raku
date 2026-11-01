# Regression: a method called on a Whatever VALUE is called on it — only a
# `*` written as the invocant curries (issue #77, FCO's stored-Whatever item).
#
# `my $w = *; $w.grep({ True })` answered a Sub: methodCall curried every
# method on a Whatever value, behind the evaluator that already makes the
# WhateverCode for a written `*.grep(…)`. Rakudo's Whatever is an ordinary
# Any there, so its list methods see the one-element list `(*,)`. Red walks
# an AST whose nodes may hold a `*` this way.

sub ck($got, $want, $desc) {
    return if $got eqv $want;
    say "FAIL: $desc — got {$got.raku}, want {$want.raku}";
    exit 1;
}

my $w = *;
ck $w.grep({ True }).raku, '(*,).Seq', '.grep is over (*,)';
ck $w.map({ 1 }).List, (1,), '.map too';
ck $w.first({ True }).raku, '*', '.first answers the Whatever';
ck $w.list.raku, '(*,)', '.list';
ck $w.elems, 1, '.elems';
ck ($w.gist, $w.Str, $w.raku), ('*', '*', '*'), 'its renderings';
ck $w.ACCEPTS(5), True, 'Whatever.ACCEPTS takes anything';
ck $w.defined, True, 'it is defined';

my @nodes = 1, *, 3;
ck @nodes.map({ .grep(Int).List }).List, ((1,), (), (3,)), 'a * among the elements, through $_';

# a WRITTEN star still curries
ck (1, 2, 3).map(*.succ).List, (2, 3, 4), '*.succ';
ck <ab cd>.map(*.substr(1)).List, <b d>, '*.substr(1)';
my $from = 1;
ck <ab cd>.map(*.substr($from)).List, <b d>, '*.substr($from)';
ck (1, 2).map(*.^name).List, <Int Int>, '*.^name';
ck <a b>.map(*.uc.lc).List, <a b>, 'a chain';
ck ((1, 2), (3, 4)).map(*».succ).List, ((2, 3), (4, 5)), '*».succ';
my &uc = *.uc;
ck uc('x'), 'X', 'a curry kept in a variable';

say 'PASS';
