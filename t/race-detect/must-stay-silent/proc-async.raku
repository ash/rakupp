# race-detect: silent
# edges: promise
# expect: 1 2
#
# Proc::Async's output taps run on reader threads; the process promise is
# kept once its output is done, so what the taps wrote is ordered before
# what follows the `await`.
my @lines;
my $proc = Proc::Async.new($*EXECUTABLE, '-e', 'say 1; say 2');
$proc.stdout.lines.tap({ @lines.push($_) });
await $proc.start;
say @lines.join(' ');
