# Regression: a head(n)/first tap that reaches its limit must fire `done` exactly
# ONCE. The source completing afterward re-fired `done` on the already-closed tap
# (the Supplier `done` handler didn't skip closed taps) — visible with 2+ taps on
# a `.head` supply as a doubled done. Surfaced building Supply.Promise for Cro.
# Contract: exit 0 + last line PASS.
my @fail;

# double-tap a head(1) Supplier supply: each tap's done fires exactly once
{
    my @events;
    my $f = Supplier.new;
    my $s = $f.Supply.head(1);
    $s.tap(-> $v { @events.push("T1v$v") }, done => { @events.push('T1done') });
    $s.tap(-> $v { @events.push("T2v$v") }, done => { @events.push('T2done') });
    start { $f.emit(1); $f.emit(2); $f.done }
    sleep 0.5;
    @fail.push("t1-done ({@events.grep('T1done').elems})") unless @events.grep('T1done').elems == 1;
    @fail.push("t2-done ({@events.grep('T2done').elems})") unless @events.grep('T2done').elems == 1;
    @fail.push('values') unless @events.grep(*.starts-with('T1v')) eqv ['T1v1'];
}

# single-tap head still delivers one value + one done
{
    my @res; my $done = 0;
    my $f = Supplier.new;
    $f.Supply.head(2).tap(-> $v { @res.push($v) }, done => { $done++ });
    start { $f.emit(10); $f.emit(20); $f.emit(30); $f.done }
    sleep 0.5;
    @fail.push("single-done ($done)") unless $done == 1;
    @fail.push("single-res (@res[])") unless @res eqv [10, 20];
}

if @fail { note "FAILED: @fail[]"; say 'FAIL' } else { say 'PASS' }
