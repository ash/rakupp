# Regression: `$supplier.Supply.Channel.Supply` delivers through the channel,
# on another thread — not on the emitter's, as it did when a live channel's
# `.Supply` handed back a Supply on the same supplier. Rakudo's channel supply
# runs on the scheduler, and code relies on the hop: Cro's WebSocket handler
# feeds the user's block through `.Channel.Supply`, and that block awaits a
# message body whose final fragment the emitter (the frame parser) delivers
# only after the emit returns. Values still arrive in order, done follows them.
# Contract: exit 0 + last line PASS. A hang is a failure too.
my @fail;
sub within($p, $secs = 5) { await Promise.anyof($p, Promise.in($secs)); $p.status ~~ Kept ?? $p.result !! 'TIMEOUT' }

# the Cro shape: the body awaits what the emitter settles after emitting
{
    my $s = Supplier.new;
    my @p = (^3).map: { Promise.new };
    my $feed = $s.Supply.Channel.Supply;
    my $out = supply { whenever $feed -> $i { emit $i * 100 + await @p[$i] } };
    my @got; my $all = Promise.new; my $av = $all.vow;
    $out.tap(-> $v { @got.push($v); $av.keep(True) if @got == 3 });
    start { for ^3 -> $i { $s.emit($i); @p[$i].keep($i + 1) } };
    within($all);
    @fail.push("await in body: @got[]") unless @got eqv [1, 102, 203];
}

# the emitter does not wait for the tap
{
    my $s = Supplier.new;
    my $gate = Promise.new;
    my @log;
    $s.Supply.Channel.Supply.tap(-> $v { await $gate; @log.push("got $v") });
    my $w = start { $s.emit(1); @log.push('emit returned'); $gate.keep(True) };
    within($w);
    sleep 0.3;
    @fail.push("order: @log[]") unless @log eqv ['emit returned', 'got 1'];
}

# order, and done after the values
{
    my $s = Supplier.new;
    my @log; my $fin = Promise.new; my $fv = $fin.vow;
    $s.Supply.Channel.Supply.tap(-> $v { @log.push($v) }, done => { @log.push('done'); $fv.keep(True) });
    $s.emit($_) for 1..5;
    $s.done;
    within($fin);
    @fail.push("sequence: @log[]") unless @log eqv [1, 2, 3, 4, 5, 'done'];
}

if @fail { note "FAILED: @fail[]"; say 'FAIL' } else { say 'PASS' }
