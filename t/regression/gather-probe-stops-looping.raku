# A gather probe that stops TAKING must still stop LOOPING.
#
#     my $evens = gather for 1 .. * { take $_ if 45 < $_ < 55 };
#     say $evens[^5];
#
# takes nine elements and then loops forever without taking again. The probe
# bounds itself two ways — a 64-take cap and a 20ms budget — but BOTH were
# only ever consulted from `take`, so a block that ran dry consulted neither
# again: DECLARING the gather never returned, and the `say` never ran.
# Fixed by checking the budget once per loop iteration too (gatherProbePoint,
# called from runLoopBody next to safePoint).
#
# NOTE: if this ever regresses it HANGS rather than failing — that is the bug.

my $evens = gather for 1 .. * { take $_ if 45 < $_ < 55 };
die "prefix was {$evens[^5].join(',')}" unless $evens[^5].join(',') eq '46,47,48,49,50';

# the same shape with an explicit block, read one element at a time
my $few = gather { for 1 .. * { take $_ if $_ < 5 } };
die "first was {$few[0]}" unless $few[0] == 1;
die "third was {$few[2]}" unless $few[2] == 3;

# a `while` that runs dry the same way
my $wh = gather { my $i = 0; while True { $i++; take $i if $i %% 7 && $i < 30 } };
die "while gather gave {$wh[^4].join(',')}" unless $wh[^4].join(',') eq '7,14,21,28';

# …and a `loop`
my $lp = gather { my $i = 0; loop { $i++; take $i if $i < 4 } };
die "loop gather gave {$lp[^3].join(',')}" unless $lp[^3].join(',') eq '1,2,3';

# a gather that keeps taking is untouched: it fills the take cap as before
my $all = gather { for 1 .. * { take $_ } };
die "endless gather gave {$all[^5].join(',')}" unless $all[^5].join(',') eq '1,2,3,4,5';

# …and a finite one is still eager and complete
my $ten = gather { for 1 .. 10 { take $_ } };
die "finite gather has {$ten.elems} elems" unless $ten.elems == 10;
die "finite gather gave {$ten.join(',')}"  unless $ten.join(',') eq '1,2,3,4,5,6,7,8,9,10';

say "PASS";
