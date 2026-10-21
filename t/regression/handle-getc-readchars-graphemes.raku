# Regression: a handle's `.getc` and `.readchars` hand out characters, which
# are graphemes, and a CR LF reads as "\n".
#
# The character reader kept a file's text one CODEPOINT to an element, so over
# "q\x[301]xy\r\nz" `.getc` was "q" and `.readchars(2)` began with the stray
# mark, and the CR and the LF of a line end came out one at a time; Rakudo's
# decoder gives out "q\x[301]", "xy", "\n", "z". `$*IN` read one UTF-8
# character at a time. Rakudo's `$*IN.getc` waits for the character AFTER the
# one it hands out, which may be a mark that belongs to it; ours reads ahead
# the same way and puts back what starts the next character, so a `.get` or
# `.lines` that follows still sees it. Found while fixing the 2026-10-21
# review's O3 (ledger O23).
#
# Runs clean under Rakudo too.

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

my $f = $*TMPDIR.add("rakupp-getc-graphemes-$*PID.txt");
LEAVE { $f.unlink if $f.e }

# ---- a file ----
$f.spurt: "q\x[301]xy\r\nz";
{
    my $h = $f.open;
    ck $h.getc, "q\x[301]", '.getc takes a letter with its mark';
    ck $h.readchars(2), "xy", '.readchars(2) goes on after it';
    ck $h.getc, "\n", 'a CR LF is one character, read as "\n"';
    ck $h.getc, "z", '…and the text goes on after it';
    ck $h.getc, Nil, '.getc at the end is Nil';
    $h.close;
}
{
    my $h = $f.open;
    ck $h.readchars(3), "q\x[301]xy", '.readchars counts characters';
    ck $h.readchars(10), "\nz", '…a CR LF as one';
    ck $h.readchars(10), "", '…and is "" at the end';
    $h.close;
}
ck $f.open.readchars, "q\x[301]xy\nz", '.readchars with no count';
ck $f.open(:nl-in("x")).readchars(9), "q\x[301]xy\nz", 'a CR LF is "\n" whatever the nl-in';

# a lone CR is a character of its own, and so is the LF that does not follow one
$f.spurt: "a\rb\nc\r";
{
    my $h = $f.open;
    my @c;
    while $h.getc -> $c { @c.push: $c }
    ck @c, ["a", "\r", "b", "\n", "c", "\r"], 'a CR alone and an LF alone';
}

# decomposed text is NFC first: e + COMBINING ACUTE is one é
$f.spurt: Blob.new(0x65, 0xCC, 0x81, 0x66);
{
    my $h = $f.open;
    ck $h.getc, "\x[e9]", 'a decomposed letter comes out composed';
    ck $h.getc, "f", '…and the next one after it';
}

# the readers hand the handle on to each other
$f.spurt: "q\x[301]x\r\nyz\r\nw";
{
    my $h = $f.open;
    ck $h.getc, "q\x[301]", 'getc first';
    ck $h.get, "x", '…then .get takes the rest of the line';
    ck $h.getc, "y", '…then .getc the next character';
    ck $h.slurp, "z\nw", '…and .slurp the rest, CR LF as "\n"';
}
{
    my $h = $f.open;
    $h.getc;
    ck $h.lines.List, ("x", "yz", "w"), '.lines after .getc';
}
{
    my $h = $f.open;
    $h.getc; $h.getc;
    ck $h.slurp, "\nyz\nw", '.slurp straight after .getc reads a CR LF as "\n"';
}

# switching the encoding midway goes on from the bytes the characters took,
# a CR LF's two included
$f.spurt: "ab\r\ncd\x[e9]\r\nf";
{
    my $h = $f.open;
    ck $h.readchars(3), "ab\n", 'three characters of UTF-8';
    $h.encoding('latin1');
    ck $h.readchars(10).ords.List, (99, 100, 0xC3, 0xA9, 10, 102), '…and the rest as Latin-1';
}
$f.spurt: "\x[e9]\r\nx", :enc<latin1>;
ck $f.open(:enc<latin1>).readchars(5), "\x[e9]\nx", 'Latin-1: a CR LF is "\n" too';

# an emoji sequence, a flag and a Hangul syllable in jamo are one character each
$f.spurt: "\x[1F469]\x[200D]\x[1F4BB]!\x[1F1EF]\x[1F1F5]\x[1F1FA]\x[1F1F8]\x[1100]\x[1161]\x[11A8]";
{
    my $h = $f.open;
    my @c;
    while $h.getc -> $c { @c.push: $c.codes }
    ck @c, [3, 1, 2, 2, 1], 'ZWJ sequence, flags and jamo are characters';
}

# ---- standard input, through a child ----
my $exe = $*EXECUTABLE.absolute;
sub child(Str $code, Blob $in) {
    my $p = run $exe, '-e', $code, :in, :out;
    $p.in.write($in);
    $p.in.close;
    $p.out.slurp(:close)
}
my $text = "q\x[301]xy\r\nz".encode;
ck child(Q[print $*IN.getc.raku, " ", $*IN.readchars(2).raku, " ", $*IN.getc.raku, " ", $*IN.getc.raku], $text),
   Q["q́" "xy" "\n" "z"], '$*IN.getc and .readchars';
ck child(Q[print $*IN.readchars(2).raku, " ", $*IN.readchars(2).raku, " ", $*IN.readchars(2).raku], $text),
   Q["q́x" "y\n" "z"], '$*IN.readchars counts characters';
ck child(Q[print $*IN.getc.ords], Blob.new(0x65, 0xCC, 0x81, 0x66)), '233', '$*IN.getc composes';
# what was read ahead to see where a character ends is the next reader's
ck child(Q[print $*IN.getc.raku, " ", $*IN.get.raku, " ", $*IN.getc.raku, " ", $*IN.lines.raku],
         "a\x[e9]b\n\x[1F600]z\n".encode),
   Q["a" "éb" "😀" ("z",).Seq], 'a character read ahead stays for .get, .getc and .lines';
ck child(Q[print $*IN.getc.raku, " ", $*IN.slurp.raku], "a\x[e9]b".encode), Q["a" "éb"], '…and for .slurp';

say $fails ?? "FAIL ($fails)" !! 'PASS';
