# Regression: the Supply factories and the coercers — where a supply comes from
# and what it turns into. Sections B and E of
# docs/dev/findings/semantics/Supply.md (S-15, S-17, S-49 to S-52), extracted
# from Rakudo 2026.08 and implemented here from the sheet.
#
# What was wrong before: `Supply.on-demand` did not exist; `Supply.interval`
# emitted nothing when its list was asked for, because a kind-based supply has
# no values until something drives it; `.wait` answered True instead of the last
# value; `.list` on a live Supply lost every value emitted between the call and
# the read; `.Seq` answered a List; and `await` on a supply that quit handed
# back Any without raising anything.
#
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $desc) {
    @fail.push("$desc: got {$got.raku}, want {$want.raku}") unless $got eqv $want;
}

# --- S-15  on-demand: a producer per tap, run synchronously ------------------
{
    my @o;
    Supply.on-demand(-> $p { $p.emit(1); $p.emit(2); $p.done }).tap({ @o.push($_) }, done => { @o.push('D') });
    check @o, [1, 2, 'D'], 'the producer runs on the tapping thread, before .tap returns';
}
{   # `$p.done` ends the SUPPLY, not the producer: the block runs on, its later
    # emits go nowhere
    my @o;
    Supply.on-demand(-> $p { $p.emit(1); $p.done; @o.push('after-done'); $p.emit(2) })
          .tap({ @o.push($_) }, done => { @o.push('D') });
    check @o, [1, 'D', 'after-done'], 'done inside the producer does not unwind it';
}
{
    my $n = 0;
    my $s = Supply.on-demand(-> $p { $n++; $p.emit($n); $p.done });
    $s.list; $s.list;
    check $n, 2, 'each tap runs the producer afresh';
}
{
    my $closed = 0;
    Supply.on-demand(-> $p { $p.emit(1); $p.done }, closing => { $closed++ }).tap({;});
    check $closed, 1, ':closing runs when done closes the tap';
}
{
    my $closed = 0;
    my $t = Supply.on-demand(-> $p { $p.emit(1) }, closing => { $closed++ }).tap({;});
    $t.close;
    check $closed, 1, ':closing runs when the tap is closed';
}
{
    my @o;
    Supply.on-demand(-> $p { die "prod" }).tap({;}, quit => { @o.push('quit:' ~ .message) });
    check @o, ['quit:prod'], 'a producer that dies is the supply quitting';
}
{
    my @o;
    Supply.on-demand(-> $p { $p.emit(1); $p.quit("qq") })
          .tap({ @o.push($_) }, quit => { @o.push('q:' ~ .message) });
    check @o, [1, 'q:qq'], 'the producer can quit its own supply';
}
check Supply.on-demand(-> $p { $p.done }).live, False, 'on-demand is not live';
check Supply.on-demand(-> $p { $p.done }).serial, True, 'on-demand is serial';

# --- S-17  interval ---------------------------------------------------------
check Supply.interval(0.02).head(3).list, (0, 1, 2), 'interval counts up from 0';
{   # the first value arrives after :delay, not after a whole interval
    my $t0 = now;
    Supply.interval(1, 0.05).head(1).list;
    check ((now - $t0) < 0.5), True, 'the first value waits :delay, not :every';
}

# --- S-49  Channel ----------------------------------------------------------
check Supply.from-list(1, 2).Channel.list, (1, 2), 'a supply drains into a Channel';
{   # done closes the channel, so a reader that walks it finishes
    my $r = Supplier.new;
    my $c = $r.Supply.Channel;
    $r.emit(1); $r.emit(2); $r.done;
    check $c.list, (1, 2), 'the channel closes when the supply is done';
}

# --- S-50  list / Seq: subscribe now, and answer the right type -------------
{
    my $sup = Supplier.new;
    my $l = $sup.Supply.list;
    $sup.emit(1); $sup.emit(2); $sup.done;
    check $l.List, (1, 2), 'the subscription is made when .list is called, not when it is read';
}
check Supply.from-list(1, 2).list.is-lazy, False, 'the list is not lazy';
check Supply.from-list(1, 2).Seq.^name,  'Seq',  '.Seq is a Seq';
check Supply.from-list(1, 2).list.^name, 'List', '.list is a List';

# --- S-51  Promise, wait, await: the LAST value -----------------------------
check Supply.from-list(1, 2, 3).wait, 3, '.wait answers the last value';
check Supply.from-list().wait, Nil, '.wait on an empty supply answers Nil';
check (await Supply.from-list(1, 2)), 2, 'await answers the last value';
check Supply.from-list(1, 2).Promise.result, 2, '.Promise keeps with the last value';

# --- S-52  a supply that quits has no value to await ------------------------
# (Rakudo hands back Any and records no error here; that is the sheet's flagged
# bug and it is not imitated.)
{
    my $r = try { await Supply.from-list(1).map({ die "w" }) };
    check ($! ?? $!.message !! 'no-error'), 'w', 'await raises the quit that ended the supply';
}
{
    my $r = try { Supply.from-list(1).map({ die "w" }).wait };
    check ($! ?? $!.message !! 'no-error'), 'w', '.wait raises it too';
}

if @fail {
    note "FAILED:\n" ~ @fail.map({ "  - $_" }).join("\n");
    exit 1;
}
say "PASS";
