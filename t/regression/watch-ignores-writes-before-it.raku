# Regression: a directory watch reports changes made after it starts, not the
# writes just before. On macOS the FSEvents stream ("since now") still delivered
# a file written a moment before the watch began, so cro's Runner — which copies
# a service directory and then watches it — saw its own `.cro.yml` as changed,
# restarted the service at once, and cro's t/tools-runner.rakutest failed, which
# made `rakupp install cro` refuse to install. Rakudo (libuv, also FSEvents)
# shows the same leftover now and then; Raku++ now drops it.
# Contract: exit 0 + last line PASS.

my $base = $*TMPDIR.add("rakupp-watch-before-{(^1_000_000_000).pick}");
mkdir $base;
my $d = $base.add('w');
mkdir $d;
$d.add('before.txt').spurt('x');            # written BEFORE the watch
$base.add('moved.txt').spurt('m');

my %seen;
my $lock = Lock.new;
$d.watch.tap: { $lock.protect: { %seen{.path.IO.basename}++ } };

sleep 0.5;
$d.add('after.txt').spurt('y');             # written AFTER the watch
$base.add('moved.txt').rename($d.add('moved.txt'));
sleep 2;

my @fail;
$lock.protect: {
    @fail.push("reported a write from before the watch") if %seen<before.txt>;
    @fail.push("missed a write after the watch")         unless %seen<after.txt>;
    @fail.push("missed a file moved in")                 unless %seen<moved.txt>;
}

$d.add($_).unlink for <before.txt after.txt moved.txt>;
$d.rmdir;
$base.rmdir;

if @fail { .say for @fail; say "FAIL" } else { say "PASS" }
exit 0;
