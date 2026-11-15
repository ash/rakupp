# Regression (issue #262, the readers beside a handle's): `$path.lines`,
# `lines($path)`, `$path.words`, a handle's `.words`, `$fh.lines.map(…)` and
# `.grep(…)`, a child process's output lines and `$fh.Supply(:size)` read as
# they go, a window of the file at a time. Each held all of its input: the
# path readers split the whole file first, `.map` and `.grep` read a handle's
# lines to the end before they started, a Proc's line Seq kept every line it
# handed out, and `.Supply` slurped the file and cut it up before emitting.
# 20 MB of text cost 154 MB through `$path.lines` and 579 MB through
# `.Supply(:size)`.
#
# Runs clean under Rakudo too.
use Telemetry;

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}
my $big = $*TMPDIR.add("rakupp-io-stream-$*PID.txt");
{ my $out = $big.open(:w); $out.print(("ab cd " x 16 ~ "x\n") x 1000) for ^300; $out.close }   # 30 MB, 300k lines
# what a read may add to the peak, in MiB, well under what it cost
sub held($desc, &read, $want, $mib = 120) {
    my $before = T<max-rss>;   # (KiB)
    ck read(), $want, $desc;
    my $grew = (T<max-rss> - $before) div 1024;
    if $grew < $mib { say "ok - …as it goes ($grew MiB)" }
    else { $fails++; say "FAIL: $desc grew the peak by $grew MiB" }
}
held '$path.lines', { my $n = 0; $n++ for $big.lines; $n }, 300_000, 180;   # (Rakudo's heap grows 120 MiB on its first)
held 'lines($path)', { my $n = 0; $n++ for lines($big); $n }, 300_000;
held '$path.words', { my $n = 0; $n++ for $big.words; $n }, 300_000 * 33;
held '$fh.words', { my $h = $big.open; my $n = 0; $n++ for $h.words; $h.close; $n }, 300_000 * 33;
held '$fh.lines.map(…)', { my $h = $big.open; my $n = 0; $n += $_ for $h.lines.map(*.chars); $h.close; $n }, 300_000 * 97;
held '$path.lines.grep(…)', { my $n = 0; $n++ for $big.lines.grep(*.ends-with("x")); $n }, 300_000;
held '$fh.Supply(:size)', {
    my $n = 0; my $h = $big.open; react { whenever $h.Supply(:size(65536)) { $n += .chars } }; $h.close; $n },
    300_000 * 98, 300;   # (Rakudo's own takes about 100 MiB here)
held "a child process's lines", {
    my $p = run $*EXECUTABLE, '-e', 'print(("ab cd " x 16 ~ "x\n") x 1000) for ^300', :out;
    my $n = 0; $n++ for $p.out.lines; $p.out.close; $n }, 300_000;
# (a million short lines read whole, in a child of its own: that read holds them all)
{
    my $f = $*TMPDIR.add("rakupp-io-stream-short-$*PID.txt");
    { my $o = $f.open(:w); $o.print("a\n" x 1_050_000); $o.close }
    LEAVE $f.unlink;
    my $p = run $*EXECUTABLE, '-e', "say '$f'.IO.lines.map(*.chars).elems", :out;
    ck $p.out.slurp(:close).trim, '1050000', '$path.lines.map(…).elems past a million lines';
}
$big.unlink;

# …and what they read is what they read before
my $f = $*TMPDIR.add("rakupp-io-stream-small-$*PID.txt");
$f.spurt: "one two\r\nthree  four\n\nfive\tsix seven\nlast";
ck $f.lines.List, ("one two", "three  four", "", "five\tsix seven", "last"), '$path.lines';
ck $f.lines(2).List, ("one two", "three  four"), '…with a limit';
ck $f.lines(:!chomp).List, ("one two\n", "three  four\n", "\n", "five\tsix seven\n", "last"), '…and :!chomp';
ck $f.words.List, <one two three four five six seven last>, '$path.words';
ck $f.words(3).List, <one two three>, '…with a limit';
ck { my $h = $f.open; my @w = $h.words(:close); (@w.elems, $h.opened) }(), (8, False), '$fh.words(:close)';
ck { my $h = $f.open; LEAVE $h.close; $h.lines.grep(/e/).eager }(), ("one two", "three  four", "five\tsix seven"),
   '$fh.lines.grep(…)';
my @c; $f.open.Supply(:size(5)).tap({ @c.push: $_ });
ck @c, ["one t", "wo\nth", "ree  ", "four\n", "\nfive", "\tsix ", "seven", "\nlast"], '$fh.Supply(:size) in chunks';
my @b; $f.open(:bin).Supply(:size(16)).tap({ @b.push: .elems });
ck @b, [16, 16, 9], '…of bytes on a :bin handle';
$f.unlink;
say $fails ?? "FAIL ($fails)" !! 'PASS';
exit $fails ?? 1 !! 0;
