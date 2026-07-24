# Regression: two react-scheduling fixes.
#   1. `done` inside a `whenever $supplier.Supply { … }` block closes the react —
#      the block runs on the emitting worker thread, so the react ctx has to be
#      re-pushed there (reactStack_ is thread-local). Before this, the most common
#      async pattern `react { whenever $data { …; done } }` hung forever.
#   2. `whenever Promise.in(N) { … }` is a real timer that fires ONCE after N
#      seconds as a live react source — it used to resolve at t=0, so a timeout
#      guard fired instantly and defeated itself.
# Contract: exit 0 + last line PASS.
my @fail;

# 1. done from a supplier whenever exits the react
{
    my $s = Supplier.new;
    my $got;
    start { sleep 0.2; $s.emit('hello'); }
    react { whenever $s.Supply -> $v { $got = $v; done; } }
    @fail.push("done-closes ($got)") unless $got eq 'hello';   # if this line runs, the react exited
}

# 2. a timeout guard: data arrives before the timeout, so the DATA branch wins
#    and the timer does NOT fire (it would have at t=0 before the fix)
{
    my $s = Supplier.new;
    my $winner = '';
    start { sleep 0.2; $s.emit('data'); }
    react {
        whenever $s.Supply -> $v { $winner = "data:$v"; done; }
        whenever Promise.in(4)  { $winner ||= 'timeout'; done; }
    }
    @fail.push("timeout-guard ($winner)") unless $winner eq 'data:data';
}

# 3. the timer path still works when nothing else arrives (fires, closes the react)
{
    my $fired = False;
    react { whenever Promise.in(0.3) { $fired = True; done; } }
    @fail.push('timer-fires') unless $fired;
}

if @fail { note "FAILED: @fail[]"; say 'FAIL' } else { say 'PASS' }
