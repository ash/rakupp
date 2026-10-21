# Regression: the compiled backends answered with the BUILT-IN operator where
# the interpreter (and Rakudo) call the program's own. `--exe` saw only
# top-level routines, and called those only for an object or enum operand, so
# a plain `sub infix:<->` over two Ints, a `my &infix:<*>` and a lexical
# `sub infix:<+>` were all ignored; `--target=js` never called a user operator
# at all. And `$v * $w` over a class with a `.Numeric` was 0 compiled, because
# only evalBinary bridged through it — which also left the interpreter's own
# `[*]`, `Z*` and `$v += 1` multiplying zeroes.
#
# The answers below are the interpreter's; t/exe/run.raku and t/js/run.raku
# hold the compiled forms to them; everything here compiles natively in both.
# The metaoperators over such a routine, which both refuse, are in
# user-operators-metaops.raku. Contract: exit 0 + last line PASS. Runs under
# Rakudo unchanged; every expectation was checked against Rakudo 2026.09.
#
# The harness writes no infix `-`: the plain sub below replaces it for the
# whole file.
my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok — $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

# ---- a plain top-level sub: every operand, every site --------------------
sub infix:<->($a, $b) { "minus($a,$b)" }
sub down($n) { $n - 1 }
class Box { method less($n) { $n - 2 } }
ck(5 - 3, "minus(5,3)", 'a plain infix sub over two Ints');
ck(down(10), "minus(10,1)", 'inside a top-level sub');
ck(Box.less(10), "minus(10,2)", 'inside a method');
{
    my $t = 10; $t -= 3;
    ck($t, "minus(10,3)", 'its compound assignment');
    my @a = 1, 2; @a[0] -= 5;
    ck(@a, ["minus(1,5)", 2], 'into an array element');
    my $s = 10;
    for ^2 { $s = $s - 1 }
    ck($s, "minus(minus(10,1),1)", 'in a loop the integer lanes would take');
}
ck((* - 1)(10), "minus(10,1)", 'a WhateverCode over it');
ck(&infix:<->(7, 2), "minus(7,2)", 'and as &infix:<->');

# ---- lexical bindings: in scope and only there ---------------------------
{
    my &infix:<*> = -> $a, $b { "star" };
    ck(2 * 3, "star", 'a my &infix:<*>');
    my $y = 2; $y *= 4;
    ck($y, "star", 'and its compound assignment');
}
ck(2 * 3, 6, 'the built-in again after its block');
{
    sub infix:<%>($a, $b) { "mod($a,$b)" }
    ck((1, 2).map({ $_ % 10 }).List, ("mod(1,10)", "mod(2,10)"), 'a lexical sub, from a closure');
    my &g = -> $x { $x % 3 };
    ck(g(5), "mod(5,3)", 'and from a pointy block');
}
ck(7 % 3, 1, 'the built-in again after that block');
sub with-op(&infix:<x>) { "a" x 2 }
ck(with-op(-> $a, $b { "param" }), "param", 'an &infix:<x> parameter');
{
    my sub prefix:<!>($x) { "bang($x)" }
    ck(!5, "bang(5)", 'a plain prefix sub over an Int');
    ck((1, 2).map({ !$_ }).List, ("bang(1)", "bang(2)"), 'and in a closure');
}
{
    sub infix:<~>($a, $b) { "[$a|$b]" }
    my $s = "a"; $s = $s ~ "b";
    ck($s, "[a|b]", 'a user ~ in the $s = $s ~ X shape');
    $s ~= "c";
    ck($s, "[[a|b]|c]", 'and ~=');
}
{
    sub infix:<cmp>($a, $b) { $b <=> $a }
    ck((3, 1, 2).sort({ $^a cmp $^b }).List, (3, 2, 1), 'a sort block that asks a user cmp');
}

# ---- a multi over the program's own class --------------------------------
class P { has $.v }
multi infix:<+>(P $a, P $b) { P.new(v => $a.v + $b.v) }
{
    my @a = P.new(v => 1); @a[0] += P.new(v => 5);
    ck(@a[0].v, 6, 'an element += takes the candidate');
    my $p = P.new(v => 7); $p += P.new(v => 1);
    ck($p.v, 8, 'and a scalar +=');
    ck(1 + 2, 3, 'core operands keep the built-in');
}

# ---- an object with .Numeric, or a Real with .Bridge ---------------------
class V { has $.x; method Numeric { $!x } }
ck(V.new(x => 2) * V.new(x => 3), 6, 'V * V through .Numeric');
ck(V.new(x => 2) + 1, 3, 'V + 1');
ck(([*] V.new(x => 2), V.new(x => 3)), 6, '[*] through .Numeric');
ck(((V.new(x => 2), V.new(x => 3)) Z* (V.new(x => 4), V.new(x => 5))).List, (8, 15), 'Z* too');
{
    my $v = V.new(x => 5); $v += 1;
    ck($v, 6, 'and $v += 1');
}
class F does Real { has $.x; method Bridge { $!x.Num } }
ck(F.new(x => 10) cmp F.new(x => 9), More, 'a Real orders through .Bridge');
ck(F.new(x => 2) * F.new(x => 3), 6e0, 'and multiplies through it');

say $fails ?? "FAIL ($fails)" !! "PASS";
exit $fails ?? 1 !! 0;
