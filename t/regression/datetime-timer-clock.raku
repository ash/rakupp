# A DateTime and the Instant naming the same moment must reach every timer as
# the same moment. They did not: `now` and `.Instant` carry kInstantEpochOffset
# (BuiltinsShared.h) and a DateTime's numeric value is raw POSIX, so anything
# comparing a DateTime straight against the `now` clock saw it ten seconds in
# the past. sleep-until(DateTime) returned False without waiting and
# Promise.at(DateTime) fired at once, while the Instant spelling of the SAME
# moment waited correctly — a silent ten-second disagreement between two
# spellings of one thing.
#
# Roast covers sleep-until (S29-context/sleep.t); the Promise and Scheduler
# paths are only covered here.
my $manual-init = INIT now;   # must be the FIRST statement: rakupp hoists an
                              # INIT only when nothing precedes it (a divergence
                              # from Rakudo, pre-existing and tracked separately
                              # in docs/dev/findings/TRIAGE.md) — putting this
                              # below the waits below would measure THAT, not the
                              # clock this file is about.
my $fail = 0;
sub check($ok, $what) { $fail++ unless $ok; say ($ok ?? "ok   " !! "FAIL ") ~ $what }

# $*INIT-INSTANT is on the `now` clock, not raw POSIX
my $skew = ($*INIT-INSTANT - $manual-init).abs;
check($skew < 5, "\$*INIT-INSTANT agrees with INIT now (skew {$skew.round(0.001)}s)");

# the two spellings agree on where the moment IS
my $d = DateTime.now.later(:seconds(2));
my $gap = $d.Instant - now;
check(1.5 < $gap < 2.5, "DateTime.Instant is ~2s ahead of now (got {$gap.round(0.01)})");

# sleep-until, both spellings
my $t0 = now;
my $slept = sleep-until $d;
my $e0 = now - $t0;
check($slept, "sleep-until(DateTime) returns True");
check(1.5 < $e0 < 3.5, "sleep-until(DateTime) actually waited (got {$e0.round(0.01)}s)");

my $t1 = now;
check(!sleep-until($t1 - 5), "sleep-until(Instant in the past) returns False without waiting");
check((now - $t1) < 0.5, "  ...and did not wait");

# Promise.at, both spellings
my $t2 = now;
await Promise.at(DateTime.now.later(:seconds(2)));
my $e2 = now - $t2;
check(1.5 < $e2 < 3.5, "Promise.at(DateTime) waits (got {$e2.round(0.01)}s)");

my $t3 = now;
await Promise.at(now + 2);
my $e3 = now - $t3;
check(1.5 < $e3 < 3.5, "Promise.at(Instant) waits (got {$e3.round(0.01)}s)");

say $fail == 0 ?? "PASS" !! "FAIL ($fail)";
