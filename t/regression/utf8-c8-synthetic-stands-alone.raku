# A byte that utf8-c8 cannot decode is a synthetic grapheme of its own, broken
# around like a Control (Roast's Unicode 18.0 utf8-c8.t asserts this). Raku++
# keeps such a byte as it came and decodes it as the codepoint of the same
# value, so 0xFF went through the UAX #29 rules as U+00FF, a letter — and a
# combining mark after it, or a Prepend before it, joined it into one cluster:
# "{0xFF}\x20E7".chars was 1, not 2. Segmentation now marks those bytes.
my @fail;
sub check($got, $want, $what) {
    @fail.push("$what: got {$got.raku} want {$want.raku}") unless $got eq $want
}
my $t = Buf.new(0xFF).decode('utf8-c8');

check "$t\x20E7".chars,                         2, 'no Extend joins it';
check "$t\c[ZWJ]".chars,                        2, 'no ZWJ joins it';
check "$t\x[E33]".chars,                        2, 'no SpacingMark joins it';
check "\x[11941]\x[11A88]$t!".chars,            3, 'a Prepend does not take it';
check "\x[1FAEB]\c[ZWJ]$t\c[ZWJ]\x[1FACC]".chars, 4, 'it is no Extended_Pictographic';
check Buf.new(0xFE, 0xE1).decode('utf8-c8').chars, 2, 'two synthetics stay two';
check Buf.new(0x0D, 0xE0, 0x0A).decode('utf8-c8').chars, 3, 'it splits a CR LF';
check "a$t\x[301]b".comb.elems,                 4, '.comb counts it the same';
check ("a$t\x[301]b" ~~ m:g/./).elems,          4, 'so does the regex dot';
check "a$t\x[301]b".index('b'),                 3, 'so does .index';
check "\x[e9]\x[301]".chars,                    1, 'a real U+00E9 still combines';
check Buf.new(0xFF).decode('utf8-c8').encode('utf8-c8').list.join(','), '255', 'it round-trips';

if @fail { note "FAILED: @fail.join('; ')"; say 'FAIL' } else { say 'PASS' }
