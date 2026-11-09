# `.AST.DEPARSE` keeps a declaration's traits, as Rakudo writes them: `is
# export` (with its tags) on a class, a sub, an enum and a subset, `is
# DEPRECATED` on a sub, and an attribute's `is default(…)` and `handles
# <…>`. Each was dropped, rendering valid Raku that meant something else.
# Every line answers the same under Rakudo 2026.09.

sub ck($got, $want, $desc) {
    return if $got eqv $want;
    say "FAIL: $desc — got {$got.raku}, want {$want.raku}";
    exit 1;
}
sub dp($code) { $code.AST.DEPARSE }

ck dp(Q[class G is export { has $.a }]), "class G is export \{\n    has \$.a\n}\n", 'a class';
ck dp(Q[sub f is export { 1 }]), "sub f is export \{\n    1\n}\n", 'a sub';
ck dp(Q[sub f is export(:a, :b) { }]), "sub f is export(:a, :b) \{ }\n", 'its tags';
ck dp(Q[sub f is export(:x) { }]), "sub f is export(:x) \{ }\n", 'one tag';
ck dp(Q[sub h is DEPRECATED { }]), "sub h is DEPRECATED \{ }\n", 'DEPRECATED';
ck dp(Q[sub h is DEPRECATED("use f") { }]), "sub h is DEPRECATED(\"use f\") \{ }\n", 'with its advice';
ck dp(Q[enum E is export <a b>]), "enum E is export <a b>\n", 'an enum';
ck dp(Q[subset S of Int is export where * > 0]), "subset S of Int is export where * > 0\n", 'a subset';
ck dp(Q[class H { has $.x is default(3) }]), "class H \{\n    has \$.x is default(3)\n}\n", 'is default';
ck dp(Q[class K { has @.l handles <elems push> }]), "class K \{\n    has \@.l handles <elems push>\n}\n", 'handles';

say 'PASS';
