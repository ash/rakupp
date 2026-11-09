# `.AST.DEPARSE` writes an empty body `{ }`, as Rakudo does, where it wrote
# "{\n}"; keeps the arrow of a pointy block with no parameters, `-> { }`,
# where it wrote a plain block (a different program: one that takes a
# topic), a `for`'s too; and writes an anonymous sub `sub { … }` and `sub ($x) { … }`,
# where it wrote `sub  {` and `sub($x)`.
# Every line answers the same under Rakudo 2026.09.

sub ck($got, $want, $desc) {
    return if $got eqv $want;
    say "FAIL: $desc — got {$got.raku}, want {$want.raku}";
    exit 1;
}
sub dp($code) { $code.AST.DEPARSE }

ck dp(Q[sub g { }; class C { }; if 1 { }; while 0 { }]), "sub g \{ }\nclass C \{ }\nif 1 \{ }\nwhile 0 \{ }\n", 'an empty body';
ck dp(Q[say -> { }]), "say -> \{ }\n", 'a pointy block with no parameters';
ck dp(Q[for 1, 2 -> { }; for 1, 2 { }]), "for 1, 2 -> \{ }\nfor 1, 2 \{ }\n", "and a `for`'s, beside one with none";
ck dp(Q[say sub { }]), "say sub \{ }\n", 'an anonymous sub';
ck dp(Q[say sub ($x) { }]), "say sub (\$x) \{ }\n", 'and with a signature';
ck Q[my &f = -> { 41 + 1 }; f()].AST.EVAL, 42, 'which runs';

say 'PASS';
