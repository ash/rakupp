# Regression: the string bitwise operators combined the UTF-8 BYTES of their
# operands instead of the CODEPOINTS, and prefix:<~^> complemented bytes and put
# the result back into a Str.
#
# For ASCII the two are the same, which is why it hid. For anything else it was
# wrong twice over: `"A" ~^ chr(0xFF)` answered TWO characters (0x82, 0xBF) where
# Rakudo answers one (0x00BE), and the bytes left in the Str were not valid
# UTF-8 — `~^ "1"` reported `.ords` of 0x0E while printing the byte 0xCE. That
# byte reached a generated page on the website and aborted a build, since BSD
# sed refuses an illegal byte sequence under a UTF-8 locale.
#
# prefix:<~^> on a Str now declines exactly as Rakudo does — there is no agreed
# meaning for "the complement of a codepoint" — but on a Blob/Buf Rakudo DOES
# implement it, and declining there too cost 151 Roast assertions before this
# test existed.
#
# NOT fixed here, and pre-existing: the INFIX `Buf ~^ Buf` answers a Str rather
# than a Buf and gets the wrong bytes. Untouched by this change — identical
# before and after — and tracked separately. (The PREFIX form on a Buf is
# correct and asserted below.)
# Contract: exit 0 + last line PASS.
my @fail;

# codepoints, not bytes — the case that was wrong
my $x = "A" ~^ chr(0xFF);
@fail.push("A ~^ chr(0xFF): {$x.ords.map(*.fmt('%04X')).join(',')}")
    unless $x.chars == 1 && $x.ords[0] == 0xBE;

# whatever comes out is well-formed text: .ords and the encoded bytes agree
@fail.push('ords/bytes disagree')
    unless $x.encode('utf8').list.join(',') eq '194,190';   # U+00BE

# ASCII still behaves as it always did
@fail.push('A ~^ B')  unless ("A" ~^ "B").ords[0] == 0x03;
@fail.push('A ~| b')  unless ("A" ~| "b").ords[0] == 0x63;
@fail.push('A ~& z')  unless ("A" ~& "z").ords[0] == 0x40;

# a high codepoint that OR-ing must preserve exactly
@fail.push('A ~| é') unless ("A" ~| chr(0xE9)).ords[0] == 0xE9;

# `~&` truncates to the shorter operand, `~|`/`~^` extend
@fail.push('~& truncates') unless ("abc" ~& "ab").chars == 2;
@fail.push('~| extends')   unless ("ab" ~| "abc").chars == 3;
@fail.push('~^ extends')   unless ("ab" ~^ "abc").chars == 3;

# prefix:<~^> on a Str declines, as in Rakudo, rather than manufacturing an
# invalid Str
@fail.push('prefix ~^ on Str should decline')
    unless (try { ~^ "1"; 0 } // 1) == 1;

# ...but on a Blob/Buf Rakudo DOES implement it, as the byte-wise complement,
# and so must we. Declining unconditionally killed S03-operators/buf.t outright
# — the file stopped emitting TAP and 151 assertions vanished from the Roast
# count, which is how this was caught.
my $nb = ~^ Buf.new(0x41, 0xFF);
@fail.push("prefix ~^ on Buf: {$nb.^name} {$nb.list.map(*.fmt('%02X')).join(' ')}")
    unless $nb.^name eq 'Buf' && $nb.list.map(*.fmt('%02X')).join(' ') eq 'BE 00';

# a longer round trip: every character of the result is valid text
my $mix = "héllo wörld" ~^ ("\0" x 11);
@fail.push('xor with NUL is identity') unless $mix eq "héllo wörld";
@fail.push('result is valid UTF-8')
    unless $mix.encode('utf8').decode('utf8') eq $mix;

if @fail { note "FAILED:\n" ~ @fail.join("\n"); say 'FAIL' }
else     { say 'PASS' }
