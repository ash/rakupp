# Regression: supply blocks and react — when a `whenever`'s events are
# delivered, and how `done`, `last`, LAST, CLOSE and QUIT end things.
# Section F of docs/dev/findings/semantics/Supply.md (items S-53 to S-68),
# extracted from Rakudo 2026.08 and implemented here from the sheet.
#
# What was wrong before: a `whenever` over a synchronous source ran its body
# INLINE, in the middle of the block that had just subscribed it, so the body's
# own statements arrived after the events they preceded, and one whenever's
# body nested inside another's. `done` did not stop the block it was in, `last`
# did not close its whenever, an explicit `done` still ran LAST, CLOSE phasers
# ran in declaration order, closing the outer tap left the inner source running,
# a `whenever`'s argument was not coerced with Supply(), and a QUIT phaser was
# ignored while a supply-block-level one swallowed a whenever body's death.
#
# The fix is one mechanism: the activation carries a QUEUE. A whenever
# subscribes at once, but anything its source delivers while a body is running
# waits its turn, tagged with the subscription it came from.
#
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $desc) {
    @fail.push("$desc: got {$got.raku}, want {$want.raku}") unless $got eqv $want;
}

# --- S-53  the body runs first; whenever events queue behind it -------------
{
    my @o;
    (supply { emit 1; whenever Supply.from-list(2) { emit $_ }; emit 3 }).tap({ @o.push($_) });
    check @o, [1, 3, 2], 'the statements after a whenever run before its events';
}
{
    my @o;
    (supply { my $t = do whenever Supply.from-list(1, 2) { emit $_ }; @o.push($t.^name) }).tap({ @o.push($_) });
    check @o, ['Tap', 1, 2], 'do whenever evaluates to the Tap, at once';
}
{
    my @o;
    (supply {
        whenever Supply.from-list(1, 2) { emit $_ * 10; whenever Supply.from-list($_) { emit $_ } }
    }).tap({ @o.push($_) });
    check @o, [10, 20, 1, 2], 'a nested whenever queues behind the body that made it';
}

# --- S-54  done ends the supply at once -------------------------------------
{
    my @o;
    (supply {
        whenever Supply.from-list(1, 2, 3) -> $v { emit $v; done if $v == 1; @o.push("after-done") }
    }).tap({ @o.push($_) });
    check @o, [1], 'done leaves the rest of the body unrun and drops later events';
}
{
    my @o;
    (supply { whenever Supply.from-list(1, 2, 3) { emit $_ * 10; done if $_ == 2 } })
        .tap({ @o.push($_) }, done => { @o.push('done') });
    check @o, [10, 20, 'done'], 'done fires the tapper, and nothing follows';
}
{
    my @o;
    (supply { whenever Supply.from-list(1, 2) { emit $_ }; done })
        .tap({ @o.push($_) }, done => { @o.push('done') });
    check @o, ['done'], 'a done in the body drops the events queued before it';
}
{
    my @o;
    my $s = Supplier.new;
    (supply { whenever $s.Supply { emit $_ }; done }).tap({ @o.push($_) }, done => { @o.push('done') });
    $s.emit(1);
    check @o, ['done'], 'done closes the inner tap, so a later emit reaches nothing';
}

# --- S-55  last and next inside a whenever ----------------------------------
{
    my @o;
    (supply { whenever Supply.from-list(1, 2, 3) { emit $_; last if $_ == 2 } })
        .tap({ @o.push($_) }, done => { @o.push('done') });
    check @o, [1, 2, 'done'], 'last closes its whenever and the supply is done';
}
{
    my @o;
    (supply { whenever Supply.from-list(1, 2) { emit $_; last; LAST { @o.push('L') } } })
        .tap({ @o.push($_) }, done => { @o.push('done') });
    check @o, [1, 'L', 'done'], 'last runs that whenever LAST phaser';
}
{
    my @o;
    (supply { whenever Supply.from-list(1, 2, 3) { next if $_ == 2; emit $_ } }).tap({ @o.push($_) });
    check @o, [1, 3], 'next skips the rest of the body for that value';
}

# --- S-56  an exception in the block is the supply's quit -------------------
{
    my @o;
    (supply { emit 1; die "boom" }).tap({ @o.push($_) }, quit => { @o.push('quit:' ~ .message) });
    check @o, [1, 'quit:boom'], 'the body dying is the supply quitting';
}
# (assigned into an array so the list is reified inside the `try` — Rakudo
# raises at reification, which can be later than the call that asked for it)
check ((try { my @l = (supply { emit 1; die "boom" }).list; @l.elems }) // $!.message), 'boom',
      '.list of a quit supply raises the exception that ended it';

# --- S-57  QUIT belongs to the whenever, and works like CATCH ---------------
{
    my @o;
    (supply {
        whenever Supply.from-list(1).map({ die "q" }) { emit $_; QUIT { default { @o.push('handled:' ~ .message) } } }
    }).tap({ @o.push($_) }, quit => { @o.push('quit') }, done => { @o.push('done') });
    check @o, ['handled:q', 'done'], 'a matching default consumes the quit';
}
{
    my @o;
    (supply {
        whenever Supply.from-list(1).map({ die "q" }) { QUIT { when X::OutOfRange { @o.push('no') } } }
    }).tap({;}, quit => { @o.push('quit:' ~ .message) }, done => { @o.push('done') });
    check @o, ['quit:q'], 'a when that does not match lets the quit through';
}
{
    my @o;
    (supply {
        whenever Supply.from-list(1).map({ die "q" }) { QUIT { @o.push('seen') } }
    }).tap({;}, quit => { @o.push('quit:' ~ .message) }, done => { @o.push('done') });
    check @o, ['seen', 'quit:q'], 'a QUIT with no when/default runs, then lets it through';
}
{
    my @o;
    (supply {
        whenever Supply.from-list(1).map({ die "q" }) { QUIT { default { @o.push('h1') } } };
        whenever Supply.from-list(2) { emit $_ }
    }).tap({ @o.push($_) }, done => { @o.push('done') });
    check @o, ['h1', 2, 'done'], 'a handled quit ends only its own whenever';
}
{   # a death in the BODY is not a source quit: the block-level QUIT never sees it
    my @o;
    (supply { whenever Supply.from-list(1) { die "in-whenever" }; QUIT { @o.push('QUIT:' ~ .message) } })
        .tap({;}, quit => { @o.push('quit:' ~ .message) });
    check @o, ['quit:in-whenever'], 'a whenever body dying reaches the tapper, not a QUIT phaser';
}

# --- S-58  LAST runs on the source's done, not on an explicit `done` --------
{
    my @o;
    (supply {
        whenever Supply.from-list(1, 2) { emit $_; LAST { @o.push('L') } };
        whenever Supply.from-list(3)    { emit $_; LAST { @o.push('L2') } }
    }).tap({ @o.push($_) }, done => { @o.push('done') });
    check @o, [1, 2, 'L', 3, 'L2', 'done'], 'each whenever LAST runs when its own source ends';
}
{
    my @o;
    (supply { whenever Supply.from-list(1) { emit $_; done; LAST { @o.push('L') } } })
        .tap({ @o.push($_) }, done => { @o.push('done') });
    check @o, [1, 'done'], 'an explicit done skips LAST';
}
{
    my @o;
    (supply { whenever Supply.from-list(1) { }; LAST { @o.push('outer-LAST') } })
        .tap({;}, done => { @o.push('done') });
    check @o, ['done'], 'a LAST at the supply-block level does not run';
}

# --- S-59  CLOSE phasers: reverse order, on every teardown path -------------
{
    my @o;
    (supply { whenever Supply.from-list(1) { emit $_ }; CLOSE { @o.push('C1') }; CLOSE { @o.push('C2') } })
        .tap({ @o.push($_) }, done => { @o.push('done') });
    check @o, [1, 'done', 'C2', 'C1'], 'CLOSE runs in reverse order after done';
}
{
    my @o;
    (supply { whenever Supply.from-list(1) { emit $_ }; CLOSE { @o.push('C1') }; CLOSE { @o.push('C2') }; done })
        .tap({ @o.push($_) }, done => { @o.push('done') });
    check @o, ['done', 'C2', 'C1'], 'an explicit done tears down the same way';
}
{
    my @o;
    my $t = (supply { whenever Supplier.new.Supply { }; CLOSE { @o.push('CLOSE') } }).tap({;});
    $t.close;
    check @o, ['CLOSE'], 'closing the tap runs CLOSE';
}

# --- S-60  whenever coerces its argument with Supply() ----------------------
{
    my @o;
    react { whenever (1, 2, 3) { @o.push($_) }; whenever "str" { @o.push($_) }; whenever 42 { @o.push($_) } };
    check @o, [1, 2, 3, 'str', 42], 'an Iterable is one event per element, a scalar is one event';
}
{
    my @o;
    react { whenever Promise.kept(42) { @o.push($_) } };
    check @o, [42], 'a kept Promise delivers its result';
}
check ((try react { whenever Promise.broken("bad") { } }) // $!.message), 'bad',
      'a broken Promise quits with its cause as a real exception';
{
    my @o;
    react { whenever Promise.in(0.01) { @o.push($_.^name) } };
    check @o, ['Bool'], 'a timer Promise delivers True';
}
{
    my $c = Channel.new;
    $c.send(1); $c.send(2); $c.close;
    my @o;
    react { whenever $c { @o.push($_) } };
    check @o, [1, 2], 'a Channel is drained and done on close';
}
{
    my $c = Channel.new;
    $c.send(1); $c.fail("cf");
    check ((try react { whenever $c { } }) // $!.message), 'cf', 'a failed Channel is a quit';
}

# --- S-57 over a Promise source: a body that dies still quits ---------------
# Every row above that dies inside a whenever uses a SUPPLY source, and those
# worked. Over a PROMISE source the death was swallowed whole — no emit, no
# done, no quit, nothing on stderr — and the activation was never released, so
# the supply hung for ever. That is the shape Cro raises an error status in
# (`die X::Cro::HTTP::Error::Client` inside `whenever` over the response
# Promise), so any 4xx/5xx blocked `await Cro::HTTP::Client.get($url)` instead
# of throwing. Four branches carried the same gap: the supply and react timer
# workers, the supply one-shot, and the react async registration.
check ((try react { whenever Promise.kept(1) { die "in-whenever" } }) // $!.message),
      'in-whenever', 'a die over a kept Promise ends the react';
check ((try react { whenever Promise.in(0.01) { die "in-whenever" } }) // $!.message),
      'in-whenever', 'a die over a timer Promise ends the react';
check ((try react { whenever start { 7 } { die "in-whenever" } }) // $!.message),
      'in-whenever', 'a die over a start Promise ends the react';
{
    my $p = Promise(supply { whenever Promise.kept(1) { die "in-whenever" } });
    await Promise.anyof($p, Promise.in(10));
    check ($p.status ~~ Broken ?? $p.cause.message !! $p.status.gist), 'in-whenever',
          'Promise(supply {…}) breaks instead of staying Planned';
}
{
    my @o;
    my $d = Promise.new;
    (supply { whenever Promise.in(0.01) { emit 1; die "after-emit" } })
        .tap({ @o.push($_) },
             quit => { @o.push('quit:' ~ .message); $d.keep unless $d },
             done => { @o.push('done'); $d.keep unless $d });
    await Promise.anyof($d, Promise.in(10));
    check @o, [1, 'quit:after-emit'], 'what was emitted stands, and the quit follows it';
}
{
    my @o;
    my $d = Promise.new;
    (supply { whenever Promise.in(0.01) { CATCH { default { @o.push('caught') } }; die "handled" } })
        .tap({;}, quit => { @o.push('quit:' ~ .message); $d.keep unless $d },
                  done => { @o.push('done'); $d.keep unless $d });
    await Promise.anyof($d, Promise.in(10));
    check @o, ['caught', 'done'], 'a CATCH inside the body still consumes the death';
}

# S-57's other half over a Promise source: only a QUIT phaser that MATCHES
# consumes the break. Merely HAVING one swallowed it, so Cro's redirect arm —
# a bare `QUIT { $request-log.end }` — dropped the quit and left the supply
# unfinished even once a dying body quit correctly; and with no phaser at all a
# `done` still followed the quit, which S-06 forbids.
sub quit-rows(&mk) {
    my @o; my $d = Promise.new;
    mk().tap({ @o.push("emit:$_") },
             quit => { @o.push('quit:' ~ .message); $d.keep unless $d },
             done => { @o.push('done');            $d.keep unless $d });
    await Promise.anyof($d, Promise.in(10));
    @o
}
check quit-rows({ supply { whenever Promise.broken("bad") { QUIT { my $x = 1 } } } }), ['quit:bad'],
      'a bare QUIT phaser runs and the break still reaches the tapper';
check quit-rows({ supply { whenever Promise.broken("bad") { } } }), ['quit:bad'],
      'a break with no QUIT phaser quits once, with no done after it';
check quit-rows({ supply { whenever Promise.broken("bad") { QUIT { default { } } } } }), ['done'],
      'a matching default consumes the break and the whenever counts as done';
check ((try react { whenever Promise.broken("bad") { QUIT { my $x = 1 } } }) // $!.message), 'bad',
      'the same rule inside react';

# --- S-61  closing the tap of a supply block stops everything ---------------
{
    my $s = Supplier.new;
    my @o;
    my $t = (supply { whenever $s.Supply { emit $_ } }).tap({ @o.push($_) });
    $s.emit(1);
    $t.close;
    $s.emit(2);
    check @o, [1], 'a closed tap stops the inner source too';
}

# --- S-62  react ------------------------------------------------------------
{
    my @o;
    react { whenever Supply.from-list(1, 2, 3) { @o.push($_); done if $_ == 2 } };
    check @o, [1, 2], 'react blocks until done';
}
{
    my @o;
    react { whenever Supply.from-list(1, 2) { @o.push($_) }; @o.push('body-end') };
    check @o, ['body-end', 1, 2], 'the react body runs before its whenever events';
}
check ((try { react { die "in-react" } }) // $!.message), 'in-react', 'a die in react propagates';
{
    my $w = "";
    {
        CONTROL { when CX::Warn { $w = .message; .resume } };
        react { emit 1; whenever Supply.from-list(1) { } };
    }
    check $w, 'Useless use of emit in react', 'emit in react warns rather than dying';
}

# --- S-63/S-64  done outside a supply, and from a tapper's callback ---------
check ((try { (supply { emit 1; emit 2 }).tap({ done }) }) // 'no-throw').defined, True,
      'done from a plain tap callback is a run-time error';
{
    my @o;
    (supply { whenever Supply.from-list(1, 2, 3) { emit $_ } })
        .tap({ @o.push($_); done if $_ == 2 }, done => { @o.push('done') });
    check @o, [1, 2], 'done from the tapper ends the supply without its done callback';
}

# --- S-65  every tap re-runs the block independently ------------------------
{
    my $sup = supply { my $x = 0; whenever Supply.from-list(1, 2) { $x += $_; emit $x } };
    check ($sup.list, $sup.list), ((1, 3), (1, 3)), 'each tap gets a fresh activation';
}
{
    my @o;
    my $s = supply { whenever Supply.from-list(1, 2) { emit $_ } };
    $s.tap({ @o.push("a$_") });
    $s.tap({ @o.push("b$_") });
    check @o, ['a1', 'a2', 'b1', 'b2'], 'two taps run the block twice, in turn';
}

# --- S-66/S-67/S-68  completion and quit --------------------------------------
check (supply { emit 1; emit 2; done; emit 3 }).list, (1, 2), 'emit after done in the body is ignored';
{
    my @o;
    (supply { whenever Supply.from-list(1, 2) { emit $_ }; whenever Supply.from-list(3) { emit $_ } })
        .tap({ @o.push($_) }, done => { @o.push('done') });
    check @o, [1, 2, 3, 'done'], 'done when the body and every whenever are done';
}
{
    my @o;
    (supply {
        whenever Supply.from-list(1) { emit $_ };
        whenever Supply.from-list(2) { emit $_; done };
        whenever Supply.interval(10) { emit "never" }
    }).tap({ @o.push($_) }, done => { @o.push('done') });
    check @o, [1, 2, 'done'], 'a still-live source does not hold up an explicit done';
}
{
    my @o;
    (supply { whenever Supply.from-list(1, 2) { emit $_ }; whenever Supply.from-list(1, 2).map({ die "q" }) { } })
        .tap({ @o.push($_) }, quit => { @o.push('quit:' ~ .message) }, done => { @o.push('done') });
    check @o, [1, 2, 'quit:q'], "an unhandled whenever quit ends the whole supply";
}

if @fail {
    note "FAILED:\n" ~ @fail.map({ "  - $_" }).join("\n");
    exit 1;
}
say "PASS";
