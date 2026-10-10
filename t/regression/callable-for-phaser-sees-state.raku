# Regression: `&block.callable_for_phaser('LAST')` made a Callable that closed
# over the block's closure only, so it could not see the block's `state`
# variables, which live in a frame between that closure and each call's own:
# `LAST say "$s files"` died with "Variable '$s' is not declared". rak's own
# documented pattern is `{ state $s = 0; NEXT $s++; LAST say "$s files"; … }`,
# and rak takes the phasers out FIRST, before any call. The phaser now closes
# over the state frame, which is made on the spot if the block has not run.
#
# Expectations checked against Rakudo 2026.09 via /opt/homebrew/bin/rakudo.

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

{
    my &b = { state $s = 0; $s++; LAST "LAST sees s=$s" };
    b() for 1..3;
    ck(&b.callable_for_phaser('LAST')(), 'LAST sees s=3', 'LAST, taken after the calls, sees the state');
}
{
    my &p = { state $s = 0; NEXT $s++; LAST "$s files"; True };
    my &nx = &p.callable_for_phaser('NEXT');
    my &ls = &p.callable_for_phaser('LAST');
    for 1..3 { p(); nx() }
    ck(ls(), '3 files', 'rak\'s pattern: NEXT and LAST taken before any call share the state');
}
{
    my &q = { state @seen; NEXT @seen.push(@seen.elems); LAST @seen.join(','); 1 };
    my &qn = &q.callable_for_phaser('NEXT');
    q(), qn() for ^3;
    ck(&q.callable_for_phaser('LAST')(), '0,1,2', 'a state array, written by NEXT and read by LAST');
}
{
    # rak runs NEXT per source while the pattern's own calls are still lazy, so
    # the phasers can run BEFORE the block ever has: its state variable is there
    # (undefined) from the start, and the block's first run still initialises it
    my &p = { state $s = 0; NEXT $s++; LAST "$s lines"; True };
    my &nx = &p.callable_for_phaser('NEXT');
    my &ls = &p.callable_for_phaser('LAST');
    nx(); nx();
    my $before = ls();
    p(); nx();
    ck(($before, ls()), ('2 lines', '1 lines'), 'NEXT before the first call; the first call still runs `= 0`');
}
{
    # running a FIRST taken out IS the closure's FIRST: the calls after it must
    # not run it again (rak calls it once, then the pattern per item). Rakudo
    # cannot call a FIRST taken out at all, so there this is skipped.
    my @o;
    my &b = { FIRST @o.push('F'); @o.push('b') };
    my &f = &b.callable_for_phaser('FIRST');
    if try { f(); True } {
        b(); b();
        ck(@o.join(' '), 'F b b', 'a FIRST taken out and run is not run again by the calls');
    }
    else {
        say "ok - a FIRST taken out cannot be called here # SKIP";
    }
}
{
    my &b = { state $s = 0; $s++; LAST { $s = 0 } };
    ck(&b.callable_for_phaser('FIRST'), Nil, 'no FIRST phaser: Nil');
    ck(&b.has-loop-phasers, True, 'has-loop-phasers');
}

say $fails ?? "FAILED $fails" !! "PASS";
