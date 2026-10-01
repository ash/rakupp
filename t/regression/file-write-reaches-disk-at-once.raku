# Regression: a `:w` handle held its output in memory until `.close`, so another
# process (a `tail -f`, a file watcher) saw an empty file while the program ran,
# and Roast's watch-path.t saw no event for a write. Rakudo's file handle writes
# through (its `.out-buffer` reports 1); each write is now one write(2) on a
# descriptor the handle keeps, which `.close` closes.
# Contract: exit 0 + last line PASS.
my @fail;

my $f = $*TMPDIR.add("rakupp-write-through-$*PID");
LEAVE $f.unlink;

my $h = open $f, :w;
@fail.push("out-buffer {$h.out-buffer}") unless $h.out-buffer == 1;
$h.say("one");
@fail.push("not on disk after say: {$f.s}") unless $f.s == 4;
my $outside = run($*EXECUTABLE, '-e', "print '$f'.IO.slurp", :out).out.slurp(:close);
@fail.push("another process read {$outside.raku}") unless $outside eq "one\n";
$h.print("two");
$h.close;
@fail.push("content {$f.slurp.raku}") unless $f.slurp eq "one\ntwo";

my $a = open $f, :a;
$a.say("!");
@fail.push("append not on disk") unless $f.slurp eq "one\ntwo!\n";
$a.close;

@fail.push("out-buffer True") unless open($f, :w, :out-buffer).out-buffer == 8192;
@fail.push("out-buffer False") unless open($f, :w, :!out-buffer).out-buffer == 0;

say @fail ?? "FAIL: @fail.join('; ')" !! "PASS";
exit @fail ?? 1 !! 0;
