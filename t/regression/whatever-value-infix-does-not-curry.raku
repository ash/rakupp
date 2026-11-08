# An infix on a Whatever VALUE is applied to it — only a `*` written there
# curries, as for methods (whatever-value-method-does-not-curry.raku): `my $w
# = *; $w ~ "a"` was a WhateverCode where Rakudo answers "*a", and `5 ~~ $w`
# one where Rakudo answers True (Whatever.ACCEPTS takes anything). The
# concatenation and smartmatch arms of evalBinary curried before asking
# whether the star was written.
# Every line answers the same under Rakudo 2026.09.

sub ck($got, $want, $desc) {
    return if $got eqv $want;
    say "FAIL: $desc — got {$got.raku}, want {$want.raku}";
    exit 1;
}

my $w = *;
ck $w ~ "a", '*a', '$w ~ "a"';
ck "a" ~ $w, 'a*', '"a" ~ $w';
ck $w ~ $w, '**', '$w ~ $w';
ck 5 ~~ $w, True, '5 ~~ $w';
ck 5 !~~ $w, False, '5 !~~ $w';
ck "x" ~~ $w, True, '"x" ~~ $w';
ck $w ~~ $w, True, '$w ~~ $w';
ck $w ~~ Whatever, True, '$w ~~ Whatever';
ck $w ~~ Int, False, '$w ~~ Int (as before)';
ck $w eqv 1, False, '$w eqv 1 (as before)';
ck $w x 2, '**', '$w x 2 (as before)';

# a WRITTEN star still curries
my &f = * ~ "a";
ck f("b"), 'ba', '* ~ "a"';
my &g = "a" ~ *;
ck g(1), 'a1', '"a" ~ *';
ck <a b>.map(* ~ "!").List, <a! b!>, '.map(* ~ "!")';
ck (* ~~ 5)(5), True, '* ~~ 5';
ck (5 ~~ *) ~~ Code, True, '5 ~~ * is a WhateverCode';
ck <1 2>.grep(* ~~ Int).elems, 2, '.grep(* ~~ Int)';
my $hit;
given * { when Whatever { $hit = 'W' } }
ck $hit, 'W', 'given * { when Whatever }';

# …and so does one before a `.&` call, which runs the code on what the
# curry makes of its argument (it ran the block on the star itself, and the
# block's own `~` curried on that Whatever value)
ck (*.&{ $_ ~ 'one' })('foo'), 'fooone', '*.&{ }';
ck (*.uc.&{ $_ ~ 'one' }.flip.&(*.tc))('foo'), 'EnoOOF', 'a chain through .&';
ck (*.uc.&{ $^a ~ $^b }('one').flip.&(*.tc).&flip)('foo'), 'FOOonE', '.&{ } with an argument';
ck <a b>.map(*.&{ .uc }).List, <A B>, 'in a .map';
ck 5.&{ $_ * 2 }, 10, 'a value before .& (as before)';

say 'PASS';
