# `q;…;`, `q,…,` and `q.….` are quotes, as Rakudo reads them, and so for
# `qq` and `Q`: `say q;abc;` died "Undefined routine 'q'" and `q.abc.` was a
# parse error. A sigilless term named q — `my \q`, a `\q` or `|q`
# parameter, `constant q` — is still that term, and a class named Q is the
# class.
# Every line answers the same under Rakudo 2026.09.

sub ck($got, $want, $desc) {
    return if $got eqv $want;
    say "FAIL: $desc — got {$got.raku}, want {$want.raku}";
    exit 1;
}

ck q;abc;, 'abc', 'q;…;';
ck q,abc,, 'abc', 'q,…,';
ck q.abc., 'abc', 'q.….';
ck qq;a b;, 'a b', 'qq';
ck Q.x y., 'x y', 'Q';
ck (q,a,, q.b.), <a b>, 'in a list';
sub f(|q) { q }
ck f(1, 2).raku, '\(1, 2)', 'a capture parameter named q';
my \q = 5;
ck (q, q.succ), (5, 6), 'a sigilless variable';
class Q { }
ck Q.^name, 'Q', 'a class named Q';

say 'PASS';
