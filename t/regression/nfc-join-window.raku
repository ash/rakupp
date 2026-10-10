# Regression: joining non-ASCII text renormalizes only where the two texts meet.
#
# `~`, `~=`, interpolation and `.join` re-normalized the WHOLE result to NFC
# whenever either side was non-ASCII (`nfcNormalize(dst + v)`), so building a
# string with `$s ~= 'é'` was quadratic: 20k appends 5.25 s. The scaling gate
# (t/scaling/run.raku, shapes run/str-append-unicode and
# run/str-append-chars-unicode) found it. A join of two NFC sides can only
# change from the last NFC boundary of the left side to the first one of the
# right side (uniNfcJoin), and the count of graphemes is carried across it. A
# side that is not NFC — rakupp keeps a file's bytes, or sprintf's, as they
# came — is normalized whole first, as before; whether a Str is NFC is cached
# on its body, so that happens once.
#
# These are the joins where normalization or grapheme segmentation DOES reach
# across: composition (e + U+0301, ȩ + U+0306 = ḝ), marks that reorder across
# the join, Hangul L+V / LV+T / L+VT, a composition whose second half is a
# starter (U+0B47 + U+0B3E), CR LF, emoji ZWJ, skin tone and regional
# indicators split between the sides. Each is checked through every join
# operator, with .chars/.codes/.ords/.NFC/.NFD, and .chars after each step of a
# growing `~=`.
#
# Also pinned, fixed in the same change because a non-NFC Str would break the
# window: the `chrs(…)` sub and `nqp::chr` did not normalize (the `.chrs`
# method and `chr` did), and `~` with an object operand joined its `.Str`
# without composing.
#
# Every expectation was checked against Rakudo 2026.09, except two where
# Rakudo's `~` disagrees with its own `.join` and `.NFC.Str` round trip (a ZWJ
# that opens the right side, and an odd run of regional indicators split
# between the sides): there it is UAX #29 and Rakudo's `.join` that are pinned.
use nqp;

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}
sub hx(@l) { @l.map(*.base(16)).join(' ') }

# name => (left, right, chars, codes, ords, NFD)
my @joins =
    'e + acute'                => ("e", "\x[301]", 1, 1, 'E9', '65 301'),
    'ȩ + breve is ḝ'           => ("\x[229]", "\x[306]", 1, 1, '1E1D', '65 327 306'),
    'ȩ + acute stays two'      => ("\x[229]", "\x[301]", 1, 2, '229 301', '65 327 301'),
    'â + dot below reorders'   => ("\x[E2]", "\x[323]", 1, 1, '1EAD', '61 323 302'),
    'marks reorder across'     => ("x\x[301]\x[302]", "\x[323]\x[328]", 1, 5, '78 328 323 301 302', '78 328 323 301 302'),
    'marks only on the right'  => ("q", "\x[328]\x[323]\x[301]", 1, 4, '71 328 323 301', '71 328 323 301'),
    'Hangul L + V'             => ("\x[1100]", "\x[1161]", 1, 1, 'AC00', '1100 1161'),
    'Hangul LV + T'            => ("\x[AC00]", "\x[11A8]", 1, 1, 'AC01', '1100 1161 11A8'),
    'Hangul L + VT'            => ("\x[1100]", "\x[1161]\x[11A8]", 1, 1, 'AC01', '1100 1161 11A8'),
    'Hangul V + T'             => ("\x[1161]", "\x[11A8]", 1, 2, '1161 11A8', '1161 11A8'),
    'Hangul LVT + T'           => ("\x[AC01]", "\x[11A8]", 1, 2, 'AC01 11A8', '1100 1161 11A8 11A8'),
    'Hangul inside text'       => ("abc\x[1100]", "\x[1161]def", 7, 7, '61 62 63 AC00 64 65 66', '61 62 63 1100 1161 64 65 66'),
    'U+0B47 + U+0B3E'          => ("\x[B47]", "\x[B3E]", 1, 1, 'B4B', 'B47 B3E'),
    'U+0B47 + mark + U+0B3E'   => ("\x[B47]\x[301]", "\x[B3E]", 1, 3, 'B47 301 B3E', 'B47 301 B3E'),
    'CR | LF'                  => ("a\r", "\nb", 3, 4, '61 D A 62', '61 D A 62'),
    'ZWJ | emoji'              => ("\x[1F468]\x[200D]", "\x[1F469]", 1, 3, '1F468 200D 1F469', '1F468 200D 1F469'),
    'emoji | ZWJ emoji'        => ("\x[1F468]", "\x[200D]\x[1F469]", 1, 3, '1F468 200D 1F469', '1F468 200D 1F469'),
    'skin tone'                => ("\x[1F44D]", "\x[1F3FD]", 1, 2, '1F44D 1F3FD', '1F44D 1F3FD'),
    'RI | RI'                  => ("\x[1F1FA]", "\x[1F1F8]", 1, 2, '1F1FA 1F1F8', '1F1FA 1F1F8'),
    'RI RI RI | RI'            => ("\x[1F1FA]\x[1F1F8]\x[1F1EB]", "\x[1F1F7]", 2, 4, '1F1FA 1F1F8 1F1EB 1F1F7', '1F1FA 1F1F8 1F1EB 1F1F7'),
    'RI | RI RI RI'            => ("\x[1F1FA]", "\x[1F1F8]\x[1F1EB]\x[1F1F7]", 2, 4, '1F1FA 1F1F8 1F1EB 1F1F7', '1F1FA 1F1F8 1F1EB 1F1F7'),
    'RI RI | RI RI'            => ("\x[1F1FA]\x[1F1F8]", "\x[1F1EB]\x[1F1F7]", 2, 4, '1F1FA 1F1F8 1F1EB 1F1F7', '1F1FA 1F1F8 1F1EB 1F1F7'),
    'Devanagari conjunct'      => ("\x[915]\x[94D]", "\x[937]", 1, 3, '915 94D 937', '915 94D 937'),
    'Prepend | ASCII'          => ("\x[600]", "1", 1, 2, '600 31', '600 31'),
    'empty | mark'             => ("", "\x[301]", 1, 1, '301', '301'),
    'mark | mark'              => ("\x[301]", "\x[301]", 1, 2, '301 301', '301 301'),
    'long left side'           => ("é" x 50 ~ "e", "\x[301]\x[302]", 51, 52, ('E9' xx 51).join(' ') ~ ' 302', ('65 301' xx 51).join(' ') ~ ' 302'),
    'CJK'                      => ("中", "文", 2, 2, '4E2D 6587', '4E2D 6587');

for @joins -> $j {
    my ($l, $r, $chars, $codes, $ords, $nfd) = $j.value;
    my $c = $l ~ $r;
    ck "{$c.chars} {$c.codes} {hx $c.ords}", "$chars $codes $ords", "{$j.key}: chars, codes, ords";
    ck hx($c.NFC.list), $ords, "{$j.key}: .NFC";
    ck hx($c.NFD.list), $nfd, "{$j.key}: .NFD";
    ck $c.NFC.Str eq $c && $c.NFD.Str eq $c, True, "{$j.key}: NFC and NFD round trips";
    my $app = $l; $app ~= $r;
    my $pre = $r; $pre = $l ~ $pre;
    my @a = $l; @a[0] ~= $r;
    my %h = k => $l; %h<k> ~= $r;
    ck ($app, "$l$r", ($l, $r).join, ([~] $l, $r), nqp::concat($l, $r), $pre, @a[0], %h<k>).map(* eq $c).all.so,
       True, "{$j.key}: ~= interpolation join [~] nqp::concat prepend elements agree";
}

# .chars after every step of a growing string (the count carried across joins)
my @steps = "e", "\x[301]", "\x[1F1FA]", "\x[1F1F8]", "\x[1F1EB]", "\x[1F468]", "\x[200D]", "\x[1F469]",
            "\x[1100]", "\x[1161]", "\x[11A8]", "\r", "\n", "\x[600]", "1";
my $s = ''; my @c;
for @steps { $s ~= $_; @c.push: $s.chars }
ck "@c[]", '1 1 2 2 3 4 4 4 5 5 5 6 6 7 7', 'stepwise .chars';
ck hx($s.ords), 'E9 1F1FA 1F1F8 1F1EB 1F468 200D 1F469 AC01 D A 600 31', 'stepwise text';
sub grow() { my $t = ''; my @n; for @steps { $t ~= $_; @n.push: $t.chars }; "@n[]" }
ck grow(), '1 1 2 2 3 4 4 4 5 5 5 6 6 7 7', 'stepwise .chars inside a sub';
my $ri = ''; my @rc; for ^7 { $ri ~= "\x[1F1FA]"; @rc.push: $ri.chars }
ck "@rc[]", '1 1 2 2 3 3 4', 'a run of regional indicators, one at a time';
my $long = 'é' x 40; my @lc;
for "\x[301]", "e", "\x[301]", "a", "\x[323]" { $long ~= $_; @lc.push: $long.chars }
ck "@lc[]", '40 41 41 42 42', '.chars on a long string grown by marks and bases';
ck hx($long.ords.tail(4)), 'E9 301 E9 1EA1', 'and its text';

# an object's .Str composes across the join
class C { method Str { "\x[301]" } }
my $o = "e"; $o ~= C.new;
ck "{$o.codes} {hx $o.ords}", '1 E9', '$s ~= an object';
ck "{("e" ~ C.new).codes}", '1', 'Str ~ an object';
ck ([~] "e", C.new).codes, 1, '[~] with an object';

# Uni operands, and the code point builders, are normalized
ck ("e".NFD ~ "\x[301]").codes, 1, 'Uni ~ Str';
ck ("e" ~ "\x[301]".NFD).codes, 1, 'Str ~ Uni';
ck hx(chrs(0x958, 0x41).encode('utf8-c8').list), 'E0 A4 95 E0 A4 BC 41', 'chrs() is NFC';
ck hx(nqp::chr(0x2126).encode.list), 'CE A9', 'nqp::chr is NFC';
ck hx((chrs(0x41, 0x958) ~ "\x[301]").ords), '41 915 93C 301', 'chrs() joined to a mark';

# a side that is not NFC is normalized whole, wherever the change is in it
# (sprintf keeps code points as given here; Rakudo's normalizes, with the same
# answers below)
my $raw = sprintf('%c%c', 0x65, 0x301);
ck "{hx ($raw ~ "\x[323]").ords} {($raw ~ "\x[323]").codes}", '1EB9 301 2', 'not-NFC left ~ a mark';
my $acc = $raw; $acc ~= "\x[323]" for ^3;
ck "{hx $acc.ords} {$acc.chars}", '1EB9 323 323 301 1', 'not-NFC accumulator ~= marks';
my $far = sprintf('%c', 0x212B) ~ ('a' x 20);
ck "{hx ($far ~ "\x[301]").ords.head(2)} {($far ~ "\x[301]").codes}", 'C5 61 21', 'a singleton far from the join';
ck ("x$raw\x[323]").codes, 3, 'interpolating a not-NFC Str';
ck ($raw, "\x[323]").join.codes, 2, 'joining a not-NFC Str';

# a UTF8-C8 synthetic composes with nothing: its byte survives the join
my $c8 = Buf.new(0x61, 0xE5).decode('utf8-c8') ~ "\x[301]";
ck hx($c8.encode('utf8-c8').list), '61 E5 CC 81', 'utf8-c8 byte next to a mark';
ck $c8.chars, 3, 'utf8-c8 byte next to a mark: .chars';

say $fails ?? "FAIL ($fails)" !! "PASS";
exit $fails ?? 1 !! 0;
