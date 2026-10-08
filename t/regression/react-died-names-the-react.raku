# Regression: an error that leaves a `react` does X::React::Died, and its gist
# names the react it ended.
#
# Only a whenever'd source that QUIT got the role, and nothing recorded where
# the react stood: the gist was the error's own chain, which names the react
# only when the body that died ran on the react's thread. Under load, Roast's
# S17-supply/syntax-nonblocking-await.t had its `Supply.interval` body die on a
# worker, and "Exception report contains react location" failed (4 of 12 runs
# on a RISC-V board with eight busy loops). A whenever body dying on the
# react's own thread, and the react body itself dying, did not get the role.
#
# Now `react` captures its own frames, and any error leaving it gets the role
# and those frames; the gist adds them under "In the react block at:". What a
# CATCH inside the react sees is the error as raised. Every check is what
# Rakudo does, except the label (Rakudo prints its own wrapper; nothing
# asserts that text).
# Contract: exit 0 + last line PASS.
my @fail;
sub check($cond, $what) { @fail.push($what) unless $cond }

# a body on another thread: the supply's whenever runs where `$s.emit` is
# called, in the `start`, so the error's own frames hold nothing of the react
sub death() { die "boom" }
sub from-elsewhere() {
    my $s = Supplier.new;
    supply {
        whenever $s.Supply { death() if $_ == 2 }
        start { sleep 0.05; $s.emit(1); $s.emit(2) }
    }
}
sub on-worker() {
    react { whenever from-elsewhere() { } }
}
{
    on-worker();
    CATCH { default {
        check .does(X::React::Died), 'worker death does X::React::Died';
        check .message eq 'boom', 'worker death keeps its message';
        check .gist.contains("boom"), "worker death: gist has the message";
        check .gist.contains("sub death"), "worker death: gist has where it died";
        check .gist.contains('on-worker'), 'worker death: gist names the react';
        check $_ ~~ X::AdHoc, 'worker death is still an X::AdHoc';
    } }
}

# a whenever body dying on the react's own thread
sub in-whenever() {
    react { whenever Supply.from-list(1, 2) { die "sync death" if $_ == 2 } }
}
{
    in-whenever();
    CATCH { default {
        check .does(X::React::Died), 'whenever death does X::React::Died';
        check .message eq 'sync death', 'whenever death keeps its message';
        check .gist.contains('in-whenever'), 'whenever death: gist names the react';
    } }
}

# the react body itself
{
    react { die "body death" }
    CATCH { default {
        check .does(X::React::Died), 'body death does X::React::Died';
        check .message eq 'body death', 'body death keeps its message';
    } }
}

# a CATCH inside the react sees the error as raised, and the react goes on
{
    my $saw = '';
    react {
        whenever Supply.from-list(1) {
            die "inner";
            CATCH { default { $saw = .^name } }
        }
    }
    check $saw eq 'X::AdHoc', "a CATCH inside the react sees the plain error (got $saw)";
}

# `done` is no error
{
    my $ok = False;
    react { whenever Supply.from-list(1) { done } }
    $ok = True;
    check $ok, 'done leaves the react without an error';
}

if @fail { note "FAILED: @fail.join('; ')"; say 'FAIL' } else { say 'PASS' }
