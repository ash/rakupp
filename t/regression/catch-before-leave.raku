# Regression: the CATCH that takes an error runs BEFORE the LEAVE phasers of
# the blocks between it and the `die` (2026-10-01).
#
# Rakudo runs an exception's handler on top of the stack, before anything is
# unwound — that is what makes `.resume` possible — and the blocks the error
# passes through are left (LEAVE, UNDO, `temp` and `let` restored) only once the
# handler is done. rakupp left each block first, so an outer CATCH came last,
# saw `temp`/`let` values already restored and the `$*x` of the dying block
# gone, and a `.resume` could not get back into the block that died. That was
# so on every path: a statement-level `die` handed on without a C++ throw
# (9cc30857) and an ordinary throw (`die … if`, a `die` in a called sub).
#
# Not covered, because rakupp still differs from Rakudo there: a handler beyond
# a builtin's callback (`.map({ … })`) or a gather's block runs after the
# LEAVEs, as before; and a `temp` is restored after the block's own LEAVE
# phasers (Rakudo restores it first), on every exit.
#
# Expectations checked against Rakudo 2026.09 via /opt/homebrew/bin/rakudo — NOT
# the bare name `raku`, which on this box is rakupp. Green on both.

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

{
    my @log;
    { CATCH { default { @log.push: 'catch' } }; { LEAVE @log.push: 'leave'; CATCH { }; die 'a' } }
    ck(@log, [<catch leave>], 'a bare `die` past an empty CATCH: the outer CATCH, then LEAVE');
    @log = ();
    { CATCH { default { @log.push: 'catch' } }; { LEAVE @log.push: 'leave'; CATCH { }; die 'b' if True } }
    ck(@log, [<catch leave>], '…a `die` with a modifier (a C++ throw)');
    @log = ();
    { CATCH { default { @log.push: 'catch' } }; { LEAVE @log.push: 'leave'; die 'c' } }
    ck(@log, [<catch leave>], '…a bare `die` in a block with no CATCH');
    @log = ();
    sub dies-d { die 'd' }
    { CATCH { default { @log.push: 'catch' } }; { LEAVE @log.push: 'leave'; dies-d() } }
    ck(@log, [<catch leave>], '…a `die` inside a sub the block calls');
    @log = ();
    { CATCH { default { @log.push: 'catch' } }; { LEAVE @log.push: 'middle'; { LEAVE @log.push: 'inner'; die 'e' } } }
    ck(@log, [<catch inner middle>], 'two levels of LEAVE come after the CATCH, inner first');
}

{
    my @log;
    { CATCH { default { @log.push: 'outer ' ~ .message } }; { LEAVE @log.push: 'leave'; CATCH { default { @log.push: 'inner'; .rethrow } }; die 'g' } }
    ck(@log, ['inner', 'outer g', 'leave'], 'an inner CATCH that rethrows: the outer CATCH runs before the inner block is left');
    @log = ();
    { CATCH { default { @log.push: 'outer ' ~ .message } }; { LEAVE @log.push: 'leave'; CATCH { default { @log.push: 'inner'; die 'new' } }; die 'g2' } }
    ck(@log, ['inner', 'outer new', 'leave'], '…one that dies anew');
    @log = ();
    { CATCH { default { @log.push: 'outermost: ' ~ .message } }; { CATCH { default { @log.push: 'mid'; die 'from mid' } }; { LEAVE @log.push: 'leave'; die 'orig' } } }
    ck(@log, ['mid', 'outermost: from mid', 'leave'], '…and a mid-level CATCH that dies, with the LEAVE further in');
    @log = ();
    { CATCH { when X::NYI { @log.push: 'NYI' } }; { LEAVE @log.push: 'mid'; CATCH { when X::AdHoc { @log.push: 'wrong' } }; { LEAVE @log.push: 'in'; X::NYI.new(feature => 'z').throw } } }
    ck(@log, [<NYI in mid>], 'a CATCH that matches nothing passes it on, still ahead of every LEAVE');
    @log = ();
    { CATCH { default { @log.push: 'catch' } }; { KEEP @log.push: 'keep'; UNDO @log.push: 'undo'; LEAVE @log.push: 'leave'; die 'h' } }
    ck(@log, [<catch leave undo>], 'UNDO (not KEEP) runs, after the CATCH');
}

{
    our $t = 'orig';
    my $seen;
    { CATCH { default { $seen = $t } }; { temp $t = 'temped'; die 'i' } }
    ck(($seen, $t), <temped orig>, 'the outer CATCH sees a `temp` value of the dying block; restored afterwards');
    our $l = 'orig';
    { CATCH { default { $seen = $l } }; { let $l = 'letted'; die 'j' } }
    ck(($seen, $l), <letted orig>, '…and a `let` value');
    sub lets { let $l = 'in sub'; die 't' }
    { CATCH { default { $seen = $l } }; lets() }
    ck(($seen, $l), ('in sub', 'orig'), '…a `let` in a called sub');
    { CATCH { default { $seen = (try $*DYN) // 'nothing' } }; { my $*DYN = 'inner-dyn'; die 'o' } }
    ck($seen, 'inner-dyn', 'a `$*x` of the block that died is visible to the CATCH');
}

{
    my @log;
    sub inner2 { LEAVE @log.push: 'inner2'; die 'k' }
    sub outer2 { LEAVE @log.push: 'outer2'; inner2() }
    sub top { CATCH { default { @log.push: 'catch' } }; LEAVE @log.push: 'top'; outer2() }
    top();
    ck(@log, [<catch inner2 outer2 top>], 'nested routines: the routine CATCH, then every LEAVE inside out');
    @log = ();
    class C { method m { LEAVE @log.push: 'm'; die 'meth' } }
    { CATCH { default { @log.push: 'catch' } }; C.new.m }
    ck(@log, [<catch m>], 'a method');
    @log = ();
    multi mm(Int $x) { LEAVE @log.push: 'mm'; die 'mm' }
    { CATCH { default { @log.push: 'catch' } }; mm(1) }
    ck(@log, [<catch mm>], 'a multi sub');
    @log = ();
    { LEAVE @log.push: 'own'; CATCH { default { @log.push: 'catch' } }; die 'l' }
    ck(@log, [<catch own>], 'a CATCH and a LEAVE in the same block: the CATCH first, as before');
}

{
    my @log;
    for 1..2 -> $i { CATCH { default { @log.push: "catch $i" } }; { LEAVE @log.push: "leave $i"; die 'p' } }
    ck(@log, ['catch 1', 'leave 1', 'catch 2', 'leave 2'], 'in a loop body');
    @log = ();
    for 1..3 -> $i { CATCH { default { @log.push: "catch $i"; next if $i == 1; last } }; { LEAVE @log.push: "leave $i"; die 'x' }; @log.push: 'not here' }
    ck(@log, ['catch 1', 'leave 1', 'catch 2', 'leave 2'], '`next` and `last` from that CATCH');
    @log = ();
    sub ret { CATCH { default { @log.push: 'catch'; return 5 } }; { LEAVE @log.push: 'leave'; die 'x' }; 7 }
    ck((ret(), @log), (5, [<catch leave>]), '`return` from it');
    @log = ();
    sub failing { CATCH { default { fail 'failed: ' ~ .message } }; { LEAVE @log.push: 'leave'; die 'nn' } }
    my $f = failing();
    ck(($f.exception.message, @log), ('failed: nn', ['leave']), '`fail` from it');
}

{
    my @log;
    { CATCH { default { @log.push: 'catch'; .resume } }; my $r = (die 'f'); @log.push: "r={$r.raku}" }
    ck(@log, ['catch', 'r=Any'], '`.resume` returns from the `die`: the rest of the expression runs');
    @log = ();
    { CATCH { default { @log.push: 'catch'; .resume } }; { LEAVE @log.push: 'leave'; die 'q'; @log.push: 'resumed' } }
    ck(@log, [<catch resumed leave>], '…back inside the block that died, before its LEAVE');
    @log = ();
    sub resumed { LEAVE @log.push: 'leave'; die 'x'; @log.push: 'in sub'; 3 }
    { CATCH { default { @log.push: 'catch'; .resume } }; my $r = resumed(); @log.push: "got $r" }
    ck(@log, ['catch', 'in sub', 'leave', 'got 3'], '…back inside a called sub');
}

{
    my @log;
    sub swallowed { LEAVE @log.push: 'leave'; die 'wl' }
    { CATCH { default { @log.push: 'wrong' } }; my $x = try swallowed(); @log.push: 'after' }
    ck(@log, [<leave after>], 'a `try` around a call takes the error: the outer CATCH never runs');
    @log = ();
    { CATCH { default { @log.push: 'wrong' } }; try { { LEAVE @log.push: 'leave'; die 'tb' } }; @log.push: 'after' }
    ck(@log, [<leave after>], '…and a `try` block');
    @log = ();
    { CATCH { default { @log.push: 'caught ' ~ .message } }; await start { LEAVE @log.push: 'leave'; die 's' } }
    ck(@log, ['leave', 'caught s'], 'a `start` block is left before `await` rethrows its error');
    my $n = 0;
    for ^1000 { CATCH { default { $n++ } }; { CATCH { }; die 'foo' } }
    ck($n, 1000, 'catch.t\'s loop: every error reaches the outer CATCH once');
}

say $fails ?? "FAIL ($fails)" !! 'PASS';
exit $fails ?? 1 !! 0;
