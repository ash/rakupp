# Regression: a Planned Promise boolifies False; only Kept/Broken are True
# (Rakudo semantics). IO::Socket::Async::SSL's handshake pump relies on this —
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

if @fail { note "FAILED: @fail[]"; say 'FAIL' } else { say 'PASS' }
