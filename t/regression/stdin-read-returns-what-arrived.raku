# Regression: `.read(N)` on stdin, or on a handle opened on a FIFO/terminal,
# returns what has arrived once at least one byte has — issue #121.
#
# `$*IN.read(10)` waited for all ten bytes, and `open("/dev/tty").read(10)` never
# returned at all: a handle on a path was slurped to its end first, and a
# terminal has none. Rakudo does one read(2), which is how Terminal::UI reads a
# key (or a whole arrow-key escape sequence) with one call. A terminal needs a
# pty to test; a pipe has the same shape.
#
# Contract: exit 0 + last line PASS. Runs unchanged under Rakudo.
my @fail;
sub ck($got, $want, $what) { @fail.push("$what: got {$got.raku}") unless $got eqv $want }

# the writer sends one byte, then waits 3 s before the rest: a read that waits
# for its full count reports 2 bytes after 3 s, one that does not, 1 at once
sub first-read(Str $code) {
    my $exe = $*EXECUTABLE.absolute;
    my $p = run 'sh', '-c', "(printf q; sleep 3; printf rest) | '$exe' -e '$code'", :out;
    my ($n, $secs) = $p.out.slurp(:close).words;
    $n eq '1' && $secs < 2 ?? 'one byte, at once' !! "$n byte(s) after {$secs}s"
}

ck first-read('my $t = now; my $b = $*IN.read(10); say $b.elems, " ", now - $t'),
   'one byte, at once', '$*IN.read(10) with one byte there';
ck first-read('my $t = now; my $f = open "/dev/stdin", :bin; my $b = $f.read(10); say $b.elems, " ", now - $t'),
   'one byte, at once', 'a handle on a FIFO path';

# …and on a regular file it still reads the whole count
my $file = $*TMPDIR.add("read-count-{$*PID}");
$file.spurt: 'abcdefghijkl';
my $fh = open $file, :bin;
ck $fh.read(10).elems, 10, 'a file reads the full count';
ck $fh.read(10).elems, 2, '…then the rest';
$fh.close;
$file.unlink;

# a pipe that has everything up front: one read takes all of it, up to the count
my $p = run $*EXECUTABLE, '-e', 'say $*IN.read(4).elems; say $*IN.read(10).elems; say $*IN.read(10).elems', :in, :out;
$p.in.spurt('abcdefgh', :close);
ck $p.out.slurp(:close).lines.join(','), '4,4,0', 'a pipe with the data already there';

if @fail { .say for @fail; say 'FAIL'; exit 1 }
say 'PASS';
