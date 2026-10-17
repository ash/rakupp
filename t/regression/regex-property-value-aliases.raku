# Regression: <:Prop<Value>> names a value by any of its aliases, and a value
# a property does not have matches nothing. Each answer in the first half is
# quoted from the UCD 18.0 text in tools/ucd, not from either engine.
#
#   - Only the spelling the generated table stores matched: <:ea<Wide>> but
#     not <:ea<W>>, <:lb<AL>> but not <:lb<Alphabetic>>, <:Age<1.1>> but not
#     <:Age<V1_1>>, <:NFC_QC<Maybe>> but not <:NFC_QC<M>>, <:bc<L>> but not
#     <:bc<Left_To_Right>>. Rakudo 2026.09 also takes one spelling per value,
#     often the other one (<:ea<W>>, not <:ea<Wide>>).
#   - A value the property's table did not spell that way matched EVERY
#     character, through the bare form's lenient rule for unknown names:
#     <:sc<Latn>>, <:gc<Uppercase_Letter>>, <:Block<Arrows>>, <:ccc<230>>.
#     So did the bare long category names: <:Uppercase_Letter>,
#     <:Combining_Mark>, <:Decimal_Number>.
#   - <:Block<…>> matched only where the block's name is also a script's
#     (<:blk<Adlam>>), through that same bare script check. U+0530, in the
#     Armenian block but unassigned (script Unknown), was not
#     <:Block<Armenian>>.
#
# Now every alias PropertyValueAliases-18.0.0.txt lists for a value matches it,
# loosely (UAX #44 LM3: case, spaces, underscores and hyphens do not count).
# The second half reads that file and checks every row of the properties
# below, at a code point that has the value and at one that has another.
#
# Contract: exit 0 + last line PASS. Red on Rakudo 2026.09, which takes one
# spelling per value and fails most of the second half.

my $fails = 0;
sub check(Str $desc, $got, $want) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "not ok - $desc"; note "GOT {$got.raku} WANT {$want.raku}" }
}

# EastAsianWidth-18.0.0.txt: 4E00..9FFF;W  0061..007A;Na
check '<:ea<W>> and <:ea<Wide>> match 中',
    (so('中' ~~ /<:ea<W>>/), so('中' ~~ /<:ea<Wide>>/)), (True, True);
check '<:East_Asian_Width<Na>> and <:ea<Narrow>> match a',
    (so('a' ~~ /<:East_Asian_Width<Na>>/), so('a' ~~ /<:ea<Narrow>>/)), (True, True);
check '<:ea<W>> does not match a', so('a' ~~ /<:ea<W>>/), False;
# LineBreak-18.0.0.txt: 0061..007A;AL   PropertyValueAliases: lb ; AL ; Alphabetic
check '<:lb<AL>> and <:Line_Break<Alphabetic>> match a',
    (so('a' ~~ /<:lb<AL>>/), so('a' ~~ /<:Line_Break<Alphabetic>>/)), (True, True);
check '<:lb<Alphabetic>> does not match a space (SP)', so(' ' ~~ /<:lb<Alphabetic>>/), False;
# DerivedAge-18.0.0.txt: 0000..01F5 ; 1.1   20C0 ; 14.0   age; 1.1 ; V1_1
check '<:Age<1.1>> and <:Age<V1_1>> match A',
    (so('A' ~~ /<:Age<1.1>>/), so('A' ~~ /<:Age<V1_1>>/)), (True, True);
check '<:Age<V1_1>> does not match U+20C0 (14.0)', so(0x20C0.chr ~~ /<:Age<V1_1>>/), False;
check '<:Age<V14_0>> matches U+20C0', so(0x20C0.chr ~~ /<:Age<V14_0>>/), True;
# DerivedNormalizationProps-18.0.0.txt: 0300..0304 ; NFC_QC; M
check '<:NFC_QC<M>> and <:NFC_Quick_Check<Maybe>> match U+0300',
    (so("\x[300]" ~~ /<:NFC_QC<M>>/), so("\x[300]" ~~ /<:NFC_Quick_Check<Maybe>>/)), (True, True);
check '<:NFC_QC<Y>> matches a, <:NFC_QC<Yes>> too',
    (so('a' ~~ /<:NFC_QC<Y>>/), so('a' ~~ /<:NFC_QC<Yes>>/)), (True, True);
# DerivedBidiClass-18.0.0.txt: 0030..0039 ; EN   bc ; EN ; European_Number
check '<:bc<Left_To_Right>> matches a, <:Bidi_Class<European_Number>> matches 0',
    (so('a' ~~ /<:bc<Left_To_Right>>/), so('0' ~~ /<:Bidi_Class<European_Number>>/)), (True, True);
check '<:bc<L>> still matches YI SYLLABLE IT and not ALEF (Roast)',
    (so("\c[YI SYLLABLE IT]" ~~ /^<:bc<L>>$/), so("\x[5D0]" ~~ /^<:bc<L>>$/)), (True, False);
# Scripts-18.0.0.txt: 0400..0481 ; Cyrillic   sc ; Cyrl ; Cyrillic
check '<:sc<Cyrl>> matches Я and not A', (so('Я' ~~ /<:sc<Cyrl>>/), so('A' ~~ /<:sc<Cyrl>>/)), (True, False);
check '<:sc<Latn>> matches A and not Я', (so('A' ~~ /<:sc<Latn>>/), so('Я' ~~ /<:sc<Latn>>/)), (True, False);
check '<:Script<Zyyy>> matches 1 (Common)', so('1' ~~ /<:Script<Zyyy>>/), True;
# Blocks-18.0.0.txt: 0530..058F; Armenian  2190..21FF; Arrows
#   blk; ASCII ; Basic_Latin
check '<:Block<Arrows>> matches → and not a', (so('→' ~~ /<:Block<Arrows>>/), so('a' ~~ /<:Block<Arrows>>/)), (True, False);
check '<:Block<Armenian>> matches U+0530 (unassigned, script Unknown)', so(0x530.chr ~~ /<:Block<Armenian>>/), True;
check '<:blk<ASCII>> and <:blk<Basic_Latin>> match a',
    (so('a' ~~ /<:blk<ASCII>>/), so('a' ~~ /<:blk<Basic_Latin>>/)), (True, True);
# DerivedCombiningClass-18.0.0.txt: 0301 ; 230   ccc; 230; A ; Above
check '<:ccc<230>>, <:ccc<A>> and <:Canonical_Combining_Class<Above>> match U+0301',
    (so("\x[301]" ~~ /<:ccc<230>>/), so("\x[301]" ~~ /<:ccc<A>>/),
     so("\x[301]" ~~ /<:Canonical_Combining_Class<Above>>/)), (True, True, True);
check '<:ccc<NR>> matches a, <:ccc<Above>> does not',
    (so('a' ~~ /<:ccc<NR>>/), so('a' ~~ /<:ccc<Above>>/)), (True, False);
# gc ; Lu ; Uppercase_Letter   gc ; LC ; Cased_Letter   gc ; M ; Mark ; Combining_Mark
check '<:gc<Uppercase_Letter>> matches A and not a',
    (so('A' ~~ /<:gc<Uppercase_Letter>>/), so('a' ~~ /<:gc<Uppercase_Letter>>/)), (True, False);
check '<:General_Category<lowercaseletter>>, <:gc<LC>>, <:gc<Cased_Letter>> match a',
    (so('a' ~~ /<:General_Category<lowercaseletter>>/), so('a' ~~ /<:gc<LC>>/),
     so('a' ~~ /<:gc<Cased_Letter>>/)), (True, True, True);
check '<:gc<Combining_Mark>> matches U+0301 and not a',
    (so("\x[301]" ~~ /<:gc<Combining_Mark>>/), so('a' ~~ /<:gc<Combining_Mark>>/)), (True, False);
check 'an unknown value matches nothing: sc, gc, lb, Block',
    (so('A' ~~ /<:sc<Bogus>>/), so('A' ~~ /<:gc<Bogus>>/), so('A' ~~ /<:lb<Bogus>>/),
     so('A' ~~ /<:Block<Bogus>>/)), (False, False, False, False);
check 'bare <:Uppercase_Letter> matches A and not a',
    (so('A' ~~ /<:Uppercase_Letter>/), so('a' ~~ /<:Uppercase_Letter>/)), (True, False);
check 'bare <:Combining_Mark> matches U+0301 and not a',
    (so("\x[301]" ~~ /<:Combining_Mark>/), so('a' ~~ /<:Combining_Mark>/)), (True, False);
check 'bare <:Decimal_Number> matches 1 and not a',
    (so('1' ~~ /<:Decimal_Number>/), so('a' ~~ /<:Decimal_Number>/)), (True, False);
check '<:Numeric_Type<Digit>> matches ², not 9 (Roast)',
    (so("\x[B2]" ~~ /<:Numeric_Type<Digit>>/), so('9' ~~ /<:Numeric_Type<Digit>>/)), (True, False);

# ---- every row of the file ---------------------------------------------------
# For each property, a code point per value: dense up to U+2FFF (where the
# one-code-point combining classes are), every fourth one above (every block
# is 16-aligned, so each still gets one). A value the sample misses is
# counted, not failed. A code point whose Str is not itself (NFC changes
# it) is skipped, since the match would see another character.
my @props = <ea lb WB SB GCB hst jt nt dt age NFC_QC NFKC_QC NFD_QC NFKD_QC
             InPC InSC bc sc ccc gc blk jg>;
my %want = @props.Set;
sub norm($s) { $s.lc.comb(/<[a..z0..9]>/).join }
my %rows;                                   # prop => [ [alias, …], … ]
for $?FILE.IO.parent.parent.parent.add('tools/ucd/PropertyValueAliases-18.0.0.txt').lines -> $line {
    my $l = $line.split('#')[0].trim;
    next unless $l;
    my @f = $l.split(';')».trim;
    %rows{@f[0]}.push: [@f[1..*]] if %want{@f[0]};
}
my %row-of;                                 # prop => norm(alias) => row index
for @props -> $p {
    for %rows{$p}.kv -> $i, @aliases {
        %row-of{$p}{norm($_)} //= $i for @aliases;
    }
}
my %at;                                     # prop => row index => code point
my %seen;                                   # prop => value as uniprop spells it
for flat(0..0x2FFF, (0x3000, 0x3004 ... 0x1FFFF)) -> $cp {
    next if 0xD800 <= $cp <= 0xDFFF;
    next unless $cp.chr.ord == $cp;
    for @props -> $p {
        my $v = $cp.uniprop($p);
        next if %seen{$p}{$v}++;            # only a value's first code point counts
        my $r = %row-of{$p}{norm($v)} // next;
        %at{$p}{$r} //= $cp;
    }
}
my ($tested, $values, $missed, @bad) = 0, 0, 0;
for @props -> $p {
    my @found = %at{$p}.keys.map(+*).sort;
    for %rows{$p}.keys -> $r {
        my $cp = %at{$p}{$r};
        unless $cp.defined { $missed++; next }
        $values++;
        my $neg = %at{$p}{@found.first(* != $r)};
        for %rows{$p}[$r].unique -> $a {
            my $rx = "<:{$p}<{$a}>>";
            my $yes = so $cp.chr ~~ /<$rx>/;
            my $no  = so $neg.chr ~~ /<$rx>/;
            $tested++;
            @bad.push: sprintf('%s at U+%04X: %s, at U+%04X: %s', $rx, $cp, $yes, $neg, $no)
                unless $yes && !$no;
        }
    }
}
note $_ for @bad.head(20);
check "every alias of every value matches it, and not another value ($tested aliases of $values values in {+@props} properties; $missed values not sampled)",
    +@bad, 0;

say $fails == 0 ?? 'PASS' !! 'FAIL';
exit($fails ?? 1 !! 0);
