# DATA-PLAN P4: the `zlib` tag's engine primitives — compress, uncompress,
# gzslurp, gzspurt, crc32, adler32.
#
# RFC 1951, 1950 and 1952 written out in src/Zlib.cpp, with no libz behind it.
# That is the point of the tag rather than a detail: a dlopen'd system library
# is not there to be found inside an `--exe` binary or in the WASM playground,
# and those are exactly where `Compress::Zlib`'s dependents are otherwise dead.
#
# Compression is not a function, so what can be pinned is only the direction
# that is. Three gates, in order of how much they are worth:
#
#   1. A stream A THIRD PARTY produced must inflate to exactly the bytes they
#      compressed — t/vectors/zlib.vec, generated from real libz and the system
#      gzip, plus eight malformed streams that must be REFUSED. That file is the
#      master copy of a corpus Compress::Zlib::Native reads too, so the copies
#      are checked identical here as well.
#   2. What we produce must be readable by them — checked against the system
#      `gunzip` in a subprocess, both directions, on real bytes.
#   3. Round trip, which is the weakest of the three: an implementation can
#      round-trip its own private format perfectly and be useless.
#
# The fourth gate has no assertion here because it is not a test: inflate takes
# UNTRUSTED input, and it was fuzzed under ASAN and UBSAN over 10,000 mutated
# and random streams with no report before this file was written.

use Test;

my &compress   = &::('rakupp-compress');
my &uncompress = &::('rakupp-uncompress');
my &gzslurp    = &::('rakupp-gzslurp');
my &gzspurt    = &::('rakupp-gzspurt');
my &crc32      = &::('rakupp-crc32');
my &adler32    = &::('rakupp-adler32');

sub unhex(Str $s) { $s eq '-' ?? Buf.new !! Buf.new($s.comb(2).map({ :16($_) })) }

# ---- 1. the vector file --------------------------------------------------

my $vecfile = $*PROGRAM.parent.parent.add('vectors/zlib.vec');
ok $vecfile.e, 'the vector file is where the suite expects it';

my %n;
my $bad = 0;
for $vecfile.lines -> $line {
    next if $line.starts-with('#') || !$line.trim;
    my @f = $line.words;
    %n{@f[0]}++;
    given @f[0] {
        when 'I' {
            my $got = try uncompress(unhex(@f[2]), |(@f[1] eq 'zlib' ?? {} !! @f[1] eq 'gzip' ?? {:gzip} !! {:raw}));
            unless $got.defined && $got eqv unhex(@f[3]) {
                $bad++;
                diag "I {@f[1]} did not inflate: { $got.defined ?? 'wrong bytes' !! $! }" if $bad < 5;
            }
        }
        when 'X' {
            my $got = try uncompress(unhex(@f[2]), |(@f[1] eq 'zlib' ?? {} !! @f[1] eq 'gzip' ?? {:gzip} !! {:raw}));
            if $got.defined { $bad++; diag "X {@f[1]} {@f[2].substr(0,40)} was ACCEPTED" if $bad < 5 }
        }
        when 'C' { $bad++ unless crc32(unhex(@f[2]))   == :16(@f[1]) }
        when 'A' { $bad++ unless adler32(unhex(@f[2])) == :16(@f[1]) }
    }
}
is %n<I>, 41, 'every inflate vector ran';
is %n<X>,  8, 'and every must-be-refused one';
is %n<C> + %n<A>, 18, 'and the checksum vectors';
is $bad, 0, "all {%n.values.sum} vectors pass — inflating, refusing and checksumming";

# The distribution carries a copy so a published tarball stands alone. Drift is
# the failure this arrangement exists to prevent, so it is checked.
my $sibling = $*PROGRAM.parent.parent.parent.parent
                       .add('raku-modules/Compress-Zlib-Native/t/vectors/zlib.vec');
if $sibling.e {
    is $sibling.slurp(:bin), $vecfile.slurp(:bin),
       'the distribution copy of the corpus is byte-identical to the master';
}
else {
    skip 'raku-modules checkout not beside this one', 1;
}

# ---- 2. what we produce, read by somebody else ---------------------------

my $tmp = $*TMPDIR.add("rakupp-zlib-{$*PID}.gz");
LEAVE { $tmp.unlink }

my $payload = (^400).map({ "line $_: the quick brown fox jumps over the lazy dog\n" }).join;
$tmp.spurt(compress($payload.encode, 6, :gzip));
is qqx{gunzip -c "$tmp"}, $payload, 'the system gunzip reads what compress(:gzip) writes';

gzspurt($tmp, $payload);
is qqx{gunzip -c "$tmp"}, $payload, '…and what gzspurt writes';

# …and the other direction, on a file the system gzip made.
my $plain = $*TMPDIR.add("rakupp-zlib-{$*PID}.txt");
$plain.spurt($payload);
LEAVE { $plain.unlink; $*TMPDIR.add("rakupp-zlib-{$*PID}.txt.gz").unlink }
qqx{gzip -9 -f "$plain"};
is gzslurp($*TMPDIR.add("rakupp-zlib-{$*PID}.txt.gz")), $payload,
   'gzslurp reads a file the system gzip made';

# ---- 3. round trip, over every framing and every level -------------------

my @bodies =
    ''.encode,
    'a'.encode,
    ('a' x 100_000).encode,                       # one long run
    Buf.new(^256),                                # every byte value
    'héllo — ünïcode ✓ 😀'.encode,
    $payload.encode,
    Buf.new((^30_000).map({ ($_ * 2654435761) +& 0xff })),   # incompressible-ish
    ;
my $rt = 0;
for @bodies -> $b {
    for 0, 1, 6, 9, -1 -> $lvl {
        for {}, {:gzip}, {:raw} -> %fmt {
            $rt++ unless uncompress(compress($b, $lvl, |%fmt), |%fmt) eqv $b;
        }
    }
}
is $rt, 0, "every body round-trips at every level in every framing ({@bodies * 5 * 3} combinations)";

# A stored block is what level 0 is FOR, and an empty input still has to make a
# legal stream rather than nothing at all.
ok compress(''.encode).elems > 0, 'compressing nothing still produces a stream';
is uncompress(compress(''.encode)).elems, 0, '…that inflates back to nothing';
ok compress(('a' x 5000).encode, 0).elems > 5000,
   'level 0 stores, so it is bigger than its input';

# ---- 4. the checksums ----------------------------------------------------

is crc32('123456789'), 0xcbf43926, 'crc32 of the check string';
is adler32('123456789'), 0x091e01de, 'adler32 of the check string';
is crc32(''), 0, 'crc32 of nothing is 0';
is adler32(''), 1, 'and adler32 of nothing is 1';
is crc32('123456789'.encode), crc32('123456789'), 'a Str is its UTF-8 either way';
# The running value is what makes them usable on a stream in pieces.
is crc32('56789', crc32('1234')), crc32('123456789'),
   'crc32 resumes from a running value';
is adler32('56789', adler32('1234')), adler32('123456789'),
   'and so does adler32';

# ---- 5. what the primitives refuse ---------------------------------------

throws-like { compress('a string') }, X::AdHoc, message => /'expected a Blob'/,
    'compress wants a Blob — a Str would silently pick an encoding';
throws-like { compress(''.encode, 10) }, X::AdHoc, message => /'between -1 and 9'/,
    'and a level in range, as the reference does';
throws-like { compress(''.encode, -2) }, X::AdHoc, message => /'between -1 and 9'/,
    'at both ends';
throws-like { compress(''.encode, 6, :gzip, :raw) }, X::AdHoc, message => /'not both'/,
    ':gzip and :raw are mutually exclusive';
throws-like { compress(''.encode, :gzipp) }, X::AdHoc, message => /'no such adverb'/,
    'a misspelled adverb is refused, not silently ignored';
throws-like { uncompress(Buf.new(1, 2, 3)) }, X::AdHoc, message => /'header'/,
    'a stream that is not zlib at all is refused';
throws-like { uncompress(Buf.new(0x1f, 0x8b), :gzip) }, X::AdHoc, message => /'eighteen bytes'/,
    'and one too short to hold a gzip trailer';
# A truncated but well-formed prefix is the interesting refusal: the header is
# fine and the failure is only found part-way through.
{
    my $z = compress($payload.encode);
    throws-like { uncompress($z.subbuf(0, $z.elems - 6)) }, X::AdHoc,
        'a truncated stream is refused rather than returning what decoded so far';
}
{
    my $z = Buf.new(compress($payload.encode).list);
    $z[$z.elems - 1] = $z[$z.elems - 1] +^ 0xff;
    throws-like { uncompress($z) }, X::AdHoc, message => /'Adler-32'/,
        'a corrupt checksum is caught, and named';
}
throws-like { gzspurt($tmp, 42, :bin) }, X::AdHoc, message => /'wants a Blob'/,
    'gzspurt(:bin) wants a Blob';

# ---- 6. return types and the backend -------------------------------------

is &::('rakupp-zlib-backend')(), 'core',
   "zlib-backend says 'core' — the engine answered, no library was loaded";
is compress(''.encode).^name, 'Buf', 'compress answers a Buf, as the reference does';
is uncompress(compress('x'.encode)).^name, 'Buf', 'and so does uncompress';
is gzslurp($tmp).^name, 'Str', 'gzslurp answers a Str';
is gzslurp($tmp, :bin).^name, 'Buf', '…and a Buf under :bin';
ok crc32('x') ~~ Int, 'the checksums answer an Int';

# ---- 7. the compiler answering a `use` must not pollute anything ----------

my $exe = $*EXECUTABLE.absolute;
sub compiles(Str $code) { run($exe, '-e', $code, :out, :err).exitcode == 0 }

nok compiles('compress("a".encode)'), 'compress is undeclared without a `use`';
nok compiles('crc32("a")'),           'and crc32';
nok compiles('zlib-backend()'),       'and zlib-backend';
nok compiles('say (&::("compress")).defined || die "visible"'),
    'and none of them is reachable by runtime lookup either';
ok compiles('&::("rakupp-crc32")("a")'),
   'the rakupp- primitive is reachable, which is the adoption mechanism';

ok compiles('{ use Data::Native <zlib>; crc32("a") }'),
   'a `use` inside a block works there';
nok compiles('{ use Data::Native <zlib>; crc32("a") }; crc32("a")'),
    'and does not escape the block';
nok compiles('use Data::Native <zlib>; md5("a")'),
    'an unclaimed tag contributes no names';
ok compiles('use Data::Native <zlib>;
             die "not claimed" unless (PROCESS::<%DATA-NATIVE-CLAIMED> // {})<zlib>;'),
   'the compiler writes the claim registry, as Compress::Zlib::Native\'s EXPORT would';

done-testing;
