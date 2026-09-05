# DATA-PLAN P3: the `digest` tag's engine primitives — six digests, six `-hex`
# twins, `hmac` and `hmac-hex`.
#
# Three things need pinning, and they are different in kind:
#
#   1. THE BYTES, against t/vectors/digest.vec — 156 vectors generated from the
#      system openssl and never from our own code. That file is the master copy
#      of a corpus Digest::Native reads too: its C, its ecosystem fallback and
#      src/Digest.cpp are three separate implementations on purpose
#      (NATIVE-MODULES-PLAN, "The architecture: independent C"), and this file
#      is what keeps them honest. So the copies are checked identical here as
#      well, or a vector added on one side quietly never reaches the other.
#
#   2. THE REFUSALS. A primitive has no module behind it, so every case has to
#      be decided — and the ones this tag decides differently from the module
#      it stands in for are written down in DATA-PLAN, not discovered here.
#
# The third gate — the same answers as the pure-Raku `Digest` and
# `Digest::HMAC`, asked in one process on any input rather than on fixed
# vectors — is data-native-digest-reference.raku. It is a separate file because
# it `#?requires` those modules, and the vectors above must still run on a
# machine that does not have them.

use Test;

my &pbackend = &::('rakupp-digest-backend');
my &phmac    = &::('rakupp-hmac');
my &phmach   = &::('rakupp-hmac-hex');
my @ALGOS    = <md5 sha1 sha224 sha256 sha384 sha512>;
my %P        = @ALGOS.map({ $_ => &::("rakupp-$_") });
my %PH       = @ALGOS.map({ $_ => &::("rakupp-{$_}-hex") });

# ---- 1. the vector file --------------------------------------------------

sub unhex(Str $s) {
    return Buf.new if $s eq '-';
    my ($body, $times) = $s.split('*');
    my $one = Buf.new($body.comb(2).map({ :16($_) }));
    $times ?? Buf.new($one.list xx +$times) !! $one
}

my $vecfile = $*PROGRAM.parent.parent.add('vectors/digest.vec');
ok $vecfile.e, 'the vector file is where the suite expects it';

my ($nh, $nm) = 0, 0;
my $bad = 0;
for $vecfile.lines -> $line {
    next if $line.starts-with('#') || !$line.trim;
    my @f = $line.words;
    if @f[0] eq 'H' {
        my ($algo, $in, $want) = @f[1..3];
        $nh++;
        my $got = %PH{$algo}(unhex($in));
        unless $got eq $want {
            $bad++;
            diag "H $algo {$in.substr(0, 40)}\n  want $want\n  got  $got" if $bad < 6;
        }
    }
    elsif @f[0] eq 'M' {
        my ($algo, $bs, $key, $msg, $want) = @f[1..5];
        $nm++;
        my $got = $bs eq '-' ?? phmach(unhex($key), unhex($msg), %P{$algo})
                             !! phmach(unhex($key), unhex($msg), %P{$algo}, +$bs);
        unless $got eq $want {
            $bad++;
            diag "M $algo bs=$bs\n  want $want\n  got  $got" if $bad < 6;
        }
    }
}
is $nh, 114, 'every digest vector in the file ran';
is $nm, 42,  'and every HMAC vector';
is $bad, 0,  "all {$nh + $nm} vectors match, byte for byte";

# The distribution carries a copy so a published tarball stands alone. Drift
# between the two is the failure this whole arrangement exists to prevent, so
# it is checked rather than trusted — when the sibling checkout is there.
my $sibling = $*PROGRAM.parent.parent.parent.parent
                       .add('raku-modules/Digest-Native/t/vectors/digest.vec');
if $sibling.e {
    is $sibling.slurp(:bin), $vecfile.slurp(:bin),
       'the distribution copy of the corpus is byte-identical to the master';
}
else {
    skip 'raku-modules checkout not beside this one', 1;
}

# ---- 2. the input types ---------------------------------------------------

is %PH<sha256>('abc'), %PH<sha256>('abc'.encode),
   'a Str is its UTF-8, so a Str and its Blob agree';
is %PH<md5>("\c[0]\x[FF]A"), %PH<md5>(Buf.new(0, 0xC3, 0xBF, 65)),
   'and a NUL and a high codepoint survive the crossing';

my $tmp = $*TMPDIR.add("rakupp-digest-{$*PID}.bin");
$tmp.spurt(Buf.new(0 ..^ 256));
LEAVE { $tmp.unlink }
is %PH<sha256>($tmp), %PH<sha256>(Buf.new(0 ..^ 256)),
   'an IO::Path is hashed as its bytes, not as decoded text';
{
    my $fh = $tmp.open(:r, :bin);
    LEAVE $fh.close;
    is %PH<sha256>($fh), %PH<sha256>(Buf.new(0 ..^ 256)), 'and an IO::Handle';
}

# ---- 3. hmac and the hash it is given -------------------------------------

# HMAC is the hash called twice, so a hash of the caller's own keeps working.
# The native path is taken only for the tag's own subs, by IDENTITY — a
# user-defined `sub sha256` must NOT be mistaken for ours.
{
    my $called = 0;
    my sub sha256($x) { $called++; %P<sha256>($x) }
    my $mine = phmach('key', 'message', &sha256);
    is $called, 2, 'a hash of the caller\'s own is called twice, as HMAC is defined';
    is $mine, phmach('key', 'message', %P<sha256>),
       '…and produces the same MAC, since it computes the same hash';
}
{
    my $n = 0;
    my sub counted($x) { $n++; %P<md5>($x) }
    phmach('k' x 500, 'message', &counted);
    is $n, 3, 'a key longer than the block size is hashed first — three calls, not two';
}

# ---- 4. what the primitives refuse ----------------------------------------

throws-like { %P<md5>(42) }, X::AdHoc, message => /'cannot digest a Int'/,
    'a number is refused rather than silently hashing "42"';
throws-like { %P<md5>(Any) }, X::AdHoc, message => /'pass a Str, a Blob'/,
    'and an undefined value, naming what is accepted';
throws-like { %P<sha256>('a', 'b') }, X::AdHoc, message => /'exactly one argument'/,
    'a second positional is refused';
throws-like { %P<sha256>('a', :initial-hash([1,2])) }, X::AdHoc,
    message => /'initial-hash'/,
    ':initial-hash is refused — a Digest::SHA2 internal, not part of this interface';
throws-like { %P<sha256>('a', :nope) }, X::AdHoc, message => /'no such adverb'/,
    'and an unknown adverb, naming it';
throws-like { phmach('k', 'm') }, X::AdHoc, message => /'blocksize'/,
    'hmac wants the hash as well as the key and the message';
throws-like { phmach('k', 'm', 'not a sub') }, X::AdHoc, message => /'Callable'/,
    'and it has to be a Callable';
throws-like { phmach('k', 'm', %P<sha256>, 0) }, X::AdHoc, message => /'between 1 and 1024'/,
    'a nonsensical block size is refused rather than allocating for it';
throws-like { phmach('k', 'm', %P<sha256>, 2**40) }, X::AdHoc, message => /'between 1 and 1024'/,
    'and so is a hostile one';

# `rakupp-sha1-hex` was a SECOND registration of this name before P3 — the
# installer's, uppercase, for the store's short/ index keys. Two registrations
# of one name meant the answer depended on the order of two blocks in
# Builtins.cpp; there is one now, and it is the tag's.
is &::('rakupp-sha1-hex')('abc'), 'a9993e364706816aba3e25717850c26c9cd0d89d',
   'rakupp-sha1-hex is the tag\'s primitive, lower case like every other -hex';

# ---- 5. return types ------------------------------------------------------

is pbackend(), 'core', "digest-backend says 'core' — the engine answered";
is %P<sha256>('abc').^name, 'Blob', 'a bare digest is a blob8';
is %P<sha256>('abc').elems, 32, 'of the algorithm\'s length';
is %PH<sha256>('abc').^name, 'Str', 'and the -hex twin is a Str';
is phmac('k', 'm', %P<sha256>).^name, 'Blob', 'hmac is a blob8 too';
is phmac('k', 'm', %P<sha256>).list.map({ .fmt('%02x') }).join,
   phmach('k', 'm', %P<sha256>), 'and hmac-hex is that blob in hex';

# ---- 6. the compiler answering a `use` must not pollute anything ----------

my $exe = $*EXECUTABLE.absolute;
sub compiles(Str $code) { run($exe, '-e', $code, :out, :err).exitcode == 0 }

nok compiles('md5("a")'),          'md5 is undeclared without a `use`';
nok compiles('sha256-hex("a")'),   'and sha256-hex';
nok compiles('hmac("k","m",&md5)'),'and hmac';
nok compiles('digest-backend()'),  'and digest-backend';
nok compiles('say (&::("sha256")).defined || die "visible"'),
    'and none of them is reachable by runtime lookup either';
ok compiles('&::("rakupp-sha256-hex")("a")'),
   'the rakupp- primitive is reachable, which is the adoption mechanism';

ok compiles('{ use Data::Native <digest>; md5("a") }'),
   'a `use` inside a block works there';
nok compiles('{ use Data::Native <digest>; md5("a") }; md5("a")'),
    'and does not escape the block';
nok compiles('use Data::Native <digest>; from-csv("a,b")'),
    'an unclaimed tag contributes no names';

ok compiles('use Data::Native <digest>;
             die "not claimed" unless (PROCESS::<%DATA-NATIVE-CLAIMED> // {})<digest>;'),
   'the compiler writes the claim registry, as Digest::Native\'s EXPORT would';

# The exported name is the routine\'s own, whichever spelling minted it first —
# `&md5.name` must not depend on whether something asked for `&::("rakupp-md5")`
# earlier in the program.
ok compiles('use Data::Native <digest>; die "name" unless &md5.name eq "md5";'),
   'an exported primitive answers to its exported name';
ok compiles('my $x = &::("rakupp-md5"); use Data::Native <digest>;
             die "name" unless &md5.name eq "md5";'),
   '…even when the rakupp- spelling was asked for first';

done-testing;
