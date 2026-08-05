# Regression: a TEXT-mode file read translates the line separator. An
# IO::Handle's default :nl-in is ["\n", "\r\n"], so a CRLF file slurps back with
# plain LF; `:bin` is the raw bytes and keeps every CR. HTTP::Tiny compares a
# generated request against a CRLF fixture read this way, and the stray CRs made
# every multipart body differ.
# Runs clean under Rakudo too.

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

my $f = $*TMPDIR.child("rakupp-crlf-{$*PID}.txt");
$f.spurt: Buf[uint8].new("a\r\nb\r\nc\n".encode);

my $text = $f.slurp;
ck $text.encode.bytes, 6, 'text slurp drops the CRs';
ck $text.ords.List, (97, 10, 98, 10, 99, 10), 'and leaves plain LFs';
ck $f.slurp(:bin).bytes, 8, ':bin keeps every byte';
ck slurp($f.Str).encode.bytes, 6, 'the sub form agrees';
ck $f.lines.List, ('a', 'b', 'c'), '.lines is unaffected';

# a lone CR is NOT a line separator and must survive
$f.spurt: Buf[uint8].new("x\ry\n".encode);
ck $f.slurp.encode.bytes, 4, 'a bare CR is left alone';

$f.unlink;

say $fails ?? "\n$fails FAILED" !! "\nPASS";
exit $fails ?? 1 !! 0;
