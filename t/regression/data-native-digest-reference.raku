# DATA-PLAN P3, the third gate: the `digest` primitives answer exactly what the
# pure-Raku reference answers, asked IN THIS PROCESS.
#
# `Digest` and `Digest::HMAC` run correctly on this engine — the SHA-512 gap
# MODULE-FINDINGS.md recorded on 2026-08-03 is closed — so the primitives have
# their oracle here rather than only in a vector file: any input at all can be
# put to both, not just the 156 openssl vectors data-native-digest.raku pins.
#
# Separate from that file because these modules are not part of this repo, and
# `#?requires` skips a whole file: the vectors must still run on a machine
# without them.

#?requires Digest::MD5
#?requires Digest::SHA1
#?requires Digest::SHA2
#?requires Digest::HMAC

use Test;

my @ALGOS = <md5 sha1 sha224 sha256 sha384 sha512>;
my %P     = @ALGOS.map({ $_ => &::("rakupp-$_") });
my %PH    = @ALGOS.map({ $_ => &::("rakupp-{$_}-hex") });
my &phmach = &::('rakupp-hmac-hex');

my (%R, &rhmac);
{
    use Digest::MD5;
    use Digest::SHA1;
    use Digest::SHA2;
    use Digest::HMAC;
    %R = md5 => &md5, sha1 => &sha1, sha224 => &sha224,
         sha256 => &sha256, sha384 => &sha384, sha512 => &sha512;
    &rhmac = &hmac-hex;
}

my @inputs = '', 'a', 'abc', 'The quick brown fox jumps over the lazy dog',
             'héllo — ünïcode ✓', ('x' x 55), ('x' x 56), ('x' x 63), ('x' x 64),
             ('x' x 65), ('x' x 111), ('x' x 112), ('x' x 127), ('x' x 128),
             ('x' x 129), ('y' x 5000);

for @ALGOS -> $a {
    my $ok = True;
    for @inputs -> $in {
        $ok &&= %P{$a}($in).list eqv %R{$a}($in).list;
    }
    ok $ok, "$a matches the pure-Raku reference on all {+@inputs} inputs";
}

# The -hex twin is the bare one hexed, and nothing else. Checked rather than
# assumed, because they are separate registrations.
for @ALGOS -> $a {
    is %PH{$a}('abc'), %P{$a}('abc').list.map({ .fmt('%02x') }).join,
       "{$a}-hex is $a in lower-case hex";
}

# HMAC against Digest::HMAC, on the calls where Digest::HMAC is correct — which
# is every hash whose real block size is 64. For SHA-384/512 it defaults to 64
# and is wrong there; DATA-PLAN records that as a deliberate divergence, and
# the vectors above pin our RFC 2104 answer.
for <md5 sha1 sha224 sha256> -> $a {
    is phmach('key', 'message', %P{$a}), rhmac('key'.encode, 'message'.encode, %R{$a}),
       "hmac-hex matches Digest::HMAC for $a";
}
# …and passing 64 explicitly reproduces Digest::HMAC's answer for the big two,
# which is the compatibility escape hatch the interface promises.
for <sha384 sha512> -> $a {
    is phmach('key', 'message', %P{$a}, 64), rhmac('key'.encode, 'message'.encode, %R{$a}),
       "an explicit :blocksize(64) reproduces Digest::HMAC's non-RFC answer for $a";
    isnt phmach('key', 'message', %P{$a}), phmach('key', 'message', %P{$a}, 64),
       "…and the default does not, because {$a} has a real block size of 128";
}

done-testing;
