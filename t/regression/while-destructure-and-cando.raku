# Regression: two fixes that took HTTP::Tiny from 5 to 7 of its 10 files,
# 2026-08-04. Both are ordinary language gaps. Checked against Rakudo.

my $ok = True;
sub ck($got, $want, $l) { unless $got eqv $want { say "FAIL: $l — " ~ $got.raku ~ ' vs ' ~ $want.raku; $ok = False } }

# 1. `while EXPR -> (:key($k), :value($v))` — a destructuring signature. Only a
#    single loop variable was accepted, so the whole file failed to parse.
{
    my @q = 1 => 'x', 2 => 'y';
    my @out;
    while @q.shift -> (:key($k), :value($v)) {
        @out.push("$k=$v");
        last unless @q;
    }
    ck(@out, ['1=x', '2=y'], 'while with a named-destructuring signature');

    my @p = (1, 2), (3, 4);
    my @sums;
    while @p.shift -> ($a, $b) {
        @sums.push($a + $b);
        last unless @p;
    }
    ck(@sums, [3, 7], 'positional destructuring too');

    my @s = 5, 6;
    my @plain;
    while @s.shift -> $x { @plain.push($x); last unless @s }
    ck(@plain, [5, 6], 'the single-variable form still works');
}

# 2. `.cando(\capture)` — the candidates that would accept that capture. Multi
#    dispatch already answers this; nothing exposed it as a method.
{
    my sub f($a, $b) { 1 }
    ck((?(&f.cando: \(1, 2)), ?(&f.cando: \(1)), ?(&f.cando: \(1, 2, 3))),
       (True, False, False), 'arity is respected');

    my proto g(|) {*}
    multi g(Int) { }
    multi g(Str) { }
    ck((?(&g.cando: \(1)), ?(&g.cando: \('x')), ?(&g.cando: \(1.5))),
       (True, True, False), 'each multi candidate is tested');

    # on a METHOD the capture's first positional is the INVOCANT, which is not a
    # parameter — this is the shape HTTP::Tiny checks a cookie jar with
    my class J { method add(Str:D $, Str:D $ --> Bool) { } }
    my $m = J.^find_method('add');
    ck((?($m.cando: \(J, 'a', 'b')), ?($m.cando: \(J, 'a')), ?($m.cando: \(J, 1, 2))),
       (True, False, False), 'a method skips its invocant');
}

say $ok ?? 'PASS' !! 'FAIL';
