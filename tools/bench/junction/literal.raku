# The expression as actually written: build AND match, every iteration.
my $reps = 300000;
my int $c = 0;
my $t0 = now;
for ^$reps { $c++ if 5 ~~ 1 | 3 | 5 }
my $build_and_match = (now - $t0) / $reps;

my $j = 1 | 3 | 5;
$t0 = now;
my int $d = 0;
for ^$reps { $d++ if 5 ~~ $j }
my $match_only = (now - $t0) / $reps;

$t0 = now;
for ^$reps { my $x = 1 | 3 | 5 }
my $build_only = (now - $t0) / $reps;

$t0 = now;
for ^$reps { }
my $loop = (now - $t0) / $reps;

printf "empty loop iteration      %7.3f us\n", $loop*1e6;
printf "build 1|3|5 only          %7.3f us  (net %6.3f)\n", $build_only*1e6, ($build_only-$loop)*1e6;
printf "match against prebuilt    %7.3f us  (net %6.3f)\n", $match_only*1e6, ($match_only-$loop)*1e6;
printf "5 ~~ 1|3|5 as written     %7.3f us  (net %6.3f)  hits=%d\n", $build_and_match*1e6, ($build_and_match-$loop)*1e6, $c;
