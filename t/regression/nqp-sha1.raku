# Regression: `nqp::sha1($str)` was missing. App::RaCoCo — Red's test dependency
# — keys its coverage cache on it and reads the digest back exactly as MoarVM
# writes it: UPPERCASE hex over the string's UTF-8 bytes.
#
# The vectors are FIPS 180-4's and App::RaCoCo's own; every one was checked
# against Rakudo.

use nqp;

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

ck(nqp::sha1('1ahs'), 'E82EFCCFBEB2F189ABB6D4BB79B02A20A277D04C',
   "App::RaCoCo's own vector");
ck(nqp::sha1(''), 'DA39A3EE5E6B4B0D3255BFEF95601890AFD80709',
   'the empty string');
ck(nqp::sha1('abc'), 'A9993E364706816ABA3E25717850C26C9CD0D89D',
   'FIPS 180-4 one-block');
ck(nqp::sha1('abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq'),
   '84983E441C3BD26EBAAE4AA1F95129E5E54670F1', 'FIPS 180-4 two-block');
ck(nqp::sha1('ä'), '961FA22F61A56E19F3F5F8867901AC8CF5E6D11F',
   'a non-ASCII string hashes its UTF-8 bytes');
ck(nqp::sha1('1ahs') eq nqp::sha1('1ahs').uc, True, 'the digest is uppercase');

say $fails ?? "FAIL ($fails)" !! "PASS";
exit $fails ?? 1 !! 0;
