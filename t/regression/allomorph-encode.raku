# Regression: `.encode` was the one Str-side method an allomorph could not do.
# `<7 8 9>` yields IntStr elements (Rakudo too), and `.chars`, `.uc`, `.ords`,
# `.substr` and the rest of the family all worked on one, because they carry no
# type guard — `.encode` was gated to VT::Str and answered "No such method
# 'encode' for invocant of type 'IntStr'". Found by a Win32 GUI turning button
# titles into wide strings: `<7 8 9 ÷>` made three of its four buttons throw.
#
# The wider rule is Cool's: every Cool stringifies before encoding, an
# allomorph by its STRING side (`<0x1F>` encodes "0x1F", not "31"). Every
# expectation below was read off Rakudo, not reasoned out — two of them were
# wrong when reasoned out.
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) {
    @fail.push("$what: got {$got.raku}, want {$want.raku}") unless $got eqv $want;
}

check(<42>.WHAT.^name,  'IntStr', 'the word list yields an allomorph');
check(<1.5>.WHAT.^name, 'RatStr', 'and a RatStr for a decimal');
check(<1e2>.WHAT.^name, 'NumStr', 'and a NumStr for an exponent');

# an allomorph encodes its string side
check(<42>.encode('utf8').list,    (52, 50),         'IntStr encodes "42"');
check(<42>.encode('utf16').elems,  2,                'and in utf16, two units');
check(<1.5>.encode('utf8').list,   (49, 46, 53),     'RatStr encodes "1.5"');
check(<1e2>.encode('utf8').list,   (49, 101, 50),    'NumStr encodes "1e2"');
check(<0x1F>.encode('utf8').list,  (48, 120, 49, 70), 'IntStr encodes "0x1F", not its value');

# and every other Cool stringifies the ordinary way
check(42.encode('utf8').list,     (52, 50),      'Int');
check((1/2).encode('utf8').list,  (48, 46, 53),  'Rat is 0.5, its Str, not 1/2');
check(1e2.encode('utf8').list,    (49, 48, 48),  'Num is 100');
check((1, 2).encode('utf8').list, (49, 32, 50),  'a List is its gist-ish Str');

note @fail.join("\n") if @fail;
say @fail ?? "FAIL" !! "PASS";
exit @fail ?? 1 !! 0;
