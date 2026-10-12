# Regression: the built-in JSON codec (jsonParseValue / jsonParseString in
# src/Builtins.cpp) types a number by its own direct path for the shapes
# nearly every document is made of, and hands back NFC text where an escape
# assembled it. The same file passes under Rakudo, whose
# Rakudo::Internals::JSON is the vendored JSON::Fast — the codec's spec.
#
# NUMBERS. Every number token used to go through numifyStr, the whole of
# Str.Numeric — radix forms, `1/3`, `2*10**3`, Unicode digits, half a dozen
# std::regex matches — which made a JSON number cost 5-30x what the
# JSON::Native extension paid to type it directly (316 KB of exponent forms:
# 60 ms, now 2 ms). An integer or plain decimal of up to 18 digits and any
# exponent form are now built by the constructors numifyStr itself uses for
# them; the cases below pin that the answer is still exactly Str.Numeric's —
# type, value and normal form — including at the 18/19-digit seam where the
# direct path hands over.
#
# TEXT. A Str is not normalised on construction here, so "a\u0301" decoded
# to an "a" and a separate combining mark: it looked like "á" and was not
# equal to it, where every Str Rakudo hands back is NFC. Found by
# JSON::Native's differential test against JSON::Fast.

use Test;

my \J = Rakudo::Internals::JSON;

my @numbers = <
    0 -0 7 -7 007 -007 17 123456789012345678 -123456789012345678
    999999999999999999 1234567890123456789 -9223372036854775808
    9223372036854775807 9223372036854775808 123456789012345678901234567890
    1.5 -1.5 0.1 -0.0 0.10 00.5 -00.50 1.000 -1.000 3.14159
    0.123456789012345678 0.1234567890123456789 12345678.1234567891
    123456789.123456789 1e3 1E-3 -2.5e+4 0e0 1E+2 1e400 -1e-400 5e-324
    1.5e300 0.000001e6
>;

plan @numbers + 9;

for @numbers -> $tok {
    my $got  = J.from-json("[$tok]")[0];
    my $want = $tok.Numeric;
    ok $got.WHAT === $want.WHAT && $got eqv $want && $got.raku eq $want.raku,
       "$tok types as Str.Numeric does ({$want.^name} {$want.raku})";
}

# the strings: a combining mark joins its base, U+1D15E splits in two, and
# text with no \u escape in it is passed through as it was
my $acute = J.from-json("\"a\x5Cu0301\"");
is $acute.codes, 1,                'a \u-escaped combining mark composes with its base';
ok $acute eq "\x[E1]",             'and compares equal to the composed character';
is J.from-json("\"\x5Cud834\x5Cudd5e\"").codes, 2,
                                   'U+1D15E, which NFC decomposes, arrives decomposed';
is J.from-json("\"e\x5Cu0301x\x5Cn\x5Cu00e9\"").codes, 4,
                                   'escaped text mixing marks and plain escapes';
is J.from-json("\"\x5Cud83d\x5Cude00\"").codes, 1,
                                   'an astral pair needs no normalisation and gets none';
is J.from-json("\"line\x5Cn\x5C\"q\x5C\" é 日\"").chars, 12,
                                   'short escapes beside non-ASCII text leave it as it was';
is J.from-json("\{\"a\x5Cu0301\": 1}").keys[0].codes, 1,
                                   'a key is normalised as a value is';
is-deeply J.from-json("[\"\x5Cu00e9\", \"x\"]"), ["\x[E9]", "x"],
                                   'a precomposed escape stays composed';
is J.from-json("\"\x5Cu0065\x[301]\"").codes, 1,
                                   'an escaped base composes with a raw mark after it';
