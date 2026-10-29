# Regression: `.read($n)` on a handle reads about $n bytes of the file, not all
# of it, and `.seek`, `.tell`, `.eof` and `.slurp(:bin)` agree with it
# (issue #176).
#
# A handle on a path is buffered here, and its first `.read` loaded the whole
# file into it and served every read from that copy: `.read(8)` of a 1 GB file
# took 5 s and 2 GB, `.seek` and `.eof` loaded it too. A handle opened to read a
# regular file reads with pread from where its cursor is now, through the
# descriptor `.native-descriptor` opens, and `.eof` and SeekFromEnd ask the
# file its size.

sub ck($got, $want, $desc) {
    return if $got eqv $want;
    say "FAIL: $desc — got {$got.raku}, want {$want.raku}";
    exit 1;
}

my $dir = $*TMPDIR.add("rakupp-handle-read-$*PID");
$dir.mkdir;
LEAVE { try { .unlink for $dir.dir; $dir.rmdir } }

my $f = $dir.add('bytes.bin');
$f.spurt(Buf.new(^256), :bin);

# reads, the cursor, the end
my $fh = $f.open(:bin);
ck $fh.read(4).list, (0, 1, 2, 3), 'the first read';
ck $fh.tell, 4, '…moves the cursor';
ck $fh.read(3).list, (4, 5, 6), 'the next read starts there';
ck $fh.eof, False, 'not at the end';
$fh.seek(250, SeekFromBeginning);
ck $fh.read(10).list, (250 .. 255).list, 'a read at the end gets what is left';
ck $fh.eof, True, 'at the end';
ck $fh.read(10).elems, 0, 'a read past the end gets nothing';
ck $fh.tell, 256, '…and the cursor stays';
$fh.seek(-2, SeekFromEnd);
ck $fh.read(5).list, (254, 255), 'SeekFromEnd';
$fh.seek(10, SeekFromBeginning);
$fh.seek(5, SeekFromCurrent);
ck $fh.read(1).list, (15,), 'SeekFromCurrent';
$fh.seek(300, SeekFromBeginning);
ck $fh.read(1).elems, 0, 'a seek past the end reads nothing';
$fh.seek(0, SeekFromBeginning);
ck $fh.read.elems, 256, 'read() takes up to 65536';
$fh.seek(100, SeekFromBeginning);
ck $fh.slurp(:bin).elems, 156, 'slurp(:bin) after a seek is what follows';
ck $fh.tell, 256, '…and leaves the cursor at the end';
$fh.close;
ck (try { $fh.read(1); 1 }), Nil, 'a closed handle refuses a read';

$fh = $f.open(:bin);
$fh.read(6);
ck $fh.slurp(:bin, :close).list, (6 .. 255).list, 'slurp(:bin) after a read is what follows';

# a file that grows between reads: the read sees what is there now
my $g = $dir.add('growing.bin');
$g.spurt(Buf.new(1, 2), :bin);
$fh = $g.open(:bin);
ck $fh.read(10).list, (1, 2), 'all there is';
my $w = $g.open(:a, :bin);
$w.write(Buf.new(3, 4));
$w.close;
ck $fh.read(10).list, (3, 4), 'and then what was appended';
$fh.close;

# a seek before any read, then a read
$fh = $f.open(:bin);
$fh.seek(200, SeekFromBeginning);
ck $fh.read(2).list, (200, 201), 'a read after a first seek';
$fh.close;

# the cost: one 8-byte read of a 64 MB file
my $big = $dir.add('big.bin');
{
    my $bh = $big.open(:w, :bin);
    my $mb = Buf.new(65 xx 1048576);
    $bh.write($mb) for ^64;
    $bh.close;
}
$fh = $big.open(:bin);
my $t = now;
my $b = $fh.read(8);
my $took = now - $t;
$fh.close;
ck $b.elems, 8, 'eight bytes of 64 MB';
# it took ~0.25 s here before the fix (the whole file was read first)
unless $took < 0.05 {
    say "FAIL: the first .read(8) of a 64 MB file took {$took.round(0.001)} s";
    exit 1;
}

say 'PASS';
