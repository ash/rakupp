# Regression: issue #41 (2026-11-10).
#
#   `sleep(333)` returned after one second: every timed wait was capped at
#   1 s. sleep, sleep-timer, sleep-until and Promise.in/at must wait the time
#   asked for, past one second, and a timer Promise is Planned until then.
#
# Expected answers are Rakudo 2026.09's (2026-11-10).

my $fails = 0;
sub check($got, $want, $what) {
    return if $got eqv $want;
    $fails++;
    say "not ok - $what\n  got:  {$got.raku}\n  want: {$want.raku}";
}

# Everything here waits about 1.3 s at once, so the file stays under 2 s: the
# timers start first and the main thread's sleep runs beside them.
my $t0 = now;
my $in = Promise.in(1.3);
my $at = Promise.at($t0 + 1.3);
my $th = start { my $t = now; sleep-until($t + 1.3); now - $t };
check $in.status, Planned, 'Promise.in(1.3) is Planned at once';
check $at.status, Planned, 'Promise.at(now + 1.3) is Planned at once';
sleep 1.3;
my $slept = now - $t0;
check $slept >= 1.25, True, 'sleep 1.3 sleeps past the old 1 s cap';
await Promise.anyof(Promise.allof($in, $at), Promise.in(5));
check ($in.status, $at.status), (Kept, Kept), 'both timer promises are kept';
check (await $th) >= 1.25, True, 'sleep-until on a thread waits the whole time';
check (now - $t0) < 1.25 * 3, True, 'the waits overlapped';

my $t1 = now; sleep(1/10); check now - $t1 >= 0.09, True, 'sleep of a Rat';
my $t2 = now; sleep('0.1'); check now - $t2 >= 0.09, True, "sleep of a Str ('…')";
my $t3 = now; sleep("0.1"); check now - $t3 >= 0.09, True, 'sleep of a Str ("…")';

if $fails { say "$fails failure(s)"; exit 1 }
say 'PASS';
