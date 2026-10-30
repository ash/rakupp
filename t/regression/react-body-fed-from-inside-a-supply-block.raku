# Regression: a react's whenever over a Supplier ran its body inside whatever
# supply block happened to be delivering on the emitting thread, so a
# `whenever` in that body joined the supply block, not the react, and the
# react ended when the Supplier did without waiting for it; a `done` there
# ended the supply block. Cro's WebSocket client emits each message into a
# Supplier from its parser's whenever, and Cro::RPC::JSON's
# t/060-websocket.rakutest registers `whenever $connection.close(…) { pass
# …; done }` inside `whenever $connection.messages`: its react returned
# first and "connection closed" passed after the subtest's own checks.
#
# Expectations checked against Rakudo 2026.09
# (~/.rakubrew/versions/moar-2026.09/install/bin/rakudo). Green on both.

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

my @log; my $l = Lock.new;
sub log($x) { $l.protect: { @log.push: $x } }

{
    my $s = Supplier.new; my $src = Supplier.new;
    # $s.emit runs inside this supply block's whenever
    supply { whenever $src.Supply -> $v { $s.emit($v) } }.tap;
    my $p = Promise.new; my $v = $p.vow;
    start { sleep 0.1; $src.emit(1); sleep 0.1; $s.done; sleep 0.1; $v.keep(True) }
    react {
        whenever $s.Supply -> $x {
            log "got $x";
            whenever $p { log 'promise'; done }
        }
    }
    log 'after react';
    ck @log.List, ('got 1', 'promise', 'after react'),
        'a whenever in the body is the react\'s: the react waits for it';
}

{
    @log = ();
    my $s = Supplier.new; my $src = Supplier.new;
    my $relayed = 0;
    supply { whenever $src.Supply -> $v { $relayed++; $s.emit($v) } }.tap;
    start { sleep 0.1; $src.emit(1) }
    react { whenever $s.Supply -> $x { log "got $x"; done } }
    $src.emit(2);
    ck ($relayed, @log.List), (2, ('got 1',)),
        '…and its `done` ends the react, not the supply block delivering';
}

say $fails ?? "FAIL ($fails)" !! 'PASS';
exit $fails ?? 1 !! 0;
