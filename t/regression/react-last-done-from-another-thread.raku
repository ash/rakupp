# Regression: `done` inside a react whenever's LAST did not end the react when
# the source was a Supplier finishing on another thread: the LAST ran there,
# where no react was current, and the `done` found nothing to end. Cro's
# WebSocket client ends its messages that way when the server closes, and a
# client's `whenever $conn.messages { … LAST done }` waited out its timeout
# (Cro::RPC::JSON's t/070-errors.rakutest).
#
# Expectations checked against Rakudo 2026.09
# (~/.rakubrew/versions/moar-2026.09/install/bin/rakudo). Green on both.

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

for 'block', 'statement' -> $form {
    my $s = Supplier::Preserving.new;
    start { sleep 0.1; $s.emit(1); $s.done };
    my $timed-out = False;
    my $t0 = now;
    if $form eq 'block' {
        react {
            whenever $s.Supply { LAST { done } }
            whenever Promise.in(3) { $timed-out = True; done }
        }
    }
    else {
        react {
            whenever $s.Supply { LAST done }
            whenever Promise.in(3) { $timed-out = True; done }
        }
    }
    ck $timed-out, False, "`done` in a LAST ({$form} form) ends the react";
}

say $fails ?? "FAIL ($fails)" !! 'PASS';
exit $fails ?? 1 !! 0;
