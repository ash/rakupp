# Regression (issue #262): a text reader over a large file holds one window
# of its text, not all of it. `.get` loaded every line of the file into the
# handle and `.readchars` every character, one Value each, held until the
# handle closed: a 40 MB file cost 250 MB through `.get` and 1.3 GB through
# `.readchars`, whatever the window the program asked for. Each reader now
# reads the next window when it walks off the end of the one it has, and
# what crosses a window's edge reads as it did: the lines, the characters,
# `.tell` and `.seek` by bytes, the hand-over to `.read`, `.slurp` and
# `.words` of the rest, a custom `nl-in`, `:!chomp`.
#
# Runs clean under Rakudo too.
use Telemetry;

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}
my $big = $*TMPDIR.add("rakupp-window-big-$*PID.txt");
{ my $out = $big.open(:w); $out.print(("x" x 99 ~ "\n") x 1000) for ^400; $out.close }   # 40 MB
# what a read may add to the peak, in MiB: under what it cost (and over what
# Rakudo's own decoder takes, about 200 MiB through `.readchars` here)
sub held($desc, &read, $want, $mib) {
    my $before = T<max-rss>;   # (KiB)
    ck read(), $want, $desc;
    my $grew = (T<max-rss> - $before) div 1024;
    if $grew < $mib { say "ok - …within a window or two ($grew MiB)" }
    else { $fails++; say "FAIL: $desc grew the peak by $grew MiB" }
}
held '.get over 40 MB', {
    my $fh = $big.open; my $n = 0;
    while $fh.get -> $l { $n += $l.chars + 1 }; $fh.close; $n }, 40_000_000, 150;
held '.lines over 40 MB', { my $fh = $big.open; my $n = 0; $n++ for $fh.lines; $fh.close; $n }, 400_000, 150;
held '.readchars over 40 MB', {
    my $fh = $big.open; my $n = 0;
    loop { my $g = $fh.readchars(65536); last unless $g.chars; $n += $g.chars }; $fh.close; $n }, 40_000_000, 400;
$big.unlink;

# about three windows: multi-byte characters, combining marks, CR LF lines
# (the character readers read a copy with LF lines and no marks, as Rakudo's
# hand a CR LF back whole and can split a mark from its base at a buffer's end)
my $f = $*TMPDIR.add("rakupp-window-$*PID.txt");
my $lf = $*TMPDIR.add("rakupp-window-lf-$*PID.txt");
my $L = 15_000;   # (about three 256 KiB windows)
my @l = (^$L).map({ ($_ %% 7 ?? "é" !! "x") ~ ("ab" x ($_ % 40)) ~ ($_ %% 11 ?? "q\x[301]" !! "") ~ " $_" });
$f.spurt: @l.kv.map(-> $i, $s { $s ~ ($i %% 5 ?? "\r\n" !! "\n") }).join;
my $text = @l.join("\n") ~ "\n";
$lf.spurt: (my $plain = $text.subst("q\x[301]", "q", :g));
sub on(&c, :$file = $f, *%o) { my $h = $file.open(|%o); LEAVE $h.close; c($h) }

ck on(-> $h { my @g; while $h.get -> $l { @g.push: $l }; (@g eqv @l, $h.eof) }), (True, True), '.get reads every line';
ck on(-> $h { $h.lines.List eqv @l.List }), True, '…and so does .lines';
ck on(-> $h { my @a = $h.lines(6000); my @b = $h.lines; (@a.elems, @b.elems, @a[*-1], @b[0]) }),
   (6000, $L - 6000, @l[5999], @l[6000]), '.lines(N), then the rest';
ck on(:file($lf), -> $h { my $t = ''; loop { my $g = $h.readchars(7777); last unless $g.chars; $t ~= $g }; $t eq $plain }),
   True, '.readchars reads every character';
ck on(:file($lf), -> $h { my $n = 0; $n++ while $h.getc.defined; $n }), $plain.chars, '…and so does .getc';
ck on(-> $h { $h.get for ^7500; my $t = $h.tell; my $l = $h.get; $h.seek($t); ($h.get, $l) }), (@l[7500], @l[7500]),
   '.tell and .seek past the first window';
ck on(-> $h { $h.get for ^7500; $h.seek(0); $h.get }), @l[0], '…back to the top';
ck on(-> $h { $h.seek(-10, SeekFromEnd); $h.get }), 'bab 14999', '…and from the end';
ck on(:!chomp, -> $h { $h.lines.join.chars }), $f.slurp.chars, ':!chomp keeps each terminator';
ck on(:file($lf), :nl-in("\n"), -> $h { my @c = $h.lines; (@c.elems, @c[$L - 1]) }), ($L, @l[$L - 1].subst("q\x[301]", "q")),
   'a custom nl-in';
ck on(:file($lf), -> $h { $h.get for ^10000; $h.getc ~ $h.readchars(5) ~ $h.get }), @l[10000].subst("q\x[301]", "q"),
   '.get, .getc, .readchars, one position';
ck on(-> $h { $h.get for ^10001; ($h.read(4).decode, $h.get) }), (@l[10001].substr(0, 4), @l[10001].substr(4)),
   '…and .read takes it over and hands it back';
ck on(-> $h { $h.get for ^12500; $h.slurp.lines.List eqv @l[12500..*].List }), True, '.slurp reads the rest';
ck on(-> $h { $h.get for ^12500; $h.words.elems }), 2 * ($L - 12500), '…and so does .words';
ck on(:file($lf), -> $h { $h.readchars(500_000); $h.slurp.chars }), $plain.chars - 500_000, '.slurp after .readchars';
ck on(-> $h { my $n = 0; for $h.lines.kv -> $k, $v { $n = $k }; $n }), $L - 1, '.lines.kv';
$lf.unlink;

# a file of one line, no newline at all: a window grows to hold it
$f.spurt: ("ab" x 400_000) ~ "é" ~ ("cd" x 200_000);
ck on(-> $h { ($h.get.chars, $h.get, $h.eof) }), (1200001, Nil, True), 'one line of 1.2 MB';
ck on(-> $h { my $n = 0; loop { my $g = $h.readchars(100_000); last unless $g.chars; $n += $g.chars }; $n }), 1200001,
   '…read by characters';
$f.unlink;
say $fails ?? "FAIL ($fails)" !! 'PASS';
exit $fails ?? 1 !! 0;
