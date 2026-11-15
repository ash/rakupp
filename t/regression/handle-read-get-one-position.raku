# Regression: a file handle has one position, shared by `.read` and the text
# readers (`.get`, `.lines`, `.getc`, `.readchars`, `.slurp`), as in Rakudo.
# Each kept a cursor of its own: `.get` after `.read(3)` answered the first
# line whole, and `.read` after `.get` started at the top of the file again.
# A line that ended in a CR LF counts both bytes, for `.read` and `.tell`.
#
# Runs clean under Rakudo too.

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

my $f = $*TMPDIR.add("rakupp-read-get-$*PID.txt");
sub on(&c) { my $h = $f.open; LEAVE $h.close; c($h) }

$f.spurt("héllo\nwörld\nthird\n");
ck on({ .read(3); .get }), 'llo', '.get after .read(3) goes on from byte 3';
ck on({ .read(3); .lines.eager.List }), ('llo', 'wörld', 'third'), '…and so do .lines';
ck on({ .read(3); .slurp }), "llo\nwörld\nthird\n", '…and .slurp';
ck on({ .read(3); .getc }), 'l', '…and .getc';
ck on({ .read(7); .readchars(3) }), 'wör', '…and .readchars';
ck on({ .get; .read(2).list }), (119, 195), '.read after .get goes on from the next line';
ck on({ .get; .read(2); .tell }), 9, '…and .tell counts both';
ck on({ .getc; .read(2).list }), (195, 169), '.read after .getc goes on from the next character';
ck on({ .read(7); .get; .read(2).list }), (116, 104), 'read, get, read: one position all along';
ck on({ .get; .read(100); .eof }), True, '.read to the end after .get is at the end';
ck on({ .get; .seek(0); .read(2).list }), (104, 195), 'a .seek moves the one position';
ck on({ .read(7); .seek(0); .get }), 'héllo', '…from either side';

$f.spurt("ab\r\ncd\r\nef");
ck on({ .get; .read(2).list }), (99, 100), 'a CR LF line ends after its LF';
ck on({ .get; .getc; .read(3).list }), (100, 13, 10), '…when .getc takes over from .get as well';
ck on({ .get; .get; .tell }), 8, '…and .tell counts the CR';
ck on({ .get; .getc; .getc; .getc; .tell }), 8, '…after .getc too';

$f.unlink;
say $fails ?? "FAIL ($fails)" !! 'PASS';
exit $fails ?? 1 !! 0;
