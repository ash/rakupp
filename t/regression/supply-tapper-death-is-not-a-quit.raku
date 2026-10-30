# Regression: a supply block whose TAPPER died on an emit quit and closed,
# and the values still to come were lost. IO::Socket::Async::SSL's
# client-server test failed under `rakupp install`: its listener emits each
# connection from `whenever $accepted-promise { emit $ssl-socket }`, the
# test's tap block dies on the first one (it calls a `.supply` method the
# class does not have), and the second client's connection never reached
# the tap — "You planned 24 tests, but ran 23".
#
# In Rakudo the tapper runs inside the supply's EMIT handler, so the block's
# own CATCH cannot see what it throws: the error comes out of `emit` (a
# CATCH in the whenever body still gets it), past the supply, to whoever
# delivered the value — the emitter's `.emit`, the caller of `.tap`, or a
# Promise's `then` that drops it. The supply neither quits nor closes.
# A whenever body's OWN death still quits the supply, and a react that taps
# a supply still dies of its body's death: that react, not the supply,
# raised it.
#
# Expectations checked against Rakudo 2026.09
# (~/.rakubrew/versions/moar-2026.09/install/bin/rakudo). Green on both.

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

{
    # the emitter of a Supplier gets the error; the next value still arrives
    my @log;
    my $src = Supplier.new;
    my $s = supply {
        whenever $src.Supply -> $v { emit $v; @log.push("after emit $v") }
        CLOSE { @log.push('CLOSE') }
    }
    $s.tap: -> $v { @log.push("tap $v"); die "boom $v" if $v == 1 },
        quit => { @log.push("quit: {.message}") };
    for 1, 2 -> $v {
        $src.emit($v);
        CATCH { default { @log.push("emitter caught: {.message}") } }
    }
    ck @log, ['tap 1', 'emitter caught: boom 1', 'tap 2', 'after emit 2'],
        'a tapper that dies does not quit the supply';
}

{
    # a CATCH in the whenever body sees it first
    my @log;
    my $src = Supplier.new;
    my $s = supply {
        whenever $src.Supply -> $v {
            emit $v;
            CATCH { default { @log.push("whenever caught: {.message}") } }
        }
    }
    $s.tap: -> $v { @log.push("tap $v"); die "boom $v" if $v == 1 },
        quit => { @log.push("quit: {.message}") };
    $src.emit($_) for 1, 2;
    ck @log, ['tap 1', 'whenever caught: boom 1', 'tap 2'],
        "the emitting body's CATCH gets the tapper's error";
}

{
    # an emit in the block body itself: the caller of .tap gets it
    my @log;
    my $s = supply { emit 1; emit 2; CLOSE { @log.push('CLOSE') } }
    try {
        $s.tap: -> $v { @log.push("tap $v"); die "boom $v" },
            quit => { @log.push("quit: {.message}") };
        CATCH { default { @log.push("tap caller caught: {.message}") } }
    }
    ck @log, ['tap 1', 'tap caller caught: boom 1'],
        'from the block body the error reaches the caller of .tap';
}

{
    # the TLS listener's shape: each value comes from a promise kept later,
    # through a whenever in a sub called from another whenever
    my @got;
    my $conns = Supplier.new;
    my @accepted = Promise.new xx 2;
    my $second = Promise.new;
    my $listener = supply {
        whenever $conns.Supply -> $i { handle($i) }
        sub handle($i) {
            whenever @accepted[$i] -> $conn { emit $conn }
        }
    }
    $listener.tap: -> $conn {
        @got.push($conn);
        $second.keep if @got == 2;
        die "the tap block died on $conn";
    }, quit => { @got.push("quit: {.message}") };
    $conns.emit($_) for 0, 1;
    @accepted[0].keep('first');
    @accepted[1].keep('second');
    await Promise.anyof($second, Promise.in(5));
    ck @got, ['first', 'second'],
        'a whenever over a promise: the next value still reaches the tap';
}

{
    # a timer's value: the supply goes on to the next source's values
    my @got;
    my $src = Supplier.new;
    my $x = Promise.new;
    my $s = supply {
        whenever Promise.in(0.05) { emit 'timer' }
        whenever $src.Supply { emit $_ }
    }
    $s.tap: -> $v { @got.push($v); $x.keep if $v eq 'x'; die "boom $v" if $v eq 'timer' },
        quit => { @got.push("quit: {.message}") };
    sleep 0.3;
    $src.emit('x');
    await Promise.anyof($x, Promise.in(5));
    ck @got, ['timer', 'x'], 'a whenever over Promise.in: the supply goes on';
}

{
    # through a chain of supplies, to the emitter at the far end
    my @log;
    my $src = Supplier.new;
    my $a = supply { whenever $src.Supply -> $v { emit $v }; CLOSE { @log.push('CLOSE a') } }
    my $b = supply { whenever $a -> $v { emit $v * 10 }; CLOSE { @log.push('CLOSE b') } }
    $b.tap: -> $v { @log.push("tap $v"); die "boom $v" if $v == 10 },
        quit => { @log.push("quit: {.message}") };
    for 1, 2 -> $v {
        $src.emit($v);
        CATCH { default { @log.push("emitter caught: {.message}") } }
    }
    ck @log, ['tap 10', 'emitter caught: boom 10', 'tap 20'],
        'the error passes through every supply in a chain';
}

{
    # another supply's tapper dying inside OUR whenever body is our own death
    my @log;
    my $src = Supplier.new;
    my $inner = supply { emit 'i' }
    my $s = supply {
        whenever $src.Supply -> $v { $inner.tap({ die 'inner tapper died' }); emit $v }
        CLOSE { @log.push('CLOSE') }
    }
    $s.tap: -> $v { @log.push("tap $v") }, quit => { @log.push("quit: {.message}") };
    $src.emit($_) for 1, 2;
    ck @log.sort.List, ('CLOSE', 'quit: inner tapper died'),
        "a whenever body's own death still quits the supply";
}

{
    # a react is the tapper: its body's death ends the react and closes the
    # supply, its QUIT does not see it, and the emitter is not told
    my @log;
    my $src = Supplier.new;
    my $s = supply { whenever $src.Supply -> $v { emit $v }; CLOSE { @log.push('CLOSE') } }
    my $ready = Promise.new;
    my $emitter = start {
        await $ready;
        for 1, 2 -> $v {
            $src.emit($v);
            CATCH { default { @log.push("emitter caught: {.message}") } }
        }
    }
    try {
        react {
            whenever $s -> $v {
                @log.push("react $v");
                die "boom $v" if $v == 1;
                QUIT { default { @log.push("QUIT saw {.message}") } }
            }
            $ready.keep;
        }
        CATCH { default { @log.push("react died: {.message}") } }
    }
    await $emitter;
    ck @log.sort.List, ('CLOSE', 'react 1', 'react died: boom 1'),
        "a react's body dying ends the react, not its source";
}

{
    # …and a supply that dies itself still reaches the react's QUIT
    my @log;
    my $s = supply { emit 1; die 'producer died' }
    react {
        whenever $s -> $v {
            @log.push("react $v");
            QUIT { default { @log.push("QUIT saw {.message}"); done } }
        }
    }
    ck @log, ['react 1', 'QUIT saw producer died'], "the producer's own death is a quit";
}

say $fails ?? "FAIL ($fails)" !! 'PASS';
exit $fails ?? 1 !! 0;
