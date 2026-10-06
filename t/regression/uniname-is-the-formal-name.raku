# .uniname answers the formal Name property, never an alias. The names table
# holds UnicodeData names and NameAliases.txt aliases side by side (so that
# \c[MVS] and uniparse('ZWSP') resolve), and the reverse map built from it let
# whichever entry sorted LAST win: U+180E answered "MVS", U+FEFF "ZWNBSP",
# U+2118 "WEIERSTRASS ELLIPTIC FUNCTION" (a correction alias), while U+01A2
# happened to come out right. Rakudo answers the formal name for all of them.
# The table now flags its aliases and the reverse map skips them.
my @fail;
sub check($got, $want, $what) {
    @fail.push("$what: got {$got.raku} want {$want.raku}") unless $got eq $want
}

check 0x180E.uniname, 'MONGOLIAN VOWEL SEPARATOR',           'abbreviation alias MVS';
check 0xFEFF.uniname, 'ZERO WIDTH NO-BREAK SPACE',           'abbreviation alias ZWNBSP';
check 0x200B.uniname, 'ZERO WIDTH SPACE',                    'abbreviation alias ZWSP';
check 0x2118.uniname, 'SCRIPT CAPITAL P',                    'correction alias';
check 0x0CDE.uniname, 'KANNADA LETTER FA',                   'correction alias LLLA';
check 0x11EE.uniname, 'HANGUL JONGSEONG SSANGIEUNG',         'correction alias';
check 0x01A2.uniname, 'LATIN CAPITAL LETTER OI',             'formal name sorting after its alias';
check 0x0007.uniname, '<control-0007>',                      'a control has no formal name';
check 0x180E.uniprop('Name'), 'MONGOLIAN VOWEL SEPARATOR',   'uniprop Name agrees';

# the aliases still resolve the other way
check "\c[MVS]".ord,                       0x180E, '\c[] takes an abbreviation';
check "\c[LATIN CAPITAL LETTER GHA]".ord,  0x01A2, '\c[] takes a correction alias';
check uniparse('ZWSP').ord,                0x200B, 'uniparse takes an alias';
check "\c[BEL]".ord,                       0x0007, '\c[] takes a control alias';

if @fail { note "FAILED: @fail.join('; ')"; say 'FAIL' } else { say 'PASS' }
