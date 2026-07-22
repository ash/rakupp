# Regression: the fixes that got JSON::Fast's from-json working under rakupp
# (v2 campaign, 2026-07-23). Each is a general correctness fix.

my $ok = True;
sub ck($got, $want, $l) { unless $got eqv $want { say "FAIL: $l — {$got.raku} vs {$want.raku}"; $ok = False } }

# 1. .Numeric / .Real on a string use the type ladder, not a blanket Num
ck("1".Numeric,   1,     '"1".Numeric is Int 1');
ck("1".Numeric.^name, 'Int', '"1".Numeric is Int type');
ck("1.5".Numeric.^name, 'Rat', '"1.5".Numeric is Rat');
ck("1e3".Numeric.^name, 'Num', '"1e3".Numeric is Num');

# 2. `--> True/False/Nil` literal return overrides a non-empty body (body runs
#    for side effects, the literal is returned)
sub sidefx(int $p is rw --> False) { $p = 5 }
my int $x;
ck(sidefx($x), False, '--> False returns False');
ck($x, 5, '--> False body side effect ran');
sub lit(--> 42) { my $ignored = 99 }
ck(lit(), 42, '--> 42 overrides body');

# 3. cooperative return escapes nqp::while / nqp::stmts
{
    use nqp;
    sub loopret() {
        my int $i = 0;
        nqp::while(1, nqp::stmts(
            ($i = nqp::add_i($i, 1)),
            nqp::if(nqp::iseq_i($i, 3), (return $i))));
        -1;
    }
    ck(loopret(), 3, 'return escapes nqp::while');
}

# 4. nqp::bindattr shares backing storage (buffer pushes show through container)
{
    use nqp;
    my @r;
    nqp::bindattr(@r, List, '$!reified', my $buf := nqp::create(IterationBuffer));
    nqp::push($buf, 10); nqp::push($buf, 20);
    ck(@r.elems, 2, 'bindattr storage is shared');
    ck(@r[1], 20, 'bindattr shared contents visible');
}

say 'PASS' if $ok;
