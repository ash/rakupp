# Regression: where our inflate's verdict differed from libz's, found by the
# first runs of t/oracle/zlib.raku (ORACLE-PLAN P1). Every expectation here is
# what libz (Python's zlib, read strictly: the stream must end where the input
# does) and GNU `gzip -d` answer for the same bytes, checked 2026-10-14.
#
#   - The zlib and gzip trailers were read from the END of the buffer rather
#     than where the deflate data stops, so a zlib stream followed by a copy
#     of itself inflated, raw deflate ignored what followed its last block,
#     and two gzip members concatenated (`cat a.gz b.gz`, RFC 1952 2.2) were
#     refused with a CRC mismatch. Now the trailer is read where the data
#     ends, nothing may follow a zlib or raw stream, and a gzip member may be
#     followed only by another.
#   - A dynamic block whose distance code has NO codes was refused; RFC 1951
#     3.2.7 allows it for a block of literals, and libz reads it.
#   - An incomplete literal/length code (and code-length code) was accepted;
#     libz refuses both unless the code is a single one-bit code.
#
# The crafted streams are one dynamic block each, written bit by bit by a
# throwaway encoder and given to libz before being pinned here.

use Test;

my &compress   = &::('rakupp-compress');
my &uncompress = &::('rakupp-uncompress');
my &gzslurp    = &::('rakupp-gzslurp');

sub unhex(Str $h) { Buf.new($h.comb(2).map({ :16($_) })) }
sub inflates($buf, $want, $desc, *%how) {
    my $got = try uncompress($buf, |%how);
    ok $got.defined && $got eqv $want, $desc or diag $got.defined ?? "got {$got.raku}" !! "refused: {$!.message}";
}
sub refuses($buf, $msg, $desc, *%how) {
    my $got = try uncompress($buf, |%how);
    ok !$got.defined && $!.message.contains($msg), $desc
        or diag $got.defined ?? "accepted as {$got.raku}" !! "refused for another reason: {$!.message}";
}

# ---- the codes ------------------------------------------------------------

inflates unhex('05c0010900000080a0adf67f442803'), 'abba'.encode,
    'a block of literals whose distance code has no codes at all', :raw;
inflates unhex('05c081000000000090ff6b00'), Buf.new,
    'a block holding only end-of-block, coded as one one-bit code', :raw;
refuses unhex('05c0010100000082a0adf87f4468'), 'literal/length code is incomplete',
    'an incomplete literal/length code (1/2 + 1/4 + 1/8)', :raw;

# ---- framing --------------------------------------------------------------

my $z = compress('hello'.encode);
refuses $z ~ $z, 'bytes after the end of the zlib stream',
    'a zlib stream followed by a copy of itself (the checksum at the end still matched)';
refuses $z ~ Buf.new(0), 'bytes after the end of the zlib stream', 'a zlib stream and one more byte';
refuses compress('hello'.encode, :raw) ~ Buf.new(1, 2, 3), 'bytes after the end of the deflate stream',
    'raw deflate followed by bytes', :raw;

my $two = compress('hello '.encode, :gzip) ~ compress('world'.encode, :gzip);
inflates $two, 'hello world'.encode, 'two gzip members inflate to both, in order', :gzip;
inflates compress(''.encode, :gzip) ~ compress('x'.encode, :gzip) ~ compress(''.encode, :gzip),
    'x'.encode, 'empty members around a full one', :gzip;
refuses compress('x'.encode, :gzip) ~ Buf.new(0 xx 4), 'do not start another',
    'zero padding after a gzip member is not another member', :gzip;
{
    my $bad = Buf.new($two);
    $bad[$bad.elems - 5] +^= 0xff;              # the second member's CRC
    refuses $bad, 'CRC-32 checksum does not match', 'each member\'s own trailer is checked', :gzip;
}

# gzslurp reads a file `cat a.gz b.gz` made, which is what gzip -d does.
{
    my $f = $*TMPDIR.add("zlib-framing-$*PID.gz");
    $f.spurt($two);
    is gzslurp($f.Str), 'hello world', 'gzslurp reads a multi-member file';
    $f.unlink;
}

done-testing;
