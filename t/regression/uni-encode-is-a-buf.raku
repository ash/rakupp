# Regression: `Uni.encode` answered what `Str.encode` answers — a utf8 for
# utf-8, a Blob[uint8] for latin-1 — where Rakudo's Uni answers a Buf for
# every encoding it can do. `$s.NFD.encode("utf8") ~~ Buf` was False and a
# `--> Buf` return check died on it (issue #251). The four normal forms are
# Unis, so the fix is Uni's; the codepoints are encoded as they are, so an
# NFD stays decomposed. Expectations read off Rakudo 2026.09.
#
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) {
    @fail.push("$what: got {$got.raku}, want {$want.raku}") unless $got eqv $want;
}

my $pre = "café";
my $dec = "cafe\x[0301]";
for <NFC NFD NFKC NFKD> -> $form {
    for $pre, $dec -> $s {
        my $b = $s."$form"().encode('utf8');
        check($b ~~ Buf, True, "$form of {$s.codes} codes ~~ Buf");
        check($b.^name, 'Buf', "$form of {$s.codes} codes .^name");
        check($b.bytes, $form.starts-with('NFC' | 'NFKC') ?? 5 !! 6, "$form of {$s.codes} codes .bytes");
    }
}
my $u = Uni.new(99, 97, 102, 233).encode('utf8');
check($u.^name, 'Buf', 'a bare Uni');
check($u.list, (99, 97, 102, 195, 169), 'its bytes');
check("café".NFD.encode.list, (99, 97, 102, 101, 204, 129), 'NFD stays decomposed');

check("aé".NFC.encode('latin1').raku,  'Buf.new(97,233)',       'latin1');
check("aé".NFC.encode('utf16le').raku, 'Buf.new(97,0,233,0)',   'utf16le');
check("aé".NFC.encode('utf16be').raku, 'Buf.new(0,97,0,233)',   'utf16be');

sub to-buf(Str $x --> Buf) { $x.NFD.encode("utf8") }
check((try to-buf($pre)).defined, True, '--> Buf accepts it');

my $m = "ab".NFD.encode; $m.push(99);
check($m.decode, 'abc', 'a mutable Buf');
check((try "café".NFD.encode('ascii')).defined, False, 'an unencodable codepoint still fails');
check("café".encode.^name, 'utf8', 'Str.encode is still a utf8');

if @fail { .note for @fail; say "FAIL"; exit 1 }
say "PASS";
