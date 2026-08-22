# Regression: `IO::Handle.spurt($content, :close)` wrote nothing.
#
# An open handle buffers its writes and flushes on .close. `.spurt` appended to
# that buffer and then ignored `:close`, so the handle was never flushed and a
# caller that slurped the path straight back read an empty file. File::Temp's
# own suite is written exactly that way — `$fh.spurt($text, :close)` and then
# read the path — which is how it surfaced.
#
# Two halves, and the second is the one the first patch got wrong: the content
# is positional and `:close` follows it, so the argument scan must not stop at
# the first positional.
my $ok = 0; my $n = 0;
sub ck($got, $want, $desc) {
    $n++;
    if $got eqv $want { $ok++ }
    else { say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

my $p = $*TMPDIR.add("spurt-close-$*PID.txt");
LEAVE { $p.unlink if $p.e }

$p.open(:w).spurt("two\nlines\n", :close);
ck($p.slurp, "two\nlines\n", 'spurt(:close) reaches the file at once');
ck($p.slurp.lines.elems, 2, '…with every line of it');

# :close after the content, and the content still binds
$p.open(:w).spurt('short', :close);
ck($p.slurp, 'short', 'a second :close spurt truncates rather than appends');

# without :close the content still arrives — on .close at the latest. (WHEN it
# arrives before that is an implementation detail and differs: rakupp buffers
# until the flush, Rakudo writes through. This file asserts the contract both
# engines keep, so it can be run under either as an oracle.)
my $fh = $p.open(:w);
$fh.spurt('buffered');
$fh.close;
ck($p.slurp, 'buffered', 'without :close, .close still flushes it');

# :close(False) is not a close
my $fh2 = $p.open(:w);
$fh2.spurt('later', :close(False));
$fh2.close;
ck($p.slurp, 'later', ':close(False) leaves the flush to .close');

say $ok == $n ?? 'PASS' !! "FAIL ($ok/$n)";
