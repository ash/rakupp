# Compiled -O and plain answer as the interpreter and Rakudo do for three
# things -O had its own path for:
#  - `~` and string interpolation join two Strs NFC across the seam, as
#    applyArith's Str arm does: `"e" ~ "\x[301]"` was two code points -O
#    (rtConcat appended the bytes) and in an interpolation (`"$a$b"`), where
#    the interpreter and Rakudo have one;
#  - `my ($a, $b) = ONE-ITEM` iterates the item, itemized or not (the single-
#    argument rule): -O bound a List held in `$j` whole to $a, and a Hash
#    gives its pairs (interpreted, $o held the Hash);
#  - `!=` between an unsigned native and a negative signed one is False
#    (Rakudo's mixed-sign quirk, Roast S02-types/signed-unsigned-native.t),
#    which the -O condition lane compared as machine Ints.
# The answers are Rakudo 2026.09's.
#
# Contract: exit 0 + last line PASS.
my @fail;
my $bin = $*EXECUTABLE.absolute;
my $dir = $*TMPDIR.add("rakupp-nfc-destructure-{$*PID}");
$dir.mkdir;
LEAVE { try { .unlink for $dir.dir; $dir.rmdir } }

my $src = q:to/END/;
    my $l = "e"; my $r = "\x[301]";
    say ($l ~ $r).codes;
    my @j = 'k' => ("e", "\x[301]");
    for @j -> $j { my ($a, $b) = $j.value; say ($a ~ $b).codes; my $e = $a; $e ~= $b; say $e.codes; say "$a$b".codes; say "<$a$b>".codes }
    my @p = ("e","f"),;
    for @p -> $j { my ($a, $b) = $j; say "$a|{$b // 'U'}" }
    sub f($j) { my ($a, $b) = $j; "$a|{$b // 'U'}" }; say f(("x","y"));
    my $s = ("p","q"); my ($c, $d) = $s; say "$c|{$d // 'U'}";
    for @p { my ($a, $b) = $_; say "$a|{$b // 'U'}" }
    my @q = ["g","h"],; for @q -> $j { my ($a, $b) = $j; say "$a|{$b // 'U'}" }
    my ($a1, $b1) = $(1, 2); say "{$a1.raku}|{$b1.raku}";
    my @z = (1, 2), (3, 4); my ($i, $k) = @z; say "{$i.raku}|{$k.raku}";
    my ($k2, $l2) = (1, 2), 3; say "{$k2.raku}|{$l2.raku}";
    sub p($q) { my ($m, $n) = $q; "{$m.raku}|{$n.raku}" }; say p((5, 6)); say p([5, 6]);
    my %hh = a => 1; my ($o, $p2) = %hh; say "{$o.raku}|{$p2.raku}";
    my uint8 $un = 255; my int8 $neg = -1; my int $zero = 0;
    my @ne; for ^2 { @ne = $un != $neg - $zero, ($un + $zero) != $neg }; say @ne;
    END
my $want = q:to/END/.trim;
    1
    1
    1
    1
    3
    e|f
    x|y
    p|q
    e|f
    g|h
    1|2
    $(1, 2)|$(3, 4)
    $(1, 2)|3
    5|6
    5|6
    :a(1)|Any
    [False True]
    END
my @progs = ('native', $src, $want),;

for @progs -> ($name, $src, $expect) {
    my $f = $dir.add("$name.raku");
    $f.spurt($src);
    my $i = run($bin, $f.absolute, :out, :err);
    my $iout = $i.out.slurp(:close).trim; $i.err.slurp(:close);
    @fail.push("$name interpreted: {$iout.raku}") unless $iout eq $expect;
    for ('-O', 'plain') -> $mode {
        my $exe = $dir.add("$name-$mode.bin");
        my @cmd = $bin, '--exe', |($mode eq '-O' ?? '-O' !! ()), $f.absolute, '-o', $exe.absolute;
        my $c = run(|@cmd, :out, :err);
        $c.out.slurp(:close); $c.err.slurp(:close);
        if !$exe.e { @fail.push("$name $mode: did not compile"); next }
        my $r = run($exe.absolute, :out, :err);
        my $cout = $r.out.slurp(:close).trim; $r.err.slurp(:close);
        @fail.push("$name $mode: {$cout.raku}") unless $cout eq $expect;
    }
}

if @fail { .say for @fail; say "FAIL" } else { say "PASS" }
