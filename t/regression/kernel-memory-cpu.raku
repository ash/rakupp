# Regression: $*KERNEL.total-memory, .free-memory and .cpu-usage (#279).
#
# None of the three had an arm of its own, so each fell to Kernel's lenient
# accessor and answered the kernel's name — "linux", "darwin" — where Rakudo
# answers Ints: bytes of physical and free memory, and the process's user +
# system CPU time in microseconds.
#
# Contract: exit 0 + last line PASS.
my @fail;
sub check($ok, $what) { @fail.push($what) unless $ok }
my $total = $*KERNEL.total-memory;
my $free  = $*KERNEL.free-memory;
check $total ~~ Int, "total-memory is an Int (got {$total.raku})";
check $free ~~ Int, "free-memory is an Int (got {$free.raku})";
check $total > 0, 'total-memory is positive';
check 0 < $free <= $total, 'free-memory is positive and at most total';
my $x = 0; $x += $_ for ^200_000;   # burn some CPU
my $cpu = $*KERNEL.cpu-usage;
check $cpu ~~ Int && $cpu > 0, "cpu-usage is a positive Int (got {$cpu.raku})";
check $*KERNEL.cpu-usage >= $cpu, 'cpu-usage does not go backwards';

if @fail { .say for @fail; say 'FAIL'; exit 1 }
say 'PASS';
