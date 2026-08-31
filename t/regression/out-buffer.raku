# Regression: IO::Handle.out-buffer — issue #51.
#
# `.out-buffer` did not exist. A program that asked for unbuffered output the
# documented way — `$*OUT.out-buffer = 0`, or `open $f, :w, :!out-buffer` —
# got no error and no effect: `say` sat in std::cout's block buffer until the
# process ended (so anything reading the pipe live saw nothing), and a file
# handle held every byte until .close. Two things had to change together:
# the size has to be honoured on every write, and for $*OUT / $*ERR it has to
# live somewhere process-wide, because reading the dynamic synthesizes a fresh
# handle each time and a write into that copy is gone by the next statement.
#
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) { @fail.push("$what: got {$got.raku} want {$want.raku}") unless $got eq $want }

my $dir = $*TMPDIR.add("rakupp-out-buffer-{$*PID}");
$dir.mkdir;
LEAVE { .unlink for $dir.dir; $dir.rmdir }

# 1. the standard handles carry a readable setting, and it sticks. Both default
#    to 0 (unbuffered), which is what Rakudo reports and how Rakudo behaves.
check($*ERR.out-buffer, 0, 'ERR-unbuffered');
check($*OUT.out-buffer, 0, 'OUT-unbuffered');
$*OUT.out-buffer = 8192;
check($*OUT.out-buffer, 8192, 'OUT-set-sticks');    # the fresh-copy trap
($*OUT, $*ERR).map: { .out-buffer = 0 }             # the form issue #51 used
check($*OUT.out-buffer, 0, 'OUT-set-through-topic');

# 2. False / True / Int all coerce the way the setter documents
my $f = $dir.add('coerce').open(:w);
$f.out-buffer = False; check($f.out-buffer, 0,    'False-is-none');
$f.out-buffer = True;  check($f.out-buffer > 0, True, 'True-is-default-size');
$f.out-buffer = 4096;  check($f.out-buffer, 4096, 'Int-is-the-size');
$f.close;

# 3. :!out-buffer at open — every write lands at once, before any close
my $log = $dir.add('log');
my $fh = open $log, :w, :!out-buffer;
check($fh.out-buffer, 0, 'open-negated-adverb');
$fh.say('first');
check($log.slurp, "first\n", 'unbuffered-write-is-on-disk');
$fh.say('second');
check($log.slurp, "first\nsecond\n", 'unbuffered-second-write');
$fh.close;
check($log.slurp, "first\nsecond\n", 'close-adds-nothing-twice');

# 4. a sized buffer holds back what fits and lets through what does not
#    (roast S32-io/out-buffering.t's rule: a write that would overflow flushes
#    what is pending first, and goes straight out if it is a bufferful itself)
my $sized = $dir.add('sized');
my $sh = open $sized, :w, :out-buffer(10);
$sh.print('x' x 15);
check($sized.s, 15, 'over-buffer-writes-through');
$sh.print('x' x 4);
check($sized.s, 15, 'under-buffer-is-held');
$sh.flush;
check($sized.s, 19, 'flush-empties');
$sh.print('y' x 4);
$sh.out-buffer = 1000;                              # the resize itself flushes
check($sized.s, 23, 'resize-flushes');
$sh.close;

# 5. rakupp's DEFAULT still buffers, and that is deliberate — Rakudo writes
#    through on every call (its $*OUT reports 0 and a fresh handle reports 1),
#    which costs a syscall per print. The knob above is the documented way to
#    ask for that; the default is not changed under programs that never do.
my $dflt = $dir.add('dflt');
my $dh = open $dflt, :w;
check($dh.out-buffer > 0, True, 'file-default-buffered');
$dh.say('held');
check($dflt.s, 0, 'default-holds-until-close');
$dh.close;
check($dflt.slurp, "held\n", 'close-writes');

if @fail { note "FAILED: @fail.join('; ')"; say 'FAIL'; exit 1 } else { say 'PASS' }
