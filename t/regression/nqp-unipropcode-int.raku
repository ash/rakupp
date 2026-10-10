# Regression: nqp::unipropcode answered the property NAME as a Str, standing in
# for MoarVM's property number, and every getuniprop_* took that name back.
# String::Utils 0.0.41 keeps the code in `my int $gcprop = nqp::unipropcode(
# "General_Category")`, so the module died loading ("expected int but got Str")
# and took highlighter, Needle::Compile and App::Rak down with it. The code is
# now MoarVM's own number (Alphabetic 33, General_Category 20), and the other
# Unicode ops take it: getuniprop_str/_int/_bool, and the unipvalcode /
# matchuniprop / hasuniprop trio rakupp did not have at all.
#
# Expectations checked against Rakudo 2026.09 via /opt/homebrew/bin/rakudo.
# Value numbers outside General_Category and East_Asian_Width are MoarVM's
# internal indices, which rakupp does not reproduce, so only how they match
# is checked for Script.

use nqp;

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

# --- the codes themselves ---------------------------------------------------
my $alpha-code = nqp::unipropcode("Alphabetic");
ck($alpha-code.^name, 'Int', 'unipropcode answers an Int');
ck($alpha-code, 33, '…MoarVM\'s number for Alphabetic');
ck(nqp::unipropcode("General_Category"), 20, 'General_Category is 20');
ck(nqp::unipropcode("gc"), 20, '…and so is its short alias');
ck(nqp::unipropcode("East_Asian_Width"), 7, 'East_Asian_Width is 7');
ck(nqp::unipropcode("Lu"), 20, 'a General_Category value names its property');
ck(nqp::unipropcode("W"), 7, '…as does an East_Asian_Width one');
ck(nqp::unipropcode("alpha"), 33, 'a lower-case name matches case-insensitively');
ck(nqp::unipropcode("ALPHA"), 0, '…an upper-case one does not');
ck(nqp::unipropcode("Nonsense"), 0, 'an unknown name is 0');

# --- String::Utils 0.0.41's shape: the code in a native int -----------------
my int $gcprop = nqp::unipropcode("General_Category");
ck(nqp::getuniprop_str(0x20, $gcprop), 'Zs', 'getuniprop_str takes the code');
ck(nqp::getuniprop_str(0x1B, $gcprop), 'Cc', '…for a control too');
ck(nqp::getuniprop_int(0x301, $gcprop), 6, 'getuniprop_int: Mn is 6 (nomark)');
ck(nqp::getuniprop_int(0x41, $gcprop), 1, '…and Lu is 1');
# collapse-whitespace, reduced: the loop that died at module load
sub collapse(str $s) {
    my @out;
    my $prev = '';
    for $s.ords -> int $c {
        my str $prop = nqp::getuniprop_str($c, $gcprop);
        if $prop eq 'Zs' || $prop eq 'Cc' || $prop eq 'Cf' {
            @out.push(' ') unless $prev eq ' ';
            $prev = ' ';
        }
        else {
            @out.push($prev = $c.chr);
        }
    }
    @out.join
}
ck(collapse("a \t\n b\x[2003]c"), 'a b c', 'collapse-whitespace reads the category through the code');

my int $ea = nqp::unipropcode("East_Asian_Width");
ck(nqp::getuniprop_str(0x61, $ea), 'Na', 'East_Asian_Width answers its short alias');
ck(nqp::getuniprop_str(0x4E00, $ea), 'W', '…W for a CJK ideograph');
ck(nqp::getuniprop_int(0x4E00, $ea), 3, '…whose number is 3');

my int $alpha = nqp::unipropcode("Alphabetic");
ck(nqp::getuniprop_bool(0x41, $alpha), 1, 'a binary property through getuniprop_bool');
ck(nqp::getuniprop_bool(0x31, $alpha), 0, '…false for a digit');
ck(nqp::getuniprop_str(0x41, $alpha), '', '…and its string form is empty, as MoarVM has it');
ck(nqp::getuniprop_bool(0x1F600, nqp::unipropcode('Emoji')), 1, 'Emoji through its code');
ck(nqp::getuniprop_bool(0x41, nqp::unipropcode('L')), 1, 'the L group');
ck(nqp::getuniprop_bool(0x31, nqp::unipropcode('L')), 0, '…is not a digit');
ck(nqp::getuniprop_bool(0x31, nqp::unipropcode('N')), 1, 'the N group is');

# --- unipvalcode / matchuniprop / hasuniprop ---------------------------------
my int $lu = nqp::unipvalcode($gcprop, "Lu");
ck($lu, 1, 'unipvalcode: Lu is 1');
ck(nqp::unipvalcode($gcprop, "Uppercase_Letter"), 1, '…by its long name too');
ck(nqp::unipvalcode($ea, "Wide"), 3, 'East_Asian_Width Wide is 3');
ck(nqp::unipvalcode($gcprop, "Nonsense"), 0, 'an unknown value is 0');
ck(nqp::matchuniprop(0x41, $gcprop, $lu), 1, 'matchuniprop: A is Lu');
ck(nqp::matchuniprop(0x61, $gcprop, $lu), 0, '…a is not');
ck(nqp::matchuniprop(0x41, $alpha, 1), 1, 'a binary property matches 1');
ck(nqp::matchuniprop(0x31, $alpha, 1), 0, '…not for a digit');
ck(nqp::matchuniprop(0x4E00, $ea, nqp::unipvalcode($ea, "W")), 1, 'East_Asian_Width W');
my int $sc = nqp::unipropcode("Script");
my int $latin = nqp::unipvalcode($sc, "Latin");
ck(nqp::matchuniprop(0x41, $sc, $latin), 1, 'Script Latin matches A');
ck(nqp::matchuniprop(0x3B1, $sc, $latin), 0, '…not alpha');
ck((^5).map({ nqp::hasuniprop("aB1 C", $_, $gcprop, $lu) }).List, (0, 1, 0, 0, 1),
   'hasuniprop at each position');
ck(nqp::hasuniprop("aB", 7, $gcprop, $lu), 0, 'hasuniprop past the end');

say $fails ?? "FAILED $fails" !! "PASS";
