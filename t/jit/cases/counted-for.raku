# `for` over an Int Range is the counted loop its own fast path already runs,
# and the kernel it tiers up to is a synthetic `loop (; $i <= END; $i++)`. What
# that synthesis has to get right is everything below: the four range forms and
# their exclusive ends, the topic as a loop variable, `last`/`next` landing on
# the synthetic increment rather than skipping it, and the same loop NODE
# entered again with a different bound.
my $out = "";

# the pointy form and the topic form, which are different slots to the kernel:
# `$_` is refused everywhere else in a kernel and admitted only here.
my $a = 0;
for 1 .. 40 -> $i { $a = $a + $i }
my $b = 0;
for 1 .. 40 { $b = $b + $_ }
$out = $out ~ "sums $a $b\n";

# all four range spellings, so the end bound handed to the kernel is checked
# against each of exclusive-from, exclusive-to and both.
my ($c, $d, $e, $f) = 0, 0, 0, 0;
for 1 .. 10  -> $i { $c = $c + $i }
for 1 ..^ 10 -> $i { $d = $d + $i }
for 1 ^.. 10 -> $i { $e = $e + $i }
for 1 ^..^ 10 -> $i { $f = $f + $i }
$out = $out ~ "ranges $c $d $e $f\n";

# an empty range and a one-element range: the kernel must not run a body the
# interpreter would not have, and must not skip one it would.
my $g = 0;
for 5 .. 4 -> $i { $g = $g + 1 }
my $h = 0;
for 7 .. 7 -> $i { $h = $h + $i }
$out = $out ~ "edges $g $h\n";

# `next` has to run the synthetic increment or the loop never ends; `last` has
# to leave it. Both unlabelled, which is all the whitelist admits.
my $odd = 0;
for 1 .. 60 -> $i {
    next if $i %% 2;
    $odd = $odd + $i;
}
my $upto = 0;
for 1 .. 1000 -> $i {
    last if $i > 25;
    $upto = $upto + $i;
}
$out = $out ~ "control $odd $upto\n";

# body locals are the kernel's own, not slots: a `my` here must not write
# through to anything outside, and must start fresh each iteration.
my $acc = 0;
for 1 .. 30 -> $i {
    my $t = $i * 3;
    my $u = $t - $i;
    $acc = $acc + $u;
}
$out = $out ~ "locals $acc\n";

# nested, where the inner loop is the one that gets hot and the outer one is
# what is worth compiling. Both are counted `for`s over Ints.
my $n = 0;
for 1 .. 20 -> $i {
    for 1 .. 20 -> $j {
        $n = $n + $i * $j;
    }
}
$out = $out ~ "nested $n\n";

# the SAME loop node, entered again with a different bound. The end is read
# from the frame at entry, not baked into the kernel, so the second call must
# not inherit the first call's limit.
sub upto($lim) {
    my $s = 0;
    for 1 .. $lim -> $i { $s = $s + $i }
    $s;
}
$out = $out ~ "reentry {upto(10)} {upto(200)} {upto(0)} {upto(10)}\n";

# A `for` variable is a READ-ONLY binding in Raku, and the kernel assigns its
# slots directly — so the scan refuses a body that writes the loop variable and
# the loop stays interpreted. That refusal is LOAD-BEARING, not tidiness: the
# interpreter rebinds the variable from the counter every iteration, so a write
# to it is forgotten at the next one, while a kernel holds one slot for the
# whole loop and would carry the write into the counter and change the trip
# count. This line pins the two lanes agreeing, which they only do because of
# the refusal.
#
# Both lanes print "refused" since the interpreter started enforcing the
# binding; before that they agreed on "none", because the divergence was the
# interpreter's and showed in the plain lane too. The scan's own refusal is
# now belt-and-braces — a body that writes the loop variable throws on the
# first iteration, long before the site is hot enough to tier up — and it
# stays, because it is what makes the refusal true by construction rather
# than by the error happening to come first.
my $err = "none";
try {
    for 1 .. 200 -> $i { $i = 9 }
    CATCH { default { $err = "refused" } }
}
$out = $out ~ "readonly $err\n";

# the loop variable must not survive the loop in either lane
my $i = "outer";
for 1 .. 50 -> $i { }
$out = $out ~ "scope $i\n";

print $out;
