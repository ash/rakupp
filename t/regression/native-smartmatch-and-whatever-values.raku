# Regression: what `--exe` made of a smartmatch and of a `*` that is a VALUE.
# Every one of these was a SILENT wrong answer — the compiled binary printed
# something different from the interpreter running the same program, and said
# nothing about it.
#
#   * `$x ~~ $matcher` never invoked a Callable matcher. That arm lives in the
#     interpreter's evalBinary, which emitted code does not run, so the compiled
#     `~~` fell through to applyArith and compared a Code object with a number:
#     `200 ~~ (* > 100)` was False. Smartmatch now goes through rtSmartmatch,
#     the interpreter's own value-level rule.
#   * a regex LITERAL on the LEFT of `~~` was emitted as a match against `$_`,
#     so `/a/ ~~ Callable` asked whether a Match is a Callable.
#   * a lone `*` was curried into a one-argument identity closure, so `my $w = *`
#     held a Sub and every `~~ Whatever` test answered False.
#   * a curried closure was NOT marked as a WhateverCode, so `*-1` held in a
#     variable subscripted from the FRONT, and `.WHAT` said `(Sub)`.
#   * `(* < 1)(0)`, `(* < 1).WHAT` and `1 < * < 5` composed the wrong thing:
#     the call, the metamodel macro and the chained comparison each curried the
#     whole expression instead of one side of it.
#   * `* > 2 && * < 5` was curried as ONE two-argument closure. A short-circuit
#     infix hands back one side or the other, so it is `* < 5`, arity one.
#   * a block-final `if`/`given` inside `do { … }` had its value dropped:
#     `my $x = do { if 1 { 'y' } else { 'n' } }` came back Any.
#   * `Foo:D` in term position lost its smiley, so `Any:D.^name` said "Any".
#
# Found while fixing issue #96 (which was the `when` half of the same story; see
# when-condition-whatever-curry.raku). Every expectation was checked against
# Rakudo. This file must stay NATIVELY compilable — a construct the backend
# refuses would bundle it with the interpreter and test nothing.

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

# --- a Callable matcher is INVOKED with the topic --------------------------
{
    my $over100 = * > 100;
    ck((200 ~~ $over100), True,  'a WhateverCode matcher is called');
    ck((3   ~~ $over100), False, '…and answers False when it should');
    my $block = { $_ > 100 };
    ck((200 ~~ $block), True,  'a block matcher is called');
    ck((3   ~~ $block), False, '…and answers False when it should');
    ck((200 ~~ (* > 100)), True, 'a matcher written in place is called too');
    ck((3   ~~ (* > 100)), False, '…and answers False when it should');
}

# --- a regex literal on the LEFT is the Regex OBJECT ----------------------
ck((/a/ ~~ Callable), True, 'a regex literal smartmatches Callable');
ck((/a/ ~~ Code),     True, '…and Code');

# --- a lone `*` is the Whatever value -------------------------------------
sub whatis($x) { $x ~~ Whatever ?? 'whatever' !! $x.^name }
{
    my $w = *;
    ck(($w ~~ Whatever), True, 'a `*` stored in a variable is a Whatever');
    ck(whatis(*), 'whatever', 'and a `*` passed as an argument is one');
}

# --- a composed `*` is a WhateverCode -------------------------------------
{
    my $c = * < 1;
    ck(($c ~~ WhateverCode), True, 'a curried expression is a WhateverCode');
    ck(($c ~~ Callable),     True, '…and a Callable');
    ck($c(0), True,  'and it computes');
    ck($c(9), False, '…both ways');
}
ck(((* < 1)(0)), True,  'a curried expression can be called where it is written');
ck(((* < 1)(9)), False, '…both ways');
ck((* < 1).WHAT.^name, 'WhateverCode', 'and .WHAT names it (a metamodel macro does not curry)');
ck((*).WHAT.^name,     'Whatever',     'while a bare `*` answers about itself');

# --- a WhateverCode subscript counts from the end, however it arrived ------
{
    my @a = 10, 20, 30;
    my $last = * - 1;
    ck(@a[$last],  30, 'a `*-1` held in a variable is still end-relative');
    ck(@a[* - 1],  30, '…as it is written out');
    ck(@a[*-2],    20, '…and two from the end');
}

# --- short-circuit infixes do not compose into one closure ----------------
{
    my $band = * > 2 && * < 5;
    ck($band(3), True,  'an && of two curries is the RIGHT side, arity one');
    ck($band(9), False, '…so a second argument is never read');
    # `||` hands back the LEFT when it is true, and a WhateverCode always is —
    # so this one is `* > 8`, not a disjunction of the two
    my $bor = * > 8 || * < 2;
    ck($bor(9), True,  'and an || is its LEFT side, arity one');
    ck($bor(1), False, '…so the right side never runs');
}

# --- a chained comparison composes as one arity-one closure ---------------
{
    my $in = 1 < * < 5;
    ck(($in ~~ Callable), True, 'a chained comparison over `*` is a closure');
    ck($in(3), True,  'and it holds inside the band');
    ck($in(9), False, '…and not outside it');
}

# --- `do { … }` ending in if/given is worth its branch --------------------
ck((do { if 1 { 'y' } else { 'n' } }), 'y', 'a do-block ending in `if` is its branch value');
ck((do { if 0 { 'y' } else { 'n' } }), 'n', '…including the else side');
ck((do { given 3 { when 3 { 'three' }; default { 'other' } } }), 'three', 'and a do-block ending in `given`');
ck((given 4 { when 3 { 'three' }; default { 'other' } }), 'other', '…as an expression on its own');

# --- a type smiley rides on the type value --------------------------------
ck(Any:D.^name, 'Any:D', 'a `:D` type names itself');
ck(Int:U.^name, 'Int:U', 'and a `:U` one');
ck((Any ~~ Any:D), False, 'a type object is not `:D`');
ck((3   ~~ Int:D), True,  'and a defined Int is');

say $fails ?? "FAIL ($fails)" !! "PASS";
exit $fails ?? 1 !! 0;
