# Regression: what t/oracle/unicode.raku (ORACLE-PLAN P3) found when it asked
# every code point for every property against the UCD 18.0 text in
# tools/ucd, re-read by a reader of its own. Each answer below is quoted from
# that text, not from either engine.
#
#   - Bidi_Class of an unassigned code point is not always L:
#     DerivedBidiClass's @missing lines make 0590..05FF R, 0600..07BF AL and
#     20A0..20CF ET, among others. The generator read only the data lines, so
#     every unassigned Hebrew, Arabic and currency code point was L — 120
#     ranges. Rakudo 2026.09 answers L too.
#   - Joining_Type of a code point ArabicShaping does not list is T when its
#     general category is Mn, Me or Cf (the file's own note), else U: U+0300
#     and U+00AD were U. Rakudo answers U too.
#   - Indic_Conjunct_Break (InCB) answered a Bool; it is None, Linker,
#     Consonant or Extend (DerivedCoreProperties), as Rakudo answers.
#   - Block outside every block answered ""; Blocks.txt's @missing says
#     No_Block, as Rakudo answers.
#   - Simple_Case_Folding (scf) answered False, as for an unknown property;
#     it is CaseFolding's C and S lines (Rakudo answers "").
#   - A mapping uniprop answers, and uniparse's answer, were built from the
#     raw code points, so U+0340's lowercase (itself, U+0300 once it is a
#     Str) and uniparse('GREEK QUESTION MARK') (";", by U+037E's singleton
#     decomposition) compared unequal to the same text made by `chr` or a
#     literal. A Str is NFC, and these are now. Rakudo agrees.
#
# Contract: exit 0 + last line PASS. Red on Rakudo 2026.09, which answers L,
# U and "" for the Bidi_Class, Joining_Type and case-folding checks (and
# keeps U+0344's simple uppercase raw); the rest pass there.

my $fails = 0;
sub check(Str $desc, $got, $want) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "not ok - $desc"; note "GOT {$got.raku} WANT {$want.raku}" }
}

# DerivedBidiClass-18.0.0.txt:
#   # @missing: 0590..05FF; Right_To_Left
#   # @missing: 0600..07BF; Arabic_Letter
#   # @missing: 20A0..20CF; European_Terminator
check 'U+0590 (unassigned, Hebrew block) is R', 0x590.uniprop('Bidi_Class'), 'R';
check 'U+05FF (unassigned, Hebrew block) is R', 0x5FF.uniprop('bc'), 'R';
check 'U+07B2 (unassigned, Thaana block) is AL', 0x7B2.uniprop('Bidi_Class'), 'AL';
check 'U+20CF (unassigned, Currency Symbols) is ET', 0x20CF.uniprop('Bidi_Class'), 'ET';
check 'U+0378 (unassigned, Greek) is still L', 0x378.uniprop('Bidi_Class'), 'L';
check 'U+05D0 HEBREW LETTER ALEF is R', 0x5D0.uniprop('Bidi_Class'), 'R';
check 'U+FFFE (noncharacter) is BN', 0xFFFE.uniprop('Bidi_Class'), 'BN';
check '<:bc<R>> matches an unassigned Hebrew code point', so(0x5EB.chr ~~ /<:bc<R>>/), True;

# ArabicShaping-18.0.0.txt: "not explicitly listed and that are of
# General_Category Mn, Me, or Cf ... Joining_Type=T"
check 'U+0300 COMBINING GRAVE ACCENT (Mn) is T', 0x300.uniprop('Joining_Type'), 'T';
check 'U+00AD SOFT HYPHEN (Cf) is T', 0xAD.uniprop('jt'), 'T';
check 'U+20DD COMBINING ENCLOSING CIRCLE (Me) is T', 0x20DD.uniprop('Joining_Type'), 'T';
check 'U+0041 (Lu, unlisted) is U', 0x41.uniprop('Joining_Type'), 'U';
check 'U+0628 ARABIC LETTER BEH (listed) is D', 0x628.uniprop('Joining_Type'), 'D';

# DerivedCoreProperties-18.0.0.txt:
#   094D          ; InCB; Linker
#   0915..0939    ; InCB; Consonant
#   0300..036F    ; InCB; Extend
#   # @missing: 0000..10FFFF; InCB; None
check 'U+094D DEVANAGARI SIGN VIRAMA is Linker', 0x94D.uniprop('InCB'), 'Linker';
check 'U+0915 DEVANAGARI LETTER KA is Consonant', 0x915.uniprop('Indic_Conjunct_Break'), 'Consonant';
check 'U+0300 is Extend', 0x300.uniprop('InCB'), 'Extend';
check 'U+0041 is None', 0x41.uniprop('InCB'), 'None';

# Blocks-18.0.0.txt: "# @missing: 0000..10FFFF; No_Block"; 2FE0..2FEF is in no block
check 'U+2FE0 is in No_Block', 0x2FE0.uniprop('Block'), 'No_Block';
check 'U+10200 is in No_Block', 0x10200.uniprop('blk'), 'No_Block';
check 'U+0041 is still in Basic Latin', 0x41.uniprop('Block'), 'Basic Latin';

# CaseFolding-18.0.0.txt:
#   0041; C; 0061;
#   1E9E; F; 0073 0073;
#   1E9E; S; 00DF;
check 'scf of A is a', 0x41.uniprop('Simple_Case_Folding'), 'a';
check 'scf of ẞ is ß (the S line, not the F one)', 0x1E9E.uniprop('scf'), 'ß';
check 'cf of ẞ is ss (the F line)', 0x1E9E.uniprop('Case_Folding'), 'ss';
check 'scf of a is a', 0x61.uniprop('Simple_Case_Folding'), 'a';

# NFC, as any Str is
check 'lc of U+0340 eq U+0300', 0x340.uniprop('Lowercase_Mapping') eq "\x[300]", True;
check 'uc of U+0390 eq 03AA 0301', 0x390.uniprop('Uppercase_Mapping') eq "\x[3AA,301]", True;
check 'suc of U+0344 eq 0308 0301', 0x344.uniprop('Simple_Uppercase_Mapping') eq "\x[308,301]", True;
check "uniparse('GREEK QUESTION MARK') eq ';'", uniparse('GREEK QUESTION MARK') eq ';', True;
check "uniparse('COMBINING GRAVE TONE MARK') eq U+0300", uniparse('COMBINING GRAVE TONE MARK') eq "\x[300]", True;
check "uniparse of A and a combining ring is Å, one char",
    (uniparse('LATIN CAPITAL LETTER A, COMBINING RING ABOVE') eq "\x[C5]", uniparse('LATIN CAPITAL LETTER A, COMBINING RING ABOVE').chars), (True, 1);

say $fails == 0 ?? 'PASS' !! 'FAIL';
exit($fails ?? 1 !! 0);
