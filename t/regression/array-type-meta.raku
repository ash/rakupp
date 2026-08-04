# Regression: `.^array_type` answers the element type of a buffer, 2026-08-04.
# Enumerated against Rakudo, including which spellings it REFUSES.

my $ok = True;
sub ck($got, $want, $l) { unless $got eqv $want { say "FAIL: $l — {$got.raku} vs {$want.raku}"; $ok = False } }

# a buffer VALUE reports its element type
ck(Buf.new(1, 2).^array_type.^name,     'uint8',  'Buf value');
ck(blob32.new(1).^array_type.^name,     'uint32', 'blob32 carries its width');
ck('ab'.encode.^array_type.^name,       'uint8',  'an encoded Str is uint8');

# and the utf* TYPE OBJECTS, which are real classes in Rakudo
ck(utf8.^array_type.^name,  'uint8',  'utf8 type object');
ck(utf16.^array_type.^name, 'uint16', 'utf16');
ck(utf32.^array_type.^name, 'uint32', 'utf32');

# …but NOT the alias spellings, which Rakudo refuses too
ck((try blob8.^array_type).defined, False, 'blob8 is an alias, not a class');
ck((try Buf.^array_type).defined,   False, 'nor does the bare Buf answer');

say $ok ?? 'PASS' !! 'FAIL';
