# A program's `multi` on a built-in prefix joins the built-in's candidates,
# as Rakudo's does: `multi prefix:<->(Str $x) { "str" }; -"a"` numified "a"
# and died, the candidate never asked, for every operand that was no object
# (`-`, `+`, `~`, `!`, `so`, `not`), and the operator called by name,
# `prefix:<->(3)`, reached the program's candidates alone. Both now try the
# program's candidates first and the built-in after. A plain `sub` of the
# name replaces the built-in, as before. And `&prefix:<++>`, `&postfix:<++>`
# and their `--` are routines that step the container they are handed, where
# they were Any.
# Every line answers the same under Rakudo 2026.09.

use MONKEY-SEE-NO-EVAL;
sub ck($got, $want, $desc) {
    return if $got eqv $want;
    say "FAIL: $desc — got {$got.raku}, want {$want.raku}";
    exit 1;
}

ck EVAL(q[multi prefix:<->(Str $x) { "str" }; my $s = "a"; (-"a", -3, -$s)]), ("str", -3, "str"), 'prefix -';
ck EVAL(q[multi prefix:<~>(Int $x) { "int" }; (~5, ~"a")]), ("int", "a"), 'prefix ~';
ck EVAL(q[multi prefix:<+>(Str $x) { "plus" }; (+"a", +"3")]), ("plus", "plus"), 'prefix +';
ck EVAL(q[multi prefix:<!>(Str $x) { "bang" }; (!"a", !1)]), ("bang", False), 'prefix !';
ck EVAL(q[multi prefix:<so>(Str $x) { "so" }; ((so "a"), (so 0))]), ("so", False), 'so';
ck EVAL(q[multi prefix:<not>(Str $x) { "not" }; ((not "a"), (not 0))]), ("not", True), 'not';
ck EVAL(q[multi prefix:<->(Str $x) { "str" }; (prefix:<->("a"), prefix:<->(3))]), ("str", -3), 'called by name';
ck EVAL(q[multi prefix:<so>(Str $x) { "so" }; (prefix:<so>("a"), prefix:<so>(0))]), ("so", False), 'so by name';
ck EVAL(q[sub prefix:<->($x) { "sub" }; (prefix:<->(3), -3)]), ("sub", "sub"), 'a plain sub replaces it';
ck EVAL(q[class R { }; multi prefix:<so>(R $a) { "red" }; ((so R.new), (so 1))]), ("red", True), 'an object';

ck (&prefix:<++>.name, &postfix:<-->.name, &postfix:<++>.^name), ('prefix:<++>', 'postfix:<-->', 'Sub'), 'their names';
my $y = 1; &prefix:<++>($y);
ck $y, 2, '&prefix:<++> steps';
my &f = &postfix:<++>; my $z = 5;
ck (f($z), $z), (5, 6), '&postfix:<++> answers the old value';
my $w = 3;
ck (&prefix:<-->($w), $w), (2, 2), '&prefix:<-->';
my $q; &postfix:<++>($q);
ck $q, 1, 'an undefined one';
my @a = 1, 2; for @a { &postfix:<++>($_) }
ck @a, [2, 3], 'through $_';

say 'PASS';
