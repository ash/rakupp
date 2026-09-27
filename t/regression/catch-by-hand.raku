# Regression: a block's CATCH that matches nothing leaves the block ONCE, and
# an error reaches a CATCH without a C++ throw where it can
# (S04-exception-handlers/catch.t, 2026-09-27).
#
# - LEAVE and UNDO ran twice when the block's CATCH matched nothing: the error
#   was rethrown into the block's own catch-all, which ran the exit again —
#   after the scope had closed, so it also restored the ENCLOSING block's
#   `temp`s, and an outer CATCH saw the old value.
# - `next` or `return` in a CATCH ran the rest of the handler anyway.
# - catch.t dies 500,000 times through two nested CATCH blocks and took 37 s
#   (Rakudo: 2.5 s): the `die`, the rethrow out of the inner block and the
#   `default` that took it were each a C++ throw, tens of µs apiece on macOS.
#   A statement-level `die` and a nested bare block's error now reach the
#   CATCH by hand, and when/default in a CATCH match without a throw; the
#   rest of this file pins down that the handler sees the same things.
#
# Expectations checked against Rakudo 2026.08 via /opt/homebrew/bin/raku — NOT
# the bare name `raku`, which on this box has pointed at rakupp. Green on both.

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

{
    my @log;
    for ^1 {
        CATCH { default { @log.push: 'caught' } }
        { CATCH { @log.push: 'inner' }; LEAVE @log.push: 'leave'; UNDO @log.push: 'undo'; KEEP @log.push: 'keep'; die 'x' }
    }
    ck(@log.grep('leave').elems, 1, 'LEAVE runs once when the block\'s CATCH matches nothing');
    ck(@log.grep('undo').elems, 1, '…UNDO once');
    ck(@log.grep('keep').elems, 0, '…KEEP not at all');
    ck((@log[0], @log.grep('caught').elems), ('inner', 1), '…and the outer CATCH gets it once, after the inner one');
}

{
    our $t = 1;
    my $seen;
    sub f() {
        temp $t = 2;
        { CATCH { }; die 'y' }
        CATCH { default { $seen = $t } }
    }
    f();
    ck($seen, 2, 'the enclosing routine\'s `temp` still holds in its CATCH');
    ck($t, 1, '…and is restored once the routine is left');
}

{
    my @log;
    for ^2 -> $i {
        CATCH { next if $i == 0; @log.push: "after next $i"; default { @log.push: "default $i" }; @log.push: 'after default' }
        die "x$i";
    }
    ck(@log, ['after next 1', 'default 1'], '`next` in a CATCH leaves it, and so does a matched `default`');
    sub h() { { CATCH { return 5; @log.push: 'after return' }; die 'z' }; 7 }
    ck(h(), 5, '`return` in a block\'s CATCH returns from the routine');
    ck(@log.elems, 2, '…without running the rest of the handler');
}

{
    my @seen;
    my &note-it = { @seen.push: .^name ~ ': ' ~ .message };
    { CATCH { default { note-it($_) } }; die 'plain' }
    { CATCH { default { note-it($_) } }; { { die 'two blocks down' } } }
    { CATCH { default { note-it($_) } }; { CATCH { when X::IO { } }; die 'past a CATCH that did not match' } }
    { CATCH { default { note-it($_) } }; die X::NYI.new(feature => 'Frobbing') }
    { CATCH { default { note-it($_) } }; try { die 'first' }; die }
    { CATCH { when X::AdHoc { note-it($_) } }; die 'a', 42 }
    ck(@seen, ['X::AdHoc: plain', 'X::AdHoc: two blocks down', 'X::AdHoc: past a CATCH that did not match',
               'X::NYI: Frobbing not yet implemented. Sorry.', 'X::AdHoc: first', 'X::AdHoc: a42'],
       'a CATCH sees what `die` made: the type and message, nested or not, $! reused, a list joined');
}

{
    my $same;
    my $caught;
    { CATCH { default { $same = $_ === $caught } }; { CATCH { default { $caught = $_; .rethrow } }; die 'id' } }
    ck($same, True, 'the same exception object reaches the outer CATCH');
    my $msg;
    { CATCH { default { $msg = .message } }; { CATCH { default { die "rewrapped: " ~ .message } }; die 'orig' } }
    ck($msg, 'rewrapped: orig', 'a CATCH that dies hands the new error out');
    my @r;
    { CATCH { default { .resume } }; @r.push: 1; die 'r'; @r.push: 2 }
    ck(@r, [1, 2], '.resume carries on after the `die`');
}

{
    my @called;
    { sub die(*@a) { @called.push: @a.join }; CATCH { default { @called.push: 'caught' } }; die 'mine'; @called.push: 'after' }
    ck(@called, ['mine', 'after'], 'a `die` of the program\'s own is called, not the built-in');
    my @g = gather for 1..3 { CATCH { default { take "caught $_" } }; { die 'two' if $_ == 2 }; take $_ };
    ck(@g, [1, 'caught two', 3], 'a CATCH inside a gather takes (its $_ the exception)');
    ck((try { { CATCH { when X::IO { } }; { die 'up' } } }) // $!.message, 'up',
       'what no CATCH matches still leaves by the ordinary way');
}

say $fails ?? "FAIL ($fails)" !! 'PASS';
exit $fails ?? 1 !! 0;
