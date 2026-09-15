# Rakudo's Proc is a class you may build BEFORE running anything: `Proc.new(:out)`
# chooses what the eventual run captures, and `.shell`/`.spawn` run it. Here a
# Proc existed only as run()'s answer, so `Proc.new` was a missing method and
# Clipboard — which reads the pasteboard with exactly this three-line shape —
# could not get past its first call.
use Test;
plan 4;

my $proc = Proc.new(:out);
isa-ok $proc, Proc,                     'Proc.new answers a Proc';

$proc.shell('echo hello');
is $proc.out.slurp(:close).trim, 'hello', '…whose .shell captures for .out';
is $proc.exitcode, 0,                   '…and records the exit code';

my $failed = Proc.new(:out);
$failed.shell('exit 3');
is $failed.exitcode, 3,                 'a non-zero exit is reported as itself';
