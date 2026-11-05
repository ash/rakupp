# A Rat's parts live in a payload body of their own (PK::Rat, 2026-11-05),
# shared by every copy of the value: a Rat literal's one body by all of its
# evaluations, `my $b = $a` by both names. The body is written in place only
# by its sole holder (fusedRatStore's `$x += 0.25` lane), and the FatRat flag
# copies a shared body before it is set. Each check below is a way the sharing
# could leak: a literal or a copy that changes when another holder is written,
# a coercion that retypes the value it came from. 1M kept Rats went from 437
# to 163 bytes an element (tools/run-memory.raku's `rats` row).
#
# Expectations checked against Rakudo 2026.09
# (~/.rakubrew/versions/moar-2026.09/install/bin/rakudo). Green on both.

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

# one literal, evaluated again and again, written through each time
my @seen;
for ^3 { my $x = 0.5; $x += 0.25; @seen.push($x) }
ck(@seen.List, (0.75, 0.75, 0.75), 'a literal written through `+=` is the same literal next time');
my @typed;
for ^3 { my Rat $x = 0.5; $x = $x + 0.25; $x = $x * 2; @typed.push($x) }
ck(@typed.List, (1.5, 1.5, 1.5), '…and through a typed `$x = $x op …`');
sub half() { 0.5 }
my $h = half(); $h += 1/4;
ck((half(), $h), (0.5, 0.75), 'a literal returned by a sub, then written');

# a copy shares the value until one side is written
my $a = 1/3;
my $b = $a;
$b += 1/3;
ck(($a, $b), (1/3, 2/3), 'a copy written leaves its source');
my @r = 1/2, 1/3, 1/4;
my @s = @r;
@s[1] += 1;
ck((@r[1], @s[1]), (1/3, 4/3), 'an array copy written leaves its source');
my %k = (1/2) => "half";
ck(%k{0.5}, "half", 'a Rat key');

# FatRat: a flag on the body, copied before it is set
my $q = 1/3;
my $f = $q.FatRat;
ck(($q.^name, $f.^name), ("Rat", "FatRat"), '`.FatRat` leaves the Rat it came from');
my $g = FatRat.new(1, 3);
my $g2 = $g;
ck(($g + 1).^name, "FatRat", 'FatRat arithmetic stays FatRat');
ck(($g2.^name, $g2.nude), ("FatRat", (1, 3)), '…and its copy is a FatRat');
ck((-$g).^name, "FatRat", 'negated');
ck($g.abs.^name, "FatRat", '`.abs`');
ck((1/3).Rat.^name, "Rat", '`.Rat` of a Rat');

# the parts, the forms and the edges
ck((3/7).nude, (3, 7), '`.nude`');
ck(((-6)/8).numerator, -3, 'normalized, the sign on the numerator');
ck((2/4).denominator, 2, '…and reduced');
ck(Rat.new(1, 0).raku, "<1/0>", 'Rat.new with a zero denominator');
ck((0.1 + 0.2 == 0.3, 1/3 === 1/3, 1/3 eqv 2/6), (True, True, True), '==, ===, eqv');
ck((2/3) ** 2, 4/9, 'a power');
ck((1/3 + 1/6).WHAT, Rat, 'a sum is a Rat');
ck((10 ** 30 + 1/3).numerator.chars, 31, 'a numerator past a machine word');
ck(((^6).map({ ($_ + 1) / 3 }).sort({ $^b <=> $^a })).List, (6/3, 5/3, 4/3, 3/3, 2/3, 1/3), 'sorted');
ck(await(start { 1/3 + 1/6 }), 1/2, 'in another thread');

say $fails ?? "FAIL ($fails)" !! 'PASS';
exit $fails ?? 1 !! 0;
