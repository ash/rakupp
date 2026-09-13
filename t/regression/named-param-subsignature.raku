# Regression: a SUB-SIGNATURE written tight against a named parameter.
#
# A sub-signature normally announces itself with a space — `Pair $p (:key($k))` —
# because a `(` straight after a parameter can also be a coercion or a callable's
# own signature. After a NAMED parameter it can be neither, and Rakudo accepts
# the tight spelling in all of these shapes. We accepted only the spaced one, so
# a module destructuring its named arguments could not be compiled at all:
# Imlib2 writes every geometry argument that way and died at the `(` with
# "expected )", 1266 lines in.
#
# `&`-sigilled parameters keep the space rule, because `&cb(Int)` really is the
# callable's signature and not a destructure. That row is the guard.
#
# Runs clean under Rakudo too.

use MONKEY-SEE-NO-EVAL;

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

# ---- alias, then a tight sub-signature ---------------------------------
{
    sub f(List :loc($l)($x, $y)) { "$x,$y" }
    ck f(loc => (1, 2)), '1,2', 'alias then a tight sub-signature';
}
{
    sub f(:loc($l)($x, $y)) { "$x,$y" }          # …and untyped
    ck f(loc => (3, 4)), '3,4', '…untyped';
}
{
    sub f(List :size($s)!($w, $h)) { "$w x $h" } # …and with the required marker between
    ck f(size => (5, 6)), '5 x 6', '…with the `!` marker before it';
}

# ---- a named VARIABLE needs a MARKER first ------------------------------
# `:$c!(…)` and `:$c?(…)` are accepted; the unmarked `:$c(…)` is not, on either
# engine — the marker is what settles the parameter and makes the paren
# unambiguous. The rejection row matters as much as the two that pass.
{
    sub f(List :$corner!($cx, $cy)) { "$cx/$cy" }
    ck f(corner => (7, 8)), '7/8', 'a named variable, required, then a tight sub-signature';
}
{
    sub f(List :$corner?($cx, $cy)) { "$cx/$cy" }
    ck f(corner => (9, 10)), '9/10', '…optional';
}
{
    my $ok = 'compiled';
    try { EVAL 'sub f(List :$c($x, $y)) { }'; CATCH { default { $ok = 'rejected' } } }
    ck $ok, 'rejected', 'an UNMARKED named variable does not take a tight sub-signature';
}

# ---- the spaced spelling still means what it always did -----------------
{
    sub f(List :loc($l) ($x, $y)) { "$x,$y" }
    ck f(loc => (11, 12)), '11,12', 'the spaced spelling is unchanged';
}

# ---- …and a `&` parameter still carries its own signature ---------------
# `&cb:(Int --> Int)` constrains what the callable accepts; it is not a
# destructure of the argument. `&` is excluded from the relaxed rule for exactly
# that reason, so this must go on meaning what it always did.
{
    sub takes-cb(&cb:(Int --> Int)) { cb(21) }
    ck takes-cb(-> Int $n --> Int { $n * 2 }), 42,
       'a `&` parameter keeps its own signature';
}

say $fails ?? "\n$fails FAILED" !! "\nPASS";
exit $fails ?? 1 !! 0;
