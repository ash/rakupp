# Regression: a `.then` callback registered on a pending promise runs on a
# worker of its own once the promise settles — not inline on the SETTLER's
# thread. Inline, an `await` in the callback could wait for the settler, which
# was blocked in the callback: Cro's client keeps the response promise from the
# socket reader, and `.then({ await .result.body })` waited for body bytes only
# that reader could deliver. A second shape: an event worker (a timer, a
# `.then`) that awaits in parallel mode must let go of the lock event workers
# share, or the worker that would settle its promise can never run.
# Contract: exit 0 + last line PASS. A hang is a failure too.
my @fail;
sub within($p, $secs = 5) { await Promise.anyof($p, Promise.in($secs)); $p.status ~~ Kept ?? $p.result !! 'TIMEOUT' }

# the settler goes on to keep what the callback awaits
{
    my $p = Promise.new; my $pv = $p.vow;
    my $q = Promise.new; my $qv = $q.vow;
    my $r = $p.then({ await $q; 'then-saw-' ~ $q.result });
    start { $pv.keep(1); $qv.keep(2) };
    my $got = within($r);
    @fail.push("settler: $got") unless $got eq 'then-saw-2';
}

# two event workers: one awaits, the other must still get to run and keep it
{
    my $x = Promise.new; my $xv = $x.vow;
    my $a = Promise.in(0.05).then({ await $x; 'a' ~ $x.result });
    my $b = Promise.in(0.2).then({ $xv.keep(1); 'b' });
    my $ga = within($a); my $gb = within($b);
    @fail.push("timers: $ga $gb") unless $ga eq 'a1' && $gb eq 'b';
}

# several callbacks in flight at once, each awaiting a later promise
{
    my @outer = (^3).map: { Promise.new };
    my @inner = (^3).map: { Promise.new };
    my @r = (^3).map: -> $i { @outer[$i].then({ await @inner[$i]; $i * 10 + @inner[$i].result }) };
    start { .keep(0) for @outer; sleep 0.05; @inner[$_].keep($_) for ^3 };
    my $all = within(Promise.allof(@r));
    @fail.push("fan-out") unless $all && @r.map(*.result).List eqv (0, 11, 22);
}

if @fail { note "FAILED: @fail[]"; say 'FAIL' } else { say 'PASS' }
