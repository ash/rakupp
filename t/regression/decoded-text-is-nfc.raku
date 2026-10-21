# Regression: text decoded from a file, $*IN or a pipe is NFC.
#
# Rakudo's decoders normalize, and so did our own `Buf.decode`, but every
# reader that turned a file's or a stream's bytes into a Str kept them as
# they came (against the choice recorded at Value.h's StrBody::nfc). A line
# `e\x[301]x` read with `.IO.lines`, `.slurp`, `$*IN.lines` or `-n` had
# `.codes` 3, was not `eq "\x[e9]x"`, failed `/^\x[e9]/` and missed as a hash
# key. Every codepoint that composes or reorders is U+0300 or above, whose
# UTF-8 leads with a byte >= 0xCC, so ASCII text pays one scan for the check.
# utf8-c8 keeps its bytes as they were: that is what it is for. Found by the
# 2026-10-21 review (O2).
#
# Runs clean under Rakudo too.

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

# "e" + COMBINING ACUTE + "x", then "b"; the NFC form of the first line is "\x[e9]x"
my $bytes = Blob.new(0x65, 0xCC, 0x81, 0x78, 0x0A, 0x62, 0x0A);
my $nfc   = "\x[e9]x";
my %seen  = $nfc => 1;
# what a line should be: two codepoints, eq the NFC form, matched as é, a hash key
sub shape($s) { ($s.codes, $s eq $nfc, so($s ~~ /^\x[e9]/), so(%seen{$s})) }
my $want = (2, True, True, True);

my $f = $*TMPDIR.add("rakupp-decoded-nfc-$*PID.txt");
$f.spurt: $bytes;
LEAVE { $f.unlink if $f.e }

# ---- a file, through the path ----
ck shape($f.IO.lines[0]),                    $want, '.IO.lines';
ck shape($f.IO.slurp.lines[0]),              $want, '.IO.slurp';
ck shape($f.IO.words[0]),                    $want, '.IO.words';
ck shape($f.IO.comb(/\S+/)[0]),              $want, '.IO.comb';
ck shape(slurp($f).lines[0]),                $want, 'slurp()';
ck shape(lines($f.IO)[0]),                   $want, 'lines($path)';
ck shape($f.IO.lines(:!chomp)[0].chomp),     $want, '.IO.lines(:!chomp)';

# ---- …and through a handle ----
ck shape($f.open.lines[0]),                  $want, '$fh.lines';
ck shape($f.open.get),                       $want, '$fh.get';
ck shape($f.open.slurp.lines[0]),            $want, '$fh.slurp';
ck shape($f.open.words[0]),                  $want, '$fh.words';
ck shape($f.open.split("\n")[0]),            $want, '$fh.split';
ck shape($f.open.readchars(2)),              $want, '$fh.readchars';
ck shape($f.open(:enc<utf8>).lines[0]),      $want, 'open(:enc<utf8>)';
ck shape($f.open(:nl-in("x")).get ~ "x"),    $want, 'open(:nl-in)';
ck shape(IO::ArgFiles.new($f).lines[0]),     $want, 'IO::ArgFiles';
ck shape($f.open.Supply.lines.list[0]),      $want, '$fh.Supply.lines';
ck shape($f.IO.slurp(:bin).decode.lines[0]), $want, 'Buf.decode (always was)';

# ---- utf8-c8 keeps the bytes it was given ----
{
    my $c8 = $f.slurp(:enc<utf8-c8>).lines[0];
    ck $c8 eq $nfc, False, 'utf8-c8 does not normalize';
    ck $c8.encode('utf8-c8').bytes, 4, '…and gives the same bytes back';
}

# ---- characters around it: CR LF, mark order, Hangul jamo, emoji ----
{
    my $g = $*TMPDIR.add("rakupp-decoded-nfc2-$*PID.txt");
    LEAVE { $g.unlink if $g.e }
    # a + COMBINING ACUTE (230) + COMBINING GRAVE BELOW (220): reordered, then composed
    # ᄀ + ᅡ (conjoining jamo): one syllable, 가
    # 👍🏽 and 🇺🇸 are NFC already and stay as they are
    $g.spurt: Blob.new(|"a".encode, 0xCC, 0x81, 0xCC, 0x96, 0x0D, 0x0A,
                       |"\x[1100]".encode, |"\x[1161]".encode, 0x0D, 0x0A,
                       |"\x[1F44D]\x[1F3FD]\x[1F1FA]\x[1F1F8]".encode, 0x0A);
    my @l = $g.lines;
    ck @l[0], "\x[e1]\x[316]", 'marks are put in order and composed, CR LF is the line end';
    ck @l[1], "\x[AC00]", 'conjoining jamo compose to a syllable';
    ck @l[2].codes, 4, 'an emoji with its modifier and a flag keep their codepoints';
    ck @l[2].chars, 2, '…and are two characters';
    ck $g.slurp.chars, 7, 'the whole text: 3 lines, 2 line ends, 2 + 1 characters';
}

# ---- standard input, -n and -p, $*ARGFILES and a pipe: through a child ----
my $exe = $*EXECUTABLE.absolute;
sub child(Str $code, *@flags, :$in = $bytes) {
    my $p = run $exe, |@flags, '-e', $code, :in, :out;
    $p.in.write($in);
    $p.in.close;
    $p.out.slurp(:close).trim
}
my $show = Q[sub show($s) { print $s.codes, " ", $s eq "\x[e9]x" }; ];
ck child($show ~ Q[show $*IN.lines[0]]),          '2 True', '$*IN.lines';
ck child($show ~ Q[show $*IN.get]),               '2 True', '$*IN.get';
ck child($show ~ Q[show $*IN.slurp.lines[0]]),    '2 True', '$*IN.slurp';
ck child($show ~ Q[show $*IN.words[0]]),          '2 True', '$*IN.words';
ck child($show ~ Q[show lines()[0]]),             '2 True', 'lines()';
ck child($show ~ Q[show get()]),                  '2 True', 'get()';
ck child($show ~ Q[show words()[0]]),             '2 True', 'words()';
ck child($show ~ Q[show prompt()]),               '2 True', 'prompt()';
ck child(Q[print .codes, " " if .starts-with("\x[e9]")], '-n'), '2', '-n';
ck child(Q[$_ = .codes], '-p').lines[0], '2', '-p';
{
    my $p = run $exe, '-e', Q[print $*ARGFILES.lines[0].codes], $f.absolute, :out;
    ck $p.out.slurp(:close), '2', '$*ARGFILES over a file';
}
{
    my $p = run $exe, '-e', Q[$*OUT.write: Blob.new(0x65, 0xCC, 0x81, 0x78, 0x0A)], :out;
    my $l = $p.out.lines[0];
    ck shape($l), $want, 'a pipe: run(:out).out.lines';
    $p = run $exe, '-e', Q[$*OUT.write: Blob.new(0x65, 0xCC, 0x81, 0x78, 0x0A)], :out;
    ck shape($p.out.slurp(:close).lines[0]), $want, '…and .out.slurp';
}

say $fails ?? "FAIL ($fails)" !! 'PASS';
