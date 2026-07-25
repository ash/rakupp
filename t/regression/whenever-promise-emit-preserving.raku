# Regression: async primitives that a Cro::HTTP::Client request exercises —
#   * `whenever $promise` in a supply block is ASYNC: an unkept promise stays
#     dormant (does NOT fire); a kept one fires the body with its RESULT (not the
#     promise object); a later-kept one fires when it settles.
#   * `whenever Promise.in(N)` in a supply block is a real timer (not immediate).
#   * `.emit` / `.take` inside a supply act on the topic ($_).
#   * Supplier::Preserving replays buffered values to a whenever that taps later.
# Contract: exit 0 + last line PASS.
my @fail;

# unkept promise must NOT fire; kept promise binds the value
{
    my $p = Promise.new;
    my @got;
    my $s = supply { whenever $p -> $v { @got.push("unkept:$v") }; whenever Promise.in(0.2) { done } };
    $s.tap(-> $x { @got.push($x) });
    sleep 1.5;
    @fail.push("unkept-fired (@got[])") if @got;
}
{
    my @got;
    supply { whenever Promise.kept(42) -> $v { emit $v } }.tap(-> $x { @got.push($x) });
    sleep 1;
    @fail.push("kept-value (@got[])") unless @got eqv [42];
}
# later-kept fires with the settled value
{
    my $p = Promise.new; my $v = $p.vow; my @got;
    supply { whenever $p -> $x { emit "s:$x" } }.tap(-> $y { @got.push($y) });
    start { sleep 0.15; $v.keep(9) };
    sleep 1.5;
    @fail.push("late-kept (@got[])") unless @got eqv ['s:9'];
}
# Promise.in in a supply block is a real timer (fires after the delay, not at t=0)
{
    my $fired-at;
    my $t0 = now;
    supply { whenever Promise.in(0.3) { $fired-at = now - $t0; emit 1 } }.tap(-> $ {});
    sleep 1.5;
    @fail.push("timer-immediate ($fired-at)") unless $fired-at.defined && $fired-at >= 0.2;
}
# `.emit` acts on the topic
@fail.push('dot-emit') unless (await Promise(supply { whenever Promise.kept(7) { .emit; done } })) == 7;

# Supplier::Preserving replays to a late whenever
{
    my $sup = Supplier::Preserving.new;
    $sup.emit('a'); $sup.emit('b');
    my @got;
    supply { whenever $sup.Supply -> $x { emit $x } }.tap(-> $y { @got.push($y) });
    sleep 1;
    @fail.push("preserving-replay (@got[])") unless @got eqv ['a', 'b'];
}

if @fail { note "FAILED: @fail[]"; say 'FAIL' } else { say 'PASS' }
