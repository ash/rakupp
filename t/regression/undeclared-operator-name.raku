# BROKE: an operator nobody declared, called by its name, did not fail.
# `infix:<nosuch>(42)` printed 42 (the one-argument form answered its argument
# whatever the operator), `&infix:<nosuch>` was a working Callable, and
# `infix:<nosuch>(1, 2)` died at run time with "Unsupported operator", which
# names an operator we have not written rather than one nobody declared.
# Rakudo refuses all of them at compile time: "Undeclared routine:
# infix:<nosuch>". So a typo in an operator's name was a silent pass here —
# FatRoast found it on 2026-10-19 by fattening `infix:<before>` into Cyrillic.
# `&prefix:<nosuch>` and `&postfix:<nosuch>` read as (Any) the same way.
# FIXED: 2026-10-19. operatorNameDeclared (InterpreterOperators.cpp) asks
# whether CORE, the scope, or a metaoperator over either declares the name;
# the call form, the `&` form and the pre-run undeclared-call check refuse one
# that nothing declares as X::Undeclared::Symbols, "Undefined routine
# 'infix:<nosuch>'" — the same error `nope()` gets. Real operators, one-argument
# forms, user operators and metaoperator spellings must keep working.
#
# Contract: exit 0 + last line PASS.

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; note "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}
sub run-code(Str $code) {
    my $p = run($*EXECUTABLE, '-e', $code, :out, :err);
    my $out = $p.out.slurp(:close);
    my $err = $p.err.slurp(:close);
    ($p.exitcode, $out, $err)
}
sub refused(Str $code, Str $name, Str $stdout, Str $desc) {
    my ($exit, $out, $err) = run-code($code);
    ck($exit != 0 && $err.contains("Undefined routine '$name'") && $out eq $stdout, True, $desc);
}

# --- the four spellings ----------------------------------------------------
# A call is refused before anything runs, as `nope()` is; the `&` form when it
# is reached, as `&nope` is (the pre-run check walks calls, not `&` names).
refused('say 0; say infix:<nosuch>(42);',    'infix:<nosuch>',  '',    'one-argument infix call is refused');
refused('say 0; say &infix:<nosuch>;',       'infix:<nosuch>',  "0\n", '`&infix:<…>` of nothing is refused');
refused('say 0; say infix:<nosuch>(1, 2);',  'infix:<nosuch>',  '',    'two-argument infix call is refused');
refused('say 0; say prefix:<nosuch>(1);',    'prefix:<nosuch>', '',    'prefix call is refused');

# …and at run time, where the program is not all plain statements
refused('say 0; sub f {}; say infix:<nosuch>(42);', 'infix:<nosuch>',   "0\n", 'run-time call form');
refused('say 0; sub f {}; say &infix:<nosuch>;',    'infix:<nosuch>',   "0\n", 'run-time `&infix` form');
refused('say 0; sub f {}; say &prefix:<nosuch>;',   'prefix:<nosuch>',  "0\n", '`&prefix:<…>` of nothing');
refused('say 0; sub f {}; say &postfix:<nosuch>;',  'postfix:<nosuch>', "0\n", '`&postfix:<…>` of nothing');
refused('say infix:<ff>(1, 2);', 'infix:<ff>', '', 'flip-flop is syntax, not a routine (as in Rakudo)');

# --- the exception itself --------------------------------------------------
{
    try EVAL q[say "ran"; infix:<nosuch>(42)];
    ck($!.^name, 'X::Undeclared::Symbols', 'it is X::Undeclared::Symbols');
    ck($!.message.starts-with("Undefined routine 'infix:<nosuch>'"), True, 'and names the routine');
    try EVAL q[sub g { infix:<nosuch>(1) }; g()];
    ck($!.^name, 'X::Undeclared::Symbols', 'inside a routine too');
    try EVAL q[infix:<befor>(1, 2)];
    ck($!.message.contains("Did you mean any of these: 'infix:<before>'"), True,
       'a misspelled operator suggests the real one');
}

# --- real operators still work ---------------------------------------------
ck(infix:<+>(5), 5,           'infix:<+>(5) is 5');
ck(infix:<->(10), 10,         'infix:<->(10) is 10');
ck(infix:<before>(42), True,  'a chaining comparison of one is True');
ck(infix:<+>(2, 3), 5,        'two arguments');
ck(&infix:<+>.name, 'infix:<+>', '`&infix:<+>` is a value');
ck(&infix:<+>(2, 3), 5,       '…and callable');
ck(&infix:<->(10), 10,        '…in its one-argument form');
ck((1, 2, 3).reduce(&infix:<+>), 6, 'reduce with `&infix:<+>`');
ck(<b a c>.sort(&infix:<cmp>).List, <a b c>, 'sort with `&infix:<cmp>`');
ck(infix:<×>(2, 3), 6,        'a Unicode spelling');
ck(infix:<xx>(1, 2).List, (1, 1), 'a word operator');
ck(infix:<~~>(1, Int), True,  'smartmatch by name');
{
    my $x = 1;
    infix:<+=>($x, 2);
    ck($x, 3, 'an assignment metaop by name assigns');
}

# metaoperator spellings over a real operator
ck(infix:<R->(1, 2), 1,                         'R-');
ck(infix:<RR->(1, 2), -1,                       'RR-');
ck(infix:<Z+>((1, 2), (3, 4)).List, (4, 6),     'Z+');
ck(infix:<X~>(<a b>, <c d>).List, <ac ad bc bd>, 'X~');
ck(infix:<!==>(1, 2), True,                     '!==');
ck(infix:<!before>(1, 2), False,                '!before');
ck(infix:<»+«>((1, 2), (3, 4)).List, (4, 6),    '»+«');
ck(infix:<min=>(my $m = 5, 2), 2,               'min=');
ck(prefix:<[+]>(1, 2, 3), 6,                    'prefix:<[+]>');
ck(prefix:<[\+]>(1, 2, 3).List, (1, 3, 6),      'prefix:<[\+]>');
ck(prefix:<-«>((1, 2)).List, (-1, -2),          'prefix:<-«>');
ck(&prefix:<->(3), -3,                          '&prefix:<->');
ck(postfix:<i>(2), 2i,                          'postfix:<i>');
ck(&circumfix:<[ ]>(1, 2), [1, 2],              '&circumfix:<[ ]>');
{
    # S is real in Rakudo; whatever the engine says about it, it is not undeclared
    try infix:<S+>(1, 2);
    ck(($!.defined ?? $!.message !! '').contains('Undefined routine'), False, 'S+ is not undeclared');
}

# user operators, before and after their declaration
ck(&infix:<later>(1, 2), 3, 'an operator declared further down');
sub infix:<later>($a, $b) { $a + $b }
{
    sub infix:<foo>($a, $b) { "$a|$b" }
    ck(infix:<foo>(1, 2), '1|2',     'a user infix by name');
    ck(&infix:<foo>(3, 4), '3|4',    '`&infix:<foo>`');
    ck(1 foo 2, '1|2',               'and as an operator');
    ck(infix:<Rfoo>(1, 2), '2|1',    'a metaop over a user infix');
    ck(([foo] 1, 2, 3), '1|2|3',     'a reduction over it');
}
{
    # A backslash in `«…»` escapes what follows, the closing quote too. These
    # were declared as `~~>\` and `\$>`, and S02-names/name.t passed only
    # because `&[~~>»]` fell through to a Callable for an operator nobody
    # declared — the silent pass above.
    sub infix:«~~>\»»($a, $b) { "$a~$b" }
    sub infix:«\$>»($a, $b) { "$a\$$b" }
    ck(&[~~>»](1, 2), '1~2', 'an escaped » inside «…» is part of the name');
    ck(&[$>](1, 2), '1$2',   'and so is an escaped $');
}
{
    multi infix:<->(Str $a, Str $b) { "s" }
    ck(infix:<->(10), 10, 'a user multi beside the built-in keeps the one-argument form');
}
{
    my &infix:<bar> = -> $a, $b { $a * $b };
    ck(infix:<bar>(3, 4), 12, 'an operator bound to a `my &infix:<…>`');
}
{
    sub postfix:<!>($n) { [*] 1..$n }
    sub prefix:<§>($x) { "s$x" }
    ck(postfix:<!>(5), 120,   'a user postfix by name');
    ck(&postfix:<!>(4), 24,   '`&postfix:<!>`');
    ck(prefix:<§>(1), 's1',   'a user prefix by name');
    ck(&prefix:<§>(2), 's2',  '`&prefix:<§>`');
}

say $fails ?? "FAIL ($fails)" !! "PASS";
exit $fails ?? 1 !! 0;
