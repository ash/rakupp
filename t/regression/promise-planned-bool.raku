# Regression: a Planned Promise boolifies False; only Kept/Broken are True
# (Rakudo semantics), for a `start` promise, a timer and a combinator too. IO::Socket::Async::SSL's handshake pump relies on this —
# `elsif $!connected-promise` must be false while the handshake is still
# negotiating, so the `orwith` branch that drives SSL_connect is taken.
# (The related async-socket client-read fix — `.tap` on an async read Supply now
#  spawns the read worker — isn't covered here: it needs an external server, so
#  it can't run reliably in a single-process regression file.)
# Contract: exit 0 + last line PASS.
my @fail;

@fail.push('planned-true')  if ?Promise.new;                 # Planned → False
@fail.push('kept-false')    unless ?Promise.kept(1);         # Kept    → True

# boolean context flips from False (Planned) to True (Kept)
{
    my $p = Promise.new;
    @fail.push('if-planned') if $p;
    $p.keep(1);
    @fail.push('if-kept') unless $p;
}
# a broken promise is also True
{
    my $p = Promise.new;
    $p.break('nope');
    @fail.push('if-broken') unless $p;
}
# a `start` promise keeps its state in its PromiseState, not in the stored
# status, and boolified False for ever (2026-10-09), so `until $w { … }` never
# ended. Kept or broken it is True; still running, False.
{
    my $w = start { 1 };
    await $w;
    @fail.push('start-kept') unless $w;
    my $b = start { die 'nope' };
    try await $b;
    @fail.push('start-broken') unless $b;
    my $gate = Promise.new;
    my $r = start { await $gate; 1 };
    @fail.push('start-running') if $r;
    $gate.keep;
    my $spins = 0;
    until $r { last if ++$spins > 1_000_000 }
    @fail.push('until-start') unless $r;
}
# a timer and a combinator keep no status either: they are folded on the spot
{
    my $t = Promise.in(0.05);
    my $any = Promise.anyof($t);
    @fail.push('timer-early') if $t;
    @fail.push('anyof-early') if $any;
    sleep 0.2;
    @fail.push('timer-due') unless $t;
    @fail.push('anyof-due') unless $any;
}

if @fail { note "FAILED: @fail[]"; say 'FAIL' } else { say 'PASS' }
